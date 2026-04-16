# server

Authoritative game server. Rust, bevy_ecs, Flatbuffers wire format v1.

Runs a 60 Hz ECS simulation (Transform integration from Velocity) and
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
|  R   |  L   |  1   |      |                  |
+------+------+------+------+------------------+
```

Client-to-server messages: `Hello`, `Heartbeat`, `LeaveNotify`, `PlayerInput`.
Server-to-client messages: `Welcome`, `Disconnect`, `WorldSnapshot`.

Schemas live in `schemas/v1/` (types.fbs, lifecycle.fbs, world.fbs).

## Tests

```sh
cargo test
```
