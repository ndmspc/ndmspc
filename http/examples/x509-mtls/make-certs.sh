#!/usr/bin/env bash
# Generate a throwaway certificate authority and certificates for the X509 (mutual
# TLS) example, so the flow can be exercised from a browser without grid credentials.
#
#   ca.pem / ca.key        test CA - import ca.pem into the browser as a trusted authority
#   server.pem / server.key  TLS server certificate (serverAuth EKU, SAN localhost + 127.0.0.1)
#   client.pem / client.key  optional test client certificate (clientAuth EKU)
#   client.p12             the client certificate as PKCS#12, for importing into a browser
#
# Everything lands in $CERT_DIR (.certs by default, self-ignored by git). Re-run after
# changing the SAN with FORCE=1.
set -euo pipefail

# `cd` writes the resolved directory to stdout when CDPATH is set and the path is
# relative (CDPATH commonly ends up containing "."), which would be captured here -
# so redirect it.
SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" >/dev/null && pwd -P)"

CERT_DIR="${CERT_DIR:-$SCRIPT_DIR/.certs}"
DAYS="${DAYS:-365}"
SERVER_SAN="${SERVER_SAN:-localhost}"
SERVER_IP_SAN="${SERVER_IP_SAN:-127.0.0.1}"
SERVER_CN="${SERVER_CN:-ndmspc-x509-test}"
CLIENT_CN="${CLIENT_CN:-x509-test-client}"
CLIENT_P12_PASSWORD="${CLIENT_P12_PASSWORD:-ndmspc}"
FORCE="${FORCE:-0}"
KEY_BITS="${KEY_BITS:-2048}"

die() { echo "error: $*" >&2; exit 1; }

command -v openssl >/dev/null || die "openssl not found"

mkdir -p "$CERT_DIR"
umask 077
# Self-ignoring: generated keys must never be committed from here.
printf '*\n!.gitignore\n' > "$CERT_DIR/.gitignore"

if [ "$FORCE" != "1" ] && [ -s "$CERT_DIR/server.pem" ] && [ -s "$CERT_DIR/client.p12" ]; then
  echo "certificates already present in $CERT_DIR (FORCE=1 to regenerate)"
  exit 0
fi

rm -f "$CERT_DIR"/{ca,server,client}.{key,pem,csr,srl} "$CERT_DIR"/{server,client}.ext "$CERT_DIR/client.p12"

# --- CA ---------------------------------------------------------------------
openssl genpkey -algorithm RSA -pkeyopt "rsa_keygen_bits:$KEY_BITS" -out "$CERT_DIR/ca.key" 2>/dev/null
openssl req -x509 -new -key "$CERT_DIR/ca.key" -sha256 -days "$DAYS" \
  -subj "/CN=NDMSPC X509 Test CA" -out "$CERT_DIR/ca.pem"

# --- server certificate -----------------------------------------------------
# serverAuth EKU plus a SAN: a browser refuses a server certificate without them.
cat > "$CERT_DIR/server.ext" <<EOF
basicConstraints=CA:FALSE
keyUsage=digitalSignature,keyEncipherment
extendedKeyUsage=serverAuth
subjectAltName=DNS:$SERVER_SAN,IP:$SERVER_IP_SAN
EOF
openssl genpkey -algorithm RSA -pkeyopt "rsa_keygen_bits:$KEY_BITS" -out "$CERT_DIR/server.key" 2>/dev/null
openssl req -new -key "$CERT_DIR/server.key" -subj "/CN=$SERVER_CN" -out "$CERT_DIR/server.csr"
openssl x509 -req -in "$CERT_DIR/server.csr" -CA "$CERT_DIR/ca.pem" -CAkey "$CERT_DIR/ca.key" \
  -CAcreateserial -days "$DAYS" -sha256 -extfile "$CERT_DIR/server.ext" -out "$CERT_DIR/server.pem"

# --- client certificate -----------------------------------------------------
cat > "$CERT_DIR/client.ext" <<EOF
basicConstraints=CA:FALSE
keyUsage=digitalSignature
extendedKeyUsage=clientAuth
EOF
openssl genpkey -algorithm RSA -pkeyopt "rsa_keygen_bits:$KEY_BITS" -out "$CERT_DIR/client.key" 2>/dev/null
openssl req -new -key "$CERT_DIR/client.key" -subj "/CN=$CLIENT_CN" -out "$CERT_DIR/client.csr"
openssl x509 -req -in "$CERT_DIR/client.csr" -CA "$CERT_DIR/ca.pem" -CAkey "$CERT_DIR/ca.key" \
  -CAcreateserial -days "$DAYS" -sha256 -extfile "$CERT_DIR/client.ext" -out "$CERT_DIR/client.pem"
openssl pkcs12 -export -inkey "$CERT_DIR/client.key" -in "$CERT_DIR/client.pem" \
  -name "$CLIENT_CN" -passout "pass:$CLIENT_P12_PASSWORD" -out "$CERT_DIR/client.p12"

chmod 600 "$CERT_DIR"/*.key "$CERT_DIR/client.p12"

echo "Generated certificates in $CERT_DIR"
echo "  CA (import as an authority) : $CERT_DIR/ca.pem"
echo "  server certificate         : $CERT_DIR/server.pem (SAN $SERVER_SAN/$SERVER_IP_SAN)"
echo "  optional test client       : $CERT_DIR/client.p12 (password: $CLIENT_P12_PASSWORD)"
