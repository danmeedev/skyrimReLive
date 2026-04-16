# CLAUDE.md — project rules for AI agents

This file is automatically loaded by Claude Code at the start of every
session in this repo. It captures project-specific conventions that agents
should follow. Keeps humans from repeating the same guidance across sessions.

## Workflow rules

### Commit after every feature step

When a feature step (as defined in `docs/ROADMAP.md`) is completed and
validated, **create a commit immediately** unless there is a specific reason
not to (e.g., the step is half-done and the codebase is in a broken
intermediate state). Don't batch multiple steps into one commit.

Commit messages follow Conventional Commits:

```
feat(server): add 20Hz WorldSnapshot broadcast (Phase 1 step 3)
```

Include the phase/step reference in the commit body so git log doubles as
a progress journal.

### Always run CI gates before declaring a step done

```sh
# Rust
cargo fmt --all && cargo clippy --workspace --all-targets -- -D warnings && cargo test --workspace

# C++ plugin (if touched)
cmake --build build --config Release   # from client/plugin/
```

If either fails, fix before marking the step complete.

### Test tiers gate specific steps

See `docs/TESTING.md` for the full matrix. Quick reference:

- **T0** (CI/cargo): gates every PR.
- **T1** (one terminal): gates any net-code change.
- **T2** (two echo-clients): gates wire-format or broadcast changes.
- **T3** (solo in-game): gates any plugin change.
- **T4** (LAN): gates ghost rendering / multiplayer-visible features.

Don't mark a step done if its required test tier hasn't passed.

## Build environment

### Rust

- Toolchain: stable MSVC (`x86_64-pc-windows-msvc`).
- cargo lives at `/c/Users/danme/.cargo/bin` — prepend to PATH in bash.
- Workspace root `Cargo.toml` at repo root; members: `server`,
  `tools/echo-client`.

### C++ plugin

- CommonLibSSE-NG from `alandtse/CommonLibVR` tag `v4.14.0` via
  FetchContent. NOT the `CharmedBaryon` fork (unmaintained, breaks on
  modern MSVC).
- Must use `-T v143` toolset (v145/MSVC 14.50 breaks CommonLib headers).
- Generator: `Visual Studio 18 2026` from the bundled CMake 4.1.x.
- vcpkg at `C:\vcpkg`; triplet `x64-windows-static-md`.
- `TOML_HEADER_ONLY=1` compile definition required (vcpkg precompiled
  tomlplusplus.lib uses newer CRT symbols than v143 provides).
- Socket.cpp must have `SKIP_PRECOMPILE_HEADERS ON` — it includes
  winsock2.h which conflicts with CommonLib's REX/W32.
- PCH wrapper at `src/PCH.h` hoists `using namespace std::literals` to
  global scope (CommonLib's PCH puts it inside `namespace SKSE`).

### flatc codegen

- `tools/regen-protos.sh` runs flatc and sed-rewrites cross-module
  imports. Generated `*_generated.rs` and `*_generated.h` are committed.
- Generated Rust files need a `#![allow(warnings, unsafe_code, ...)]`
  header prepended by the regen script; without it workspace lints reject
  the machine-generated code.

## SKSE plugin lifecycle

- **kDataLoaded** is too early for gameplay access. Use it only for config
  load and console-command registration.
- **kPostLoadGame / kNewGame** is the "world is ready" signal. Connect to
  the server here (or later via `rl connect`).
- Always null-check `player->parentCell` before reading transforms or
  spawning objects. Without this the plugin crashes on main-menu / load
  transitions.
- Do NOT use self-rescheduling `AddTask` loops — SKSE's task pump drains
  multiple tasks per frame, turning a self-enqueue into an in-frame
  infinite loop. Use a background thread that pumps `AddTask` at a
  throttled cadence instead.

## Architecture rules (from docs/DEV_RULES.md)

- **H1 — Server authoritative.** Phase 1 trusts client transforms as a
  known compromise; don't extend this to new state without a proposal.
- **H2 — No LGPL/GPL/AGPL/MPL deps.** `cargo deny` enforces on Rust side;
  manually vet C++ deps before adding to `vcpkg.json`.
- **H3 — Wire schemas evolve forward only.** No field reuse, no renumber.
  Protocol version in the 4-byte header; mismatch → Disconnect.
- **H4 — `unsafe` Rust denied at workspace level.** Allow per-function
  with a `// SAFETY:` comment.

## Console command API

The plugin registers `rl` as a console command (hijacking an unused vanilla
slot). Subcommands: `status`, `connect`, `disconnect`, `cell`, `demo`,
`help`. Future UIs call `relive::commands::execute(cmdline)` — treat the
command dispatcher as the API, not `Plugin.h` directly.
