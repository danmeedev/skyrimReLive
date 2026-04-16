# SkyrimReLive: an open-source multiplayer mod, and why it had to exist

## What this mod is

SkyrimReLive lets you and other people play in the same Skyrim world at the same time. You see them. They see you. You both walk around Whiterun together, draw your weapons together, and (eventually) fight the same dragon together. There's no Bethesda server you connect to. There's no central matchmaker. You or a friend run the server on a regular PC, and your group connects directly to it.

It's a Skyrim Script Extender plugin (a `.dll` you drop in `Data\SKSE\Plugins\`) plus a small stand-alone server program. No Bethesda assets are modified, no Creation Kit content ships with it, no other mod gets touched. It's pure additive code.

## Where it's going

The mod is being built so the same codebase scales from **two friends in your living room** all the way up to **a public role-play server with dozens of players**. The exact same server binary handles both — for small groups you run it on your gaming PC alongside Skyrim. For a public RP community you'd put it on a cheap Linux VPS and let people connect over the internet.

The phased roadmap, in plain language:
- **Phase 1 (done)** — see your friend, watch them move
- **Phase 2 (in progress)** — see them animate properly: walking, running, sneaking, drawing weapons
- **Phase 3** — cell transitions, doors, containers, basic world state shared
- **Phase 4** — NPCs that exist in the shared world rather than per-client
- **Phase 5** — character persistence, accounts, save state
- **Phase 6** — proper public-server hosting, MMO-grade infrastructure
- **Phase 7** — class systems, PvP zones, economy, the gameplay layer that turns a multiplayer engine into a multiplayer *game*

The architecture is server-authoritative from day one specifically so the small-group story doesn't make the large-group story impossible later. Every design decision is documented as a proposal in the GitHub repo, peer-reviewable, with the trade-offs spelled out.

## What works today (Phase 1 + Phase 2 steps 2.1, 2.2 & 2.3)

- **Connect to a server** by entering its address in `SkyrimReLive.toml` and loading any save.
- **See other players** in the same cell as ghost actors. They appear, move, and despawn on disconnect.
- **Watch them walk, run, sneak** — locomotion animations are synchronized, not just position.
- **Watch them draw and sheath weapons** — combat stance is mirrored across clients.
- **Hit each other in melee** — swing your sword at another player and it actually lands. The server validates each swing's range and rate, applies damage, and the target's client plays a stagger when the blow is heavy. Server-authoritative so no client can fake hits.
- **In-game console commands** for everything: `rl status` to see what's happening, `rl connect`/`disconnect` to control the link, `rl cell` to pin replication to one cell, `rl demo` to spawn a synthetic ghost for solo testing.
- **Smooth motion** via 100 ms snapshot interpolation — ghosts glide rather than teleport between updates.
- **Self-hosted networking** that just works: Tailscale for friend groups (zero port-forwarding), IPv6 direct, port-forwarding, or cloud VPS. Pick whichever your network situation allows.

## What's coming (next few releases)

- **Aim direction + ranged combat** (Phase 2.5) — pitch replication so people can see where you're aiming, plus extending the combat hook to bows and spells. Cheat-prevention (Phase 2.4 anti-teleport / speedhack) is deferred behind this — friend-trust mode is fine with `coc`, and a future opt-in "strict mode" server config is the right home for hardening when public servers become a thing.
- **Cell transitions** (Phase 3) — walk into Dragonsreach together, not just stand outside.
- **Persistent characters** (Phase 5) — your level, inventory, and equipment survive disconnects.
- **Custom ghost appearance** — right now you appear as a clone of yourself; the longer plan is shipping with a proper custom NPC base so each player has their own appearance.

