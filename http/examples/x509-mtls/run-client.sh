#!/usr/bin/env bash
# Connect ndmspc-ws-client to an X509 (mutual TLS) NDMSPC server, presenting the grid
# user certificate. Set WITH_CERT=0 to connect without a certificate (must be rejected).
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(readlink -m "$SCRIPT_DIR/../../..")"

GLOBUS_DIR="${GLOBUS_DIR:-$HOME/.globus}"
CLIENT_CERT="${CLIENT_CERT:-${GLOBUS_CERT:-$GLOBUS_DIR/usercert.pem}}"
CLIENT_KEY="${CLIENT_KEY:-${GLOBUS_KEY:-$GLOBUS_DIR/userkey.pem}}"
GLOBUS_PASSWORD_FILE="${GLOBUS_PASSWORD_FILE:-$GLOBUS_DIR/password.txt}"
GRID_CA_PATH="${GRID_CA_PATH:-/cvmfs/alice.cern.ch/etc/grid-security/certificates}"
PORT="${PORT:-8444}"
URL="${URL:-wss://localhost:$PORT/ws/root.websocket}"
TIMEOUT="${TIMEOUT:-10}"
WITH_CERT="${WITH_CERT:-1}"
CLIENT_BIN="${CLIENT_BIN:-$PROJECT_DIR/bin/ndmspc-ws-client}"

die() { echo "error: $*" >&2; exit 1; }

[ -x "$CLIENT_BIN" ] || die "client binary not found: $CLIENT_BIN (run ./scripts/make.sh install)"

args=(--url "$URL" --ca-path "$GRID_CA_PATH" --timeout "$TIMEOUT" -r 1)

if [ "$WITH_CERT" = "1" ]; then
  [ -r "$CLIENT_CERT" ] || die "certificate not readable: $CLIENT_CERT"
  [ -r "$CLIENT_KEY" ] || die "private key not readable: $CLIENT_KEY"

  password="${GLOBUS_PASSWORD:-}"
  if [ -z "$password" ] && [ -r "$GLOBUS_PASSWORD_FILE" ]; then
    password="$(base64 -d < "$GLOBUS_PASSWORD_FILE" 2>/dev/null || true)"
  fi

  args+=(--cert "$CLIENT_CERT" --key "$CLIENT_KEY")
  # Passphrase sources, in order: GLOBUS_PASSWORD (plain text), the base64 password file,
  # then an interactive prompt from the client when the key is encrypted and a terminal
  # is attached.
  if [ -n "$password" ]; then
    args+=(--key-pass "$password")
  elif [ -r "$GLOBUS_PASSWORD_FILE" ]; then
    args+=(--key-pass-file "$GLOBUS_PASSWORD_FILE")
  fi

  # The grid *user* certificate is clientAuth-only (no serverAuth EKU, no SAN), so it is
  # not a valid server certificate for a verifying client; see the README. mTLS client
  # authentication is still enforced by the server.
  args+=(--allow-insecure)
else
  args+=(--allow-insecure)
fi

echo "Connecting $URL (with_cert=$WITH_CERT)..."
"$CLIENT_BIN" "${args[@]}"
