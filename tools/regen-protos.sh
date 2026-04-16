#!/usr/bin/env bash
# Regenerate flatbuffer code from schemas/v1/*.fbs.
#
# Re-run whenever a .fbs file changes. Generated files are committed.
# CI checks that the committed files match the regen output.
#
# Requires: flatc on PATH (winget install Google.flatbuffers).

set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "$SCRIPT_DIR/.." && pwd)"

# Locate flatc (winget installs to user-local AppData; not always on PATH in fresh shells).
FLATC="${FLATC:-flatc}"
if ! command -v "$FLATC" >/dev/null 2>&1; then
    CAND="$(find "$HOME/AppData/Local/Microsoft/WinGet" -name 'flatc.exe' 2>/dev/null | head -1)"
    if [[ -n "$CAND" ]]; then FLATC="$CAND"; fi
fi
"$FLATC" --version >/dev/null || { echo "flatc not found. winget install Google.flatbuffers"; exit 1; }

SCHEMA_DIR="$REPO/schemas/v1"
RUST_OUT="$REPO/server/src/proto/v1"
CPP_OUT="$REPO/client/plugin/src/proto/v1"

mkdir -p "$RUST_OUT" "$CPP_OUT"

echo "==> Rust codegen → $RUST_OUT"
"$FLATC" --rust -o "$RUST_OUT" -I "$SCHEMA_DIR" \
    "$SCHEMA_DIR/types.fbs" "$SCHEMA_DIR/lifecycle.fbs" "$SCHEMA_DIR/world.fbs"

# flatc emits `use crate::types_generated::*` which assumes generated files
# live at the crate root. Rewrite to absolute crate paths under `proto::v1`
# (matches the wrapper module). `super::` would also work but breaks inside
# the nested `mod skyrim_relive { mod v_1 { ... } }` blocks the generator emits.
# Each generated file wraps its types in `pub mod skyrim_relive { pub mod v_1 { ... } }`.
# A bare `use crate::types_generated::*` glob only reaches the top-of-file scope,
# which contains only the outer module — bare cross-namespace types like
# `DisconnectCode` then fail to resolve. Reach all the way into the deepest
# namespace so bare names work.
for f in "$RUST_OUT"/*_generated.rs; do
    sed -i 's|use crate::types_generated::|use crate::proto::v1::types_generated::skyrim_relive::v_1::|g' "$f"
    sed -i 's|use crate::lifecycle_generated::|use crate::proto::v1::lifecycle_generated::skyrim_relive::v_1::|g' "$f"
    sed -i 's|use crate::world_generated::|use crate::proto::v1::world_generated::skyrim_relive::v_1::|g' "$f"
    # Prepend a sweeping allow so workspace lints don't reject machine-generated code.
    sed -i '1i #![allow(warnings, unsafe_code, clippy::all, clippy::pedantic, clippy::nursery, clippy::restriction, clippy::cargo, clippy::style, clippy::complexity, clippy::correctness, clippy::perf, clippy::suspicious)]' "$f"
done

echo "==> C++ codegen → $CPP_OUT"
"$FLATC" --cpp -o "$CPP_OUT" -I "$SCHEMA_DIR" \
    "$SCHEMA_DIR/types.fbs" "$SCHEMA_DIR/lifecycle.fbs" "$SCHEMA_DIR/world.fbs"

echo "==> Done. Generated files:"
ls "$RUST_OUT"
ls "$CPP_OUT"
