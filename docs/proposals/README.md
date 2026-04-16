# Proposals

Lightweight RFC process. Required for changes to:

- Wire format / protocol semantics
- Authority model (server vs client decides what)
- License policy
- New dependencies with significant binary/transitive impact

Optional but encouraged for any change you want feedback on before
implementing.

## Process

1. Copy `0000-template.md` to `NNNN-short-title.md`. Pick the next free number
   when your PR is ready to merge.
2. Open a PR with just the proposal. Discuss in the PR.
3. When the proposal merges, status becomes "accepted". Implementation can
   then land in subsequent PRs.

Proposals can be amended after acceptance via a follow-up PR — keep history
in the file's "Changelog" section.
