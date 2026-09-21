#!/usr/bin/env bash
# Start the NDMSPC ngnt server with X509 (mutual TLS) authentication.
#
# The defaults reproduce the grid example: the ~/.globus user certificate is the server
# certificate and the ALICE grid CA path verifies clients. Override SERVER_CERT /
# SERVER_KEY (for example with the certificates from make-certs.sh, which a browser can
# trust) and CA_FILE / CA_PATH (for example the CERN CA) to test other combinations. Set
# CORS to let a browser call the front door from another origin.
set -euo pipefail

# `cd` writes the resolved directory to stdout when CDPATH is set and the path is
# relative (CDPATH commonly ends up containing "."), which would be captured here -
# so redirect it.
SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" >/dev/null && pwd -P)"
PROJECT_DIR="$(readlink -m "$SCRIPT_DIR/../../..")"

GLOBUS_DIR="${GLOBUS_DIR:-$HOME/.globus}"
GLOBUS_CERT="${GLOBUS_CERT:-$GLOBUS_DIR/usercert.pem}"
GLOBUS_KEY="${GLOBUS_KEY:-$GLOBUS_DIR/userkey.pem}"
GLOBUS_PASSWORD_FILE="${GLOBUS_PASSWORD_FILE:-$GLOBUS_DIR/password.txt}"
GRID_CA_PATH="${GRID_CA_PATH:-/cvmfs/alice.cern.ch/etc/grid-security/certificates}"

SERVER_CERT="${SERVER_CERT:-$GLOBUS_CERT}"
SERVER_KEY="${SERVER_KEY:-$GLOBUS_KEY}"
CA_FILE="${CA_FILE:-}"
CA_PATH="${CA_PATH:-$GRID_CA_PATH}"
CORS="${CORS:-}"
PORT="${PORT:-8444}"
INTERNAL_PORT="${INTERNAL_PORT:-8081}"
SERVER_BIN="${SERVER_BIN:-$PROJECT_DIR/bin/ndmspc-server}"
KEY_OUT="${KEY_OUT:-}"

die() { echo "error: $*" >&2; exit 1; }

[ -r "$SERVER_CERT" ] || die "server certificate not readable: $SERVER_CERT"
[ -r "$SERVER_KEY" ] || die "server private key not readable: $SERVER_KEY"
if [ -z "$CA_FILE" ] && [ ! -d "$CA_PATH" ]; then
  die "no client CA: set CA_FILE to a CA bundle or CA_PATH to a CA directory (not found: $CA_PATH)"
fi
[ -x "$SERVER_BIN" ] || die "server binary not found: $SERVER_BIN (run ./scripts/make.sh install)"

# Passphrase: GLOBUS_PASSWORD (plain) wins, otherwise base64 in GLOBUS_PASSWORD_FILE.
password="${GLOBUS_PASSWORD:-}"
if [ -z "$password" ] && [ -r "$GLOBUS_PASSWORD_FILE" ]; then
  password="$(base64 -d < "$GLOBUS_PASSWORD_FILE" 2>/dev/null || true)"
fi

# httplib's TLS server loads the key from a file and has no passphrase callback, so an
# encrypted key must be decrypted to a private temporary file first.
runtime_dir=""
if [ -z "$KEY_OUT" ]; then
  runtime_dir="$(mktemp -d "${TMPDIR:-/tmp}/ndmspc-x509.XXXXXX")"
  KEY_OUT="$runtime_dir/userkey.pem"
fi
umask 077
if [ -n "$password" ]; then
  openssl pkey -in "$SERVER_KEY" -passin "pass:$password" -out "$KEY_OUT" \
    || die "failed to decrypt $SERVER_KEY (wrong passphrase?)"
else
  openssl pkey -in "$SERVER_KEY" -out "$KEY_OUT" \
    || die "failed to read $SERVER_KEY (passphrase required?)"
fi
chmod 600 "$KEY_OUT"

export NDMSPC_DIR="${NDMSPC_DIR:-$PROJECT_DIR}"

echo "Starting X509 ngnt server:"
echo "  public TLS port : $PORT"
echo "  internal engine : 127.0.0.1:$INTERNAL_PORT"
echo "  server cert     : $SERVER_CERT"
echo "  server key      : $KEY_OUT (decrypted from $SERVER_KEY)"
if [ -n "$CA_FILE" ]; then echo "  client CA file  : $CA_FILE"; fi
if [ -d "$CA_PATH" ]; then echo "  client CA path  : $CA_PATH"; fi
if [ -n "$CORS" ]; then echo "  CORS origins    : $CORS"; fi

server_pid=""
cleanup() {
  if [ -n "$server_pid" ]; then
    kill "$server_pid" 2>/dev/null || true
    wait "$server_pid" 2>/dev/null || true
  fi
  if [ -n "$runtime_dir" ]; then rm -rf "$runtime_dir"; fi
}
trap cleanup EXIT INT TERM

args=(-p "$PORT" --x509-cert "$SERVER_CERT" --x509-key "$KEY_OUT" --x509-internal-port "$INTERNAL_PORT")
if [ -n "$CA_FILE" ]; then args+=(--x509-ca-file "$CA_FILE"); fi
if [ -d "$CA_PATH" ]; then args+=(--x509-ca-path "$CA_PATH"); fi
if [ -n "$CORS" ]; then args+=(--x509-cors "$CORS"); fi

"$SERVER_BIN" "${args[@]}" &
server_pid=$!
wait "$server_pid"
