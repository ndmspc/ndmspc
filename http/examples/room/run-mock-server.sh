#!/usr/bin/env bash
# Start the room router mock (mock_mcp_server.py) in the foreground.
#
# The real router (Ndmspc::NRoomRouter) refuses to register outside Kubernetes, so this
# mock answers the same MCP endpoint for local development and testing.
set -euo pipefail

# `cd` writes the resolved directory to stdout when CDPATH is set and the path is
# relative (CDPATH commonly ends up containing "."), which would be captured here -
# so redirect it.
SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" >/dev/null && pwd -P)"

export HOST="${HOST:-127.0.0.1}"
export PORT="${PORT:-8090}"
export SEED="${SEED:-demo}"
export TTL="${TTL:-60}"
export FAIL="${FAIL:-0}"

command -v python3 >/dev/null 2>&1 || { echo "error: python3 is required" >&2; exit 1; }

exec python3 "$SCRIPT_DIR/mock_mcp_server.py"
