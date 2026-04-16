# 0001 — Phase 1: two players in one cell

- **Status:** accepted
- **Author(s):** SkyrimReLive contributors
- **Created:** 2026-04-15
- **Supersedes:** —

## Summary

Replace the Phase 0 text-based UDP hello with a real wire format (Flatbuffers
over UDP), a connection lifecycle, and a position-replication loop. End state:
two players running the same client connect to one server and see each other
moving in real time inside a single Skyrim cell. No NPCs, combat, inventory,
or cell transitions yet — those are Phase 2+.

This is the first proposal that exercises Hard Rule H3 (schema-forward) and
sets the precedent for every future protocol change.

## Motivation

Phase 0 proved the pipe works. Phase 1 turns the pipe into a protocol. We
need decisions on:

1. Wire format (binary, schema-evolvable)
2. Packet framing and channel semantics (reliable vs unreliable)
3. Connection lifecycle (handshake, heartbeat, disconnect)
4. Server data model (how a player is represented in the ECS)
5. Client representation (how a remote player is shown in-game)
6. Tick rates and the authoritative tick clock
7. How the client interpolates between server snapshots

Settling these now, in a small scope, prevents a Phase 2 rewrite when
animation/combat sync gets bolted on.

## Design

### Scope

**In:**
- 2–8 players in one fixed cell (Whiterun exterior, near the gates).
- Each connected client renders ghost actors for every other connected client.
- Position + yaw replicated at server-broadcast rate.
- Connection lifecycle: connect, welcome, heartbeat, graceful disconnect, timeout.
- Wire-format versioning baked into the handshake.

**Out (deferred):**
- Animation graph state (Phase 2)
- Combat / damage (Phase 2)
- Cell transitions, doors, containers (Phase 3)
- NPC simulation (Phase 4)
- Persistence, accounts (Phase 5)
- Reliable congestion control beyond a simple ack window (Phase 5/6)
- Anti-cheat (Phase 6+)
- Cloud deployment (Phase 6)

### Wire format: Flatbuffers

Schemas live in `schemas/v1/*.fbs`. Codegen output:

- Rust: `server/src/proto/` (via `flatc --rust`)
- C++: `client/plugin/src/proto/` (via `flatc --cpp`)

Codegen runs in the build (cargo build script for Rust, CMake custom
command for C++). `flatc` is installed via vcpkg (host-side dep) and rustup
isn't involved.

Why Flatbuffers (vs alternatives — see below): zero-copy reads matter at
60 Hz on the client, both languages have first-party support, schema
evolution rules match Hard Rule H3 (additive only).

### Framing

Each UDP datagram = one logical message. No fragmentation in Phase 1
(messages stay under typical MTU ~1200 bytes; a single position update is
~50 bytes). Datagram layout:

```
+--------+--------+--------+----------------+
| 0xRL   |  ver   |  type  | flatbuf body   |
| 1 byte | 1 byte | 1 byte | variable       |
+--------+--------+--------+----------------+
```

- `0xRL` (`0x52` `0x4C`) — magic prefix, two bytes. Cheap reject for stray packets.

  Wait — that's two bytes, not one. Correcting:

```
+--------+--------+--------+--------+----------------+
|  0x52  |  0x4C  |  ver   |  type  | flatbuf body   |
+--------+--------+--------+--------+----------------+
   "R"      "L"    1 byte   1 byte   variable
```

- `ver` — protocol version. **Phase 1 = `1`.** Mismatched versions get a
  `Disconnect{reason="version_mismatch"}` and the connection closes.
- `type` — discriminator for which flatbuffer table follows. Values listed
  below.

No reliability layer in Phase 1. Position updates are inherently lossy-OK —
the next snapshot supersedes it. Lifecycle messages (Hello, Welcome,
Disconnect) use a small app-level retry: send up to 3 times spaced 200 ms
apart, give up if no ack equivalent observed.

### Message types

| `type` | Direction       | Name           | Reliability |
| -----: | --------------- | -------------- | ----------- |
|      1 | C → S           | `Hello`        | retry x3    |
|      2 | S → C           | `Welcome`      | retry x3    |
|      3 | C → S           | `Heartbeat`    | unreliable  |
|      4 | C → S           | `LeaveNotify`  | retry x3    |
|      5 | S → C           | `Disconnect`   | retry x3    |
|     16 | C → S           | `PlayerInput`  | unreliable  |
|     17 | S → C           | `WorldSnapshot`| unreliable  |

Lifecycle types occupy `1..15`; world-state types start at `16` so we have
room to add more lifecycle/admin messages without bumping the version.

### Connection lifecycle

```
Client                                   Server
  | --------- Hello{name, ver=1} ----->  |
  |                                      |  spawn entity, assign player_id
  | <----- Welcome{player_id, tick_rate} |
  |                                      |
  | -- PlayerInput @60Hz ------------->  |  validate, write into ECS
  | <- WorldSnapshot @20Hz ------------  |  AoI cull, send snapshot
  | -- Heartbeat @1Hz --------------->   |  reset client timeout
  |                                      |
  | --------- LeaveNotify ----------->   |  remove entity
  | <-------- Disconnect{ack} --------   |
```

