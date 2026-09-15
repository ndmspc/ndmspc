# NDMSPC HTTP server

The HTTP server supports optional authentication for both WebSocket connections and plain HTTP API requests. Two mutually exclusive mechanisms are available: **Keycloak/OpenID Connect** bearer-token authentication and **X509 client-certificate (mutual TLS)** authentication (see the end of this document). Authentication is disabled when neither is configured, preserving anonymous behavior.

When authentication is enabled, each WebSocket connection must send a Keycloak access token in its first message before it can receive heartbeats, appear in the client list, relay messages, or call the HTTP API through the WebSocket bridge. Plain HTTP `/api/*` requests must present the token in an `Authorization: Bearer` header (see below).

## Local Keycloak setup

Start a development Keycloak instance on port `8081`:

```bash
podman run --rm --name keycloak \
  -p 8081:8080 \
  -e KC_BOOTSTRAP_ADMIN_USERNAME=admin \
  -e KC_BOOTSTRAP_ADMIN_PASSWORD=admin \
  quay.io/keycloak/keycloak:26.7.3 start-dev
```

Docker can be used instead by replacing `podman` with `docker`.

Open <http://localhost:8081/admin> and sign in with `admin` / `admin`, then create the following configuration.

### Realm

Create a realm named `ndmspc`.

The resulting issuer URL is:

```text
http://localhost:8081/realms/ndmspc
```

Its discovery document should be available at:

```text
http://localhost:8081/realms/ndmspc/.well-known/openid-configuration
```

### Client

Create an OpenID Connect client with these settings:

| Setting | Value |
|---|---|
| Client ID | `ndmspc-ui` |
| Client authentication | Off |
| Standard flow | On |
| Valid redirect URIs | `http://localhost:8080/*` |
| Web origins | `http://localhost:8080` |

Adjust the redirect URI and web origin when NDMSPC runs on a different address.

### Access-token audience

NDMSPC validates the JWT `aud` claim and does not treat `azp` as a substitute. Configure an audience mapper so access tokens contain `ndmspc` in `aud`:

1. Open the `ndmspc-ui` client.
2. Open **Client scopes** and select its dedicated client scope.
3. Open **Mappers** and add an **Audience** mapper.
4. Set **Included Custom Audience** to `ndmspc`.
5. Enable **Add to access token**.

A valid access token must contain either:

```json
{"aud":"ndmspc"}
```

or:

```json
{"aud":["ndmspc"]}
```

### Test user

Create a user in the `ndmspc` realm and assign a non-temporary password under **Credentials**. Role enforcement is not currently enabled; the server verifies token identity, issuer, audience, signature, and validity times.

## NDMSPC server configuration

For local development, configure the server with environment variables:

```bash
export NDMSPC_OIDC_ISSUER=http://localhost:8081/realms/ndmspc
export NDMSPC_OIDC_AUDIENCE=ndmspc
export NDMSPC_OIDC_ALLOW_INSECURE_HTTP=true

ndmspc-server start ngnt
```

Plain HTTP is rejected unless `NDMSPC_OIDC_ALLOW_INSECURE_HTTP` is enabled. Do not enable it outside local development; use HTTPS and optionally configure `NDMSPC_OIDC_CA_FILE` (a CA bundle) or `NDMSPC_OIDC_CA_PATH` (a directory of hashed CA certificates) for a private certificate authority.

The equivalent command-line configuration is:

```bash
ndmspc-server start ngnt \
  --oidc-issuer http://localhost:8081/realms/ndmspc \
  --oidc-audience ndmspc \
  --oidc-allow-insecure-http
```

Available settings:

