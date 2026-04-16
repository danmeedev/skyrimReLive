# Player Setup — connecting to someone's SkyrimReLive server

If you just want to play on a friend's server, this is the doc. You need:

- **Skyrim Special Edition** on Steam (runtime 1.6.1170 — current Steam build)
- **SKSE64 2.2.6** — https://skse.silverlock.org → `skse64_2_02_06.7z`
- **Address Library for SKSE Plugins** — https://www.nexusmods.com/skyrimspecialedition/mods/32444 → "All in one (all game versions)"
- **SkyrimReLive** plugin DLL + config

Ask the host for:
1. The server address (e.g. `100.x.y.z` if they use Tailscale, or their IPv6/public IP)
2. The server port (default `27015`)
3. Any extra setup if they're not using Tailscale (see "Networking options" below)

## Quick path (you already have SkyrimSE)

1. **Install SKSE64**: download `skse64_2_02_06.7z` from skse.silverlock.org, extract the root files (loader + core DLLs) into `<Steam>\steamapps\common\Skyrim Special Edition\`.
2. **Install Address Library**: download "All in one (all game versions)" from Nexus mod 32444, extract, copy every `version*.bin` file into `<Skyrim>\Data\SKSE\Plugins\`.
3. **Install SkyrimReLive**: copy the plugin DLL + config into `<Skyrim>\Data\SKSE\Plugins\`. (If you built from source, run `tools/setup.ps1` — it does this automatically. If a release exists, follow the release page.)
4. **Edit the config**: open `<Skyrim>\Data\SKSE\Plugins\SkyrimReLive.toml` and set:
   ```toml
   server_host = "<host's address>"
   server_port = 27015
   player_name = "YourName"
   ```
5. **Launch via `skse64_loader.exe`** (not `SkyrimSELauncher.exe`). Load a save. You're connected.

## Building the plugin from source

Until there's a tagged release, you have to build the DLL yourself.

```sh
# prereqs (one-time):
# - Visual Studio 2022 (or 2026) with C++ desktop workload
# - Rust: winget install Rustlang.Rustup
# - git

git clone https://github.com/danmeedev/skyrimReLive
cd skyrimReLive

# PowerShell (or any shell):
.\tools\setup.ps1
```

`setup.ps1` detects your Skyrim install, downloads SKSE64 if missing, fetches
CommonLibSSE-NG, builds the plugin, and deploys `SkyrimReLive.dll` +
`SkyrimReLive.toml` into `Data/SKSE/Plugins/`. Address Library is the one
thing it can't auto-download (Nexus login-walled) — it prints the URL and
path if AL is missing.

## Networking options

### Tailscale (easiest)

Install Tailscale (https://tailscale.com/download) on your machine and log in with the account your host invited you to. Your host's Tailscale IP looks like `100.x.y.z`. Use that as `server_host`. No firewall config needed.

### IPv6 direct

If your host gave you an IPv6 address (like `2001:db8::1`), put it in the config as:

```toml
server_host = "2001:db8::1"
```

Check at https://test-ipv6.com that *you* have IPv6 first. If it says you don't, this won't work — ask for the Tailscale address instead.

### Host is port-forwarding IPv4

Put their public IPv4 in the config. Nothing else on your side.

## Verifying it works

After launching the game and loading a save, check:

```
%USERPROFILE%\Documents\My Games\Skyrim Special Edition\SKSE\SkyrimReLive.log
```

Success looks like:

```
SkyrimReLive plugin loaded
config loaded from Data/SKSE/Plugins/SkyrimReLive.toml: host=... port=27015 name=...
connecting to ...:27015 as ...
Welcome: player_id=N tick=60Hz snap=20Hz
net client started; sending PlayerInput @60Hz
connected as player_id=N
```

If instead you see `timed out waiting for Welcome`, either the server isn't running, `server_host` is wrong, or there's a firewall/NAT in the way. If you see `plugin SkyrimReLive.dll disabled, address library needs to be updated`, install Address Library (step 2 above).

## What's supported right now

- Phase 1 done: you connect, your position streams to the server, and other
  players appear as ghost actors (vanilla Lydia clones) at their replicated
  positions. Snapshots are smoothed with 100 ms render-delay interpolation,
  so ghosts glide rather than teleport between updates. Replication is
  cell-gated (only active when you and the other player are in the same
  cell, optionally pinned via `target_cell`).
- Phase 2 in progress (steps 2.1 + 2.2 + 2.3 done): ghosts play the
  correct locomotion animation (idle/walk/run/sneak), draw or sheath
  their weapon in sync with the remote player, and melee hits between
  players actually land — the server validates each swing's range and
  rate, applies damage, and the target's client plays a stagger when
  the blow is heavy. Pitch/aim replication and ranged-combat support
  are still pending (step 2.5). Anti-cheat-style transform validation
  (step 2.4) is deferred — this is a friend-trust mod and players are
  free to use console commands like `coc`.

For solo validation without a second player, use `rl demo start` from the
in-game console — it spawns a synthetic ghost that orbits you with the run
animation playing, exercising the same render path real ghosts use.

Check [`ROADMAP.md`](ROADMAP.md) for current status.
