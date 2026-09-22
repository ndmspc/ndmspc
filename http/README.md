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
| `X-NDMSPC-Email` | JWT `email` claim (the header is absent when the token carries none) |

Client-supplied `X-NDMSPC-*` headers are never trusted: they are stripped from API-over-WebSocket messages and ignored by the bearer check.

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
ndmspc-mcp                                                  # uses $NDMSPC_DIR/macros/tools/toolBase.C,toolNgnt.C
ndmspc-mcp --rooms                                         # additionally serve the room router (NRoomRouter)
ndmspc-mcp -m /path/toolBase.C,/path/toolNgnt.C
ndmspc-mcp --all-tools                                      # also expose debug/openapi actions
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
| `room/open` | GET, POST | Ensure a room. `wait` (body, query, or `NDMSPC_ROOM_WAIT`) defaults to true and answers with the room's URL; `wait=false` registers the room and returns at once with `state=preparing`, leaving the work to a background thread. |
| `room/status` | GET | Whether a room is known, its revision, and — while it is being created — the phase it has reached. |
| `room/list` | GET | Every room being tracked, including those still preparing and those whose creation failed. |
| `room/close` | DELETE | Delete a room's HTTPRoute and Knative Service; a creation still running for it is cancelled. |
| `room/state` | GET, POST | Internal: a room reports its session here and fetches it back when it wakes. Hidden from the MCP tool list. |
| `room/backup` | GET | Every tracked room and its session as one JSON document. |
| `room/restore` | POST | Ensure every room in such a document and replay its session. Additive: rooms not named are untouched. |

### Creating a room in the background

Creating a room means creating a Knative Service, waiting for its first revision, pinning the
HTTPRoute and replaying the session — tens of seconds, minutes when the room has to roll. ROOT's
`THttpServer` serves one request at a time, so doing that on the request thread freezes the router
for its whole duration; that is why `room/open` has the `wait` flag. `NDMSPC_ROOM_MAX_PREPARING`
(default 4) bounds how many creations run at once. A worker checks between steps whether its room
was closed or superseded, so a slow creation cannot outlive the room it belongs to, and a close that
lands while a Service is being created takes that Service back out again.

A room that could not be created is reported as `state=failed` with the reason in `error` and, when
the router can name it, a stable `code`:

- `no_capacity` — the cluster cannot place the room's pod. The message is the scheduler's own
  (`0/1 nodes are available: 1 Insufficient cpu`), read from the pod: the Knative Service only ever
  says "waiting for a Revision to become ready". The verdict has to repeat, so a race while another
  room scales up cannot fail this one. This needs `get`/`list` on pods (core) in the namespace;
  without it nothing breaks, the room simply reports the timeout instead.
- `name_conflict` — the name the room needs is already taken by an object the router did not create
  (it carries no `ndmspc.io/room` label), so that object is left untouched rather than overwritten
  or deleted. Pick another room id, or free the name. This is why the router must not live inside
  the room prefix namespace: a router named `ndmspc-room-router` is exactly the name the room id
  `router` asks for. The devops role names the router `ndmspc-router` (rooms are `ndmspc-room-<id>`)
  so the two cannot meet.
- *(empty)* — anything else: a failed apply, a roll that never got there, a timeout.

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
| A script | `X-NDMSPC-Room-Token: <hex>` | no query string to build, nothing in the URL bar |
| The page's own scripts | the cookie the page sets | a page cannot add a header to its API or websocket calls, so a page request that carried a valid token answers with `Set-Cookie: ndmspc-room-access=<token>; Path=/; HttpOnly; SameSite=Lax` and the browser sends it from then on |

Refusals keep the shapes this server already uses: an `/api` request answers HTTP 200 with
`{"result": "failure", "code": ..., "error": ...}`, where the code is `access_denied` (no token),
`invalid_access_token` (a token that grants nothing) or `read_only` (a non-GET with a read-only
token). A page request is answered `404` — ROOT's civetweb cannot send a body together with an error
status, so `_404_` is the only refusal it offers, which also avoids telling a stranger that the room
exists. A websocket upgrade that carries no valid token is refused. The page's own `/assets/*` stay
open: they are inert files, and the browser re-fetches them on reload with the cookie it was given.

Nothing is enforced when `NDMSPC_ROOM_ACCESS` is absent, which is how a room created before this
existed (or served by an older image) keeps working — the switch is that variable, not the code. The
router's own calls into a room (capturing a session, replaying one) present the read-write token, so
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

