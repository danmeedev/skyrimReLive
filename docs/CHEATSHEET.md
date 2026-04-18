# SkyrimReLive Cheatsheet

Quick reference for everything you need during play, hosting, and development.

---

## In-game console commands

Press `~` to open the Skyrim console, then type:

| Command                      | What it does                                          |
| ---------------------------- | ----------------------------------------------------- |
| `rl status`                  | Connection state, player_id, ghost count, inputs sent, snapshots received, your current position + yaw |
| `rl connect`                 | Connect using values from `SkyrimReLive.toml`         |
| `rl connect 192.168.1.5`     | Connect to a specific host (default port 27015)       |
| `rl connect 192.168.1.5 27016` | Connect to a specific host and port                |
| `rl disconnect`              | Close the connection cleanly                          |
| `rl cell`                    | Show current cell form ID, target cell, active status |
| `rl cell set`                | Pin target to the cell you're standing in right now   |
| `rl cell set 0x1A27A`        | Pin target to a specific cell form ID                 |
| `rl cell clear`              | Clear target (any cell is active)                     |
| `rl demo start`              | Spawn a Lydia clone that orbits you (solo ghost test) |
| `rl demo stop`               | Stop the demo ghost (despawns after ~3 s)             |
| `rl players`                 | Player list: name, level, top skills, HP, cell        |
| `rl chat <msg>`              | Send text chat to all connected players                |
| `rl admin`                   | Authenticate as admin (open by default, no password)   |
| `rl admin <password>`        | Authenticate with admin password                       |
| `rl cmd pvp on\|off`         | Toggle PvP for all players (admin)                     |
| `rl cmd kick <id>`           | Disconnect a player by ID (admin)                      |
| `rl cmd time <hour>`         | Set time of day for all (0-24) (admin)                 |
| `rl cmd weather <preset>`    | Set weather: clear/rain/snow/storm/fog/<formid> (admin)|
| `rl cmd give <pid> <formid> [count]` | Give item(s) to a player (admin)              |
| `rl cmd spawn <base_formid>` | Spawn NPC or object at your position (admin)           |
| `rl cmd npcs`                | List spawned NPCs with zeus_ids (admin)                |
| `rl cmd npc <zid> <order>`   | NPC order: follow/wait/moveto/aggro/combat/passive/delete (admin) |
| `rl cmd obj <zid> <action>`  | Object action: delete/moveto (admin)                   |
| `rl cmd tp <pid> <x y z>`   | Teleport player to coordinates (admin)                 |
| `rl cmd tp <pid> tome`       | Teleport player to your position (admin)               |
| `rl cmd help`                | List all admin commands (admin)                        |
| `rl help`                    | List all subcommands                                  |
| **F8**                       | Toggle ImGui admin overlay (Zeus UI)                   |

---

## Client config (`SkyrimReLive.toml`)

Location: `<Skyrim>/Data/SKSE/Plugins/SkyrimReLive.toml`

```toml
# Server to connect to
server_host = "127.0.0.1"       # localhost for solo; LAN/Tailscale IP for friends
server_port = 27015              # default UDP port

# Your display name (shows in server logs)
player_name = "dovahkiin"

# Auto-connect when a save loads? Set false to require manual `rl connect`
auto_connect = true

# Restrict replication to one cell (hex form ID). 0 = any cell allowed.
# Use `rl cell` in-game to find your current cell, then `rl cell set` to pin.
target_cell = "0x0"
```

**Common `server_host` values:**
- `127.0.0.1` — solo test (server on same machine)
- `100.x.y.z` — Tailscale IP (get from host: `tailscale ip -4`)
- `192.168.x.y` — LAN IP (get from host: `ipconfig`)
- `2001:db8::1` — IPv6 direct (both sides need IPv6)
- `203.0.113.5` — WAN IPv4 (host must port-forward UDP 27015)

---

## Server config (`server.toml`)

Location: `server/server.toml` (or override with `RELIVE_CONFIG=path`)

```toml
# UDP bind address. [::]:27015 = dual-stack (IPv4 + IPv6).
# Use 0.0.0.0:27015 for IPv4-only.
bind = "[::]:27015"

# Server simulation tick rate (Hz). Integrates Transform from Velocity.
tick_rate_hz = 60

# WorldSnapshot broadcast rate (Hz). Each connected client gets one per tick.
snapshot_rate_hz = 20

# Despawn a client after this many seconds of silence.
connection_timeout_s = 5

# How often the timeout sweep runs (ms).
gc_interval_ms = 500

# Admin password. Empty string = open (no password required).
admin_password = ""

# How often to poll player data for `rl players` (seconds).
player_list_poll_s = 5.0

# PvP toggle. false = melee/ranged/spell hits between players are dropped.
pvp_enabled = false

# Default spell damage (flat sentinel until real magnitude reading).
spell_damage_default = 25.0
```

> **Note:** `server.toml` is auto-generated with all fields documented on
> first run if it doesn't exist.

---

## Server usage

```sh
# Run (release recommended for accurate tick timing)
cd server && cargo run --release

# Run with custom config
RELIVE_CONFIG=/path/to/custom.toml cargo run --release

# Run with debug logging
RUST_LOG=debug cargo run --release

# Build only (no run)
cargo build --release
# Binary at: target/release/skyrim-relive-server.exe
```

**Server log output (what the lines mean):**

