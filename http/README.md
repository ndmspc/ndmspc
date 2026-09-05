# NDMSPC HTTP server

The HTTP server supports optional Keycloak/OpenID Connect authentication for both WebSocket connections and plain HTTP API requests. Authentication is disabled when no OIDC issuer and audience are configured, preserving anonymous behavior.

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

Plain HTTP is rejected unless `NDMSPC_OIDC_ALLOW_INSECURE_HTTP` is enabled. Do not enable it outside local development; use HTTPS and optionally configure `NDMSPC_OIDC_CA_FILE` for a private certificate authority.

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