The full roadmap with status checkboxes lives in the [GitHub repo](https://github.com/danmeedev/skyrimReLive/blob/master/docs/ROADMAP.md).

## Setting it up (the short version)

The Description tab on this mod page has the full step-by-step. The five-second version:

1. Install **SKSE64 2.2.6** and **Address Library for SKSE Plugins**.
2. Drop `SkyrimReLive.dll` and `SkyrimReLive.toml` in `Data\SKSE\Plugins\`.
3. Edit `SkyrimReLive.toml` — set `server_host` to whatever address your server is at.
4. Launch via `skse64_loader.exe`.
5. Load a save. Press `~`, type `rl status` to confirm you're connected.

You also need *somewhere* to run the server. The recommended setup for friend groups is **Tailscale** — a free mesh-VPN that lets your friend reach your PC over the internet without you opening any ports on your router. Setup takes about ten minutes per person. Full hosting walkthrough is in the [HOST_SETUP guide](https://github.com/danmeedev/skyrimReLive/blob/master/docs/HOST_SETUP.md).

## Why this mod matters

I'm going to be direct about this part because it's the reason the project exists.

The other multiplayer Skyrim mods I tried all had the same three problems:

**They're closed source.** Skyrim Together Reborn is technically MIT-licensed in theory but the build pipeline, the design decisions, and the actual development happen behind closed doors. Keizaal Online is fully private. When something breaks, you can't see why. When you want a feature, you can't add it. When the original devs lose interest or get into a dispute, the project dies and there's nothing the community can salvage.

**They're buggy at a foundational level.** Not surface bugs — *architectural* bugs. State desyncs that no patch will ever fix because the network model assumes things that aren't true. Crashes that recur for years. The kind of problems that mean the codebase needs a rewrite, but a rewrite needs the source.

**They're restrictive about who gets to use them and how.** Hosting decisions are made for you. Mod compatibility is gated. Servers are sometimes pulled or restricted at the dev's discretion. You're a guest on someone else's project even though you're playing on your own machine.

I started SkyrimReLive because I think those three problems are connected, and the answer to all three is the same answer: **build it open, build it independent, build it so anyone can host, fork, or replace any piece.**

Concretely:

- **Apache-2.0 license.** Take the code. Fork it. Run a paid hosting service with it if you want. Modify it. The only thing the license requires is that you keep the copyright notice. There's no "non-commercial only" trap, no "must contribute back" trap, no "you can use it but we own your changes" trap.
- **Every design decision is a proposal in the repo.** Why is the wire format what it is? Why does the server use Rust? Why does the ghost render the way it does? It's all in `docs/proposals/`, with the alternatives I considered and rejected. If you disagree, you can say so in an issue and the conversation is public.
- **No private build pipeline.** What you see on GitHub is exactly what's in the DLL. CI runs on every commit. The schemas, the server, the tools, the helper scripts — all checked in.
- **No telemetry, no phone-home, no analytics.** The plugin connects to the server you tell it to and nothing else. The server logs what you'd expect a game server to log (who connected, who disconnected) and nothing else. There are no third-party services involved at all.
- **No credit hoarding.** I don't gatekeep contributors, I don't lock anyone out of the build process, and if someone makes a meaningful contribution they get credit in the commit log and the changelog. If this project ever has a "team," that team will be the people who actually showed up and did the work, not the ones who got there first.

If you've been burned by closed-source multiplayer mods that got abandoned, broken, or dramatic, this one is meant to be the opposite of that. Not because I'm a better person — because the structure of the project doesn't let me become the bottleneck. If I get hit by a bus, someone forks the repo and keeps going. The project survives the developer.

## What I'd love help with

If you can do any of these, you can move the project forward:

- **Test it and file good bug reports.** The more situations the mod gets into, the more bugs we find. GitHub Issues link is in the description.
- **Build the C++ plugin from source** to verify the build works on your machine — different MSVC versions, different VS installs, different Skyrim install paths. The setup guide is in the repo.
- **Run a server, even a tiny one,** and let a friend connect. Real users in real conditions surface things that automated tests can't.
- **Contribute code.** Phase 2 needs an animation-graph variable whitelist explorer. Phase 3 needs cell-transition design. Phase 4 needs a server-side AI subset. There's room.
- **Document.** If something in the setup tripped you up, the docs probably need a fix. PRs welcome.
- **Use it.** The mod gets better as more people play it. Even if you just hop on with one friend for an hour, that's data the project didn't have before.

## Credits

- **Inspired by** Skyrim Together Reborn and Keizaal Online. They proved Skyrim multiplayer is possible. SkyrimReLive is what I wished those mods were structurally — not better gameplay, just an open foundation.
- **Built on** CommonLibSSE-NG (alandtse fork) for the SKSE plugin layer, Flatbuffers for the wire format, bevy_ecs for the server simulation, tokio for async I/O, and a long list of permissively-licensed open-source libraries you can find in `Cargo.toml` and `vcpkg.json`.
- **No assets, code, or design lifted from any other multiplayer Skyrim mod.** This is independent re-implementation from public Skyrim/SKSE documentation.

## Final word

If you only take one thing from this article: **the source is at [github.com/danmeedev/skyrimReLive](https://github.com/danmeedev/skyrimReLive)**. Everything I wrote above is verifiable by reading the code and the proposals. If you find something in the codebase that contradicts what's in this article, file an issue and I'll either fix the code or fix the article. That's the deal.

Have fun. Don't trust mod authors who won't show their work.