| Command-line option | Environment variable | Default |
|---|---|---:|
| `--oidc-issuer` | `NDMSPC_OIDC_ISSUER` | Authentication disabled |
| `--oidc-audience` | `NDMSPC_OIDC_AUDIENCE` | Empty |
| `--oidc-ca-file` | `NDMSPC_OIDC_CA_FILE` | System trust store |
| `--oidc-ca-path` | `NDMSPC_OIDC_CA_PATH` | System trust store |
| `--oidc-clock-skew` | `NDMSPC_OIDC_CLOCK_SKEW_SECONDS` | `30` seconds |
| `--oidc-jwks-refresh` | `NDMSPC_OIDC_JWKS_REFRESH_SECONDS` | `300` seconds |
| `--oidc-jwks-max-stale` | `NDMSPC_OIDC_JWKS_MAX_STALE_SECONDS` | `86400` seconds |
| `--oidc-auth-timeout` | `NDMSPC_OIDC_AUTH_TIMEOUT_SECONDS` | `15` seconds |
| `--oidc-http-timeout` | `NDMSPC_OIDC_HTTP_TIMEOUT_MS` | `5000` milliseconds |
| `--oidc-allow-insecure-http` | `NDMSPC_OIDC_ALLOW_INSECURE_HTTP` | `false` |

If either issuer or audience is configured, both are required. The server performs OIDC discovery and loads the initial JWKS during startup. Invalid configuration or an unavailable provider causes startup to fail rather than falling back to anonymous access.

## WebSocket authentication protocol

Connect to the normal ROOT WebSocket endpoint:

```text
ws://localhost:8080/ws/root.websocket
```

Immediately after the socket opens, send this as the first frame:

```json
{
  "event": "authenticate",
  "token": "<keycloak-access-token>"
}
```

After successful verification, the server sends an authentication acknowledgement followed by the existing welcome and client-list messages:

```json
{
  "event": "authenticated",
  "payload": {
    "username": "alice",
    "expiresAt": 1730000000000
  }
}
```

The username comes from `preferred_username`, falling back to the JWT `sub` claim.

A client may send another `authenticate` message after refreshing its Keycloak token. The refreshed token must have the same `sub`; changing users requires a new WebSocket connection. Expired sessions are removed and closed.

Authentication failures use this shape:

```json
{
  "event": "authentication_error",
  "payload": {
    "code": "invalid_token",
    "message": "Authentication failed",
    "retryable": false
  }
}
```

Client-supplied `Authorization` headers inside API-over-WebSocket messages are removed and are never trusted as the connection identity.

## HTTP API authentication (Bearer)

When OIDC authentication is enabled, the plain HTTP API (`/api/<group>/<action>`) also requires a Keycloak access token. Unlike the WebSocket flow, the token is supplied with each request via the standard `Authorization` header:

```bash
curl -i \
  -H "Authorization: Bearer <keycloak-access-token>" \
  http://localhost:8080/api/state
```

Requests without a valid token are rejected with an error JSON body and a `WWW-Authenticate: Bearer` response header. The error body carries a stable code (same codes as the WebSocket flow) plus a diagnostic message:

```json
{
  "error": {
    "code": "token_expired",
    "message": "JWT is expired",
    "retryable": false
  }
}
```

Possible codes: `authentication_required` (missing header), `invalid_authorization_header` (not a `Bearer` token), `invalid_token`, `token_expired`, `invalid_issuer`, `invalid_audience`, `unsupported_algorithm`, `unknown_key`, and `provider_unavailable` (`retryable: true`).

### HTTP status codes

The embedded ROOT HTTP server (`THttpServer`) cannot attach a custom body to an error status: its only error marker (`Set404`) produces an empty `404 Not Found` reply. Rejected `/api/*` requests therefore arrive with a `200 OK` transport status, but the JSON `error.code` field (plus a `WWW-Authenticate: Bearer` response header) unambiguously signals the rejection, and responses are marked `Cache-Control: no-store`. A standalone (non-ROOT) HTTP server can use the same `NOidcHttpAuthenticator::Authenticate()` component to return proper `401 Unauthorized` (or `503 Service Unavailable` when `retryable` is true) responses.

### Exempt endpoints

The following requests remain anonymous so clients can bootstrap and render the inspector UI without a token:

| Endpoint | Purpose |
|---|---|
| `GET /api/` | Root info. Reports `state.authentication.enabled` so the UI can detect that authentication is required. |
| `/api/openapi/inspector` and `/api/inspector/openapi` | JSON-schema for the inspector. |

WebSocket endpoints and static assets are unaffected: WebSocket connections use the `authenticate` first-frame protocol, and files under the configured asset locations are served as before.

### Verified identity on responses

Successful requests carry the verified identity in response headers:

