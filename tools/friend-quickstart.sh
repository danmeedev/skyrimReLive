#!/usr/bin/env bash
# Bash wrapper around tools/friend-quickstart.ps1 for git-bash users.
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
exec powershell -NoProfile -ExecutionPolicy Bypass -File "$SCRIPT_DIR/friend-quickstart.ps1" "$@"
