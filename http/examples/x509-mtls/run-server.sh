#!/usr/bin/env bash
# Start the NDMSPC ngnt server with X509 (mutual TLS) authentication using a grid
# user certificate and the ALICE grid CA path for client-certificate verification.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(readlink -m "$SCRIPT_DIR/../../..")"

GLOBUS_DIR="${GLOBUS_DIR:-$HOME/.globus}"
GLOBUS_CERT="${GLOBUS_CERT:-$GLOBUS_DIR/usercert.pem}"
GLOBUS_KEY="${GLOBUS_KEY:-$GLOBUS_DIR/userkey.pem}"
GLOBUS_PASSWORD_FILE="${GLOBUS_PASSWORD_FILE:-$GLOBUS_DIR/password.txt}"
GRID_CA_PATH="${GRID_CA_PATH:-/cvmfs/alice.cern.ch/etc/grid-security/certificates}"
PORT="${PORT:-8444}"
INTERNAL_PORT="${INTERNAL_PORT:-8081}"
SERVER_BIN="${SERVER_BIN:-$PROJECT_DIR/bin/ndmspc-server}"
KEY_OUT="${KEY_OUT:-}"

die() { echo "error: $*" >&2; exit 1; }

[ -r "$GLOBUS_CERT" ] || die "certificate not readable: $GLOBUS_CERT"
[ -r "$GLOBUS_KEY" ] || die "private key not readable: $GLOBUS_KEY"
[ -d "$GRID_CA_PATH" ] || die "grid CA directory not found: $GRID_CA_PATH"
[ -x "$SERVER_BIN" ] || die "server binary not found: $SERVER_BIN (run ./scripts/make.sh install)"

# Passphrase: GLOBUS_PASSWORD (plain) wins, otherwise base64 in GLOBUS_PASSWORD_FILE.
password="${GLOBUS_PASSWORD:-}"
if [ -z "$password" ] && [ -r "$GLOBUS_PASSWORD_FILE" ]; then
  password="$(base64 -d < "$GLOBUS_PASSWORD_FILE" 2>/dev/null || true)"
fi

# httplib's TLS server loads the key from a file and has no passphrase callback, so an
# encrypted grid key must be decrypted to a private temporary file first.
runtime_dir=""
if [ -z "$KEY_OUT" ]; then
  runtime_dir="$(mktemp -d "${TMPDIR:-/tmp}/ndmspc-x509.XXXXXX")"
  KEY_OUT="$runtime_dir/userkey.pem"
fi
umask 077
if [ -n "$password" ]; then
  openssl pkey -in "$GLOBUS_KEY" -passin "pass:$password" -out "$KEY_OUT" \
    || die "failed to decrypt $GLOBUS_KEY (wrong passphrase?)"
else
  openssl pkey -in "$GLOBUS_KEY" -out "$KEY_OUT" \
    || die "failed to read $GLOBUS_KEY (passphrase required?)"
fi
chmod 600 "$KEY_OUT"

export NDMSPC_DIR="${NDMSPC_DIR:-$PROJECT_DIR}"

echo "Starting X509 ngnt server:"
echo "  public TLS port : $PORT"
echo "  internal engine : 127.0.0.1:$INTERNAL_PORT"
echo "  server cert     : $GLOBUS_CERT"
echo "  server key      : $KEY_OUT (decrypted from $GLOBUS_KEY)"
echo "  client CA path  : $GRID_CA_PATH"

server_pid=""
cleanup() {
  if [ -n "$server_pid" ]; then
    kill "$server_pid" 2>/dev/null || true
    wait "$server_pid" 2>/dev/null || true
  fi
  if [ -n "$runtime_dir" ]; then rm -rf "$runtime_dir"; fi
}
trap cleanup EXIT INT TERM

"$SERVER_BIN" start ngnt \
  -p "$PORT" \
  --x509-cert "$GLOBUS_CERT" \
  --x509-key "$KEY_OUT" \
  --x509-ca-path "$GRID_CA_PATH" \
  --x509-internal-port "$INTERNAL_PORT" &
server_pid=$!
wait "$server_pid"