If the server doesn't see `Heartbeat` or `PlayerInput` from a client for 5 s,
it sends `Disconnect{reason="timeout"}` and removes the entity.

### Server data model (ECS)

Use `bevy_ecs` (just the ECS crate, no Bevy app/render). Components:

- `Player { id: PlayerId, name: String }` — identity.
- `Connection { addr: SocketAddr, last_heard: Instant }` — net state.
- `Transform { pos: Vec3, yaw: f32 }` — replicated state.
- `Velocity { v: Vec3 }` — for server-side dead-reckoning between inputs.
- `Cell { id: CellId }` — for AoI bucketing (single value in Phase 1).

Systems:

1. `recv_input` — drain UDP socket, decode `PlayerInput`, update `Transform`/`Velocity`.
2. `step_simulation` — server tick at 60 Hz; advance `Transform` from `Velocity`
   for clients that didn't send input this tick (smooths jitter).
3. `broadcast_snapshot` — at 20 Hz, build one `WorldSnapshot` per client
   containing every other player's `Transform`. (AoI cull is no-op in Phase
   1 since one cell = everyone sees everyone.)
4. `expire_stale` — drop connections whose `last_heard` exceeds the timeout.

### Client representation

The plugin maintains a map `player_id -> ghost actor TESObjectREFR*`.

- On `WorldSnapshot` containing a new `player_id`: spawn a ghost actor.
- On `WorldSnapshot` missing a previously-seen `player_id`: despawn the
  ghost actor.
- On every snapshot: update each ghost's transform target. Render uses
  interpolation (see below).

**Open question:** how to spawn the ghost. Options under evaluation:
- `PlaceAtMe` with the player's `Actor` form ID — clones the player, but
  Bethesda's behavior with `PlayerRef` clones is quirky and has been a
  multiplayer-mod footgun.
- Spawn a vanilla NPC (e.g., a Whiterun guard) and rename/retex.
- Ship an ESP file with a custom `ActorBase` for ghosts.

Phase 1 picks the cheapest that works (probably option 2). Picking the
canonical answer is a Phase 2 sub-task because animation sync constrains it.

### Interpolation

Server snapshots arrive at 20 Hz (50 ms cadence). Client renders ghosts
delayed by **100 ms** behind the latest snapshot, interpolating linearly
between the two snapshots that bracket the render time. This is standard
Source-engine-style snapshot interpolation. 100 ms covers a 5-snapshot
window — survives one packet loss with no visible glitch.

Local player is *not* interpolated. The local player is rendered as
Skyrim normally renders them; the client merely sends `PlayerInput` with the
local transform.

### Authority model

For Phase 1, position is **client-authoritative for the local player only**.
The server accepts whatever transform the client sends and rebroadcasts it
verbatim. This is technically a violation of Hard Rule H1 — but only
locally: the server is authoritative over *which* clients are in the world
and *whether* their messages reach others. No game-state mutation is taken
on a client's word.

Phase 2 tightens this: the server validates physical plausibility (no
teleport jumps, no superspeed) and applies clamps. Phase 4 (NPC sim) makes
the server actually simulate things, at which point full server authority is
trivial because the client just sends inputs, not transforms.

This is called out explicitly so we don't pretend H1 is satisfied — it's a
phased compromise with a known fix.

### Tick rates

- Client send (`PlayerInput`): **60 Hz**, hooked to the player's update tick.
  If the engine tick is unstable, throttle to 60 Hz with a fixed timer.
- Server simulation: **60 Hz** fixed. Server clock is the wall-clock
  `Instant`-based monotonic clock; tick number broadcast in `WorldSnapshot`.
- Server broadcast (`WorldSnapshot`): **20 Hz**. Reduces bandwidth ~3x
  versus broadcasting every tick, with no visible quality loss after
  interpolation.
- Heartbeat: **1 Hz**.

### Bandwidth budget

Per-direction, per-player, steady state:

- C → S: 60 × ~30 B `PlayerInput` ≈ 1.8 KB/s
- S → C: 20 × (3 B header + 30 B/player snapshot × N players) — for N=8,
  that's 20 × 243 B ≈ 4.9 KB/s

For 8 players, total server egress is 8 × 4.9 KB/s ≈ 40 KB/s. Trivial.

## Alternatives considered

### Wire format

| Option            | Why considered                            | Why rejected                          |
| ----------------- | ----------------------------------------- | ------------------------------------- |
| **Cap'n Proto**   | Zero-copy like FB, smaller wire size      | Less mature C++ tooling on Windows; fewer Rust users on it |
| **protobuf**      | Universally known                         | No zero-copy reads, allocation churn at 60 Hz |
| **postcard / serde** | Tiny, idiomatic Rust                   | C++ side has no native consumer; would have to handroll |
| **Custom binary** | Smallest deps                             | No schema evolution discipline; reinvents Flatbuffers' design |

### Transport

