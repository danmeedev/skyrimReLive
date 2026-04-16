# Contributing

Thanks for your interest. SkyrimReLive is Apache-2.0 licensed and we want it to
stay that way — so a few rules exist to keep the codebase coherent and the
license clean. Read this once before your first PR.

## Just want to play or host?

This doc is for code contributors. If you want to **play** on a friend's
server, see [`docs/PLAYER_SETUP.md`](docs/PLAYER_SETUP.md). If you want to
**host** a server for yourself and friends, see
[`docs/HOST_SETUP.md`](docs/HOST_SETUP.md).

## Workflow

- Fork, branch, PR. No direct pushes to `main`.
- Branch names: `feat/short-thing`, `fix/short-thing`, `docs/short-thing`,
  `chore/short-thing`. Use what fits — we don't gate on this.
- Keep PRs small. One concern per PR. If you find yourself writing "and also"
  in the description, split it.
- Rebase, don't merge, when updating against `main`.

## Commits

Conventional Commits subjects, imperative mood:

```
feat(server): add interest-management grid
fix(plugin): handle missing animgraph variable
docs(roadmap): mark phase 0 complete
chore(deps): bump tokio to 1.53
```

The body explains *why*, not what — the diff already shows what.

## Testing scopes

Success criteria for every phase, plus the test tiers T0–T5 and what they
gate, live in [`docs/TESTING.md`](docs/TESTING.md). Check which tier applies
to your PR before claiming it's done.

## Required CI gates

Every PR must pass:

- `cargo fmt --all --check`
- `cargo clippy --workspace --all-targets -- -D warnings`
- `cargo build --workspace --all-targets` on Linux and Windows
- `cargo test --workspace`
- `cargo deny check` (license / source / bans)
- `clang-format --dry-run --Werror` on `client/plugin/src/`

Run locally before pushing:

```sh
cargo fmt --all && cargo clippy --workspace --all-targets -- -D warnings && cargo test --workspace
```

## Proposals (RFCs)

Before changing any of these, open a proposal in `docs/proposals/`:

- The wire format or protocol semantics
- The authority model (who decides what)
- A new dependency that adds significant binary size, transitive deps, or
  unfamiliar runtime
- The license policy

Use `docs/proposals/0000-template.md`. Number sequentially when merged.

For everything else, open a PR. We don't need a proposal to fix a bug.

## Code style

The Hard Rules are in [`docs/DEV_RULES.md`](docs/DEV_RULES.md). Read it.
The short version:

- **Server-authoritative.** Never trust the client for game state.
- **No LGPL/GPL/AGPL/MPL deps.** Apache-2.0/MIT/BSD/ISC only. CI enforces this.
- **Schemas evolve forward.** No field reuse, no breaking changes without a
  protocol version bump and a proposal.
- **Comments explain why.** If the *what* isn't clear from the code, rewrite
  the code.
- **No premature abstraction.** Three similar lines beats a wrong trait.

## Licensing your contribution

By submitting a PR you agree your contribution is licensed under Apache-2.0.
We use a DCO (Developer Certificate of Origin) sign-off — add `-s` to your
commits:

```sh
git commit -s -m "feat(server): ..."
```

## Reporting security issues

Don't open a public issue. See [`SECURITY.md`](SECURITY.md).