| Log line                                | Meaning                                      |
| --------------------------------------- | -------------------------------------------- |
| `skyrim-relive-server listening`        | Server started, accepting connections        |
| `Hello accepted, entity spawned`        | A client connected                           |
| `LeaveNotify accepted, entity despawned`| A client disconnected gracefully             |
| `connection timed out, despawning`      | Client went silent for > `connection_timeout_s` |
| `version mismatch; sending Disconnect`  | Client has wrong protocol version            |
| `replacing previous connection`         | Same IP:port reconnected (old entity dropped)|

---

## Launch scripts

```powershell
# First-time setup (installs SKSE, builds plugin, deploys)
.\tools\setup.ps1

# Every launch (rebuilds, deploys, starts server, launches Skyrim)
.\tools\launch.ps1

# Launch without rebuilding
.\tools\launch.ps1 -NoRebuild

# Launch without starting server (connect to a remote one)
.\tools\launch.ps1 -NoServer

# Launch and auto-stop server when Skyrim exits
.\tools\launch.ps1 -WaitForGame
```

From git-bash: use `tools/setup.sh` / `tools/launch.sh` (thin wrappers).

---

## Echo-client (headless testing without Skyrim)

```sh
# Quick handshake test
cd tools/echo-client && cargo run -- --name smoke

# Stay connected 10s, send synthetic motion, count snapshots
cargo run -- --name alice --keepalive 10 --leave

# Test version mismatch
cargo run -- --bad-version

# Two clients in parallel (verify they see each other)
./target/debug/skyrim-relive-echo-client --name alice --keepalive 5 --leave &
./target/debug/skyrim-relive-echo-client --name bob   --keepalive 5 --leave &
wait
```

---

## Networking quick reference

| Method         | Host does                                  | Client sets `server_host` to |
| -------------- | ------------------------------------------ | ---------------------------- |
| **Tailscale**  | Install Tailscale, invite friend           | Host's `100.x.y.z`          |
| **IPv6**       | Bind `[::]:27015`, open firewall UDP 27015 | Host's global IPv6 addr      |
| **Port forward** | Router: forward UDP 27015 → LAN IP      | Host's WAN IPv4 (ipify.org)  |
| **LAN**        | Nothing extra                              | Host's `192.168.x.y`        |
| **Same PC**    | Nothing extra                              | `127.0.0.1`                 |

**Firewall (Windows Defender):**
```powershell
New-NetFirewallRule -DisplayName "SkyrimReLive" -Direction Inbound -Protocol UDP -LocalPort 27015 -Action Allow
```

**Check if port is reachable (from client machine):**
```sh
nc -u <host-ip> 27015
# type anything, hit Enter — server should log "packet rejected"
```

---

## Log file locations

| Log                    | Path                                                                        |
| ---------------------- | --------------------------------------------------------------------------- |
| Plugin log             | `%USERPROFILE%\Documents\My Games\Skyrim Special Edition\SKSE\SkyrimReLive.log` |
| SKSE log               | `%USERPROFILE%\Documents\My Games\Skyrim Special Edition\SKSE\skse64.log`  |
| Server log             | stdout (the terminal window `launch.ps1` opens)                            |
| Plugin config          | `<Skyrim>\Data\SKSE\Plugins\SkyrimReLive.toml`                            |
| Server config          | `server\server.toml` (or `$RELIVE_CONFIG`)                                |

---

## Troubleshooting

| Symptom                                     | Likely cause                          | Fix                                              |
| ------------------------------------------- | ------------------------------------- | ------------------------------------------------ |
| `rl status` → `state=idle`                  | Not connected yet                     | `rl connect` or check `auto_connect` in TOML     |
| `timed out waiting for Welcome`             | Server not running or unreachable     | Start server; check host/port/firewall            |
| `plugin disabled, address library`          | Address Library not installed         | Get from Nexus mod 32444, copy .bin files         |
| `inputs_sent=0` after walking               | `parentCell` null (not in game world) | Load a save first; check for main-menu connect    |
| `snapshots_received=0` but `inputs_sent` ok | Server not broadcasting               | Check server log for errors                       |
| `ghosts=0` with two players connected       | Both in different cells + target set  | `rl cell clear` on both clients                   |
| Ghost appears but doesn't move              | Interpolation buffer empty            | Wait 200 ms; if still stuck, check server log     |
| Ghost teleports instead of gliding          | Only 1 snapshot in buffer             | Network jitter; will self-correct after ~500 ms   |
| Skyrim freezes on load                      | Plugin blocking main thread           | Likely fixed; update to latest DLL                |
| `version mismatch` on connect               | Client/server built from different code | Rebuild both from same commit                   |
| Server shows `recv_from failed`             | Stale ICMP from dead client           | Harmless (suppressed for code 10054)              |

---

## Dev commands

```sh
# Full CI check (run before every commit)
cargo fmt --all
cargo clippy --workspace --all-targets -- -D warnings
cargo test --workspace

# Rebuild plugin
cd client/plugin
cmake --build build --config Release

# Regenerate flatbuffer code after schema changes
tools/regen-protos.sh

# Build server release binary
cd server && cargo build --release
```

---

## Key files at a glance

```
SkyrimReLive.toml          client config (host, port, name, auto_connect, target_cell)
server.toml                server config (bind, tick rates, timeout)
CLAUDE.md                  AI agent rules (commit policy, build quirks)
docs/ROADMAP.md            phase plan with checkboxes
docs/TESTING.md            test tiers T0-T5 with pass criteria
docs/HOST_SETUP.md         hosting guide (Tailscale, IPv6, port-forward)
docs/PLAYER_SETUP.md       player install guide
docs/DEV_RULES.md          hard rules H1-H5, soft rules S1-S7
docs/proposals/0001-*.md   Phase 1 replication design (accepted)
```
