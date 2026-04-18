# Host Setup — running a SkyrimReLive server

If you're the one running the server for yourself and a friend (or a small
group, or a public MMO later), this is the doc.

## Prereqs (one-time)

- **Rust**: `winget install Rustlang.Rustup` (Windows) or https://rustup.rs
- **git**

## Build & run the server

```sh
git clone https://github.com/danmeedev/skyrimReLive
cd skyrimReLive/server

cargo run --release
```

Expected startup log:

```
config loaded path=server.toml
skyrim-relive-server listening bind=[::]:27015 version=2 tick_hz=60 snap_hz=20 timeout_s=5 dual_stack=true
```

That's it — the server is running and listening on both IPv4 and IPv6 on
port 27015.

## Configuration

`server.toml` is **auto-generated** with all fields documented on first
run if it doesn't exist. Edit `server/server.toml`:

```toml
bind                  = "[::]:27015"   # dual-stack; use "0.0.0.0:27015" for v4-only
tick_rate_hz          = 60             # server simulation rate
snapshot_rate_hz      = 20             # world-state broadcast rate
connection_timeout_s  = 5              # despawn silent clients after this many seconds
gc_interval_ms        = 500            # how often the timeout sweep runs

# Zeus admin settings
admin_password        = ""             # empty = open (no password required)
player_list_poll_s    = 5.0            # how often to refresh player data for `rl players`
pvp_enabled           = false          # false = melee/ranged/spell hits between players dropped
spell_damage_default  = 25.0           # flat spell damage sentinel (real magnitude reading deferred)
```

Override the path via `RELIVE_CONFIG=<path> cargo run`. Unknown fields are
rejected (typo-safe).

### Zeus admin mode

Zeus gives the host admin/DM controls. No extra setup required — it's
built into the server and plugin.

**Console commands (press `~` in-game):**
- `rl admin` — authenticate as admin (open by default)
- `rl cmd help` — list all admin commands
- `rl cmd pvp on|off` — toggle PvP live
- `rl cmd kick <id>` — disconnect a player
- `rl cmd time <hour>` — set time for all (0-24)
- `rl cmd weather clear|rain|snow|storm|fog|<formid>` — set weather
- `rl cmd give <pid> <formid> [count]` — give items
- `rl cmd spawn <base_formid>` — spawn NPC/object at your position
- `rl cmd npcs` — list spawned NPCs
- `rl cmd npc <zid> follow|wait|moveto|aggro|combat|passive|delete`
- `rl cmd obj <zid> delete|moveto`
- `rl cmd tp <pid> <x y z>` or `tp <pid> tome`

**ImGui overlay (F8):** visual admin panel with time slider, weather
dropdown, searchable form browser (18 categories), player table with
kick, spawned NPC management, and PvP toggle. Game controls are disabled
while the overlay is active.

---

## Networking — how your friend reaches you

The biggest hosting decision. Four options, roughly easiest to hardest:

### 1. Tailscale — recommended for small groups

Pros: zero port forwarding, works from anywhere, free for personal use, encrypted.
Cons: everyone needs to install Tailscale on their machine.

**Setup:**

1. Install Tailscale on your machine: https://tailscale.com/download. Sign up (free).
2. Log in with `tailscale up` (or via the tray icon on Windows). You now have a Tailscale IP, something like `100.64.1.2`. Confirm with `tailscale ip -4` or in the admin console at https://login.tailscale.com/admin/machines.
3. Invite your friend: admin console → Users → "Invite external user". Send them the magic link.
4. They install Tailscale, accept the invite, and now see your machine in their tailnet.
5. They put your `100.x.y.z` address in their `SkyrimReLive.toml`.

**What changes on the server side:** nothing. Our `bind = "[::]:27015"` already listens on all interfaces, including the Tailscale virtual one.

**Hardening (recommended once friends are in your tailnet):** see [`TAILSCALE_HARDENING.md`](TAILSCALE_HARDENING.md) — locks the friends group down to UDP 27015 only on your gaming PC so they can't reach anything else on your network. ~10 minutes.

### 2. IPv6 direct — free, works when both of you have IPv6

Pros: no third-party software, peer-to-peer, zero cost.
Cons: requires both of you to have working IPv6; not all ISPs do it right.

**Setup:**

1. Visit https://test-ipv6.com — both you and your friend should get a 10/10 score. If either of you gets "No IPv6", skip to option 3 or 1.
2. Find your IPv6 address with `ipconfig /all` on Windows and look for `IPv6 Address . . . . : 2001:...` (the *global* one, not the `fe80::` link-local or `fd00::` unique-local).
3. Allow inbound UDP 27015 in Windows Defender: `New-NetFirewallRule -DisplayName "SkyrimReLive" -Direction Inbound -Protocol UDP -LocalPort 27015 -Action Allow` (from an elevated PowerShell).
4. Give your friend the IPv6 address. They paste it into `server_host` — square brackets are not needed in the config since we also parse bare IPv6.

**Caveat:** residential IPv6 prefixes are sometimes re-delegated by ISPs every few days. If connections break after a week, check whether your address changed.

### 3. Port-forwarding IPv4 — classic, always works if you can configure the router

Pros: works for any IPv4-only friend.
Cons: requires router admin access; some ISPs use CGNAT and won't let you forward at all.

