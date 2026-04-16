# schemas

Shared Flatbuffers wire-format schemas. Codegen outputs go to:
- `server/src/proto/v1/` (Rust)
- `client/plugin/src/proto/v1/` (C++)

## v1 schemas

```
schemas/v1/
  types.fbs       MessageType enum (Hello=1..WorldSnapshot=17),
                  DisconnectCode enum.
  lifecycle.fbs   Hello, Welcome, Heartbeat, LeaveNotify, Disconnect tables.
  world.fbs       Vec3 struct, Transform struct (pos + yaw), PlayerState
                  struct, PlayerInput table, WorldSnapshot table.
```

All packets use a 4-byte header (`R L ver type`) before the Flatbuffer body.
The `ver` byte is currently `1`. The `type` byte maps to `MessageType`.

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