| Header | Value |
|---|---|
| `X-NDMSPC-User` | `preferred_username` (falls back to `sub`) |
| `X-NDMSPC-Subject` | JWT `sub` claim |
| `X-NDMSPC-Token-Expires` | Token expiry as Unix seconds |
| `X-NDMSPC-Authenticated` | JWT `sub` claim |

Client-supplied `X-NDMSPC-*` headers are never trusted: they are stripped from API-over-WebSocket messages and ignored by the bearer check.

### Reuse in a standalone HTTP server

The bearer logic is transport-neutral. A non-ROOT server can call `Ndmspc::NOidcHttpAuthenticator::Authenticate(verifier, authorizationHeader)` directly and map the result to HTTP status codes (`401`, or `503` for `provider_unavailable`). The `verifier` is obtained from `NHttpServer::GetOidcVerifier()` when the server is constructed with OIDC configuration.

## UI configuration

The NDMSPC UI uses these build-time settings:

```bash
VITE_KEYCLOAK_URL=http://localhost:8081
VITE_KEYCLOAK_REALM=ndmspc
VITE_KEYCLOAK_CLIENT=ndmspc-ui
```

The UI WebSocket integration must send the current Keycloak access token as the first frame, wait for the `authenticated` event before sending normal requests, and re-authenticate after refreshing the token.

## X509 client-certificate (mutual TLS) authentication

X509 client-certificate authentication is a **mutually exclusive alternative** to OIDC. It is
selected by configuring the X509 environment variables / command-line options instead of the
OIDC ones; enabling both is an error.

The authentication is performed with **mutual TLS (mTLS)** at the TLS handshake, so a client
must present a certificate signed by a trusted CA to connect at all. The authenticated username
is taken from the certificate's **subject DN** — the Common Name (`CN`) by default, or the full
distinguished name with `--x509-identity dn`.

### How it works

The server is fronted by an `httplib`-based TLS listener that:

1. Requires and verifies a client certificate against the configured CA bundle, rejecting
   connections that present no certificate or one signed by an untrusted CA.
2. Extracts the Common Name (or full DN) of the verified certificate as the caller identity.
3. Forwards HTTP `/api/*` requests to the internal ROOT HTTP engine, injecting the identity via
   the `X-NDMSPC-User` and `X-NDMSPC-Subject` headers.
4. For WebSocket connections, terminates `wss://` at the front door, bridges the connection to
   the internal ROOT WebSocket engine, and rewrites `username` in the server messages to the
   certificate identity. Clients **must not** send the OIDC-style first-frame `authenticate`
   message — the mTLS handshake already authenticated them.

The internal ROOT engine binds to loopback only and runs with OIDC disabled; the front door is
the sole externally reachable endpoint and the sole authentication gate.

### Enabling X509 authentication

```bash
export NDMSPC_X509_CERT=/path/to/server.pem
export NDMSPC_X509_KEY=/path/to/server.key
export NDMSPC_X509_CA_FILE=/path/to/ca.pem

ndmspc-server start ngnt
```

The equivalent command-line configuration is:

```bash
ndmspc-server start ngnt \
  -p 8443 \
  --x509-cert /path/to/server.pem \
  --x509-key /path/to/server.key \
  --x509-ca-file /path/to/ca.pem
```

Client certificates may also be verified against a **directory of hashed CA certificates**
(the OpenSSL `c_rehash` layout used by grid middleware) instead of a single bundle. Point
`--x509-ca-path` / `NDMSPC_X509_CA_PATH` at the directory, for example the CERN/ALICE grid
trust anchors:

```bash
ndmspc-server start ngnt \
  -p 8443 \
  --x509-cert /path/to/server.pem \
  --x509-key /path/to/server.key \
  --x509-ca-path /cvmfs/alice.cern.ch/etc/grid-security/certificates
```

The CA file and CA path may be combined; client certificates are then accepted when they
verify against either location.

Available settings:

| Command-line option | Environment variable | Default |
|---|---|---:|
| `--x509-cert` | `NDMSPC_X509_CERT` | Empty (disabled) |
| `--x509-key` | `NDMSPC_X509_KEY` | Empty |
| `--x509-ca-file` | `NDMSPC_X509_CA_FILE` | Empty (required unless `--x509-ca-path` is set) |
| `--x509-ca-path` | `NDMSPC_X509_CA_PATH` | Empty (required unless `--x509-ca-file` is set) |
| `--x509-verify-optional` | `NDMSPC_X509_VERIFY_OPTIONAL` | `false` (client cert required) |
| `--x509-identity` | `NDMSPC_X509_IDENTITY` | `cn` |
| `--x509-internal-port` | `NDMSPC_X509_INTERNAL_PORT` | `8081` |

When a CA file or CA path is set, client certificates are **required** by default. Set
`--x509-verify-optional` to accept clients that present no certificate (a presented
certificate is still verified). At least one CA location is required when certificates are
mandatory.

Only the `start ngnt` subcommand supports X509 mode in this release; `default` and `stress`
reject X509 configuration.

### Creating a test certificate authority and certificates

```bash
# CA
openssl genpkey -algorithm RSA -pkeyopt rsa_keygen_bits:2048 -out ca.key
openssl req -x509 -new -key ca.key -sha256 -days 365 -subj "/CN=NDMSPC Test CA" -out ca.pem

# Server certificate (SAN must include the host you connect to)
openssl genpkey -algorithm RSA -pkeyopt rsa_keygen_bits:2048 -out server.key
openssl req -new -key server.key -subj "/CN=ndmspc-server" -out server.csr
printf "subjectAltName=IP:127.0.0.1\n" > san.cnf
openssl x509 -req -in server.csr -CA ca.pem -CAkey ca.key -CAcreateserial -days 365 -sha256 \
  -extfile san.cnf -out server.pem

# Client certificate (CN is used as the username)
openssl genpkey -algorithm RSA -pkeyopt rsa_keygen_bits:2048 -out client.key
openssl req -new -key client.key -subj "/CN=alice" -out client.csr
printf "basicConstraints=CA:FALSE\nextendedKeyUsage=clientAuth\n" > client.cnf
openssl x509 -req -in client.csr -CA ca.pem -CAkey ca.key -CAcreateserial -days 365 -sha256 \
  -extfile client.cnf -out client.pem
```

### Accessing the server with a client certificate

```bash
curl --cert client.pem --key client.key --cacert ca.pem \
     https://localhost:8443/api/state
```

Successful requests carry the certificate identity:

| Header | Value |
|---|---|
| `X-NDMSPC-User` | Certificate Common Name (or full DN with `--x509-identity dn`) |
| `X-NDMSPC-Subject` | Same value |

Requesting without `--cert` (or with a certificate not signed by `ca.pem`) fails at the TLS
handshake and returns no HTTP response.

For WebSocket, connect to `wss://localhost:8443/ws/root.websocket` presenting `client.pem` /
`client.key`. Do **not** send an `authenticate` frame; the server authenticates from the
certificate and sends the welcome / client-list messages with `payload.username` set to the
certificate identity.

### Runnable example with a grid user certificate

The [`examples/x509-mtls/`](examples/x509-mtls/) directory contains a runnable server +
WebSocket client example that uses a `~/.globus` grid user certificate and the ALICE grid
CA path (`/cvmfs/alice.cern.ch/etc/grid-security/certificates`) to verify clients:

```bash
./http/examples/x509-mtls/run-demo.sh
# or, separately:
./http/examples/x509-mtls/run-server.sh
./http/examples/x509-mtls/run-client.sh
```

Because the grid *user* certificate is valid only for client authentication (no
`serverAuth` EKU, no SAN), the example uses it as the server certificate and the client
disables server verification with `--allow-insecure`; the server still enforces and
verifies the client certificate. See the example's `README.md` for details.
# Create password file

```bash
mkdir ~/.globus
read -s -r -p "Enter your secret: " secret && echo -n $secret | base64 > ~/.globus/password.txt
chmod 400 ~/.globus/password.txt
```

# ndmspc-ws-client

A WebSocket client for connecting to NDMSPC (and compatible) servers. It supports plain `ws://`
and TLS `wss://` connections, and can authenticate in two mutually exclusive ways:

