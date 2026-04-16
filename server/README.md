# server

Authoritative game server. Rust, bevy_ecs, Flatbuffers wire format
(protocol v2).

Runs a 60 Hz ECS simulation (Transform integration from Velocity; AnimState
holds the latest replicated animation graph variables per player) and
broadcasts `WorldSnapshot` to all connected clients at 20 Hz. Connection
lifecycle: Hello/Welcome handshake, Heartbeat keepalive, LeaveNotify for
graceful disconnect, server-initiated Disconnect (version mismatch, timeout).

## Run

```sh
cargo run --release
```

Debug builds work but release is recommended for accurate tick timing.

## Configuration

The server reads `server.toml` from the working directory on startup.
Override the path with `RELIVE_CONFIG=path/to/file.toml`. Missing or
malformed config falls back to compiled-in defaults (never a hard failure).

```toml
bind = "0.0.0.0:27015"      # UDP bind address
tick_rate_hz = 60            # sim tick rate
snapshot_rate_hz = 20        # WorldSnapshot broadcast rate
connection_timeout_s = 5     # no-packet timeout before despawn
gc_interval_ms = 500         # how often timeout sweep runs
```

### Dual-stack IPv6

Bind to `[::]:27015` to accept both IPv6 and IPv4 (v4-mapped) peers. The
server uses `socket2` to disable `IPV6_V6ONLY`, so a single `[::]` socket
serves both address families.

```toml
bind = "[::]:27015"
```

## Protocol

Binary Flatbuffers over UDP. Every packet has a 4-byte header:

```
+------+------+------+------+------------------+
| 0x52 | 0x4C | ver  | type | flatbuffer body  |
|  R   |  L   |  2   |      |                  |
+------+------+------+------+------------------+
```

Protocol version is `2` since Phase 2 step 2.1 (PlayerState was converted
from a Flatbuffers struct to a table to allow adding variable-length
fields). v1 clients receive `Disconnect{VersionMismatch}`.

Client-to-server messages: `Hello`, `Heartbeat`, `LeaveNotify`, `PlayerInput`.
Server-to-client messages: `Welcome`, `Disconnect`, `WorldSnapshot`.

`PlayerInput` carries the local player's transform plus a curated set of
animation graph variables (locomotion: `Speed`, `Direction`, `IsRunning`,
`IsSprinting`, `IsSneaking`; weapon state: `IsEquipping`, `IsUnequipping`,
`iState`, `weapon_drawn`). The server stores them in an `AnimState`
component and forwards them in `PlayerState` entries inside each
`WorldSnapshot`.

Schemas live in `schemas/v1/` (types.fbs, lifecycle.fbs, world.fbs). The
directory name reflects historical filesystem layout, not the on-wire
version byte -- see `schemas/README.md`.

## Tests

```sh
cargo test
```
