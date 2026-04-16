# Architecture

**Current state: Phase 2 in progress (steps 2.1 + 2.2 + 2.3 done).**
Players connect, replicate transforms, and see each other as ghost
actors that play the correct locomotion animation (walk/run/sneak),
weapon-draw/sheath transitions, and take/deliver melee damage —
server-validated swings produce DamageApply packets that the target's
client renders as a stagger. Steps 2.4-2.5 (transform validation,
pitch replication / ranged combat prep) are not started. The server
runs an authoritative ECS sim; the client is a thin SKSE plugin.

## Guiding principles

1. **Server-authoritative from day one.** Co-op mode runs the same server binary
   on localhost; MMO mode runs it in the cloud with extra services around it.
   Never a host-authoritative shortcut -- it would make MMO mode a rewrite.
2. **Thin client.** The SKSE plugin hooks the game and serializes state. No
   gameplay rules live on the client.
3. **No proprietary deps in core.** Cloud adapters (S3, managed Postgres, etc.)
   are pluggable, not baked in.
4. **Apache-2.0.** Avoid LGPL contamination -- do not link TiltedPhoques libs.

## Topology (Phase 2 in progress)

```
+-- Skyrim.exe + SKSE plugin (client) ----------------+
|  Sends PlayerInput @ 60 Hz: pos + yaw plus          |
|    locomotion graph vars (Speed, Direction,         |
|    IsRunning, IsSprinting, IsSneaking) and weapon   |
|    state (IsEquipping, IsUnequipping, iState,       |
|    weapon_drawn) read via GetGraphVariable*.        |
|  Receives WorldSnapshot @ 20 Hz.                    |
|  Spawns ghost actors, 100 ms interpolation; applies |
|    received graph vars to ghosts via                |
|    SetGraphVariableFloat/Bool/Int so they animate.  |
|  Cell gating: replication active per cell.          |
+-------------+---------------------------------------+
              | UDP (raw, unreliable)
              v
+-----------------------------------------------------+
|  Server (single binary, Rust)                       |
|  - bevy_ecs world: Player, Connection,              |
|    Transform, Velocity, AnimState components        |
|  - 60 Hz sim tick (integrate Transform; AnimState   |
|    holds the latest replicated graph-var values)    |
|  - 20 Hz WorldSnapshot broadcast (PlayerState as    |
|    a Flatbuffers table, carries anim + weapon)      |
|  - Connection lifecycle: Hello/Welcome,             |
|    Heartbeat, LeaveNotify, Disconnect,              |
|    timeout GC                                       |
|  - TOML config (server.toml)                        |
|  - Dual-stack IPv6 via socket2                      |
+-----------------------------------------------------+
```

Future phases add reliable channels (GameNetworkingSockets), interest
management, MMO services (auth, character DB, zone director), and
persistence. The topology diagram will grow -- but the single-binary
server stays the same.

## Technology choices

| Layer              | Choice                           | Why                                  |
| ------------------ | -------------------------------- | ------------------------------------ |
| Client plugin      | C++23, CommonLibSSE-NG (v4.14.0) | Forced -- this is the SKSE world.    |
| Client config      | tomlplusplus (via vcpkg)         | TOML parsing for SkyrimReLive.toml.  |
| Server             | Rust                             | Safety + perf; avoids C++ in sim.    |
| ECS                | bevy_ecs                         | Natural fit for replication.         |
| Transport          | Raw UDP (tokio + socket2)        | Simplest; dual-stack IPv6 support.   |
| Wire format        | Flatbuffers (protocol v2)        | Zero-copy, schema-evolvable.         |
| Packet header      | 4-byte `R L ver type` (ver=2)    | Magic + version before FB parsing.   |
| Server config      | toml (serde)                     | Simple file-based config.            |
| Co-op DB           | SQLite (planned)                 | Zero-install, file-based.            |
| MMO DB             | Postgres + Redis (planned)       | Standard horizontal story.           |
| License            | Apache-2.0                       | Permissive; forks allowed.           |

## Phase boundaries

See `ROADMAP.md`. Architecture must hold across phases -- each phase adds
layers, not rewrites. Watch for accidental client-authority in Phase 1/2.

Phase 1 trusts the client for its own transform (known caveat from proposal
0001). Server-side plausibility checks land in Phase 2 step 2.4 alongside
combat (proposal 0002).
