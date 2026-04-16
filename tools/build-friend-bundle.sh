#!/usr/bin/env bash
# Bash wrapper around tools/build-friend-bundle.ps1 for git-bash users.
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
exec powershell -NoProfile -ExecutionPolicy Bypass -File "$SCRIPT_DIR/build-friend-bundle.ps1" "$@"