A caller counts as identified when the server verified it, or when it asserted an owner. A verified
identity wins: a request cannot claim to be someone else while carrying a token that says otherwise.
A caller that says nothing about itself - a script, or `ndmspc-room-tui` run with no credentials - is
answered exactly as it was before, which is what keeps operator tooling working. A room with no owner
belongs to nobody, so it is shown to nobody but an admin or an anonymous caller.

Refusals use the shape room actions always use - `result: "failure"` with a stable `code` - and the
code here is `not_owner`, for a room that belongs to someone else. `room/open` refuses it too, because
that call answers with the room's own link: without the refusal, knowing a room id would be enough to
walk into the room. `room/backup` exports the rooms its caller may see and `room/restore` refuses a
document entry that belongs to someone else, so neither can be used to reach around `room/list`.

**The assertion is not a boundary.** With no OIDC configured - or an engine that was not told an
authenticating front door stands in front of it - anyone who can reach the router can claim any
owner, so this decides what people are *shown*, not what they may have. What makes it a boundary is a
verified identity: OIDC, or the X509 front door. The room's own access tokens remain the gate on the
room itself, whoever created it.

### Architecture

The rooms it tracks, the configuration and the Kubernetes access all live in the object, and the
cluster is reached through one seam:

```cpp
class IRoomCluster {                          // what the router needs from Kubernetes
  virtual NHttpResponse Request(method, path, body, contentType) = 0;
};
```

Everything above that seam — building the Service and HTTPRoute objects, the 404-then-POST apply,
reading a status back, waiting for a revision, telling "no capacity" from a timeout, adopting the
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
same thing the UI does with the identity of whoever is signed in to it (see
[Ownership and admins](#ownership-and-admins)). A login (`--oidc-*`, or a client certificate) needs
no flag: the router believes the verified identity, and an asserted owner never overrides one.

### Keys

| Key | Action |
|---|---|
| `↑` / `↓` (`j` / `k`, `PgUp` / `PgDn`, `Home` / `End`) | Move the selection |
| `Enter` | Refresh the selected room's status |
| `c` / `o` / `a` | Create a room (`o` / `a` are kept as aliases) |
| `d` / `Del` | Delete a room, after confirmation |
| `r` | Refresh the room list now |
| `p` | Pause / resume the automatic refresh |
| `t` | Switch the detail pane between the read-write and the read-only link |
| `?` | Key help |
| `q` / `Esc` | Quit |

An idle room keeps its Service but runs no pods, so `idle` with 0 pods is the normal
resting state and is presented as such rather than as a problem; the detail pane shows the
`/api?room=<id>` URL to hand to a client and the `/ws/root.websocket?room=<id>` URL for a
WebSocket client — the handshake carries the same `?room=` parameter, which is what keeps a
room awake.

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
been creating — and `failed` when a creation did not get there. The detail pane adds the creation's
`phase` (`service`, `ready`, `route`, `restore`) while it is still preparing, and its error when it
failed.

A failure carries the router's `error` and, when it can name the cause, a stable `code`:
`no_capacity` means the cluster could not place the room's pod — the row reads `no capacity`, the
detail pane shows the scheduler's own message (`0/1 nodes are available: 1 Insufficient cpu`), and
the status line announces it for the room you just created. The router reads that reason from the
pod itself, so its service account needs `get` / `list` on `pods`; without that permission the
failure is still reported, just without the cause.

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
```

`--backup` writes the router's rooms and their sessions to a file and `--restore` brings
them back, creating any room that is missing — see [Room backup and
restore](#room-backup-and-restore) below. `--backup` refuses to overwrite an existing file
unless you add `--force`.

### Options

| Option | Env | Default | Meaning |
|---|---|---|---|
| `--url,-u` | `NDMSPC_ROOM_URL` | `http://localhost:8080` | Router base URL, or a full `.../api/mcp` endpoint |
| `--refresh,-r` | | `5` | Seconds between automatic refreshes (`0` = manual only) |
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
- It is captured while the room is running: the room reports it after any request that
  changes the session, and the router also captures opportunistically during `room/list`
  (which already knows whether the room has pods, since talking to a scaled-to-zero room
  would wake it). It is replayed during `room/open`, and by the room itself when it wakes.

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

`room/open` reports what happened in its payload: `"restored": true` with
`"session": "restored"`, or `"session": "live"` when the room was already in use, or
`"restoreError"` when a replay step failed. `room/status` reports `"hasSnapshot"`.

Two rules make this safe:

- a room with nothing open is never captured, so a freshly started pod cannot overwrite a
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
- The router's opportunistic capture runs **off the request path**: the router serves one
  request at a time, so a capture inside `room/list` would cycle with a room that is calling
  the router back to restore itself, and starve that fetch.

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
