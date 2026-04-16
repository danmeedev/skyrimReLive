# Dev Rules

Operational rules for working in this codebase. The Hard Rules are
non-negotiable; the Soft Rules are defaults you can override with a clear
reason in the PR description.

---

## Hard Rules

### H1 — Server is authoritative

For anything that affects shared world state or another player, the server
decides. The client predicts and renders; it never tells the truth.

**Why:** Co-op is `localhost` mode of the same architecture as MMO. If we let
client-authority creep in for "just co-op," MMO mode becomes a rewrite, not a
deployment.

**How to apply:** When syncing a new piece of state, ask "if a malicious client
sent the wrong value, what happens?" If the answer is "they cheat" or "the
world breaks," it must be server-validated. Render-only state (animation
tweens, particle FX) can be client-authoritative.

### H2 — No LGPL / GPL / AGPL / MPL dependencies

Apache-2.0, MIT, BSD-2/3, ISC, Zlib, Unicode, CC0 only. Enforced by
`cargo-deny`. For C++ deps, vet manually before adding to `vcpkg.json`.

**Why:** The whole point of this project is an open, fork-friendly codebase
that small hosts and commercial hosts can both use. Copyleft contamination
breaks that promise.

**How to apply:** New dependency PRs must include a comment noting the license.
If you need functionality from a copyleft library, reimplement the small
piece you need under Apache-2.0.

### H3 — Wire-format schemas evolve forward only

Once a field is defined, its tag/ID is permanent. No reuse, no renumber,
no semantic change. Breaking changes require a protocol version bump in the
handshake and a proposal in `docs/proposals/`.

**Why:** Self-hosters will run mismatched client/server versions. We need
graceful degradation, not crashes.

**How to apply:** Add fields, never modify or remove. Mark deprecated fields
in the schema comment. Server treats unknown fields as ignorable. Both sides
include a `protocol_version` in the handshake; mismatched versions get a
clear error, not a desync.

### H4 — `unsafe` Rust is denied at the workspace lint level

If you genuinely need `unsafe`, allow it on the specific function with a
`// SAFETY:` comment and call it out in the PR.

**Why:** The server is meant to be fearlessly hosted by random people. Memory
safety is a feature.

### H5 — Secrets and credentials never enter the repo

No `.env` files, no API keys, no character DB dumps, no test credentials.
`.gitignore` is the safety net, not the policy. Use env vars and document them
in the relevant `README.md`.

---

## Soft Rules (defaults — override in the PR description if you must)

### S1 — Comments explain *why*, not *what*

Code with self-explanatory names doesn't need a comment to repeat itself. A
comment earns its keep when it captures a hidden constraint, a workaround, or
a subtle invariant that a future reader would otherwise miss.

```rust
// BAD: increment counter
counter += 1;

// GOOD: handshake retries are bounded so a hostile client can't spin us
// forever; the limit matches the client's own retry policy in net/handshake.cpp
counter += 1;
```

### S2 — No premature abstraction

Three similar lines is fine. Four is fine. When you see the fifth and the
right factoring is obvious, then refactor. Don't design for hypothetical
future variants.

### S3 — Test what's plausible to break

Pure logic gets unit tests (we have `handle()` in `server/src/main.rs` as a
template). I/O integration gets one happy-path smoke test, not exhaustive
mocks. SKSE plugin gameplay code is not unit-testable — keep gameplay logic
out of the plugin (replicate from server) so the testable surface stays in
Rust.

### S4 — Performance budget for the server tick

The server tick must fit in 16ms at design load (50 players per zone). If your
change adds a hot-path allocation or a sync system that grows superlinearly
with player count, call it out in the PR. We don't pre-optimize, but we don't
ship unbounded work either.

### S5 — Logs over print

Use `tracing` macros (`info!`, `warn!`, `error!`, `debug!`). They're
structured and filterable. Plain `println!` is fine in `tools/` CLIs.

### S6 — Format on save

Run `rustfmt` and `clang-format`. CI will reject otherwise. The
`.editorconfig` covers indent/EOL for less-opinionated files.

### S7 — One log line per round-trip class

When adding net features, log once per *event class* (e.g., "player joined")
not once per packet. The hello server logs each round-trip because it's
phase 0 — by phase 2 we'll be doing 60Hz updates and that ratio breaks.

---

## When in doubt

Open a draft PR with a question. The answer's usually faster than guessing.
