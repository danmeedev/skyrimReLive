# schemas

Shared Flatbuffers wire-format schemas. Codegen outputs go to:
- `server/src/proto/v1/` (Rust)
- `client/plugin/src/proto/v1/` (C++)

## Directory naming vs on-wire protocol version

The schema directory is named `v1/` for historical reasons. The on-wire
protocol version (the `ver` byte in every packet header) is **`2`** as of
Phase 2 step 2.1, when `PlayerState` was converted from a Flatbuffers
`struct` (inline, fixed-size) to a `table` (heap-allocated, supports
adding new fields). The struct→table change is binary-incompatible, so
the version byte was bumped; v1 clients now receive
`Disconnect{VersionMismatch}` from the server.

The directory was deliberately not renamed to `v2/` (and the generated
`proto/v1/` paths weren't moved either). There is no deployed v1 anywhere,
so renaming would be churn for no gain. Future breaking changes that
warrant a fresh schema set will create a sibling `schemas/v2/` directory
alongside the existing one.

## Schema files

```
schemas/v1/
  types.fbs       MessageType enum (Hello=1..WorldSnapshot=17),
                  DisconnectCode enum.
  lifecycle.fbs   Hello, Welcome, Heartbeat, LeaveNotify, Disconnect tables.
  world.fbs       Vec3 struct, Transform struct (pos + yaw), PlayerState
                  table (transform + locomotion graph vars + weapon
                  state), PlayerInput table, WorldSnapshot table.
```

All packets use a 4-byte header (`R L ver type`) before the Flatbuffer body.
The `ver` byte is `2`. The `type` byte maps to `MessageType`.

## Regenerating

Run `tools/regen-protos.sh` after editing any `.fbs` file. Requires `flatc`
on PATH (`winget install Google.flatbuffers`).

```sh
tools/regen-protos.sh
```

The script:
1. Generates Rust code into `server/src/proto/v1/`
2. Rewrites cross-file `use` paths for the Rust module layout
3. Prepends `#![allow(...)]` to suppress lints on generated code
4. Generates C++ headers into `client/plugin/src/proto/v1/`

Generated files are committed to the repo. CI checks that committed files
match regen output.

## Schema evolution rules

- Additions only; never remove or renumber fields.
- Never reuse a deleted field's ID or enum value.
- Version the root table when breaking changes require a new schema set.
