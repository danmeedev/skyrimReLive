# Roadmap

## Phase 0 — Foundations  ← **we are here**

- [x] Repo scaffold, license, gitignore
- [x] Rust server: hello-world UDP echo
- [x] Rust CLI test client
- [x] Rust toolchain validated end-to-end (round-trip on 2026-04-14)
- [x] vcpkg install + SKSE plugin builds (`SkyrimReLive.dll`, ~567 KB)
- [x] SKSE64 + Address Library installed; plugin loads in Skyrim SE 1.6.1170
- [x] SKSE plugin sends a UDP hello packet on `kDataLoaded` and logs server reply
- [x] End-to-end in-game round-trip validated 2026-04-15 (~1 ms)
- [ ] CI: cargo build, cargo test, cmake configure, clang-format check

## Phase 1 — Two players in one cell

Per accepted proposal `docs/proposals/0001-phase-1-replication.md`.

- [x] **Step 1: schemas + framing.** Flatbuffers schemas (`schemas/v1/`),
  4-byte packet header (R L ver type), Rust codegen, server speaks binary
  Hello/Welcome, version-mismatch closes with Disconnect.
- [x] **Step 2: server lifecycle + ECS skeleton.** `bevy_ecs` 0.16,
  components `Player/Connection/Transform/Velocity`, Hello→spawn entity,
  Heartbeat→touch last_heard, LeaveNotify→despawn, 5 s timeout sweep at 2 Hz.
  Echo-client gained `--keepalive`/`--leave` flags for lifecycle testing.
- [x] **Step 3: server sim tick + snapshot broadcast.** 60 Hz integration of
  Transform from Velocity, 20 Hz per-peer `WorldSnapshot` containing every
  *other* connected player. Verified two concurrent clients each receive
  20 snaps/s and see each other's `player_id`.
- [ ] Step 4: client (SKSE) sends PlayerInput at 60 Hz
- [x] **Step 5: ghost spawning + server-side PlayerInput handling.**
  Plugin reads WorldSnapshot and spawns a ghost actor (vanilla-NPC clone
  via `PlaceObjectAtMe`) for each remote player, snapping to the latest
  known transform. Despawns ghosts stale for >3 s. Server-side:
  `PlayerInput` now writes the peer's transform into the ECS. T2 and
  T3 validated (solo in-game, 2026-04-15): plugin connects, inputs
  flow, snapshots return, `rl status` shows counters advancing.
  Deferred: custom ESP spawner, 100 ms interpolation (step 5.5), proper
  HUD toast via Papyrus bridge (currently routes through ConsoleLog).
- [x] **Step 6: cell gating.** `CellWatcher` polls `PlayerCharacter::parentCell`
  on the main-thread tick; ghost rendering is gated on cell match.
  `target_cell` configurable via TOML + `rl cell` command; 0/"any" default
  keeps solo testing unrestricted. Validated in-game 2026-04-15.
- [x] **Step 5.5: 100 ms interpolation.** Per-ghost history deque (last 10
  snapshots with wall-clock arrival time); render target = `now - 100ms`
  with linear lerp between bracketing pair, shortest-arc yaw, clamp to
  nearest endpoint when outside history. Ghost motion now smooth instead
  of 20 Hz teleport. `rl demo start` spawns a synthetic orbiting ghost
  (Lydia clone, radius 150, 1 rev / 3 s) to validate rendering solo.

## Phase 2 — Animation + combat sync

Per accepted proposal `docs/proposals/0002-phase-2-animation-combat.md`.

- [x] **Step 2.1: locomotion animation sync (wire v2).**
  PlayerState converted struct→table; PlayerInput and PlayerState gain
  Speed/Direction/IsRunning/IsSprinting/IsSneaking fields. Server holds
  AnimState component; plugin reads graph vars from PlayerCharacter and
  applies to ghost actors via SetGraphVariableFloat/Bool. Demo ghost
  synthesizes run animation. Wire-format version bumped to 2.
- [x] **Step 2.2: weapon state sync.** PlayerInput/PlayerState gain
  IsEquipping/IsUnequipping/iState/weapon_drawn (additive to v2 tables,
  no version bump). Plugin reads the local player's graph vars + queries
  IsWeaponDrawn(); Ghost applies via SetGraphVariableBool/Int so remote
  ghosts play draw/sheath transitions and hold the right combat stance.
- [x] **Step 2.3a: server-side combat authority.** New CombatEvent (C→S)
  and DamageApply (S→C) message types. Server validates rate-limit,
  target existence, self-attack, range vs weapon_reach (with 50-unit
  slack); clamps damage; tracks Health component per player; sends
  DamageApply with stagger flag (≥30 damage). Echo-client `--attack`
  flag exercises the path. T2 verified end-to-end.
- [x] **Step 2.3b: plugin combat hook.** BSTEventSink<TESHitEvent>
  installed at kPostLoadGame. Local-player melee hits on a known ghost
  ship a CombatEvent with classified attack type (light/power/bash),
  weapon reach, and base damage; hits on NPCs / unknown actors are
  dropped. Incoming DamageApply schedules a main-thread task that fires
  staggerStart on the player's animation graph (when the server flagged
  damage ≥ 30) and prints a console hit summary. Sink is idempotent so
  reconnect cycles don't stack subscriptions.
- [ ] Step 2.5: pitch replication + ranged combat prep
- [ ] (deferred) Step 2.4: server-side transform validation (anti-teleport
  / speedhack). Friend-trust co-op doesn't need hardening — `coc` and
  similar shortcuts are explicitly fine. The validator design is still
  on the table for a future opt-in "strict mode" server-rules config
  when someone wants to host for untrusted players.

## Phase 3 — World state

- Cell transitions
- Containers, doors, activators
- Inventory replication (server-owned inventory)

## Phase 4 — NPC strategy

- Co-op: client-owned NPC simulation, others replicate
- MMO track: server-authoritative NPC AI subset (navmesh + behavior trees
  reimplemented server-side for MMO-relevant NPCs)

## Phase 5 — Persistence + accounts

- SQLite schema: characters, world diffs
- Optional auth service (co-op: none; MMO: proper accounts)

## Phase 6 — MMO services

- Zone director (ownership of cells across zone servers)
- Character DB migration SQLite → Postgres
- Horizontal zone scaling, seamless zone transfer

## Phase 7 — Content / gameplay systems

- Class/skill overlays
- PvP zones
- Economy, guilds
- Keizaal-style MMO content layer
