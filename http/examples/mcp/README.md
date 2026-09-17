# MCP server example

Runnable example that exposes the NDMSPC `ngnt` actions through the **Model Context
Protocol** and drives them over both supported transports:

- **stdio** — `ndmspc-mcp`, a self-contained process an MCP client spawns. It embeds the
  server machinery (no network listener), loads the built-in macros and serves JSON-RPC
  2.0 on stdin/stdout.
- **Streamable HTTP** — `POST /api/mcp` on the running `ndmspc-server`
  (on by default; disable with `--mcp false`). It shares the live session (opened `NGnTree`,
  navigator, workspace and state point) with the browser UI.

## Files

| File | Purpose |
|---|---|
| `run-server.sh` | Starts `ndmspc-server --mcp true` (MCP endpoint enabled). |
| `run-curl.sh` | Drives `POST /api/mcp` with curl: `initialize`, `tools/list`, `tools/call`. |
| `run-demo.sh` | End-to-end check of both transports: stdio responses on stdout, HTTP responses on a live server; prints `RESULT: PASS`. |
| `mcp.json` | Sample MCP client configuration for both transports. |

## Prerequisites

```bash
# Build the server and the stdio launcher (from the repository root).
./scripts/make.sh install
```

## Quick start

```bash
./run-demo.sh
```

If the default port is in use, override it: `PORT=18081 ./run-demo.sh`.

## Running the parts separately

```bash
# Terminal 1: HTTP server with the MCP endpoint
./run-server.sh

# Terminal 2: curl the endpoint
./run-curl.sh                       # initialize + tools/list + tools/call
FILE=/path/to/tree.root ./run-curl.sh   # also open a real tree

# Or run the standalone stdio server by hand
printf '%s\n%s\n' \
  '{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-06-18"}}' \
  '{"jsonrpc":"2.0","id":2,"method":"tools/list","params":{}}' \
  | "$(git rev-parse --show-toplevel)/bin/ndmspc-mcp"
```

Only JSON-RPC messages are written to **stdout**; the logger and any `Print()` output
from the macros go to **stderr**.

## Registering with an MCP client

See `mcp.json`. Point the client at either the stdio binary:

```json
{ "mcpServers": { "ndmspc-ngnt": { "command": "/path/to/bin/ndmspc-mcp" } } }
```

or the running server's HTTP endpoint:

```json
{ "mcpServers": { "ndmspc-ngnt-http": { "url": "http://localhost:8080/api/mcp" } } }
```

### Command Code

`cmd` launches stdio servers itself, in an environment that does **not** source the NDMSPC
profile. The installed binary's RUNPATH only covers `/usr/lib64/root`, so pass
`LD_LIBRARY_PATH` (and `ROOT_INCLUDE_PATH`, so the macros can find the ndmspc headers):

```bash
REPO=/path/to/ndmspc

# stdio — self-contained, nothing else to run
cmd mcp add --scope project \
  --env LD_LIBRARY_PATH="$REPO/lib" \
  --env ROOT_INCLUDE_PATH="$REPO/include" \
  ndmspc-ngnt -- "$REPO/bin/ndmspc-mcp" \
  -m "$REPO/macros/builtin/httpNgntBase.C,$REPO/macros/builtin/httpNgnt.C"
```

Or the same from JSON (default `local` scope):

```bash
cmd mcp add-json ndmspc-ngnt '{
  "type": "stdio",
  "command": "/path/to/ndmspc/bin/ndmspc-mcp",
  "args": ["-m", "/path/to/ndmspc/macros/builtin/httpNgntBase.C,/path/to/ndmspc/macros/builtin/httpNgnt.C"],
  "env": {"LD_LIBRARY_PATH": "/path/to/ndmspc/lib", "ROOT_INCLUDE_PATH": "/path/to/ndmspc/include"}
}'
```

HTTP transport — start the server first (`./run-server.sh`); this shares the live UI session:

```bash
cmd mcp add --scope project --transport http ndmspc-ngnt-http http://localhost:8080/api/mcp
```

The HTTP MCP endpoint is on by default: `run-server.sh` starts the server with `--mcp true`
(disable with `--mcp false` or `NDMSPC_MCP=0`). When disabled, `/api/mcp` returns
`{"error": "MCP endpoint is disabled"}`. The stdio launcher is unaffected.

Verify and use:

```bash
cmd mcp list
cmd mcp get ndmspc-ngnt
cmd                    # then: /mcp  -> connection status + tool count
```

Tools surface as `mcp__<server>__<tool>`, e.g. `mcp__ndmspc-ngnt__ngnt_open`. With OIDC
enabled on the server, add `--header "Authorization: Bearer <token>"`. Remove with
`cmd mcp remove ndmspc-ngnt`.

## Tools

One tool is created per registered handler, named by replacing `/` with `_`
(`ngnt/open` → `ngnt_open`). Each tool's `inputSchema` is taken from the workspace
inspector schema and extended with a `method` property (`GET`/`POST`/`PATCH`/`DELETE`,
default `POST`), since the ngnt actions are verb-sensitive. Internal routes (`debug`,
`openapi/inspector`, `inspector/openapi`) are hidden; `health` and `state` are exposed.

Descriptions and other MCP metadata come from the **handler macro** (e.g. `httpNgnt.C`),
registered with `Ndmspc::RegisterMcpTool(...)` next to the handler — see the
"Annotating tools from a macro" section of `../../README.md`. Editing a description is a
macro edit, not a C++ recompile.

Every `tools/call` is routed through the normal request path, so history entries,
workspace updates and WebSocket broadcasts are identical to those produced by a UI client.

## Configuration

| Variable | Default | Meaning |
|---|---|---|
| `PORT` | `8080` | HTTP port (`run-server.sh`, `run-curl.sh`) / `18080` (`run-demo.sh`). |
| `URL` | `http://localhost:$PORT/api/mcp` | MCP endpoint used by `run-curl.sh`. |
| `FILE` | *(unset)* | Optional ROOT file to open with `ngnt_open` in `run-curl.sh`. |
| `MCP_BIN` | `$PROJECT_DIR/bin/ndmspc-mcp` | stdio launcher binary. |
| `SERVER_BIN` | `$PROJECT_DIR/bin/ndmspc-server` | HTTP server binary. |
| `MACROS` | `$PROJECT_DIR/macros/builtin/httpNgntBase.C,httpNgnt.C` | Macro list to load. |
