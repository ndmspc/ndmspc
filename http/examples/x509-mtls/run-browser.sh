#!/usr/bin/env bash
# Start the NDMSPC ngnt server with X509 (mutual TLS) authentication for a browser test.
#
# The TLS server certificate comes from make-certs.sh (so a browser can trust it after
# importing the test CA), while client certificates are verified against CA_PATH -
# point it at the CERN CA to authenticate with your CERN user certificate.
#
#   CA_PATH=/path/to/cern-ca-dir CA_FILE=/path/to/cern-ca.pem ./run-browser.sh
set -euo pipefail

# `cd` writes the resolved directory to stdout when CDPATH is set and the path is
# relative (CDPATH commonly ends up containing "."), which would be captured here -
# so redirect it.
SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" >/dev/null && pwd -P)"

CERT_DIR="${CERT_DIR:-$SCRIPT_DIR/.certs}"
# Grid trust anchors used by the other scripts; override with the CERN CA for a CERN cert.
GRID_CA_PATH="${GRID_CA_PATH:-/cvmfs/alice.cern.ch/etc/grid-security/certificates}"
CA_PATH="${CA_PATH:-$GRID_CA_PATH}"
CA_FILE="${CA_FILE:-}"
CORS="${CORS:-http://localhost:5173}"
PORT="${PORT:-8444}"
INTERNAL_PORT="${INTERNAL_PORT:-8081}"
CLIENT_P12_PASSWORD="${CLIENT_P12_PASSWORD:-ndmspc}"

die() { echo "error: $*" >&2; exit 1; }

[ -s "$CERT_DIR/server.pem" ] || "$SCRIPT_DIR/make-certs.sh"

if [ -z "$CA_FILE" ] && [ ! -d "$CA_PATH" ]; then
  die "no client CA directory at $CA_PATH: set CA_PATH to the CERN CA (hashed) directory, or CA_FILE to a CA bundle"
fi

cat <<EOF
X509 browser test
  1. trust the test CA in Firefox: $CERT_DIR/ca.pem
     Settings -> Privacy & Security -> Certificates -> View Certificates ->
     Authorities -> Import -> select ca.pem -> "Trust this CA to identify websites".
  2. client certificate: your CERN user certificate (already in Firefox), or the
     generated test one: $CERT_DIR/client.p12 (password: $CLIENT_P12_PASSWORD)
     imported under "Your Certificates".
  3. verify clients against: ${CA_FILE:-$CA_PATH}
  4. start the UI (ndmspc-ui) and set in .env.local:
         VITE_X509_PROBE_URL="https://localhost:$PORT/api/state"
     then restart "npm run dev" and use "Sign in with X.509 certificate".
  (CORS allowed origin: $CORS)

EOF

SERVER_CERT="$CERT_DIR/server.pem" \
SERVER_KEY="$CERT_DIR/server.key" \
CA_PATH="$CA_PATH" \
CA_FILE="$CA_FILE" \
CORS="$CORS" \
PORT="$PORT" \
INTERNAL_PORT="$INTERNAL_PORT" \
  "$SCRIPT_DIR/run-server.sh"
