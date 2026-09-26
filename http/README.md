# NDMSPC HTTP server

The HTTP server supports optional authentication for both WebSocket connections and plain HTTP API requests. Two mutually exclusive mechanisms are available: **Keycloak/OpenID Connect** bearer-token authentication and **X509 client-certificate (mutual TLS)** authentication (see the end of this document). Authentication is disabled when neither is configured, preserving anonymous behavior.

When authentication is enabled, each WebSocket connection must send a Keycloak access token in its first message before it can receive heartbeats, appear in the client list, relay messages, or call the HTTP API through the WebSocket bridge. Plain HTTP `/api/*` requests must present the token in an `Authorization: Bearer` header (see below).

## Layout

The module is organized by feature - the sources of a feature live in their own directory:

| Directory | What lives there |
| --------- | ---------------- |
| `auth/` | OIDC (Keycloak) and X509 client-certificate authentication, token clients, passphrase input |
| `room/` | the room router and its client: `NRoomRouter`, `NRoomClient`, `NRoomSession` |
| `mcp/` | the MCP endpoint |
| `server/` | the HTTP/WebSocket layer: `NHttpServer` (engine, workspace, handler map, MCP, rooms), requests, handlers, clients, and the ngnt server pieces (`NWorkspace`, `NHistoryEntry`, `NRouteContext`, `NSchemaBuilder`) |
| `cli/`, `tui/`, `examples/` | the executables, the room TUI and runnable examples |

**Includes do not follow the directories.** Every header still installs flat into
`include/ndmspc/http/`, so a source living in `http/room/` is included exactly as it always was:

```cpp
#include <ndmspc/http/NRoomRouter.h>
```

That is deliberate: user macros and out-of-tree code include the installed paths, and those must not
move when the sources are reorganized. `http/CMakeLists.txt` globs the feature directories and lets
`RootLib` copy and install every header into the flat namespace; it additionally mirrors the feature
headers inside the build tree, because the dictionary generator is handed each header as
`ndmspc/http/<feature>/<Header>.h` and resolves that under `build/include`. Neither of those copies is
installed twice or visible to a consumer.

Contrast `ai/llm` and `ai/agents`: each has its own `RootLib` package and therefore *nested* include
paths (`ndmspc/ai/llm/NLlmChatClient.h`). Directory structure changes the include path there; here it
does not.

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

ndmspc-server
```

Plain HTTP is rejected unless `NDMSPC_OIDC_ALLOW_INSECURE_HTTP` is enabled. Do not enable it outside local development; use HTTPS and optionally configure `NDMSPC_OIDC_CA_FILE` (a CA bundle) or `NDMSPC_OIDC_CA_PATH` (a directory of hashed CA certificates) for a private certificate authority.

The equivalent command-line configuration is:

```bash
ndmspc-server \
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

The websocket is **on by default** and can be turned off with `--ws false` (`NDMSPC_WS=0`), which
leaves `/ws/root.websocket` unserved — see the room router section for why that is worth doing where
clients reach a room instead.

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

The following requests do not need a user token, so clients can bootstrap and render the inspector
UI, and a room can talk to the router:

| Endpoint | Purpose |
|---|---|
| `GET /api/` | Root info. Reports `state.authentication.enabled` so the UI can detect that authentication is required. |
| `/api/openapi/inspector` and `/api/inspector/openapi` | JSON-schema for the inspector. |
| `POST /api/room/state` | Internal: a room reports its session here and fetches it back when it wakes. Not anonymous, though - a room has no user token to present, so it sends the access token it was created with in `?token=`, and the room router checks it against the room the request names (see [Room session restore](#room-session-restore)). |

WebSocket endpoints and static assets are unaffected: WebSocket connections use the `authenticate` first-frame protocol, and files under the configured asset locations are served as before.

A request the server dispatches **itself** is not checked again either. A tool call (`POST /api/mcp`) runs as the caller whose own request already passed this gate, and a room replaying its stored session has no client behind it at all; both go back through the same dispatch path (`ProcessRequestAs`), which is what tells the server they are its own. Without that, every tool call and every restore would be refused for want of a token the server had just verified - the room's own gate is skipped for the same reason, since the caller's request already carried the room's token.

### Verified identity on responses

Successful requests carry the verified identity in response headers:

| Header | Value |
|---|---|
| `X-Ndmspc-User` | `preferred_username` (falls back to `sub`) |
| `X-Ndmspc-Subject` | JWT `sub` claim |
| `X-Ndmspc-Token-Expires` | Token expiry as Unix seconds |
| `X-Ndmspc-Authenticated` | JWT `sub` claim |
| `X-Ndmspc-Email` | JWT `email` claim (the header is absent when the token carries none) |

Client-supplied `X-Ndmspc-*` headers are never trusted: they are stripped from API-over-WebSocket messages and ignored by the bearer check.

The identity also travels *to* the handler, which otherwise only ever receives its method and its input: the server writes it into that input as the `_identity` key (`{user, email, subject, verified}`) — the same seam the request's query already uses as `_query`. A client-supplied `_identity` is replaced by what the server itself established, so an action can trust `verified`. What acts on it today is the [room router](#ownership-and-admins).

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
   the `X-Ndmspc-User` and `X-Ndmspc-Subject` headers.
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

ndmspc-server
```

The equivalent command-line configuration is:

```bash
ndmspc-server \
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
ndmspc-server \
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
| `--x509-cors` | `NDMSPC_X509_CORS` | Empty (CORS disabled) |

When a CA file or CA path is set, client certificates are **required** by default. Set
`--x509-verify-optional` to accept clients that present no certificate (a presented
certificate is still verified). At least one CA location is required when certificates are
mandatory.

X509 mode is supported by `ndmspc-server`.

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
| `X-Ndmspc-User` | Certificate Common Name (or full DN with `--x509-identity dn`) |
| `X-Ndmspc-Subject` | Same value |

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
  It embeds the server machinery (no network listener), registers the base actions and
  loads the macro named with `-m` (`toolNgnt.C` by default), and
  serves JSON-RPC 2.0 on stdin/stdout.

Supported JSON-RPC methods: `initialize`, `notifications/initialized`, `tools/list`,
`tools/call`, `ping`.

### Tools

One tool is created per registered handler, named by replacing `/` with `_`
(`ngnt/open` → `ngnt_open`). Each tool's `inputSchema` is taken from the workspace
inspector schema and extended with a `method` property (`GET`/`POST`/`PATCH`/`DELETE`,
default `POST`), because the ngnt actions are verb-sensitive. Internal routes
(`openapi/inspector`, `inspector/openapi`) are hidden; `health` and `state` are exposed — they come
from the server itself (`Ndmspc::RegisterBaseActions`, built in) rather than from a macro, and
`debug` is an example of a name the default filter hides for a macro that registers one.

Every `tools/call` is routed through the normal request path (the same dispatch used by
the HTTP API and the WebSocket bridge), so history entries, workspace updates and
WebSocket broadcasts are identical to those produced by a UI client.

### Annotating tools from a macro

Descriptions and other MCP metadata live in the **handler macro**, not in C++. Register
them next to the handler, keyed by the same action name used in the handler map:

```cpp
#include <ndmspc/http/NHttpServer.h>

void toolMyCustom()
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
ndmspc-server -p 8080                # MCP endpoint enabled (default)
ndmspc-server --mcp false -p 8080    # or: NDMSPC_MCP=0 ndmspc-server

curl -s localhost:8080/api/mcp -H 'Content-Type: application/json' \
  -d '{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-06-18"}}'
curl -s localhost:8080/api/mcp -H 'Content-Type: application/json' \
  -d '{"jsonrpc":"2.0","id":2,"method":"tools/list","params":{}}'
curl -s localhost:8080/api/mcp -H 'Content-Type: application/json' \
  -d '{"jsonrpc":"2.0","id":3,"method":"tools/call","params":{"name":"ngnt_open","arguments":{"method":"POST","file":"test.root"}}}'
```

With `--mcp false` (or `NDMSPC_MCP=0`), requests to `/api/mcp` return
`{"error": "MCP endpoint is disabled"}` and the rest of the API is unaffected. The switch
is also available programmatically via `NHttpServer::SetMcpEnabled(false)`. The stdio
launcher `ndmspc-mcp` is unaffected — it is an MCP server by definition.

Responses are plain `application/json` (the spec also permits an SSE stream; this server
returns JSON). `initialize` responses carry an `Mcp-Session-Id` header. When OIDC is
enabled, include the bearer token (`Authorization: Bearer ...`), like any other `/api/*`
call — the endpoint is not exempt from authentication.

### stdio transport

```bash
ndmspc-mcp                                                  # uses $NDMSPC_DIR/macros/tools/toolNgnt.C
ndmspc-mcp --rooms                                         # serve the room router instead (rooms only, no macro)
ndmspc-mcp -m /path/toolNgnt.C
ndmspc-mcp --all-tools                                      # also expose openapi actions
```

Only JSON-RPC messages are written to stdout; the logger and any `Print()` output from the
macros are redirected to stderr. Point an MCP client at the binary:

```json
{ "mcpServers": { "ndmspc-ngnt": { "command": "/path/to/bin/ndmspc-mcp" } } }
```

See [`examples/mcp`](examples/mcp) for a runnable example covering both transports.

## Room router (`NRoomRouter`)

`Ndmspc::NRoomRouter` is the framework part of the rooms feature: the always-on entry Service of an
NDMSPC deployment on Knative. `ndmspc-server --rooms true` (or `NDMSPC_ROOMS=1`)
registers it; without that flag a server is an ordinary NDMSPC server and none of this runs.

A router serves **rooms and nothing else**: it loads no macro, so the image CMD's `-m` list is
ignored and the standalone tools view has nothing to talk to on the entry. The server's own base
actions (`health`, `state`) are built in (`Ndmspc::RegisterBaseActions`) and are not registered
either, so `/api/health` on a router answers "Unsupported action" — a room is where tools live, and
the room view carries its own tool panel. The page itself is still served, and `/api/mcp` still
answers the `room_*` tools.

Rooms are Knative Services created through the in-cluster API (`KUBERNETES_SERVICE_HOST`/`PORT`),
so asking for them without that environment is refused at startup: the server logs why and exits
non-zero before it builds the server or loads any macro. Where it can serve rooms it says so, with
the namespace and room prefix it will use — `Rooms enabled: serving /api/room/* from the room router
in namespace 'default' (one Knative Service per room, named 'ndmspc-room-<id>')` — so a deployment
can tell from its logs that room serving is on rather than inferring it from a working `room/list`.

It creates one Knative Service per room on demand and one HTTPRoute matching `?room=<id>` that
points straight at that room's revision, so steady-state traffic goes gateway → room and never
touches the router again. The actions it registers (in the same handler map the macros use, so they
are served both as `/api/room/*` and as MCP tools):

| Action | Methods | What it does |
| ------ | ------- | ------------ |
| `room/open` | GET, POST | Ensure a room. `wait` (body, query, or `NDMSPC_ROOM_WAIT`) defaults to true and answers with the room's URL; `wait=false` registers the room and returns at once with `state=preparing`, leaving the work to a background thread. A room the cluster has no room for answers `state=pending` — the room exists and is kept (see [Room router](#room-router-nroomrouter)). `profile` (body or query) picks one of the skeleton's sizes — see [Room profiles](#room-profiles) — and resizes an existing room when it differs from the one it runs. |
| `room/status` | GET | Whether a room is known, its revision, and — while it is being created — the phase it has reached; `state=pending` with `code=no_capacity` while it waits for cluster resources. Finishing a pending room that can now be placed happens here (and in `room/list`). |
| `room/list` | GET | Every room being tracked, including those still preparing, those waiting for resources (`state=pending`) and those whose creation failed. |
| `room/capacity` | GET | What the cluster has for rooms, what they and everything else reserve, and what is left — see [Cluster capacity](#cluster-capacity). |
| `room/close` | DELETE | Delete a room's HTTPRoute and Knative Service; a creation still running for it is cancelled. |
| `room/state` | GET, POST | Internal: a room reports its session here and fetches it back when it wakes. Hidden from the MCP tool list. |
| `room/backup` | GET | Every tracked room and its session as one JSON document. |
| `room/restore` | POST | Ensure every room in such a document and replay its session. Additive: rooms not named are untouched. |

### Watching the rooms (instead of polling)

`room/list` asked over a websocket subscribes that connection: the router then pushes the list to it
whenever it changes, each watcher answered as the caller it registered as (see *Ownership and
visibility*), so a rooms view does not have to poll. A view opens `/ws/root.websocket?rooms=1` — the
router accepts a socket that asks for the rooms list, where one that names neither a room nor `rooms`
is still refused — authenticates as it would for a room, and calls `room/list` over the socket: that
call is both the first list and the subscription.

The pushes are `{"event":"rooms","payload":<the room/list payload>}`, sent only when the list changed
for that watcher, at most every `NDMSPC_ROOM_WATCH_INTERVAL` (default 2s; `0` serves no pushes). The
router's own actions push at once rather than waiting for that interval, so a create, a close or a
restore shows up as soon as it happened, and a cluster-side change (a room becoming ready, its pods
going up or down) arrives within the interval. A view keeps its own interval as the fallback, so a
socket that is down - or a deployment with watching off - still refreshes; `room/list` over HTTP is
unchanged, and so is every script and MCP client that uses it.

### Creating a room in the background

Creating a room means creating a Knative Service, waiting for its first revision, pinning the
HTTPRoute and replaying the session — tens of seconds, minutes when the room has to roll. ROOT's
`THttpServer` serves one request at a time, so doing that on the request thread freezes the router
for its whole duration; that is why `room/open` has the `wait` flag. `NDMSPC_ROOM_MAX_PREPARING`
(default 4) bounds how many creations run at once. A worker checks between steps whether its room
was closed or superseded, so a slow creation cannot outlive the room it belongs to, and a close that
lands while a Service is being created takes that Service back out again.

A room that is not serving is reported with its `state`, the reason in `error` and, when the router
can name it, a stable `code`. Waiting for resources and failing are two different things:

- `pending` — the cluster has no room for the room's pod right now. The message is the scheduler's
  own (`0/1 nodes are available: 1 Insufficient cpu`), read from the pod: the Knative Service only
  ever says "waiting for a Revision to become ready". The verdict has to repeat, so a race while
  another room scales up cannot change this room's state. This needs `get`/`list` on pods (core) in
  the namespace; without it nothing breaks, the room simply reports the timeout instead. The room's
  Service is **kept** — it is what the cluster still has to place a pod for — and `code` is
  `no_capacity`. The room comes up by itself once there is room for it: the next `room/list` or
  `room/status` pins its revision's HTTPRoute and reports it ready, so nothing has to be opened
  again.
- `name_conflict` — the name the room needs is already taken by an object the router did not create
  (it carries no `ndmspc.io/room` label), so that object is left untouched rather than overwritten
  or deleted. Pick another room id, or free the name. This is why the router must not live inside
  the room prefix namespace: a router named `ndmspc-room-router` is exactly the name the room id
  `router` asks for. The devops role names the router `ndmspc-router` (rooms are `ndmspc-room-<id>`)
  so the two cannot meet.
- *(empty)* — anything else: a failed apply, a container that keeps dying, a timeout. A creation
  that **failed** leaves nothing behind: the router deletes the half-created Service and HTTPRoute,
  so the next `room/open` creates the room from scratch rather than patching what failed — and a
  room whose route does not exist cannot take its `?room=<id>` link away from the router. The
  reason is kept in the registry until then, so the next `room/open` reports what happened to its
  predecessor.

`room/close` and the idle sweep only delete objects carrying the room label, so a name taken by
anything else survives both.

### Websockets

The router serves no session of its own, so a websocket to this server must name a room it is
tracking: `/ws/root.websocket?room=<id>`. Without the parameter, or naming a room the router does
not know, the upgrade is refused (the client sees a failed handshake, the reason is logged) — such a
connection is going to the wrong endpoint, since a connection for a room is routed to that room's
own pod, which keeps serving its websocket either way. `--ws false` (`NDMSPC_WS=0`) serves no
websocket at all, and then none of this applies.

### Access tokens (rw / ro)

A room can be given **access tokens**, and a room that has them serves nothing without one. They
gate *joining the room* — the room's page, its `/api` and its websocket — not the router: the router
keeps serving without a token, and it is the room's own process that turns a stranger away.

Two are minted per room when it is created, one per level:

- **`rw`** may do anything in the room;
- **`ro`** may only read: a non-GET is refused.

They are reported as `access` in `room_open`, `room_status`, `room_list` and the backup document, and
`room_open`'s `url` carries the **read-write** one — that URL is the link a client hands on. The pair
is also kept on the room's own Service, as the annotation `ndmspc.io/room-access`, and handed to the
room as the environment variable `NDMSPC_ROOM_ACCESS` (a JSON object with the two tokens). Keeping
them on the Service is what lets a router restart — and an idle room coming back — still know which
links open it. A room is only ever given tokens when it is created: an existing room adopted after a
router restart keeps whatever its annotation says, so a room created before this existed has none.

A client presents its token in one of three ways, depending on what it is:

| Client | Carries the token as | Why |
| ------ | -------------------- | --- |
| A browser following a link | `?token=<hex>` in the URL | the link is what was handed out |
| A script | `X-Ndmspc-Room-Token: <hex>` | no query string to build, nothing in the URL bar |
| The page's own scripts | the cookie the page sets | a page cannot add a header to its API or websocket calls, so a page request that carried a valid token answers with `Set-Cookie: ndmspc-room-access=<token>; Path=/; HttpOnly; SameSite=Lax` and the browser sends it from then on |

A page link also states the level it was handed out at, as `?access=rw|ro` beside the token: a viewer
reads it to open the room at that level - a read-only link lists nothing rather than asking for what
the room would refuse - without having to probe the token. The room checks the two agree and refuses
the page (`404`, the same as an unknown token) when they do not, so a link whose level was edited does
not open: the token is what opens the room, and the level is only what the page acts on. A link that
states no level (one handed out before this existed) is admitted as it always was. `room/open`'s URL
and the links `ndmspc-room-tui` shows carry the level of the link being handed out.

Refusals keep the shapes this server already uses: an `/api` request answers HTTP 200 with
`{"result": "failure", "code": ..., "error": ...}`, where the code is `access_denied` (no token),
`invalid_access_token` (a token that grants nothing) or `read_only` (a non-GET with a read-only
token). A page request is answered `404` — ROOT's civetweb cannot send a body together with an error
status, so `_404_` is the only refusal it offers, which also avoids telling a stranger that the room
exists. A websocket upgrade that carries no valid token is refused. The page's own `/assets/*` stay
open: they are inert files, and the browser re-fetches them on reload with the cookie it was given.

Nothing is enforced when `NDMSPC_ROOM_ACCESS` is absent, which is how a room created before this
existed (or served by an older image) keeps working — the switch is that variable, not the code. The
router's own call into a room (replaying a session) presents the read-write token, so
they are admitted like any other client.

The token travels in the URL, so it appears in the room pod's request log and in a browser's history;
that is the price of a link that can be handed on, and the header is there for clients that would
rather not put it in a URL.

### Ownership and admins

A room belongs to whoever creates it. The router records that owner when the room is made - the
verified identity of the caller (the email or user name of a token it checked itself, or the
certificate a mutual-TLS front door checked), or, when nothing verified the request, the `owner` it
asserts - and keeps it on the room's own Service as the annotation `ndmspc.io/room-owner`, beside its
access tokens, so it survives a router restart, an idle room waking up, and a restore. Every payload
reports it as `owner`; a room created before this existed simply has none.

Who is shown what follows from it:

| Caller | `room/list` | `status` / `open` / `close` | `backup` / `restore` |
| ------ | ----------- | --------------------------- | -------------------- |
| An admin (`NDMSPC_ROOM_ADMINS`; matched on any of the caller's identifiers, case-insensitively) | every room | any room | every room |
| Identified, not an admin | their own rooms | only their own - otherwise `not_owner` | only their own; a document entry belonging to someone else fails with `not_owner` |
| Nobody identified | every room | any room | every room |

A new room is named after whoever asks for it: `mine` becomes `alice@example.com-mine` (see below).
`room/list` also says whether the caller was answered as an admin (`admin`), so a view can show why it
is being given more than its own rooms without keeping a second copy of the list.

A caller counts as identified when the server verified it, or when it asserted an owner. A verified
identity wins: a request cannot claim to be someone else while carrying a token that says otherwise.
A client that knows both names for itself should send both - `owner` (what names its rooms) and
`owner_email` - because an admin list may be written in either, and one name alone matches only that
one. A caller that says nothing about itself - a script, or `ndmspc-room-tui` run with no credentials
- is answered exactly as it was before, which is what keeps operator tooling working. A room with no
owner belongs to nobody, so it is shown to nobody but an admin or an anonymous caller.

The owner is part of the room's **id**, not only of its metadata: an identified caller's `mine` is
stored as `alice-mine` (the owner is the name it is called by - its user name, or its email when that
is all a token carries), and that is the id in its link, its Service, its session and every
payload - which is what lets two people both own a room called "mine" without one of them taking the
other's. An id that already names a room is always that room (a link that was handed on has to keep
working whoever follows it, and a room created before ownership existed keeps its own id); only an id
that names nothing yet becomes the caller's own. So `room/open` answers with the id it used, and says
whether it created the room (`created`) or found it already there - it is *ensure*, and an existing
room is never an error. `room/restore` is the one exception: a document names its rooms, and they come
back under the names they were exported with.

Refusals use the shape room actions always use - `result: "failure"` with a stable `code` - and the
code here is `not_owner`, for a room that belongs to someone else (or to nobody). `room/open` refuses
it too, because that call answers with the room's own link: without the refusal, knowing a room id
would be enough to walk into the room. `room/backup` exports the rooms its caller may see and
`room/restore` refuses a document entry that belongs to someone else, so neither can be used to reach
around `room/list`.

**The assertion is not a boundary.** With no OIDC configured - or an engine that was not told an
authenticating front door stands in front of it - anyone who can reach the router can claim any
owner, so this decides what people are *shown*, not what they may have. What makes it a boundary is a
verified identity: OIDC, or the X509 front door. The room's own access tokens remain the gate on the
room itself, whoever created it.

### Declared resources

A room's container is shaped by the **room skeleton** — `room-skeleton.json` in the ConfigMap the
router clones per room — so that is where a room's requests and limits are set. `room/list` and
`room/status` report what the room's Service declares, alongside everything else a room is:

```json
"resources": {
  "requests": { "cpu": "500m", "memory": "512Mi" },
  "limits":   { "cpu": "2",    "memory": "2Gi" }
}
```

The values are handed over exactly as Kubernetes holds them (`500m`, `2`, `512Mi`). A side that
declares nothing is left out, and a room whose skeleton declares nothing reports no `resources` at
all, so a view shows a dash rather than a zero. The skeleton the role ships today sets no resources,
which is why the field appears once one does.

This is what a room is *allowed*, not what it is *using*: it is read from the Service, so it holds
while the room is scaled to zero, which is when it is most useful. Usage is a different question and
needs the cluster's metrics API — a scaled-to-zero room has no pod to measure, and a running room's
own process reports its CPU and memory over the websocket heartbeat instead.

### Room profiles

A room's size is a **profile**: a named set of container resources the deployment defines in the room
skeleton, beside `serviceSpec`, and `room/open` takes one by name:

```json
"defaultProfile": "small",
"profiles": {
  "small":  { "resources": { "requests": {"cpu":"250m","memory":"256Mi"}, "limits": {"cpu":"1","memory":"1Gi"} } },
  "medium": { "resources": { "requests": {"cpu":"500m","memory":"512Mi"}, "limits": {"cpu":"2","memory":"2Gi"} } },
  "large":  { "resources": { "requests": {"cpu":"1","memory":"1Gi"},      "limits": {"cpu":"4","memory":"4Gi"} } }
}
```

The names are the deployment's — the router never knows what "small" means, it only resolves it — so
a deployment can offer as many, and call them whatever, as it likes (the devops role's
`ndmspc_room_profiles` is what renders this block). `room/open` with `profile` (body, or `?profile=`
in the query) welds that profile's `resources` onto the room's container; without one a room keeps
the profile it already has, and a room that has none takes `defaultProfile`. `room/list` reports
`profiles`, each with its resources, and `defaultProfile`, so a client can offer the choice without
knowing the names in advance, and reports each room's `profile` beside its resources.

The choice is kept as the annotation `ndmspc.io/room-profile`, so it survives a router restart,
an idle room waking up and a restore; `room/status` and `room/open` report it too. Opening an existing
room with a **different** profile resizes it — the room's container resources are patched from the new
profile, which rolls a new revision — while an open that names the same profile, or none, leaves the
Service alone. A name the deployment does not offer fails with `code: unknown_profile`, and the room is
not created. A skeleton that declares no profiles at all is a deployment that offers no sizes: rooms
are then whatever their own spec declares, which is what every room was before profiles existed.

A room created before its deployment offered profiles has none, and takes `defaultProfile` the next
time it is opened — which resizes it from whatever it declared on its own. The rules are one rule
("the size in play is the one asked for, else the room's, else the default"), so a deployment that
turns profiles on moves its existing rooms onto the default rather than leaving them at a size
nothing describes any more; give such a room a profile explicitly to choose where it lands.

### Cluster capacity

`room/capacity` answers "how much do the rooms cost, and what is left?" with the numbers the
scheduler itself uses. Nothing is measured live — there is no metrics-server in this deployment — so
"used" means **reserved**: the `requests` a room's containers declare, which is exactly what decides
whether the next room can start (`no_capacity`). A room's live consumption is a different question,
and the room's own page answers it from the heartbeat.

```json
{ "nodes": 1,
  "allocatable": { "cpuMillis": 8000, "memBytes": 17179869184 },
  "requests":    { "cpuMillis": 4100, "memBytes": 7516192768 },
  "limits":      { "cpuMillis": 9000, "memBytes": 17179869184 },
  "rooms":       { "count": 3,
                   "requests": { "cpuMillis": 1250, "memBytes": 2147483648 },
                   "limits":   { "cpuMillis": 4000, "memBytes": 8589934592 } },
  "other":       { "requests": { "cpuMillis": 2850, "memBytes": 5368709120 },
                   "limits":   { "cpuMillis": 5000, "memBytes": 8589934592 } },
  "free":        { "cpuMillis": 3900, "memBytes": 9663676416 },
  "perNode": [
    { "name": "kind-control-plane",
      "allocatable": { "cpuMillis": 8000, "memBytes": 17179869184 },
      "requests":    { "cpuMillis": 4100, "memBytes": 7516192768 },
      "free":        { "cpuMillis": 3900, "memBytes": 9663676416 } }
  ],
  "complete": true }
```

- `allocatable` is the sum of the nodes' `status.allocatable`; amounts are base units
  (`cpuMillis`, `memBytes`) because they are sums, not the quantities Kubernetes wrote down.
- `requests` and `limits` are the two sides of what the cluster's pods declare: what they reserve, and
  the most they may reach. They are summed over every pod on the cluster that has not finished, and
  the `rooms`/`other` groups split that the same way — `rooms` over the pods carrying
  `serving.knative.dev/service` (a room's pod *and* Knative's sidecar beside it, so a room's real cost
  is what it says), `other` everything that is not a room. A pod that declares no limit contributes
  nothing to the limits side rather than a ceiling it does not have.
- `free` is allocatable minus requests, floored at zero. **Free is always about reservations**, whatever
  a client shows: it is reservations the scheduler places, so only those predict whether another room
  starts — a limits lens says how much a running room can burst into, not what fits.
- `fits` says how many more rooms of each profile could start on the emptiest node, and it comes in both
  sides (`{"requests": {"small": {"count": 14, "limitedBy": "cpu"}, …}, "limits": {…}}`): by what a room
  *reserves*, and by the *ceilings* it may reach — the second subtracting the pods' limits from the node
  the way the first subtracts their requests, so it answers "if every room reached its limit at once".
  The reservations count is the placement prediction; the limits count is the worst case.
- **`perNode` matters**, because free memory across a cluster is not free memory on a node: 4 GiB free
  spread as 2 + 2 starts nothing. The largest single node's `free` is what predicts whether a room of a
  given profile can start, which is why the rooms panel reports both.
- `complete: false` means part of the answer could not be read — the router needs `get`/`list` on
  `nodes` and cluster-wide `list` on `pods` (the deployment's `roles/ndmspc` grants it as a read-only
  `ClusterRole`). The parts it could read are still reported, and nothing is estimated: a figure the
  router cannot stand behind is a figure it does not send.

### Architecture

The rooms it tracks, the configuration and the Kubernetes access all live in the object, and the
cluster is reached through one seam:

```cpp
class IRoomCluster {                          // what the router needs from Kubernetes
  virtual NHttpResponse Request(method, path, body, contentType) = 0;
};
```

Everything above that seam — building the Service and HTTPRoute objects, the 404-then-POST apply,
reading a status back, waiting for a revision, telling "no capacity" (a room waiting for resources)
from a timeout, adopting the
rooms that already exist, expiring idle ones, deciding whether a websocket may be served — is the
router's own logic, and is what `test/test_NRoomRouter.cxx` exercises against an in-memory cluster
and through the actions themselves: no Kubernetes, no server, no HTTP. The workers it starts are
owned by the object and joined when it goes away.

## Room management TUI (`ndmspc-room-tui`)

`ndmspc-room-tui` is a terminal UI for the room router (`Ndmspc::NRoomRouter`). It
lists the rooms the router is tracking with their live state and drives the four room
actions over the MCP endpoint, so it needs no cluster-side tooling of its own.

```bash
ndmspc-room-tui --url http://ndmspc.127.0.0.1.sslip.io:8009
```

The router must have the room macro loaded (`--rooms true` / `NDMSPC_ROOMS=1`); when the
room tools are missing the tool says so at startup instead of showing an empty table.

By default the tool says nothing about who is using it, which the router answers as an operator's
tool: every room, every action. `--owner <email-or-user-name>` (or `NDMSPC_ROOM_OWNER`) makes it act
as someone instead, so the router then shows it only that owner's rooms and refuses the rest - the
same thing the UI does with the identity of whoever is signed in to it, or with the anonymous one its
deployment was told to claim while nobody is (`VITE_NDMSPC_ANONYMOUS_USER`); see
[Ownership and admins](#ownership-and-admins). A login (`--oidc-*`, or a client certificate) needs
no flag: the router believes the verified identity, and an asserted owner never overrides one.

### Keys

| Key | Action |
|---|---|
| `↑` / `↓` (`j` / `k`, `PgUp` / `PgDn`, `Home` / `End`) | Move the selection |
| `Enter` | Refresh the selected room's status |
| `c` / `o` / `a` | Create a room (`o` / `a` are kept as aliases) |
| `d` / `Del` | Delete a room, after confirmation |
| `r` | Read the room list now (the router pushes it while the socket is up) |
| `t` | Switch the detail pane between the read-write and the read-only link |
| `?` | Key help |
| `q` / `Esc` | Quit |

An idle room keeps its Service but runs no pods, so `idle` with 0 pods is the normal
resting state and is presented as such rather than as a problem; the detail pane shows the
`/api?room=<id>` URL to hand to a client and the `/ws/root.websocket?room=<id>` URL for a
WebSocket client — the handshake carries the same `?room=` parameter, which is what keeps a
room awake.

The room itself lives for as long as something wants it. `NDMSPC_ROOM_IDLE_TTL` (default 24h) is how
long a room may go unused before the router deletes it — route, then Service — and a room is unused
when nothing wants it **and** its pod is not running. What wants a room is `room/open` (a client
asking for it, or a handed-on link being followed); asking a room's *state* (`room/status`) does not,
because that is what a view does when it selects a row — counting it would keep a room alive for as
long as somebody had it selected. Traffic into a room goes
gateway → room and never reaches the router, so a running pod is the router's only evidence that
somebody is in there: anything using the room keeps that pod up (an open WebSocket counts), and every
read that finds a room running moves its idle clock forward, so its countdown never runs out while it
is in use. The sweep that enforces the TTL runs on `room/list`, `room/status` and `room/open` — a view
that polls enforces it while it watches, and a room is deleted about the TTL after its pod has gone,
not on the next create. A room still being created, or waiting for resources, is never swept either.

That clock outlives the router: each room's Service carries when it was last wanted
(`ndmspc.io/room-seen`, written whenever the stored copy has drifted a fraction of the TTL from the
one in memory), and `Adopt` reads it back at startup. Without it a restart — a rollout, a re-apply —
handed every room a full idle TTL, so a room that had been idle for a day read as just used and
nothing ever expired. Annotating a Service creates no revision, so this cannot disturb a running room.

For a room that was given access tokens (see [Access tokens](#access-tokens-rw--ro)) the pane also
shows which level it is displaying and appends that token to all three URLs, so the link that gets
handed on is chosen where it is copied: `t` switches between the read-write and the read-only one.
A room the router reports no tokens for (an older one) shows the plain URLs and nothing to choose.

On the router that parameter is not optional: with rooms enabled the router's own websocket serves
nothing, so a connection to `/ws/root.websocket` without `?room=<id>` — or naming a room the router
is not tracking — is refused by the router's policy (`http/room/NRoomRouter.cxx`); a server without
rooms still accepts every websocket. The client sees a failed handshake, since an HTTP upgrade has
nowhere to carry an explanation; the reason is in the router's log.

The router can also serve no websocket at all: `ndmspc-server --ws false` (or
`NDMSPC_WS=0`) leaves the endpoint out entirely, so every upgrade is refused whatever it carries —
useful where only scripts and the UI talk to the entry service. It is on by default, like every
other server's, and leaves room clients untouched either way: a connection naming a room is routed
to that room's own pod, which keeps serving its websocket.

The table shows what the router reports: the room, whose it is (`OWNER`; `-` for a room created
before ownership existed, or by nobody identifiable), `active` / `idle` for a room that is up, `preparing` with
a spinner while its creation is still running — the `SEEN` column then reads as how long it has
been creating — `waiting` for a room the cluster has no room for yet, and `failed` / `killed` when a
creation did not get there. The detail pane says the same word for the room's state (the row and the
pane are one vocabulary: the router's `pending` reads as `waiting`, and a room that is up reads by
what it is doing), adds the creation's `phase` (`service`, `ready`, `route`, `restore`) while it is
still being prepared, and its reason when it is not serving.

Waiting and failing are different: a `pending` room carries `code=no_capacity`, the scheduler's own
message (`0/1 nodes are available: 1 Insufficient cpu`) and is left to come up on its own — the row
reads `waiting`, and only `room/close` takes it back. A `failed` creation carries the router's
`error` and, when it can name the cause, a stable `code`: `container_error` means the room's own
container keeps dying — a room created at a profile too small for what it loads is killed by the
kernel before it can serve, and the failure says so instead of waiting out the timeout. The router
reads that reason from the pod itself, so its service account needs `get` / `list` on `pods`;
without that permission the failure is still reported, just without the cause.

### What killed a room

A room that runs out of memory is killed by the kernel, so it never gets to say anything itself: the
websocket closes and the only witness left is the pod's own status. The router reads that status —
`status.containerStatuses[].lastState.terminated` — and reports it on every room description:

```json
"lastError": {
  "reason": "OOMKilled",     // the container's own reason ("Error", "Evicted", ...)
  "exitCode": 137,
  "at": 1758639421,          // epoch seconds it terminated
  "restarts": 2,             // how many times the container had been restarted by then
  "message": "the room was killed for using more memory than its limit"
}
```

The block is absent for a room that has never died, and a container that exited `Completed` is not a
failure and is not reported. `room/list` reports it per room, `room/status` and `room/open` report it
for the room they are about, and a room whose container keeps dying while it is being created fails
with `container_error` carrying the same block.

Because Knative deletes a pod when its revision scales to zero, the pod is not a durable record: what
the router finds is remembered in its registry (so it keeps reporting while the pod is gone) and as
the Service annotation `ndmspc.io/room-last-error` (so it survives a router restart, an idle room
waking up and a restore — `room/list`'s first refresh after a restart reads it back). The annotation
dies with the Service, so closing a room clears its history: a room created again says nothing about
its predecessor. The memory a room may use is its profile's limit, so an `OOMKilled` note is the
deployment saying "give this room a bigger profile".

Creating a room does not block the screen: the TUI calls `room/open` with `wait=false`, so the
router registers the room and does the slow part — a Knative Service, its first revision, the
HTTPRoute, the session replay — in the background while the TUI keeps refreshing. That is what lets
several rooms be created one after another, and it is also why one slow room no longer freezes the
router for everyone else. A scripted caller keeps the old behaviour: `--open` waits for the room to
be ready (following it with `room/status`; `--no-wait` skips that), so the URL it prints is usable
straight away.

Actions still run one at a time in the TUI, so a key that would start one is refused with a message
naming what is still running, rather than quietly ignored.

### Scripted use

With any of these flags (and no terminal needed) the same binary performs a single action,
prints the router's payload as JSON and exits — `0` on success, `1` when the router reports
a failure, `2` for a bad invocation:

```bash
ndmspc-room-tui --url "$BASE" --list
ndmspc-room-tui --url "$BASE" --open myroom
ndmspc-room-tui --url "$BASE" --status myroom
ndmspc-room-tui --url "$BASE" --close myroom
ndmspc-room-tui --url "$BASE" --backup rooms.json    # export the rooms and their sessions
ndmspc-room-tui --url "$BASE" --restore rooms.json   # re-create and replay them
ndmspc-room-tui --url "$BASE" --restore rooms.json --replace   # as they were exported, over what is there
```

`--backup` writes the router's rooms and their sessions to a file and `--restore` brings
them back, creating any room that is missing — see [Room backup and
restore](#room-backup-and-restore) below. `--backup` refuses to overwrite an existing file
unless you add `--force`, and `--restore` leaves a room that is already there alone unless you
add `--replace` (which deletes it first, so the document's session is what it comes back with).

### Options

| Option | Env | Default | Meaning |
|---|---|---|---|
| `--url,-u` | `NDMSPC_ROOM_URL` | `http://localhost:8080` | Router base URL, or a full `.../api/mcp` endpoint |
| `--refresh,-r` | | `5` | Seconds between automatic refreshes (`0` = manual only) |
| `--replace` | | `false` | With `--restore`: delete a room the document names that already exists before restoring it, so the document's session wins over what the room holds |
| `--owner` | `NDMSPC_ROOM_OWNER` | | Act as this owner (an email address or user name): the router then shows only its rooms. Empty says nothing about the caller, which keeps the operator's view of every room |
| `--cert` / `--key` | | | Client certificate and key for mutual TLS |
| `--key-pass` / `--key-pass-file` | `NDMSPC_KEY_PASS` / `NDMSPC_KEY_PASS_FILE` | | Private-key passphrase, or a base64 file holding it; an encrypted key with no source prompts on a terminal |
| `--ca-file` / `--ca-path` | | | Verify the server against a specific CA |
| `--allow-insecure` | | | Do not verify the server certificate |
| `--oidc-issuer` / `--oidc-client-id` / `--oidc-client-secret` / `--oidc-grant` / `--oidc-username` / `--oidc-password` | | | Obtain an OIDC access token and send it as `Authorization: Bearer ...` |
| `--oidc-ca-file` / `--oidc-ca-path` / `--oidc-allow-insecure-http` | | | TLS trust for the OIDC issuer |
| `--oidc-token-refresh` | | `300` | Re-obtain the access token after this many seconds |
| `--connect-retries` | | `3` | Attempts before giving up on a router that is not answering yet; a rejected certificate, a wrong URL or an authentication failure is reported immediately rather than retried |

See [`examples/room`](examples/room) for a runnable example: it mocks the router's MCP
endpoint so the tool can be exercised end to end without a cluster.

## Room session restore

A room is created with `min-scale 0`, so an idle room runs no pods at all. When it scales
back up it is a brand-new process: the file it had open, its navigator and its drill-down
are gone. The room router therefore remembers each room's **session** and replays it when
the room is next opened, so `room/open` hands back a room holding what it held before.

- The snapshot is compact and replayable: the opened file, the request bodies of the actions
  that define state (`ngnt/open`, `ngnt/reshape`) and the drill-down state point.
- It is stored as the `ndmspc.io/room-state` annotation on the room's own Knative Service,
  so it survives a router restart (the router re-adopts it) and travels with the room.
  Annotating a Service does not create a revision.
- It is captured while the room is running: the room reports it itself after any request that
  changes the session (see `NHttpServer::RoomSessionPush`), and nothing else asks the room for it.
  The router deliberately never polls a room: a request of its own would keep the room's pod awake,
  so a room that was merely being listed could never go idle. It is replayed during `room/open`, and
  by the room itself when it wakes.

Both ways a room can come back are covered:

- a client that calls `room/open` gets the restored session in that same call;
- a client that goes straight for `?room=<id>` (HTTP **or** WebSocket) wakes the room without
  the router ever seeing it, so the room restores **itself**: the first request fetches its
  snapshot from the router and replays it before being served, and the WebSocket path does the
  same before the connection is served. The very first request already sees the restored
  session.

The room side needs `NDMSPC_ROOM_STATE_URL` on the room (the router injects it, derived from
Knative's `K_SERVICE`), and the snapshot endpoints `POST`/`GET /api/room/state` on the router,
which are internal and hidden from the MCP tool list.

That channel authenticates the room rather than a user: the room sends its own read-write token
(`NDMSPC_ROOM_ACCESS`, injected beside the URL) in the `?token=` parameter, and the router checks
it against the room the request names. It is the same credential the room enforces on its own API. A
room that was given no
tokens - an older image, or one created before access existed - reports without one, and so does a
report that arrives before the router has re-adopted the room after a restart: there is nothing to
check those against. A report the router refuses is logged with the router's own reason and sent
again at the next change to the session, so a rejection is never mistaken for a stored session.

The router never asks a room for its session: the room reports it over this channel whenever it
changes, and that report is the whole story (a request from the router into the room would keep the
room's pod awake, so a room being listed could never go idle and its idle TTL would never apply). A
report the router refuses - an unknown token, a room it has not adopted yet - is logged with the
router's own reason, and the room sends it again at the next change.

`room/open` reports what happened in its payload: `"restored": true` with
`"session": "restored"`, or `"session": "live"` when the room was already in use, or
`"restoreError"` when a replay step failed. `room/status` reports `"hasSnapshot"`.

Two rules make this safe:

- a room with nothing open never reports one, so a freshly started pod cannot overwrite a
  good snapshot with emptiness;
- a room that already has a file open is never restored over - the live session wins.

Three more details keep it working in practice:

- The snapshot lives on the room's own Service, and the router **reads it back from there**
  when its in-memory copy is empty (a router restart, or a scale-from-zero) rather than
  telling a room that has one that it has none - which would be exactly the moment a room is
  waking and asking for its session.
- A room **bounds** how long it waits for the router: short timeouts and a few retries, so a
  cold or busy router cannot stall a client for a minute. If the fetch still fails, the room
  serves the request and tries again shortly after.
- The report runs **off the room's request path**: the room serves one request at a time too, so a
  slow report cannot delay the next client.

**What is deliberately not persisted.** This restores the session, not data. A room's
filesystem is ephemeral, so anything written into a ROOT file is lost when the room scales to
zero. That is a deliberate choice for now: nothing in a room writes to a ROOT file today
(`ngnt/open` opens read-only and no handler writes), so rooms are read-only sessions over the
files baked into the image, and the restore above is what makes them feel continuous across
scaling.

Persisting data would mean choosing a backend and a lifetime. Backends: a per-room PVC created
by the router (works, but node-local with a single-node storage class), an RWX filesystem (NFS /
CephFS, tolerant of rescheduling), object storage (S3/MinIO with a sync at the end of a session,
nothing attached to the pod), or pointing rooms at an existing data service such as EOS. And
because a room's owner is only a name the router recorded - it authenticates nobody, and the room
itself knows its access tokens, not its users - storage would live and die with the room unless a
workspace were keyed by an authenticated user the room could also see, which needs OIDC to reach the
room itself.

## Room backup and restore

`room/backup` exports the router's state — every room it is tracking, and each room's
session — as one JSON document; `room/restore` takes such a document, ensures every room in
it from the **current** skeleton and replays its session. It is the same snapshot the
session restore above keeps, so what comes back is the session, not data.

```bash
ndmspc-room-tui --url "$BASE" --backup rooms.json    # or: curl -s "$BASE/api/room/backup" > rooms.json
ndmspc-room-tui --url "$BASE" --restore rooms.json   # onto this deployment, or a freshly installed one
```

The document is deliberately **not** a Kubernetes manifest dump: rooms are re-created from
today's skeleton (image, env, autoscaling) and their routes re-pinned, because a backed-up
HTTPRoute pins a revision name that will not exist after a rebuild. It carries no ROOT files
— see *What is deliberately not persisted* above — so it stays small enough to keep in
version control: each snapshot is capped at 64 KB, so a few dozen rooms are well under a
megabyte. A snapshot naming a file that no longer exists in the room image fails that room,
with the message in the reply, rather than half-restoring it.

Restoring is **additive and convergent**: rooms not named in the document are untouched,
nothing is deleted, and a room that already has a file open is left alone (reported as
`"session": "live"`). Re-running a restore is therefore safe, and it is also slow by nature
— each room is created and waits for its revision to be ready, so restoring many rooms takes
minutes; a client timeout must not be read as a failure. Per-room failures come back in
`failed[]` with their error, the CLI exits non-zero when any room failed, and each restored
room is annotated with its session again so the annotation store is repopulated rather than
left to depend on the file.

`replace` (body/query on `room/restore`, `--replace` in the TUI) asks for something else: a room
the document names that is already there is **deleted first**, so the document's session is what
it comes back holding instead of the room keeping what it was left holding. It is off by default
because it is the destructive half of a restore — a live room is replaced, not spared — and only
rooms the caller may see are touched: a document naming someone else's room neither deletes it
nor restores it.

A document can be refused outright rather than acted on half-way: an unknown `version`, or a
`router.param`/`router.prefix` that disagrees with this router, means it came from a
differently configured deployment and would create wrongly named rooms here.

### Restoring from a file in devops

The `ndmspc` role in [ndmspc/devops](https://gitlab.com/ndmspc/devops) wraps both ends, so a
deployment can carry its room set in version control:

```bash
ansible-playbook playbooks/local.yml --tags backup ...      # writes the file to ~/.ndmspc/env/<cluster>/
# commit it as roles/ndmspc/files/ndmspc-rooms.json, then, to bring the rooms back:
ansible-playbook playbooks/local.yml --tags restore ... -e ndmspc_rooms_restore_enabled=true
```

Neither tag runs as part of `install`/`apply` unless asked: a restore creates rooms, so it
needs `ndmspc_rooms_restore_enabled=true` — with that set, `install`/`init`/`apply` bring the
rooms back too, which is how a fresh deployment comes up with its room set.