- **X509 client certificates (mutual TLS)** — presents a client certificate to the server,
  which must verify it against its configured CA. See `--cert`, `--key`, `--ca-file`,
  `--ca-path`.
- **OAuth2/Keycloak (OIDC)** — obtains an access token from a Keycloak token endpoint and sends
  it as the server's first-frame WebSocket `authenticate` message, then waits for the
  `authenticated` acknowledgement.

### Private-key passphrase

An encrypted `--key` needs a passphrase. The client resolves it in this order:

1. `--key-pass <pass>` (or the `NDMSPC_KEY_PASS` environment variable);
2. `--key-pass-file <file>` (or `NDMSPC_KEY_PASS_FILE`) — a base64-encoded passphrase file,
   the same convention as `~/.globus/password.txt`;
3. an **interactive prompt** (terminal echo disabled) when the key is detected as encrypted
   and stdin is a terminal.

If the key is encrypted and none of the above is available, the client fails with an
actionable error instead of blocking, so scripts and services never hang waiting for input.
Passing the passphrase on the command line exposes it to other local users via the process
list; prefer `--key-pass-file`, `NDMSPC_KEY_PASS`, or the interactive prompt.

The OIDC flow supports the `client_credentials` (default) and `password` grants against
`{issuer}/protocol/openid-connect/token`.

```bash
# Keycloak (OIDC) client credentials grant
ndmspc-ws-client \
  --url wss://localhost:8080/ws/root.websocket \
  --oidc-issuer http://localhost:8081/realms/ndmspc \
  --oidc-client-id ndmspc-ui \
  --oidc-client-secret <secret> \
  --oidc-allow-insecure-http \
  --timeout 5

# Keycloak (OIDC) password grant
ndmspc-ws-client \
  --url wss://localhost:8080/ws/root.websocket \
  --oidc-issuer http://localhost:8081/realms/ndmspc \
  --oidc-client-id ndmspc-ui \
  --oidc-grant password \
  --oidc-username alice \
  --oidc-password <password> \
  --oidc-allow-insecure-http \
  --timeout 5

# X509 client certificate (mutual TLS)
ndmspc-ws-client \
  --url wss://localhost:8443/ws/root.websocket \
  --cert client.pem --key client.key \
  --ca-file ca.pem \
  --timeout 5
```

Use `--ca-path <dir>` instead of `--ca-file` to verify the server certificate against a
directory of hashed CA certificates (for example
`/cvmfs/alice.cern.ch/etc/grid-security/certificates`). When both are given, the CA file
takes precedence and the CA directory is ignored.

