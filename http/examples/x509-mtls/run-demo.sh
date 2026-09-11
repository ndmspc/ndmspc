#!/usr/bin/env bash
# End-to-end check of the X509 (mutual TLS) server + WebSocket client example:
#   1. an authenticated client presenting the grid certificate must connect and be
#      recognised by its certificate identity (CN);
#   2. a client without a certificate must be rejected at the TLS handshake.
set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
GLOBUS_DIR="${GLOBUS_DIR:-$HOME/.globus}"
GLOBUS_CERT="${GLOBUS_CERT:-$GLOBUS_DIR/usercert.pem}"
PORT="${PORT:-8444}"
LOG="${LOG:-$(mktemp "${TMPDIR:-/tmp}/ndmspc-x509-demo.XXXXXX.log")}"

"$SCRIPT_DIR/run-server.sh" >"$LOG" 2>&1 &
server_pid=$!
cleanup() {
  kill "$server_pid" 2>/dev/null
  wait "$server_pid" 2>/dev/null
}
trap cleanup EXIT INT TERM

echo "Waiting for the X509 server (log: $LOG) ..."
ready=0
for _ in $(seq 1 120); do
  if grep -q "X509 server is running and ready to use" "$LOG" 2>/dev/null; then
    ready=1
    break
  fi
  if ! kill -0 "$server_pid" 2>/dev/null; then break; fi
  sleep 0.5
done

if [ "$ready" != 1 ]; then
  echo "Server did not become ready. Output:"
  cat "$LOG"
  echo "RESULT: FAIL"
  exit 1
fi

expected="$(openssl x509 -in "$GLOBUS_CERT" -noout -subject 2>/dev/null |
  grep -oE 'CN[[:space:]]*=[[:space:]]*[^,]*' | head -n1 |
  sed -E 's/^CN[[:space:]]*=[[:space:]]*//; s/^[[:space:]]+//; s/[[:space:]]+$//')"

echo
echo "== 1/2 authenticated WebSocket client (expect success, username='$expected') =="
auth_out="$(TIMEOUT="${TIMEOUT:-8}" "$SCRIPT_DIR/run-client.sh" 2>&1)"
auth_rc=$?
echo "$auth_out"

echo
echo "== 2/2 anonymous WebSocket client, no certificate (expect rejection) =="
WITH_CERT=0 TIMEOUT="${ANON_TIMEOUT:-4}" "$SCRIPT_DIR/run-client.sh" >/dev/null 2>&1
anon_rc=$?

echo
fail=0
if [ "$auth_rc" -ne 0 ]; then
  echo "FAIL: authenticated client exited $auth_rc"
  fail=1
elif ! grep -qF "\"username\":\"$expected\"" <<<"$auth_out"; then
  echo "FAIL: authenticated client did not report username '$expected'"
  fail=1
else
  echo "OK: authenticated client connected as '$expected'"
fi
if [ "$anon_rc" -eq 0 ]; then
  echo "FAIL: client without a certificate was accepted"
  fail=1
else
  echo "OK: client without a certificate was rejected"
fi

echo
if [ "$fail" -eq 0 ]; then echo "RESULT: PASS"; else echo "RESULT: FAIL"; fi
exit "$fail"
