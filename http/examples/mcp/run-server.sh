#!/usr/bin/env bash
# Start the ngnt HTTP server with the MCP endpoint enabled (POST /api/mcp).
set -euo pipefail

# `cd` writes the resolved directory to stdout when CDPATH is set and the path is
# relative (CDPATH commonly ends up containing "."), which would be captured here -
# so redirect it.
SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" >/dev/null && pwd -P)"
PROJECT_DIR="$(readlink -m "$SCRIPT_DIR/../../..")"

PORT="${PORT:-8080}"
SERVER_BIN="${SERVER_BIN:-$PROJECT_DIR/bin/ndmspc-server}"
MACROS="${MACROS:-$PROJECT_DIR/macros/tools/toolNgnt.C}"

die() { echo "error: $*" >&2; exit 1; }

[ -x "$SERVER_BIN" ] || die "server binary not found: $SERVER_BIN (run ./scripts/make.sh install)"
[ -r "${MACROS%%,*}" ] || die "macro not readable: ${MACROS%%,*}"

export NDMSPC_DIR="${NDMSPC_DIR:-$PROJECT_DIR}"

echo "Starting ngnt server on port $PORT (MCP endpoint: POST http://localhost:$PORT/api/mcp)"
exec "$SERVER_BIN" -p "$PORT" --mcp true -m "$MACROS"
