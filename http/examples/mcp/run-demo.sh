#!/usr/bin/env bash
# End-to-end check of the NDMSPC MCP server over both transports:
#   1. stdio  — ndmspc-mcp answers on stdout, logs on stderr;
#   2. HTTP   — POST /api/mcp on a running ngnt server answers initialize/tools/list.
set -uo pipefail

# `cd` writes the resolved directory to stdout when CDPATH is set and the path is
# relative (CDPATH commonly ends up containing "."), which would be captured here -
# so redirect it.
SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" >/dev/null && pwd -P)"
PROJECT_DIR="$(readlink -m "$SCRIPT_DIR/../../..")"

MCP_BIN="${MCP_BIN:-$PROJECT_DIR/bin/ndmspc-mcp}"
SERVER_BIN="${SERVER_BIN:-$PROJECT_DIR/bin/ndmspc-server}"
MACROS="${MACROS:-$PROJECT_DIR/macros/tools/toolBase.C,$PROJECT_DIR/macros/tools/toolNgnt.C}"
PORT="${PORT:-18080}"
LOG="${LOG:-$(mktemp "${TMPDIR:-/tmp}/ndmspc-mcp-demo.XXXXXX.log")}"

die() { echo "error: $*" >&2; exit 1; }
[ -x "$MCP_BIN" ] || die "mcp binary not found: $MCP_BIN (run ./scripts/make.sh install)"
[ -x "$SERVER_BIN" ] || die "server binary not found: $SERVER_BIN (run ./scripts/make.sh install)"

fail=0

echo "== 1/2 stdio transport =="
stdio_out="$(printf '%s\n%s\n%s\n' \
  '{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-06-18"}}' \
  '{"jsonrpc":"2.0","id":2,"method":"tools/list","params":{}}' \
  '{"jsonrpc":"2.0","id":3,"method":"tools/call","params":{"name":"state","arguments":{"method":"GET"}}}' \
  | "$MCP_BIN" -m "$MACROS" 2>/dev/null)"

count="$(printf '%s\n' "$stdio_out" | grep -c '^{')"
if [ "$count" = "3" ]; then
  echo "OK: 3 JSON-RPC responses on stdout"
else
  echo "FAIL: expected 3 responses, got $count"
  fail=1
fi
if grep -q '"name":"ngnt_open"' <<<"$stdio_out"; then
  echo "OK: tools/list exposes ngnt_open"
else
  echo "FAIL: ngnt_open missing from tools/list"
  fail=1
fi
if grep -q '"isError":false' <<<"$stdio_out"; then
  echo "OK: tools/call succeeded"
else
  echo "FAIL: tools/call did not succeed"
  fail=1
fi

echo
echo "== 2/2 HTTP transport (POST /api/mcp on port $PORT) =="
"$SERVER_BIN" -p "$PORT" --mcp true -m "$MACROS" >"$LOG" 2>&1 &
server_pid=$!
cleanup() {
  kill "$server_pid" 2>/dev/null
  wait "$server_pid" 2>/dev/null
}
trap cleanup EXIT INT TERM

ready=0
for _ in $(seq 1 120); do
  if grep -q "ready to use" "$LOG" 2>/dev/null; then
    ready=1
    break
  fi
  kill -0 "$server_pid" 2>/dev/null || break
  sleep 0.5
done
if [ "$ready" != 1 ]; then
  echo "FAIL: server did not become ready"
  cat "$LOG"
  echo "RESULT: FAIL"
  exit 1
fi
echo "OK: server ready (log: $LOG)"

U="http://localhost:$PORT/api/mcp"
init="$(curl -sS "$U" -H 'Content-Type: application/json' -d '{"jsonrpc":"2.0","id":1,"method":"initialize","params":{}}')"
if grep -q '"serverInfo"' <<<"$init"; then
  echo "OK: initialize answered"
else
  echo "FAIL: initialize: $init"
  fail=1
fi

tools="$(curl -sS "$U" -H 'Content-Type: application/json' -d '{"jsonrpc":"2.0","id":2,"method":"tools/list","params":{}}')"
if grep -q '"name":"ngnt_reshape"' <<<"$tools"; then
  echo "OK: tools/list exposes ngnt_reshape"
else
  echo "FAIL: tools/list: $tools"
  fail=1
fi

echo
if [ "$fail" -eq 0 ]; then echo "RESULT: PASS"; else echo "RESULT: FAIL"; fi
exit "$fail"
