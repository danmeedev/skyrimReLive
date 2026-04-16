## What

<!-- One sentence: what does this change do? -->

## Why

<!-- The motivation. Link an issue or proposal if there is one. -->

## How tested

<!-- cargo test? local round-trip? in-game? what platforms? -->

## Checklist

- [ ] CI green (fmt, clippy, build, test, cargo-deny, clang-format)
- [ ] No new LGPL/GPL/AGPL/MPL dependencies
- [ ] Wire-format changes are additive (or include a versioned proposal)
- [ ] Server remains authoritative for any new gameplay state
- [ ] If this changes the protocol or authority model, a proposal exists in `docs/proposals/`