| Option                       | Why considered                          | Why rejected (for now)                 |
| ---------------------------- | --------------------------------------- | -------------------------------------- |
| **GameNetworkingSockets**    | Reliable channels, NAT, congestion ctrl | Heavy dep; we don't need congestion or NAT in Phase 1 (LAN/localhost). Adopt in Phase 5/6 when cloud needs it. |
| **QUIC (quinn / msquic)**    | Modern, encrypted, multiplexed          | Mandatory TLS overkill for LAN co-op; bigger handshake than Phase 1 needs |
| **renet** (Rust-only)        | Nice API                                | C++ side has no equivalent, would need re-implementation |

### Authority model

| Option                    | Why considered          | Why rejected                          |
| ------------------------- | ----------------------- | ------------------------------------- |
| **Full server authority** | Matches Hard Rule H1    | Requires server-side physics/movement simulation reverse-engineered from Skyrim — out of Phase 1 scope. Will arrive in Phase 2/4. |
| **Pure peer-to-peer**     | Simple                  | Would block MMO mode forever (Hard Rule H1 violation we can't undo) |

### Ghost-actor spawning

See "Open questions" — this is the one un-decided piece of Phase 1.

## Resolved decisions

### Ghost-actor spawning (resolved 2026-04-15)

Reliability-ranked options:

1. Custom ESP with purpose-built `ActorBase`
2. Runtime-created `ActorBase` via `TESDataHandler::CreateForm`
3. Hijack a vanilla NPC (e.g., Whiterun guard) and reposition
4. `PlaceAtMe(Player)` clone — last resort, known crash-prone

**Phase 1 ships option 1 (custom ESP) as the primary.** Code is structured
behind a `GhostSpawner` interface so additional implementations can be added
in follow-up PRs without restructuring. Runtime fallback chain (try option 2
on failure, then 3, then 4) is wired in only when the primary is observed
failing in real conditions — per Soft Rule S2.

The ESP is generated by a build step (xEdit script in `tools/build-esp/`)
checked into the repo. The setup script copies it into `Data/` alongside
the DLL. Distributing a single tiny ESP is acceptable install friction for
the reliability gain.

### Cell-entry detection (resolved 2026-04-15)

Reliability-ranked options:

1. `RE::TESCellAttachDetachEvent` sink (Bethesda event system)
2. SKSE `MessagingInterface` (`kPostLoadGame` + cell-check)
3. Function hook on `PlayerCharacter::SetParentCell` (Address-Library-resolved)
4. Poll `player->parentCell` every game tick

**Phase 1 ships option 1 as the primary** via a `RE::BSTEventSink<RE::TESCellAttachDetachEvent>`
registered against `RE::ScriptEventSourceHolder`. Same fallback discipline
as ghost spawning: `CellWatcher` interface with one implementation; add
others when needed.

### Yaw representation (resolved 2026-04-15)

**`f32` radians.** At Phase 1 scale (≤8 players, one cell) the `i16` quantized
saving is <100 B/s — well below the threshold where complexity is justified.
Revisit at Phase 5/6 when bandwidth is measured to matter.

Pitch and roll are not replicated in Phase 1 (player avatars are upright;
weapon-aim pitch arrives in Phase 2 alongside combat sync).

## Open questions (deferred to later phases)

1. **Tick clock origin.** Server tick number starts at 0 on server boot.
   Should the client also know the tick number for input lag visualization?
   Defer to Phase 2.
2. **Coordinate frame.** Skyrim uses cell-local coordinates within
   interior cells but world-coordinates within `TamrielWorldspace`. Phase 1
   is one exterior cell — world coordinates everywhere. Phase 3 (cell
   transitions) needs a wire format that distinguishes.

## Migration / rollout

- **Wire-format version:** `1`. Set in the packet header. Future
  versions get a new number; the server and client both check on `Hello`
  and refuse mismatched versions.
- **Phase 0 protocol removed entirely.** No backward compatibility — the
  text `HELLO`/`WORLD` exchange is replaced by binary `Hello`/`Welcome`.
  Phase 0 was alpha-zero; nothing else depends on it.
- **`tools/echo-client` updated** to send a binary `Hello`/`Heartbeat` so it
  remains useful as a manual smoke test.
- **`docs/ARCHITECTURE.md`** updated to reference v1 wire format and the
  Phase 1 ECS layout.

## Changelog

- 2026-04-15: initial draft.
- 2026-04-15: accepted. Resolved ghost-spawning (custom ESP primary, fallback
  chain structure), cell-entry detection (TESCellAttachDetachEvent primary),
  and yaw representation (f32 radians).
- 2026-04-15: ghost-spawning primary **revised** from option 1 (custom ESP)
  to option 3 (vanilla-NPC `PlaceObjectAtMe`) for Phase 1. Custom ESP
  properly done requires an ESP authoring/distribution pipeline outside the
  Phase 1 time budget. The `GhostSpawner` interface still holds; custom ESP
  is a future drop-in upgrade. Also: 100 ms interpolation deferred to step 5.5
  — Phase 1 step 5 ships snap-to-latest (visible stepping at 20 Hz) to get
  to the "two players see each other" milestone fastest.
