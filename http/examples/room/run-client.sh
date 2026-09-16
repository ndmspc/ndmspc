#!/usr/bin/env bash
# Run ndmspc-room-tui against a room router.
#
#   ./run-client.sh              # the interactive TUI
#   ./run-client.sh --list       # headless; extra arguments are passed through
#
# Point it somewhere else with URL=... (e.g. the real deployment's external URL).
set -euo pipefail

# `cd` writes the resolved directory to stdout when CDPATH is set and the path is
# relative (CDPATH commonly ends up containing "."), which would be captured here -
# so redirect it.
SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" >/dev/null && pwd -P)"
PROJECT_DIR="$(readlink -m "$SCRIPT_DIR/../../..")"

HOST="${HOST:-127.0.0.1}"
PORT="${PORT:-8090}"
URL="${URL:-http://$HOST:$PORT}"
CLIENT_BIN="${CLIENT_BIN:-$PROJECT_DIR/bin/ndmspc-room-tui}"

die() { echo "error: $*" >&2; exit 1; }

[ -x "$CLIENT_BIN" ] || die "client binary not found: $CLIENT_BIN (run ./scripts/make.sh install)"

exec "$CLIENT_BIN" --url "$URL" "$@"
