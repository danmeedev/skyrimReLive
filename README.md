# SkyrimReLive

Open-source multiplayer for Skyrim Special Edition. Single codebase scales
from self-hosted small-group co-op on a home PC to cloud-deployed MMO.

Inspired by Skyrim Together Reborn and Keizaal Online. Independently
implemented, Apache-2.0 licensed, no proprietary dependencies.

**Current status: Phase 2 in progress.** Steps 2.1 (locomotion animation
sync) and 2.2 (weapon state sync) are done; steps 2.3-2.5 (combat events
+ damage authority, transform validation, pitch replication) remain.
Players connect, see each other as ghost actors that walk, run, sneak,
and draw/sheath weapons in sync, and move in real time across a shared
cell. The server runs an authoritative ECS sim at 60 Hz and broadcasts
world snapshots at 20 Hz over a binary Flatbuffers wire format
(protocol v2).

---

## Pick your path

### I want to play on a friend's server

Read **[`docs/PLAYER_SETUP.md`](docs/PLAYER_SETUP.md)**.

Short version: install Skyrim SE + SKSE64 + Address Library + our plugin,
edit `SkyrimReLive.toml` (server host/port, player name), launch via
`skse64_loader.exe`. The plugin auto-connects on save load and renders
other players as ghost actors.

### I want to host a server (for friends or publicly)

Read **[`docs/HOST_SETUP.md`](docs/HOST_SETUP.md)**.

Short version: `cargo run --release` in `server/`. Configure via
`server.toml` (bind address, tick rates, timeout). Networking options:
Tailscale recommended for friend groups; IPv6 dual-stack or port-forwarding
as fallback.

### Quick reference for commands, configs, and troubleshooting

Read **[`docs/CHEATSHEET.md`](docs/CHEATSHEET.md)**.

### I want to contribute code

Read **[`CONTRIBUTING.md`](CONTRIBUTING.md)** and
**[`docs/DEV_RULES.md`](docs/DEV_RULES.md)** before your first PR.
Architecture lives in **[`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md)**,
phased plan in **[`docs/ROADMAP.md`](docs/ROADMAP.md)**, test protocols in
**[`docs/TESTING.md`](docs/TESTING.md)**.

---

## Repository layout

```
client/plugin/     SKSE plugin (C++, CommonLibSSE-NG via CMake FetchContent).
                   Connects to server, sends PlayerInput, renders ghosts.
server/            Rust server (bevy_ecs, tokio, flatbuffers). Authoritative
                   sim at 60 Hz, snapshot broadcast at 20 Hz.
tools/             setup.ps1 / launch.ps1, echo-client, regen-protos.sh.
schemas/v1/        Flatbuffers schemas (types.fbs, lifecycle.fbs, world.fbs).
docs/              Architecture, roadmap, testing, setup guides, proposals.
```

## 30-second smoke test (no game required)

```sh
# Terminal 1 -- server
cd server && cargo run

# Terminal 2 -- echo client (binary Hello, not text)
cd tools/echo-client && cargo run -- --name smoke
```

Expected output in terminal 2:

```
sent: Hello { name = "smoke", version = 1 }
Welcome { player_id = 1, tick = 60 Hz, snapshot = 20 Hz, your_addr = Some("127.0.0.1:<port>") }
```

The echo-client sends a binary Flatbuffers Hello with the 4-byte `RL`
packet header and receives a binary Welcome back. This proves the transport,
wire format, and connection lifecycle without needing Skyrim installed.

For a longer test, add `--keepalive 10` to stay connected for 10 seconds,
sending synthetic PlayerInput at 20 Hz and receiving WorldSnapshot packets.

## License

Apache-2.0. See `LICENSE`. No LGPL/GPL/AGPL/MPL dependencies -- full policy
and rationale in `docs/DEV_RULES.md`.
