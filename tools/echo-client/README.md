# echo-client

Rust CLI test tool. Sends a binary Flatbuffers `Hello` (with the 4-byte `RL`
packet header) to the server and prints the `Welcome` or `Disconnect` reply.
Validates the full wire format and connection lifecycle without Skyrim.

## Usage

```sh
cargo run -- --name dovahkiin
cargo run -- --name dovahkiin --server 127.0.0.1:27015
```

## Flags

| Flag                   | Description                                            |
| ---------------------- | ------------------------------------------------------ |
| `--name <name>`        | Player display name (default: `dovahkiin`).            |
| `--server <host:port>` | Server address (default: `127.0.0.1:27015`).           |
| `--keepalive <secs>`   | Stay connected for N seconds after Welcome. Sends      |
|                        | Heartbeat at 1 Hz and synthetic PlayerInput at 20 Hz   |
|                        | (circular motion). Receives and counts WorldSnapshots. |
| `--leave`              | Send a `LeaveNotify` before exiting (graceful close).  |
| `--bad-version`        | Corrupt the protocol version byte in the Hello packet  |
|                        | to trigger a `Disconnect(VersionMismatch)` response.   |

## Examples

```sh
# Quick handshake test
cargo run -- --name smoke

# Stay alive 10s, send synthetic motion, count snapshots
cargo run -- --name test --keepalive 10 --leave

# Verify version mismatch handling
cargo run -- --bad-version
```

## Keepalive output

During `--keepalive`, the client sends `PlayerInput` with synthetic circular
motion (cos/sin at 20 Hz) so that other connected clients (or a second
echo-client) see non-zero position data in their WorldSnapshots. On exit it
prints snapshot count, effective rate, last server tick, and any other
player IDs seen.
