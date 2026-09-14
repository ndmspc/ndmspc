#!/usr/bin/env bash
# Drive the MCP HTTP endpoint with curl: initialize, tools/list and tools/call.
set -euo pipefail

PORT="${PORT:-8080}"
URL="${URL:-http://localhost:$PORT/api/mcp}"
FILE="${FILE:-}"
H='Content-Type: application/json'

call() {
  curl -sS "$URL" -H "$H" -d "$1"
  echo
}

echo "== initialize =="
call '{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-06-18"}}'

echo "== tools/list =="
call '{"jsonrpc":"2.0","id":2,"method":"tools/list","params":{}}'

echo "== tools/call: ngnt_open GET =="
call '{"jsonrpc":"2.0","id":3,"method":"tools/call","params":{"name":"ngnt_open","arguments":{"method":"GET"}}}'

if [ -n "$FILE" ]; then
  echo "== tools/call: ngnt_open POST $FILE =="
  call "{\"jsonrpc\":\"2.0\",\"id\":4,\"method\":\"tools/call\",\"params\":{\"name\":\"ngnt_open\",\"arguments\":{\"method\":\"POST\",\"file\":\"$FILE\"}}}"
fi
