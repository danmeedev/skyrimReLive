# 0004 — Phase 3: world state (cells, doors, containers)

- **Status:** accepted
- **Author(s):** SkyrimReLive contributors
- **Created:** 2026-04-16
- **Supersedes:** —

## Summary

Make the shared world feel real: players can walk through doors into the
same interior, see each other across cell transitions, and (in later
steps) interact with the same doors and containers. Phase 2 proved
combat works; Phase 3 proves the *world* works.

## Motivation

Today both players must be in the same cell and within 5000 world units,
or the ghost doesn't render. Walking into Dragonsreach despawns the
other player's ghost because the interior is a different cell. This is
the single biggest "it doesn't actually work" moment in a playtest.

## Design

### Step 3.1 — Cell-aware Area of Interest

**Goal:** replace the hard 5000-unit distance gate with proper cell
matching. Ghosts auto-spawn when a remote player enters your cell,
auto-despawn when they leave.

#### Wire delta (additive, still v2)

```
table PlayerInput {
    ...
    cell_form_id: uint32 = 0;   // 0 = unknown / exterior worldspace
}

table PlayerState {
    ...
    cell_form_id: uint32 = 0;
}
```

#### Server

New `Cell` component:
```rust
#[derive(Component, Debug, Default, Clone, Copy)]
pub struct Cell {
    pub form_id: u32,
}
```

`handle_player_input` writes `cell.form_id` from the input.

`broadcast_snapshot` changes from "send everyone except self" to
"send only players whose cell matches self's cell." This is the AoI
filter. For exterior worldspace cells (form_id = 0 or Tamriel-class),
fall back to the existing behaviour (everyone in the worldspace is
visible) — exterior cells are large and adjacent cells overlap visually.

#### Plugin

`Net::send_player_input` reads `player->parentCell->GetFormID()` and
ships it.

`Ghost::tick_main_thread` replaces the distance gate with a cell gate:
- If the remote player's `cell_form_id` doesn't match the local
  player's `parentCell->GetFormID()`, despawn the ghost.
- If it does match and the ghost isn't spawned, spawn it.

The existing `Cell` module (`cell::instance()`) currently just polls
parentCell and exposes `is_active()`. Extend it to also report the
current FormID and detect transitions.

#### Interior coordinate systems

Interior cells use their own local coordinate system (origin at the
cell's center, not worldspace). When both players are in the same
interior, their coordinates are in the same frame — `SetPosition` just
works. Cross-cell coordinate transforms are not needed because we never
render a ghost in a cell they aren't in.

### Step 3.2 — Cell transition events (follow-up)

**Goal:** make ghost appear/disappear instantly on door load instead of
waiting for the next PlayerInput tick (~16 ms later).

New `CellChange` message (C→S):
```
table CellChange {
    new_cell_form_id: uint32;
    client_time_ms: uint64;
}
```

Server updates the player's `Cell` component immediately and triggers a
one-shot broadcast so all peers in the old and new cells get updated
player lists. Peers in the old cell despawn the ghost; peers in the new
cell spawn it.

Plugin: detect cell change via `parentCell` polling in the existing
`cell::CellWatcher`. When the cell changes, fire `CellChange`
immediately (don't wait for the next PlayerInput).

### Step 3.3 — Doors and activators (follow-up)

New message: `ActivateEvent { form_id: uint32, activated: bool }`.
When a player opens a door (activates a TESObjectDOOR), the plugin
sends an ActivateEvent. Server broadcasts to all peers in the same
cell. Receiving plugin calls `door->Activate(player, ...)` so the
animation plays.

Scoped to doors and pull-chains for the first pass. Generic activators
(levers, buttons, traps) extend the same path.

### Step 3.4 — Container sync (stretch)

Server-owned container state. When player A loots a chest, the server
records which items were taken. Player B's client reflects the removal.
Requires a server-side item registry — defer design until 3.1-3.3 are
proven.

## Alternatives considered

1. **Distance-only AoI (keep the 5000u gate, extend to 10000u).**
   Fails for interiors — Dragonsreach is a different cell at origin
   (0,0,0), so distance from an exterior player is huge. Cell matching
   is the only reliable boundary.

2. **Client-side cell filter only (no server involvement).**
   Works for spawn/despawn but wastes bandwidth — server still sends
   every player's state to every client, and clients throw away
   cross-cell data. Server-side AoI is trivial (one u32 compare) and
   saves bandwidth proportionally to the number of occupied cells.

3. **Worldspace-level grouping instead of cell-level.**
   Too coarse — Tamriel is one worldspace containing thousands of
   exterior cells. Interior cells are where the boundary matters.

## Open questions

- **Exterior cell adjacency:** two players in adjacent exterior cells
  (e.g., one just outside Whiterun, one 50 feet away but technically
  in the next cell) should still see each other. Initial ship: treat
  all exterior cells in the same worldspace as one AoI group. Finer
  exterior-cell culling is Phase 6 optimization.
- **Fast travel:** fast travel changes cells. The plugin detects this
  via parentCell polling (same as door load). No special handling
  needed — CellChange fires, server re-routes.
- **Loading screens:** during a loading screen, parentCell is null.
  The plugin already null-checks this (CLAUDE.md rule). Ghost ticker
  pauses, resumes when the new cell loads.

## Migration / rollout

Wire-additive, no version bump. Old clients that don't send
`cell_form_id` default to 0 — server treats 0 as "unknown, include
in all broadcasts" (backwards-compatible, just less efficient). Old
clients still see everyone regardless of cell (existing behavior).

## Changelog

- 2026-04-16: initial draft, accepted