For a self-signed test server, combine the OIDC options with `--allow-insecure` (or point
`--ca-file` / `--ca-path` at the server's CA) so the TLS layer trusts the server certificate.

The client is built on cpp-httplib, whose TLS layer offers a single chain-verification
switch. `--allow-self-signed` therefore behaves like `--allow-insecure` (chain verification
is disabled); it cannot accept a self-signed CA while still verifying the rest of the chain.
`--skip-hostname-check` remains a separate switch.

## MCP server

The registered ngnt actions are also exposed through the **Model Context Protocol** (MCP),
so an LLM agent can drive the same server session as the browser UI. Two transports are
available:

- **Streamable HTTP** — in-process endpoint on the running server at `POST /api/mcp`. It
  shares the live session (opened `NGnTree`, navigator, workspace and state point) and
  honours the same authentication as every other `/api/*` route.
- **stdio** — the `ndmspc-mcp` launcher, a self-contained process an MCP client spawns.
  It embeds the server machinery (no network listener), loads the built-in macros and
  serves JSON-RPC 2.0 on stdin/stdout.

Supported JSON-RPC methods: `initialize`, `notifications/initialized`, `tools/list`,
`tools/call`, `ping`.

### Tools

One tool is created per registered handler, named by replacing `/` with `_`
(`ngnt/open` → `ngnt_open`). Each tool's `inputSchema` is taken from the workspace
inspector schema and extended with a `method` property (`GET`/`POST`/`PATCH`/`DELETE`,
default `POST`), because the ngnt actions are verb-sensitive. Internal routes (`debug`,
`openapi/inspector`, `inspector/openapi`) are hidden; `health` and `state` are exposed.

Every `tools/call` is routed through the normal request path (the same dispatch used by
the HTTP API and the WebSocket bridge), so history entries, workspace updates and
WebSocket broadcasts are identical to those produced by a UI client.

### Annotating tools from a macro

Descriptions and other MCP metadata live in the **handler macro**, not in C++. Register
them next to the handler, keyed by the same action name used in the handler map:

```cpp
#include <ndmspc/http/NGnHttpServer.h>

void httpMyCustom()
{
  auto & handlers = *(Ndmspc::gNdmspcHttpHandlers);

  Ndmspc::RegisterMcpTool("myplugin/summary", "Return a summary of the current state.");
  // or with more control:
  Ndmspc::RegisterMcpTool("myplugin/summary", {
      .description = "Return a summary of the current state.",
      .title       = "Summary",
      .methods     = {"GET"},
      .hidden      = false,
      .inputSchema = {{"properties", {{"verbose", {{"type", "boolean"}}}}}},
  });

  handlers["myplugin/summary"] = [](std::string method, json & in, json & out, json & wsOut,
                        std::map<std::string, TObject *> & objects) { /* ... */ };
}
```

`NMcpToolInfo` fields:

| Field | Meaning |
|---|---|
| `description` | Tool description shown to the model (falls back to a generic string). |
| `title` | Optional MCP `title`; defaults to the action-derived tool name. |
| `methods` | Allowed HTTP verbs; narrows the tool's `method` enum. Empty = all four. |
| `hidden` | Exclude the action from MCP entirely (neither listed nor callable). |
| `inputSchema` | Extra JSON-Schema properties merged on top of the auto-derived schema. |

Changing a description requires only editing the macro and reloading — no recompilation of
the server. Actions with no registered metadata keep the generic description, and
`debug`/`openapi/inspector`/`inspector/openapi` stay excluded by default.

### HTTP transport

The endpoint is **on by default** and can be disabled with `--mcp false`:

```bash
ndmspc-server start ngnt -p 8080                # MCP endpoint enabled (default)
ndmspc-server start ngnt --mcp false -p 8080    # or: NDMSPC_MCP=0 ndmspc-server start ngnt

curl -s localhost:8080/api/mcp -H 'Content-Type: application/json' \
  -d '{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-06-18"}}'
curl -s localhost:8080/api/mcp -H 'Content-Type: application/json' \
  -d '{"jsonrpc":"2.0","id":2,"method":"tools/list","params":{}}'
curl -s localhost:8080/api/mcp -H 'Content-Type: application/json' \
  -d '{"jsonrpc":"2.0","id":3,"method":"tools/call","params":{"name":"ngnt_open","arguments":{"method":"POST","file":"test.root"}}}'
```

With `--mcp false` (or `NDMSPC_MCP=0`), requests to `/api/mcp` return
`{"error": "MCP endpoint is disabled"}` and the rest of the API is unaffected. The switch
is also available programmatically via `NGnHttpServer::SetMcpEnabled(false)`. The stdio
launcher `ndmspc-mcp` is unaffected — it is an MCP server by definition.

Responses are plain `application/json` (the spec also permits an SSE stream; this server
returns JSON). `initialize` responses carry an `Mcp-Session-Id` header. When OIDC is
enabled, include the bearer token (`Authorization: Bearer ...`), like any other `/api/*`
call — the endpoint is not exempt from authentication.

### stdio transport

```bash
ndmspc-mcp                                                  # uses $NDMSPC_DIR/macros/builtin/httpNgntBase.C,httpNgnt.C
ndmspc-mcp --rooms                                         # additionally load the room router macro (httpRoom.C)
ndmspc-mcp -m /path/httpNgntBase.C,/path/httpNgnt.C
ndmspc-mcp --all-tools                                      # also expose debug/openapi actions
```

Only JSON-RPC messages are written to stdout; the logger and any `Print()` output from the
macros are redirected to stderr. Point an MCP client at the binary:

```json
{ "mcpServers": { "ndmspc-ngnt": { "command": "/path/to/bin/ndmspc-mcp" } } }
```

See [`examples/mcp`](examples/mcp) for a runnable example covering both transports.
