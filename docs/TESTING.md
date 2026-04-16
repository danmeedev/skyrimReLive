# Testing — Scopes & Success Criteria

Every checkbox in `ROADMAP.md` gets marked done only when the corresponding
tests below pass. If a test becomes flaky, flag it in the PR — we don't merge
with known-flaky gates.

## Test tiers

Cost and coverage go up as the tier number goes up. Run lower tiers first;
they catch 80% of regressions at <1% of the effort.

| Tier | Who drives it | Where it runs              | Round-trip time |
| ---- | ------------- | -------------------------- | --------------- |
| T0   | CI + cargo    | laptop, no game            | seconds         |
| T1   | Dev (local)   | one terminal, headless     | seconds         |
| T2   | Dev (local)   | 2+ terminals, headless     | ~10 s           |
| T3   | Dev (solo)    | Skyrim + server on one box | ~1 min          |
| T4   | Dev + cousin  | LAN, two game installs     | ~5 min          |
| T5   | Dev + cousin  | WAN, two game installs     | ~10 min         |

---

## T0 — Automated (CI gates)

Blocks every PR merge. Run via `cargo fmt --all && cargo clippy --workspace --all-targets -- -D warnings && cargo test --workspace` locally; GitHub Actions runs the same.

**Pass criteria (all must hold):**

1. `cargo fmt --all --check` — zero diff.
2. `cargo clippy --workspace --all-targets -- -D warnings` — zero warnings on Linux + Windows.
3. `cargo build --workspace --all-targets` — zero errors on Linux + Windows.
4. `cargo test --workspace` — all tests pass; current baseline: 8/8.
5. `cargo deny check bans licenses sources` — no LGPL/GPL/AGPL/MPL deps, no wildcard versions, no unknown git sources.
6. `clang-format --dry-run --Werror` — no diff in `client/plugin/src/`.
7. Plugin CMake configure + build succeeds (tracked, but optional in CI until the toolchain is Linux-reproducible).

**Rollback trigger:** any gate red on `main`. Revert the offending commit before further work.

---

## T1 — Local smoke (one terminal)

Sanity checks that the server and CLI tools start cleanly. ~30 s total.

**T1.1 — server starts with default config**

```sh
cd server && cargo run
```

Pass: log line `skyrim-relive-server listening bind=0.0.0.0:27015 version=2 tick_hz=60 snap_hz=20 timeout_s=5 pvp=false` appears within 2 s.

**T1.2 — server starts without a config file**

```sh
cd server && RELIVE_CONFIG=/tmp/nope.toml cargo run
```

Pass: log line `config load failed; using defaults` appears, then the listening line — server does not crash.

**T1.3 — echo-client happy path**

Server running from T1.1, in another terminal:

```sh
cd tools/echo-client && cargo run -- --name smoke
```

Pass: prints `Welcome { player_id = N, tick = 60 Hz, snapshot = 20 Hz, your_addr = ... }`; server logs `Hello accepted, entity spawned`.

**T1.4 — echo-client bad version is rejected**

```sh
cd tools/echo-client && cargo run -- --bad-version
```

Pass: client prints `Disconnect { code = VersionMismatch, reason = "server speaks v2" }`; server logs `version mismatch; sending Disconnect`.

**T1.5 — PvP disabled blocks combat events**

Server running with default config (`pvp_enabled = false`). Two echo-clients connected:

```sh
cd tools/echo-client && cargo run -- --name alice --keepalive 3 --attack 2
cd tools/echo-client && cargo run -- --name bob   --keepalive 3
```

Pass: server logs `CombatEvent dropped: PvP disabled` for each attack. No `combat hit applied` lines.

**T1.6 — PvP enabled allows combat events**

Server config: `pvp_enabled = true`. Repeat T1.5.

Pass: server logs `combat hit applied` lines. No `PvP disabled` lines.

---

## T2 — Local multi-client smoke

Exercises replication without the game. Run every time the wire format,
server sim, or broadcast changes.

**T2.1 — two clients see each other**