**Setup:**

1. Find your LAN IPv4 (`ipconfig`, `IPv4 Address`). Usually `192.168.x.y`.
2. Find your WAN IPv4 at https://api.ipify.org.
3. Log into your router (commonly `http://192.168.1.1` or `http://192.168.0.1`).
4. Forward external **UDP** port `27015` → internal `<your LAN IPv4>:27015`.
5. Allow inbound UDP 27015 in Windows Defender (see the IPv6 section).
6. Give your friend the WAN IPv4. They paste it into `server_host`.

**CGNAT check:** if your WAN IPv4 (from ipify) doesn't match what your router's admin page says under "Internet" / "WAN", you're on CGNAT and can't port-forward. Use Tailscale.

### 4. Cloud VPS — for a public server or MMO-style hosting

Pros: always online, public IPv4, you don't need anything at home.
Cons: costs money (~$4–6/mo); requires light Linux admin.

**Setup (rough outline — we don't yet ship a Docker image; Phase 6 will):**

1. Rent a small VPS (Hetzner CX11, DigitalOcean basic droplet, Vultr, etc.).
2. Install Rust + git.
3. Clone the repo, `cargo build --release --bin skyrim-relive-server`.
4. Run under a systemd unit (or `screen`/`tmux` for a first test) pointing at a `server.toml`.
5. Open UDP 27015 in the cloud firewall. Don't forget.
6. Give players the VPS public IP.

For many players, you'll want Phase 6's full services around this — see
`ROADMAP.md`.

---

## Connecting with a friend — end-to-end walkthrough

Easiest happy path with Tailscale:

1. **You (host):**
   - `cargo run --release` in `server/` — leave the terminal open.
   - Note your Tailscale IPv4 (e.g. `100.64.1.2`).
2. **Your friend (player):** follows [`PLAYER_SETUP.md`](PLAYER_SETUP.md):
   - Installs Tailscale, joins your tailnet.
   - Installs Skyrim SE + SKSE + Address Library.
   - Builds or downloads `SkyrimReLive.dll`, drops it + `SkyrimReLive.toml` in `Data\SKSE\Plugins\`.
   - Edits `SkyrimReLive.toml`: `server_host = "100.64.1.2"`, `server_port = 27015`.
   - Launches Skyrim via `skse64_loader.exe`.
3. **You watch the server log**:
   ```
   Hello accepted, entity spawned peer=100.64.1.2:<port> name=<their_name> player_id=1 connected=1
   ```
   You can also run your own client alongside (same machine or another) — Tailscale addresses work from localhost too.

If the server log stays silent, see the troubleshooting section below.

---

## Troubleshooting

**Nothing happens when friend connects.**
- Server log shows no `Hello`? The packet never arrived. Check: firewall (Windows Defender, router, cloud); `server_host` on client matches yours; you're both using the same port (default 27015).
- Test reachability from the client machine: `nc -u <your-address> 27015` then type junk and hit Enter. The server should log a "packet rejected" warning — confirms the packet reaches you.

**Client logs `timed out waiting for Welcome`.**
- The Hello left the client but no reply came back. Same causes as above — usually an asymmetric firewall that allows outbound but drops inbound return traffic. Windows Defender prompts you on first bind; if you dismissed that, add the rule manually (see IPv6 section).

**Version mismatch.**
- `Disconnect { code = VersionMismatch }` means client and server were built from different wire-format versions. Both must rebuild from the same `schemas/v1/` snapshot. (The directory is named `v1/` for historical reasons; the on-wire protocol byte is `2` since Phase 2 — see `schemas/README.md`.)

**Host is running behind CGNAT (no router port forward works).**
- Skip options 2 and 3. Use Tailscale or rent a VPS.

**Friend's ISP drops UDP entirely.**
- Very rare, but happens on some mobile hotspots and corporate networks. Tailscale works around this (it falls back to relay-via-DERP when direct UDP fails), at the cost of some latency.

---

## Security notes

- The server still trusts the client's position (known H1 caveat from Phase 1 — see the architecture doc). Anti-teleport / speedhack validation (Phase 2 step 2.4) is deferred since this is a friend-trust mod; the planned home is an opt-in "strict mode" config. Don't run a public server with random players until then.
- Admin auth is open by default (no password). Set `admin_password` in `server.toml` if hosting for untrusted players. Admin commands (`rl cmd ...`) require auth.
- Never put a private config (future: passwords, secrets) in the repo. `.gitignore` covers `.env` / `*.local.toml`.

---

## Uptime / running in the background

**Foreground (development):** `cargo run --release` and leave the terminal open.

**Windows Service (rough):** until we ship a proper installer, you can wrap the binary with NSSM (https://nssm.cc) or run it under a scheduled task. A dedicated systemd-style setup is on the Phase 6 list.

**Linux / VPS:** a one-file systemd unit works well:

```ini
[Unit]
Description=SkyrimReLive server
After=network-online.target

[Service]
ExecStart=/opt/skyrim-relive/skyrim-relive-server
WorkingDirectory=/opt/skyrim-relive
Restart=on-failure
User=relive

[Install]
WantedBy=multi-user.target
```

`server.toml` in the same working directory, `RELIVE_CONFIG=` to override.
