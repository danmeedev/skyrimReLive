# client/plugin

SKSE plugin for Skyrim Special Edition. Thin client -- connects to the
server, sends `PlayerInput` at 60 Hz, receives `WorldSnapshot` at 20 Hz,
and renders other players as ghost actors with 100 ms snapshot interpolation.

## Prerequisites

- Skyrim Special Edition runtime **1.6.1170** (Steam-current AE)
- SKSE64 **2.2.6** matching that runtime -- https://skse.silverlock.org/
  download `skse64_2_02_06.7z`, extract loader + core DLLs into the Skyrim
  install directory
- Visual Studio 2026 (or 2022) with C++ desktop workload, including the **v143
  toolset** (v145/MSVC 14.50 currently breaks CommonLibSSE-NG headers -- pin to
  v143 with `-T v143`)
- vcpkg cloned and bootstrapped (`git clone https://github.com/microsoft/vcpkg`,
  then `vcpkg/bootstrap-vcpkg.bat -disableMetrics`); export `VCPKG_ROOT`

CommonLibSSE-NG is pulled via CMake `FetchContent` from
`alandtse/CommonLibVR` v4.14.0 (the actively maintained fork). vcpkg only
provides its C++ deps (fmt, spdlog, rapidcsv, directxtk, directxmath, xbyak).

## Build

```sh
export VCPKG_ROOT=/c/vcpkg   # or wherever you cloned vcpkg
cd client/plugin
cmake -S . -B build \
    -G "Visual Studio 18 2026" -A x64 -T v143 \
    -DCMAKE_TOOLCHAIN_FILE=$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake \
    -DVCPKG_TARGET_TRIPLET=x64-windows-static-md \
    -DCMAKE_POLICY_VERSION_MINIMUM=3.5
cmake --build build --config Release
```

If you're on VS 2022 (not 2026), use `-G "Visual Studio 17 2022"` and the
v143 toolset is the default -- drop `-T v143`.

First configure compiles the vcpkg deps (~1 min cached). First build compiles
CommonLibVR (~5-10 min). Subsequent builds of just the plugin are seconds.

Output: `build/Release/SkyrimReLive.dll`.

## Deploy

```sh
mkdir -p "/c/Program Files (x86)/Steam/steamapps/common/Skyrim Special Edition/Data/SKSE/Plugins"
cp build/Release/SkyrimReLive.dll \
   "/c/Program Files (x86)/Steam/steamapps/common/Skyrim Special Edition/Data/SKSE/Plugins/"
```

## Configuration

The plugin reads `SkyrimReLive.toml` from the SKSE plugins directory
(`<Skyrim>/Data/SKSE/Plugins/SkyrimReLive.toml`). Missing file or parse
error falls back to defaults -- never a hard failure.

```toml
server_host = "127.0.0.1"
server_port = 27015
player_name = "dovahkiin"
auto_connect = true              # connect on save load; false = manual `rl connect`
target_cell_form_id = 0          # 0 = any cell; nonzero = restrict to one cell
```

## Runtime behavior

On save load (or `rl connect`), the plugin:

1. Opens a UDP socket on a background thread.
2. Sends a binary `Hello` (Flatbuffers, 4-byte `RL` packet header).
3. Waits for `Welcome` (receives assigned player_id and server rates).
4. Enters the main loop:
   - **Net thread**: reads `PlayerCharacter` position/yaw at 60 Hz, ships
     `PlayerInput`. Drains incoming `WorldSnapshot` packets and queues
     decoded player states for the main thread.
   - **Main thread** (SKSE TaskInterface): drains the queue, spawns ghost
     actors for new players, interpolates transforms, despawns stale ghosts.

### Ghost rendering

Other players appear as Lydia clones (vanilla NPC form `0x000A2C94`) spawned
via `PlaceObjectAtMe`. AI is disabled on ghosts. Transform updates use
100 ms render-delay interpolation: the client renders behind the latest
snapshot and linearly interpolates between bracketing history entries.
Ghosts are despawned after 180 ticks (~3 s) with no updates.

### Cell gating

Replication is only active when the local player is in a valid cell. If
`target_cell_form_id` is set to a nonzero value, replication is further
restricted to that specific cell. The cell watcher polls
`PlayerCharacter::parentCell` on each main-thread tick.

## Console commands

Open the Skyrim console (`~` key) and type `rl <subcommand>`:

| Command                    | Description                                  |
| -------------------------- | -------------------------------------------- |
| `rl status`                | Connection state, player_id, ghost count,    |
|                            | inputs sent, snapshots received, local pos.  |
| `rl connect [host] [port]` | Connect (defaults from config if omitted).   |
| `rl disconnect`            | Close the connection.                        |
| `rl cell`                  | Show current and target cell form IDs.       |
| `rl cell set [hex]`        | Pin target to current cell or given form ID. |
| `rl cell clear`            | Clear target (any cell is active).           |
| `rl demo start`            | Spawn a synthetic orbiting ghost (solo test).|
| `rl demo stop`             | Despawn the demo ghost.                      |
| `rl help`                  | List all subcommands.                        |

## Architecture

```
src/
  Plugin.cpp/h     SKSE entry point, auto-connect on world load, demo mode.
  Net.cpp/h        Background net thread: Hello/Welcome, PlayerInput send,
                   WorldSnapshot receive, Heartbeat.
  Socket.cpp/h     Platform UDP socket abstraction (Winsock2).
  Ghost.cpp/h      Ghost actor manager: spawn, interpolate, despawn.
                   Thread-safe net->main queue. VanillaCloneSpawner (Lydia).
  Cell.cpp/h       Cell watcher: polls parentCell, target gating.
  Commands.cpp/h   Console command registration and dispatch.
  Config.cpp/h     TOML config loader (tomlplusplus).
  proto/v1/        Generated Flatbuffers C++ headers.
  PCH.h            Precompiled header.
```

## Logs

`%USERPROFILE%\Documents\My Games\Skyrim Special Edition\SKSE\SkyrimReLive.log`

To test the transport without Skyrim, use `tools/echo-client` instead.