```sh
# Terminal 1: server
cd server && cargo run

# Terminal 2 and 3, started within ~1 s of each other:
cd tools/echo-client && cargo run -- --name alice  --keepalive 3 --leave
cd tools/echo-client && cargo run -- --name bob    --keepalive 3 --leave
```

Pass (all):
- Each client's final line reports ~60 snapshots received over 3 s (≈20/s).
- `saw other player_ids` on alice contains bob's id and vice versa.
- Server log shows both Hello/spawn and both Leave/despawn events, no warnings.

**T2.2 — timeout expiry**

```sh
cd tools/echo-client && cargo run -- --name ghost  # no --keepalive, no --leave
# wait ≥ 5s + connection_timeout_s
```

Pass: server logs `connection timed out, despawning` within ~5.5 s of the Hello.

**T2.3 — reconnect from same addr replaces old entity**

Start a client twice from the same terminal without --leave in between. (Second Hello arrives before the first client's timeout.)

Pass: server logs `replacing previous connection from same addr` on the second Hello; only one entity remains in the world.

---

## T3 — Solo in-game (one Skyrim, one server)

Validates the SKSE plugin in the real engine. Required before marking any
Phase 1 step ≥ 4 done.

**Preconditions:**
- SkyrimSE.exe not running.
- `tools/setup.ps1` has completed cleanly at least once on this machine.
- AL `versionlib-1-6-1170-0.bin` exists in `Data/SKSE/Plugins/`.

**T3.1 — plugin loads**

```powershell
.\tools\launch.ps1
```

Let Skyrim load to the main menu. Then:

```sh
cat "C:/Users/danme/Documents/My Games/Skyrim Special Edition/SKSE/skse64.log" | grep SkyrimReLive
```

Pass: line `loading plugin "SkyrimReLive"` appears; no `disabled` suffix.

**T3.2 — plugin connects on kDataLoaded**

Load any save (or new game).

```sh
cat "C:/Users/danme/Documents/My Games/Skyrim Special Edition/SKSE/SkyrimReLive.log"
```

Pass (in order, within a few seconds of entering the world):
- `config loaded from Data/SKSE/Plugins/SkyrimReLive.toml: host=127.0.0.1 port=27015 name=...`
- `net client started; sending PlayerInput @60Hz`
- `Welcome: player_id=N tick=60Hz snap=20Hz`

Server log shows: `Hello accepted, entity spawned` for that player_id.

**T3.3 — PlayerInput stream at steady rate**

Stay connected for 10 s. Pass: no `recv_from failed` warnings on server (other than the expected 10054 after a previous timed-out peer); no plugin-side error lines.
(Step 4 does not yet log position to server — the *absence* of errors is the success signal, plus the fact that `WorldSnapshot` traffic keeps flowing.)

**T3.4 — disconnect is clean**

Exit Skyrim via the menu (not Alt-F4). Pass: server log eventually shows `connection timed out, despawning` for that player_id within `connection_timeout_s` seconds. No crashes in `SkyrimReLive.log`.

**T3.5 — console commands work**

Open console with `~`. Test each:

| Command              | Expected                                                    |
|----------------------|-------------------------------------------------------------|
| `rl status`          | `state=connected server=... player_id=N ghosts=N ...`       |
| `rl cell`            | Shows current cell FormID (nonzero when in-world)           |
| `rl demo start`      | Ghost actor spawns, orbits you at run speed                 |
| `rl demo stop`       | Ghost despawns within 1 second                              |
| `rl disconnect`      | `state=idle` on next `rl status`                            |
| `rl connect`         | Reconnects, new player_id assigned                          |
| `rl help`            | Lists all commands                                          |

Pass: all commands respond as listed. No console errors, no crash.

**T3.6 — demo ghost plays locomotion animation**

Run `rl demo start`. Watch the ghost.

Pass: ghost visibly runs in a circle (not T-posing, not sliding, not standing still). Arms and legs move. Ghost faces tangent to its orbit path.

Fail indicators: T-pose = AI / graph issue. Sliding without animation = ActorState bits not applied. Standing still = Speed value not reaching ghost.

**T3.7 — cell change detection (solo)**

Walk from Whiterun exterior into Dragonsreach.

Pass: `SkyrimReLive.log` shows no errors during load screen. After entering, `rl cell` reports a different FormID than when outside. `rl status` still shows `state=connected`.

**T3.8 — pitch capture (solo)**

Aim up at the sky, then down at the ground. Check `SkyrimReLive.log` or add a temporary debug line.

Pass: pitch value changes when you aim up/down (negative = up, positive = down in Skyrim convention). Value near 0 when looking straight ahead.

**Failure modes to watch for:**
- `plugin SkyrimReLive.dll disabled, address library needs to be updated` → AL binaries not installed.
- `failed to open address library` dialog → same.
- `timed out waiting for Welcome` in SkyrimReLive.log → server not running, or firewall blocking UDP 27015.
- Skyrim hard crashes at main menu → plugin ABI mismatch (usually runtime version drift).

---

## T4 — LAN co-op (two machines, same network)

Gates Phase 1 step 5 "done" — you can't claim the ghost actor works without
two players.

**Preconditions:**
- Both machines on the same LAN.
- Host has `server_host = "0.0.0.0"` (or the LAN-facing interface) and Windows Defender allows inbound UDP 27015 for `skyrim-relive-server.exe`.
- Client machine's `Data/SKSE/Plugins/SkyrimReLive.toml` has `server_host = "<host LAN IP>"`.

**T4.1 — client connects from second machine**

Host runs server. Client launches Skyrim via skse64_loader.

Pass: server logs both Hellos with distinct peer IPs; each `SkyrimReLive.log` reports the other player_id in its snapshot counter.

**T4.2 — ghost actor appears (Phase 1 step 5+)**

Both players spawn in Whiterun exterior. Player A stands still. Player B walks toward Player A.

Pass: Player A sees a character where Player B is, moving in a recognizable way within 150 ms of B's actual motion.

**T4.3 — ghost disappears on disconnect**

Player B exits to main menu or closes Skyrim.

Pass: within `connection_timeout_s` seconds, Player A's client despawns the ghost actor — no permanent orphan in the world.

**T4.4 — LAN jitter doesn't break interpolation**

Simulate moderate wifi by having player B run from LAN cable → wifi mid-session (or tether through phone).

Pass: ghost motion may stutter briefly but never teleports violently, and doesn't desync once the connection stabilizes.

**T4.5 — locomotion animation (walk / run / sneak)**

Both players in Whiterun exterior. Player B walks, then runs, then sneaks.

Pass: Player A sees the ghost:
- Walk: slow gait, arms relaxed
- Run: fast gait, arms pumping
- Sneak: crouched posture, slower movement

Fail indicators: ghost slides without leg animation (ActorState bits not applied), ghost stuck in idle while position tracks (Speed not reaching ghost), ghost T-poses (behavior graph not processing).

**T4.6 — weapon draw/sheath animation**

Player B draws a weapon, waits 3 s, sheathes it.

Pass: Player A sees the ghost play the draw animation, hold the weapon-drawn stance, then play the sheath animation.

Fail indicators: weapon pops in/out without animation (graph vars not reaching ghost), ghost never draws (IsEquipping not sent), ghost draws but never sheathes (IsUnequipping not sent).

**T4.7 — melee combat (PvP enabled)**

Server config: `pvp_enabled = true`. Both in same cell, close range.

Player A draws a sword and swings at Player B's ghost.

Pass:
- Player A's `SkyrimReLive.log`: `shipped CombatEvent: target_pid=N class=0 type=0 ...`
- Server log: `combat hit applied attacker=... target=... damage=... new_hp=...`
- Player B sees: console message `[SkyrimReLive] hit by player_id=N for X dmg (HP Y)`. If damage ≥ 30, Player B also staggers visibly.

**T4.8 — melee combat (PvP disabled)**

Server config: `pvp_enabled = false` (default). Player A swings at Player B.

Pass:
- Player A's log still shows `shipped CombatEvent`
- Server log: `CombatEvent dropped: PvP disabled`
- Player B sees nothing — no stagger, no console message

**T4.9 — ranged combat (bow)**

Server config: `pvp_enabled = true`. Player A equips a bow, stands ~30 feet from Player B's ghost. Player A shoots Player B.

Pass:
- Player A's log: `shipped CombatEvent: ... class=1 ...` (BowArrow)
- Server log: `combat hit applied`
- Player B: stagger + console hit message (if damage ≥ 30)

Fail indicators: server rejects with "out of range" (reach check using melee path instead of ranged), attack_class still 0 (classification not working).

**T4.10 — ranged combat (spell)**

Player A equips a destruction spell (e.g., Flames, Firebolt), casts at Player B's ghost.

Pass:
- Player A's log: `shipped CombatEvent: ... class=2 ...` (Spell)
- Server log: `combat hit applied` with sentinel damage value (~25)
- Player B: console hit message

Note: Flames (concentration spell) may fire multiple TESHitEvents quickly. Server rate-limits to 5 hits/s — some may be dropped. Expected.

**T4.11 — pitch visible on ghost**

Player B aims straight up at the sky.

Pass: Player A sees Player B's ghost actor visibly tilted upward (head/torso angled). Player B aims down → ghost tilts down.

Fail indicators: ghost always faces straight ahead regardless of B's aim (pitch not sent or not applied via SetAngle).

**T4.12 — cell transition: enter interior together**

Both players stand outside Dragonsreach. Player A enters. Player B enters.

Pass:
- While only A is inside: B's ghost is NOT visible inside Dragonsreach (different cells, AoI filter active)
- Once both are inside: B's ghost spawns inside Dragonsreach. Both can see each other.
- A's log: `ghost player_id=N cell mismatch ... despawning` when B was outside; `spawned ghost for player_id=N` when B enters.

**T4.13 — cell transition: exit interior**

From T4.12, Player B exits Dragonsreach back to the exterior.

Pass:
- Player A (still inside) sees B's ghost despawn
- Player A exits too → both back in exterior, ghosts respawn

**T4.14 — cell transition: fast travel**

Player A fast-travels to Riften while Player B stays in Whiterun.

Pass: B's ghost of A despawns. A doesn't see B anymore (different cells). No crashes during load screen. `rl status` still shows connected.

**T4.15 — ghost despawn on disconnect (multiplayer)**

Player B exits Skyrim or `rl disconnect`.

Pass: within ~5 s (connection_timeout_s + staleness timer), Player A sees B's ghost despawn. No orphaned ghost left in the world.

**T4.16 — reconnect after disconnect**

Player B does `rl disconnect`, then `rl connect`.

Pass: B gets a new player_id. A sees B's ghost despawn (old), then respawn (new). Both `rl status` show connected with correct ghost count.

**T4.17 — power attack and bash classification**

PvP enabled. Player A does a power attack (hold attack button) on B's ghost, then a bash (block + attack).

Pass:
- Power attack log: `type=1` (power)
- Bash log: `type=2` (bash)
- Server processes both without rejection

**Failure modes:**
- Client can't reach server → firewall on host; check with `nc -u <host> 27015` from client.
- Connection works but no ghost → check server's `broadcast_snapshot` actually sends to both peers.
- Ghost spawns but drifts away and never catches up → interpolation-buffer math is off.
- Combat events dropped with "out of range" → distance calc wrong or reach values off.
- Animations play on wrong player → player_id mapping error in ghost manager.
- Cell transition crashes → parentCell null-check missed somewhere.

---

## T5 — WAN co-op (across the internet)

Gates readiness for public hosting. Unlocks moving past Phase 1.

**Preconditions:**
- Host has UDP 27015 port-forwarded from their router, OR is using a VPN mesh (Tailscale, ZeroTier) that both players have joined.
- Client `server_host` is the host's WAN IP (or the VPN IP).
- Host's public IP is stable for the duration of the test (check with https://api.ipify.org).

**T5.1 — connect across WAN**

Same protocol as T4.1 but across the internet.

Pass: same as T4.1, plus round-trip latency (Welcome reply time vs Hello send time, measurable from client logs) is under 150 ms for most home-internet pairings.

**T5.2 — packet loss tolerance**

Run the session for 60 s with at least 1 minute of movement from both players. Use a tool like `clumsy` (https://jagt.github.io/clumsy/) on the host to drop 2% of outbound UDP.

Pass: ghost motion visibly degraded but not broken; no hard disconnects; snapshot receive-rate drops by less than ~5% (20 Hz → ≥ 19 Hz after loss).

**T5.3 — rejoin after ISP reconnect**

Client disconnects their network for 10 s (disable wifi), reconnects. Phase 1 has no reconnect logic — this test should *fail cleanly*: the client times out, sends Disconnect, and on rejoin the user has to restart Skyrim. That's the current expected behavior; upgrade gate moves to Phase 2.

---

## Pre-merge checklist (copy into every non-trivial PR)

- [ ] T0 green locally (fmt, clippy `-D warnings`, test, deny, clang-format)
- [ ] T1 smoke runs clean if this PR touches net code
- [ ] T2 run green if this PR touches wire format, server sim, or broadcast
- [ ] T3 solo in-game run green if this PR touches the plugin
- [ ] T4 LAN run green if this PR touches replication, ghost rendering, or cell handling
- [ ] T5 WAN run green if this PR is cutting a release / tagging a milestone
- [ ] Bandwidth budget note in PR description if this PR adds per-tick traffic
- [ ] New proposal linked if this PR changes wire format or authority model

## Per-phase acceptance summary

| Phase / Step              | Required test tier | Key test cases | Status |
| ------------------------- | ------------------ | -------------- | ------ |
| 0 — hello round-trip      | T1, T3             | T1.1, T1.3, T3.1, T3.2 | ✅ done |
| 1.1 — schemas + framing   | T0, T1             | T1.4 | ✅ done |
| 1.2 — ECS lifecycle       | T0, T1, T2         | T2.1, T2.2, T2.3 | ✅ done |
| 1.3 — sim + snapshot      | T0, T2             | T2.1 | ✅ done |
| 1.4 — plugin sends input  | T0, T3             | T3.3 | ✅ done |
| 1.5 — ghosts + interp     | T0, T4             | T3.6, T4.2, T4.4 | ✅ done |
| 1.6 — cell gating         | T0, T3, T4         | T3.5, T3.7 | ✅ done |
| 2.1 — locomotion anim     | T0, T3, T4         | T3.6, T4.5 | ✅ done |
| 2.2 — weapon state        | T0, T3, T4         | T4.6 | ✅ done |
| 2.3a — server combat auth | T0, T1, T2         | T1.5, T1.6 | ✅ done |
| 2.3b — plugin combat hook | T0, T4             | T4.7, T4.8, T4.17 | ✅ done |
| 2.4 — transform validation| —                  | — | deferred |
| 2.5 — pitch + ranged      | T0, T4             | T3.8, T4.9, T4.10, T4.11 | ✅ done |
| 3.1 — cell-aware AoI      | T0, T3, T4         | T3.7, T4.12, T4.13, T4.14 | ✅ done |
| 3.2 — cell transition msg | T0, T4             | — | future |
| 3.3 — doors/activators    | T0, T4             | — | future |
| 3.4 — container sync      | T0, T4             | — | future |
| 4 — NPC strategy          | T0, T4             | — | future |
| 5 — persistence + auth    | T0, T4, T5         | — | future |
| 6 — MMO services          | T0, T4, T5         | — | future |
| 7 — content / gameplay    | T0, T4, T5         | — | future |

---

## Adding a new test

When a class of bug escapes this list, add its reproduction here — don't just
fix it. A bug that made it past T0–T5 once will make it past them again.

File the new test case under the lowest tier that reliably catches it, and
mark in the commit message *which prior bug class it's guarding against*.
