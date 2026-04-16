# tools/

Helper scripts and small utilities.

## echo-client/

Rust CLI test tool that sends a binary Flatbuffers Hello to the server and
prints the Welcome reply. Supports `--keepalive` (stay connected, send
synthetic PlayerInput at 20 Hz, receive WorldSnapshots), `--leave` (graceful
disconnect), and `--bad-version` (test version mismatch handling). See its
own [README](echo-client/README.md).

## regen-protos.sh

Regenerates Flatbuffers codegen from `schemas/v1/*.fbs` into both
`server/src/proto/v1/` (Rust) and `client/plugin/src/proto/v1/` (C++).
Requires `flatc` on PATH (`winget install Google.flatbuffers`). Generated
files are committed; CI checks they match regen output.

```sh
tools/regen-protos.sh
```

## setup.ps1 / setup.sh

One-time setup. Idempotent -- safe to re-run.

```sh
tools/setup.sh                   # bash wrapper
# or directly:
powershell -File tools/setup.ps1
```

What it does:
1. Verifies prereqs (Skyrim SE, Rust, CMake, vcpkg, 7-Zip)
2. Bootstraps vcpkg at `C:\vcpkg` if not present
3. Installs 7-Zip via winget if needed
4. Downloads and installs SKSE64 2.2.6 if not present
5. Configures and builds the plugin
6. Deploys `SkyrimReLive.dll` to `<Skyrim>\Data\SKSE\Plugins\`

Flags:
- `-SkyrimRoot <path>` -- override Skyrim install location
- `-VcpkgRoot <path>` -- override vcpkg location
- `-SkipSkse` -- don't touch SKSE
- `-SkipBuild` -- don't (re)build the plugin

## launch.ps1 / launch.sh

Every-launch helper. Builds (incrementally), redeploys, starts the server in
a new window, launches `skse64_loader.exe`.

```sh
tools/launch.sh
```

Flags:
- `-NoRebuild` -- skip plugin rebuild and deploy
- `-NoServer` -- don't start the server (e.g. you already have one)
- `-WaitForGame` -- block until Skyrim exits, then stop the server

After launch, load a save and check connection status with the `rl status`
console command. Logs go to:
`%USERPROFILE%\Documents\My Games\Skyrim Special Edition\SKSE\SkyrimReLive.log`
