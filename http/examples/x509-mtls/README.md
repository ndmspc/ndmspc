# X509 (mutual TLS) server + WebSocket client example

Runnable example that starts the NDMSPC `ngnt` server with X509 client-certificate
(mutual TLS) authentication and connects the `ndmspc-ws-client` over `wss://` using a
**grid user certificate** from `~/.globus`, verifying the client against the ALICE grid
trust anchors in `/cvmfs/alice.cern.ch/etc/grid-security/certificates`.

The authenticated identity is taken from the certificate's subject DN (Common Name by
default), so a `CN=mvala, ...` certificate is presented to the server as user `mvala`
(visible in the WebSocket `welcome`/`clients`/`heartbeat` messages and in the
`X-NDMSPC-User` HTTP response header).

## Files

| File | Purpose |
|---|---|
| `run-server.sh` | Starts `ndmspc-server` with the grid certificate as the server certificate and the grid CA path for client verification. |
| `run-client.sh` | Runs `ndmspc-ws-client` over `wss://`, presenting the grid certificate. |
| `run-demo.sh` | End-to-end check: starts the server, runs an authenticated client (must succeed) and an anonymous client (must be rejected), then stops the server. |

## Prerequisites

```bash
# Build the server and client first (from the repository root).
./scripts/make.sh install

# Grid credentials and trust anchors.
ls ~/.globus/usercert.pem ~/.globus/userkey.pem
ls /cvmfs/alice.cern.ch/etc/grid-security/certificates
```

The private key is normally encrypted. `run-client.sh` resolves its passphrase from
`GLOBUS_PASSWORD` (plain text), then from the base64-encoded `~/.globus/password.txt` (see
the note at the end of `../../README.md`), and otherwise lets `ndmspc-ws-client` **prompt
for it interactively** with terminal echo disabled. To always be prompted, unset both
sources (e.g. `GLOBUS_PASSWORD_FILE=/nonexistent ./run-client.sh`). The `httplib` TLS
server has no passphrase callback, so `run-server.sh` always decrypts the key into a
private temporary file and removes it on exit; it does not prompt.

## Quick start

```bash
./run-demo.sh
```

If the default ports are already in use, override them, for example
`PORT=9443 INTERNAL_PORT=9081 ./run-demo.sh` (the server reports an actionable bind error
otherwise).

Expected result:

- the authenticated client prints `welcome`/`clients` (and later `heartbeat`) frames with
  `"username":"<your CN>"` and exits `0`;
- the anonymous client (no certificate) is rejected at the TLS handshake and exits non-zero;
- the script prints `RESULT: PASS`.

## Running the parts separately

```bash
# Terminal 1: start the server (foreground, Ctrl-C to stop)
./run-server.sh

# Terminal 2: authenticated WebSocket client
./run-client.sh

# Terminal 2 (optional): HTTP API with the same certificate
curl -sk --cert ~/.globus/usercert.pem --key <decrypted-key> \
  https://localhost:8443/api/state -D - -o /dev/null
```

## Configuration

All scripts honour these environment variables:

| Variable | Default | Meaning |
|---|---|---|
| `GLOBUS_DIR` | `~/.globus` | Directory holding the grid credentials. |
| `GLOBUS_CERT` | `$GLOBUS_DIR/usercert.pem` | Server/client certificate. |
| `GLOBUS_KEY` | `$GLOBUS_DIR/userkey.pem` | Certificate private key (may be encrypted). |
| `GLOBUS_PASSWORD` | *(unset)* | Plain passphrase; overrides `GLOBUS_PASSWORD_FILE`. |
| `GLOBUS_PASSWORD_FILE` | `$GLOBUS_DIR/password.txt` | Base64-encoded passphrase; when absent the client prompts. |
| `GRID_CA_PATH` | `/cvmfs/alice.cern.ch/etc/grid-security/certificates` | Hashed CA directory used to verify client certificates. |
| `PORT` | `8444` | Public TLS port of the front door. |
| `INTERNAL_PORT` | `8081` | Loopback port of the internal ROOT engine. |
| `URL` | `wss://localhost:$PORT/ws/root.websocket` | WebSocket endpoint for the client. |
| `TIMEOUT` | `10` | Client run time in seconds. |
| `WITH_CERT` | `1` | Set to `0` to connect without a client certificate (negative test). |

`run-client.sh` also honours the client's own passphrase variables when the `GLOBUS_*`
sources are unset: `NDMSPC_KEY_PASS` (plain text, used when `GLOBUS_PASSWORD` is unset) and
`NDMSPC_KEY_PASS_FILE` (base64-encoded).

## Why the client uses `--allow-insecure`

A grid **user** certificate is issued for client authentication only: its Extended Key
Usage is `TLS Web Client Authentication` and it carries no `serverAuth` EKU and no IP
SAN. When the example reuses it as the *server* certificate, a verifying client rejects
it ("SSL server verification failed"), because the certificate is not valid for the TLS
server purpose. The client therefore disables server-certificate verification
(`--allow-insecure`).

This does **not** weaken the mTLS enforcement being demonstrated: the server still
requires and verifies a client certificate against the grid CA path, and derives the
identity from it. The durable setup is a proper server certificate (serverAuth EKU plus a
SAN for the host you connect to) signed by a locally trusted CA; the server certificate
is orthogonal to the client-certificate authentication shown here.

## Note on the WebSocket bridge

The X509 front door terminates `wss://` and bridges the connection to the internal ROOT
engine. `httplib`'s WebSocket client only accepts `ws://`/`wss://` URLs, so the bridge
rewrites the internal `http://` base to `ws://`; without that rewrite the upstream
connection fails and the front door closes the client with code `1011` ("backend
unavailable"). `run-demo.sh` exercises this path end-to-end.
