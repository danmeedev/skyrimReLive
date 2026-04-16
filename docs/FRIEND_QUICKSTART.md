# Friend Quickstart — joining a SkyrimReLive game

This doc is the **single entry point** for someone who wants to *play* on
a friend's SkyrimReLive server. It's written so any LLM (Cursor's AI,
Copilot, etc.) can follow it step-by-step with you and recover from
common errors.

If you're hosting the server, you want [`HOST_SETUP.md`](HOST_SETUP.md)
instead. If you want to contribute code, see [`CONTRIBUTING.md`](../CONTRIBUTING.md).

---

## What you'll need (gather these before starting)

1. **Skyrim Special Edition** on Steam, runtime version **`1.6.1170`**.
   To check: right-click `SkyrimSE.exe` (in your Steam install folder)
   → Properties → Details → "File version" should be `1.6.1170.0`.
2. **The host's Tailscale IP** (your friend will tell you, format `100.x.y.z`).
3. **The host's Tailscale invite link** (your friend will send you a magic
   link by email or message).
4. **A Nexus Mods account** (free) — needed once to download Address Library.
5. **About 30 minutes** the first time. Subsequent updates take ~2 minutes.

---

## Pick your scenario

| Scenario                                            | Path to follow             |
| --------------------------------------------------- | -------------------------- |
| Host sent me a `SkyrimReLive-friend.zip`            | **[Scenario A](#scenario-a-i-have-a-zip-from-the-host)** (easiest) |
| I want to grab the latest from GitHub Releases      | **[Scenario B](#scenario-b-download-the-latest-release-from-github)** |
| I want to build the plugin from source              | **[Scenario C](#scenario-c-build-from-source-advanced)** |
| I'm not sure / I want the AI to figure it out       | **[Scenario D](#scenario-d-let-ai-decide)** |

---

## Scenario A — I have a zip from the host

1. **Extract the zip** somewhere convenient (Desktop, Documents — anywhere
   you can find it again). You'll see these files inside:
   ```
   INSTALL.bat
   LAUNCH.bat
   UNINSTALL.bat
   README.txt
   files/
   scripts/
   ```
2. **(Optional but recommended) Drop your SKSE and Address Library
   downloads into the `downloads/` folder** before running the installer:
   - SKSE 2.2.6 `.7z` from <https://skse.silverlock.org/>
   - "All in one (all game versions)" `.zip` from Nexus mod 32444
   - Drop the **archives** as-is — the installer extracts them. This
     skips the manual "navigate the archive, copy these specific files"
     step.
3. **Double-click `INSTALL.bat`**. The installer walks you through the
   rest: it auto-installs anything you placed in `downloads/`, opens
   browser tabs for anything missing, deploys the plugin, and probes
   the host's server.
3. Once you see "Install complete!", **double-click `LAUNCH.bat`** to
   play. Load any save, press `~` in-game, type `rl status`. You should
   see `state=connected ...`.

**That's it.** Skip the rest of this doc.

---

## Scenario B — Download the latest release from GitHub

1. **Open the releases page**:
   <https://github.com/danmeedev/skyrimReLive/releases>
2. Find the latest release. Under **Assets**, download
   `SkyrimReLive-friend.zip`.
3. Continue with **[Scenario A](#scenario-a-i-have-a-zip-from-the-host)**.

> If there are no releases yet, the host hasn't published one. Ask them
> to either send you the zip directly (Scenario A) or to publish a
> release. Otherwise, skip to **[Scenario C](#scenario-c-build-from-source-advanced)**.

---

## Scenario C — Build from source (advanced)

Use this if you want to modify the code, can't get a pre-built bundle,
or just want to live dangerously.

### One-shot prerequisites (install once on your machine)

| Tool                     | Install command                                              |
| ------------------------ | ------------------------------------------------------------ |
| **Git**                  | `winget install Git.Git`                                     |
| **Visual Studio 2022 or 2026** with the *Desktop development with C++* workload (~10 GB download) | <https://visualstudio.microsoft.com/downloads/> |
| **Rust toolchain**       | `winget install Rustlang.Rustup`                             |

You do *not* need the Rust server to play — only the C++ plugin matters
for friends. But the build script uses Rust tooling, so install it.

### Steps

```powershell
# 1. Clone the repo
git clone https://github.com/danmeedev/skyrimReLive
cd skyrimReLive

# 2. Run the friend quickstart — it'll detect missing deps, build the
#    plugin, and produce a friend bundle for you.
.\tools\friend-quickstart.ps1 -HostIp <ASK YOUR FRIEND> -HostName <FRIEND'S NAME>
```

The script will:
1. Check that Visual Studio + Rust are installed.
2. Bootstrap vcpkg in `C:\vcpkg` if missing.
3. Build the SkyrimReLive plugin (~5–10 min on first run, then cached).
4. Generate a personalized friend bundle in `dist/SkyrimReLive-friend/`.
5. Run the bundle's `INSTALL.bat` for you.

When it finishes, **double-click `dist\SkyrimReLive-friend\LAUNCH.bat`**
to play. Re-run that whenever you want to play.

---

## Scenario D — Let AI decide

If you're using Cursor (or another AI editor), open this folder in your
editor and ask the AI:

> "I want to play SkyrimReLive on my friend's server. Walk me through
> setup. My friend's Tailscale IP is `<paste here>` and their name is
> `<paste here>`."

The AI should:
1. Read this `FRIEND_QUICKSTART.md` and decide which scenario applies.
2. Check what you have installed (VS, Rust, Skyrim, etc.).
3. Run the right commands for you.

If the AI gets confused, paste **this exact instruction**:

> Read `docs/FRIEND_QUICKSTART.md` and follow Scenario A, B, or C based
> on what's available. Default to A if I have a zip. If not, try B
> (download from GitHub Releases). Only fall back to C if both fail.
> Don't invent steps not in the doc.

---

## Verifying it worked (in-game)

After installing and launching:

1. Wait for the main menu, then **load any save** (or start a new game).
2. Wait until you're in the world and can move.
3. Press the **tilde key (`~`)** to open the console.
4. Type:
   ```
   rl status
   ```
5. **Expected output**:
   ```
   state=connected server=100.x.y.z:27015 player_id=N ghosts=N inputs_sent=N snapshots_received=N pos=(...) yaw=...
   ```
6. **If you see `state=idle`**: type `rl connect` to manually connect.
7. **If you see `state=failed`**: see [Troubleshooting](#troubleshooting) below.

---

## In-game commands (open console with `~`)

| Command                      | What it does                                          |
| ---------------------------- | ----------------------------------------------------- |
| `rl status`                  | Connection state, ghost count, packet stats, your pos |
| `rl connect`                 | Manually connect (config is reread)                   |
| `rl disconnect`              | Cleanly close the connection                          |
| `rl cell`                    | Show your current cell + replication target           |
| `rl demo start`              | Spawn a fake ghost orbiting you (test rendering solo) |
| `rl demo stop`               | Despawn the demo ghost                                |
| `rl help`                    | List all commands                                     |

---

## Troubleshooting

### Plugin loaded but `rl status` says `state=idle` or `state=failed`

**Cause:** Cannot reach the host's server.

**Fix sequence:**
1. Confirm your Tailscale tray icon is **green** (signed in, connected).
   - If not: open Tailscale → sign in → wait for green.
2. Confirm the host has the server **running**.
   - If not: ask them to start it.
3. Confirm your `SkyrimReLive.toml` has the **right IP**.
   - Open `<Skyrim>\Data\SKSE\Plugins\SkyrimReLive.toml`
   - The `server_host` line should match what your friend told you.
4. Try `rl connect` in the console to retry.

### `plugin SkyrimReLive.dll disabled, address library needs to be updated`

**Cause:** Address Library isn't installed, or its `.bin` files are in
the wrong folder.

**Fix:**
1. Download "All in one (all game versions)" from
   <https://www.nexusmods.com/skyrimspecialedition/mods/32444>.
2. Extract the archive. Inside, navigate to `SKSE\Plugins\`.
3. Copy every file matching `versionlib-1-6-*.bin` into
   `<Skyrim>\Data\SKSE\Plugins\`.
4. Re-launch Skyrim via `skse64_loader.exe`.

### Skyrim crashes on save load

**Diagnostic:**
1. Try loading a *different* save first.
2. If that works, the original save may be incompatible (rare). Use the
   working save.
3. If both crash, check that no other multiplayer mod (especially
   *Skyrim Together Reborn*) is installed. They will conflict.

### Game freezes when connecting

**Cause:** Plugin trying to connect before the world is fully loaded.

**Fix:**
1. Edit `<Skyrim>\Data\SKSE\Plugins\SkyrimReLive.toml`.
2. Change `auto_connect = true` to `auto_connect = false`.
3. Save. Re-launch Skyrim.
4. Once in-world, press `~` and type `rl connect`.

### `Disconnect { code = VersionMismatch }`

**Cause:** Your plugin and the host's server were built from different
commits.

**Fix:** Ask the host which commit/version they're running. Either rebuild
the plugin from that commit, or have the host send you the matching
`SkyrimReLive.dll`.

### Antivirus deleted `SkyrimReLive.dll`

**Cause:** Your antivirus flagged an unsigned DLL as suspicious. False
positive.

**Fix:** Add an exception for the SkyrimReLive folder, then re-run the
installer. Or build it from source yourself (Scenario C) so you trust
the binary.

---

## Logs (for debugging — send these to the host if you need help)

Both files are at:

```
C:\Users\<you>\Documents\My Games\Skyrim Special Edition\SKSE\
```

| File                | What it shows                                          |
| ------------------- | ------------------------------------------------------ |
| `SkyrimReLive.log`  | Our plugin's log: connection events, errors            |
| `skse64.log`        | SKSE's log; grep for "SkyrimReLive" for plugin-load info |

To send to the host: open the file in Notepad, copy the contents, paste
into Discord/email.

---

## What's in this for you (a brief sales pitch)

- **You'll see your friend** as a ghost actor in the same Skyrim cell.
- They'll **walk, run, sneak** in real time as you watch.
- They'll **draw and sheath weapons** and you'll see the animations.
- **Combat damage** lands hits between players: swing your weapon at
  another player and the server validates the hit, applies damage, and
  tells the target's client to play a stagger animation when it's a
  heavy blow.
- It's all **open source** (Apache-2.0). You can read every line of the
  plugin DLL's source code in this repo.

What it does *not* do yet:
- No NPC sync (your bandits aren't their bandits).
- No persistent inventory.
- No quest sync.
- Other players currently appear as a clone of *your* character (custom
  appearance is on the roadmap).

See [`ROADMAP.md`](ROADMAP.md) for the full plan.

---

## Found a bug, want a feature, want to contribute?

- File issues at <https://github.com/danmeedev/skyrimReLive/issues>.
- Read [`CONTRIBUTING.md`](../CONTRIBUTING.md) before sending PRs.
- Or just message the host directly.
