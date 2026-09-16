# Room management TUI (`ndmspc-room-tui`)

Runnable example for `ndmspc-room-tui`, the terminal UI that manages the rooms of the
NDMSPC **room router** — the ngnt server with `macros/builtin/httpRoom.C` loaded, which
creates one Knative Service per room on demand.

The tool drives the router's room actions over its **MCP endpoint** (`POST /api/mcp`,
tools `room_list` / `room_open` / `room_status` / `room_close` / `room_backup` /
`room_restore`), so every room operation goes through one interface rather than a second,
REST-shaped one.

## Why there is a mock here

`httpRoom.C` calls `std::exit(1)` unless `KUBERNETES_SERVICE_HOST` is set: a room *is*
a Knative Service, so the real router cannot run on a workstation. `mock_mcp_server.py`
therefore answers the same MCP endpoint with the same envelopes the router produces,
which makes the client testable end to end without a cluster.

The mock is written against `httpRoom.C` and `NMcpServer.cxx`, not against the client:
the handler envelope (`{"result":"success","payload":{…}}` /
`{"result":"failure","error":"…"}` with HTTP 200 either way), the `content` /
`structuredContent` / `isError` shape, the per-action `method` validation, the
`Missing room id (send it in the body as {"room": "<id>"})` message and the
`ndmspc-room-<slug>` resource naming all come from there.

## Files

| File | Purpose |
|---|---|
| `mock_mcp_server.py` | The mock room router (Python standard library only). |
| `run-mock-server.sh` | Starts the mock in the foreground. |
| `run-client.sh` | Runs `ndmspc-room-tui` against a URL (TUI, or a headless action). |
| `run-demo.sh` | End-to-end check of every action, backup/restore included, plus the failure path; prints `RESULT: PASS`. |

## Prerequisites

```bash
# From the repository root: builds bin/ndmspc-room-tui (and the rest of the project).
./scripts/make.sh install

python3 --version    # the mock and run-demo.sh need python3
```

## Quick start

```bash
./run-demo.sh
```

It starts the mock, then asserts: `--list` sees the seeded room; `--open` returns the
room and its `?room=<id>` URL; `--status` reports it tracked and ready; the new room
appears in `--list`; `--close` removes it and it disappears from `--list`; `--backup`
writes a file holding the rooms and their sessions; `--restore` brings a closed room back
**with its session**; and a router that fails is reported with a non-zero exit code. It
prints `RESULT: PASS` at the end.

## Running the parts separately

```bash
# Terminal 1: the mock room router (foreground, Ctrl-C to stop)
./run-mock-server.sh

# Terminal 2: the interactive UI (o open, d close, enter status, ? help, q quit)
./run-client.sh

# Terminal 2: or a single action, no terminal needed
./run-client.sh --list
./run-client.sh --open myroom
./run-client.sh --status myroom
./run-client.sh --close myroom
./run-client.sh --backup /tmp/rooms.json   # export the rooms and their sessions
./run-client.sh --restore /tmp/rooms.json  # re-create and replay them
```

The headless actions print the router's payload as indented JSON and exit `0` on
success, `1` when the router reports a failure, and `2` for a bad invocation (an
unreachable router, or no terminal for the UI).

## Configuration

| Variable | Default | Meaning |
|---|---|---|
| `HOST` | `127.0.0.1` | Address the mock binds to. |
| `PORT` | `8090` | Mock port (`run-demo.sh` also uses `PORT + 1` for its failure case). |
| `SEED` | `demo` | Comma-separated room ids the mock pre-registers. |
| `TTL` | `3600` | Idle TTL the mock reports from `room/list`. |
| `FAIL` | `0` | `1` makes every room action fail, to exercise the error path. |
| `URL` | `http://$HOST:$PORT` | Router URL used by `run-client.sh` (`--url` is also accepted directly). |
| `CLIENT_BIN` | `$PROJECT_DIR/bin/ndmspc-room-tui` | Client binary to run. |

## Against the real router

Rooms need an in-cluster Kubernetes API, so use the kind deployment from
[ndmspc/devops](https://gitlab.com/ndmspc/devops) (see the *Rooms* section of the
top-level `README.md`). The router must be started with `--rooms true` /
`NDMSPC_ROOMS=1`; without it the room tools do not exist and the client reports that
the router returned no room payload.

```bash
BASE=http://ndmspc.127.0.0.1.sslip.io:8009      # the deployment's external URL

# Smoke test first: it needs no terminal and fails fast if the URL is wrong.
bin/ndmspc-room-tui --url "$BASE" --list

# Then the UI.
bin/ndmspc-room-tui --url "$BASE"
```

A wrong URL, a router without the MCP endpoint, or an unset `--rooms` all fail at
startup with the URL, the derived MCP endpoint and an `http://`-vs-`https://` hint,
rather than starting a UI that shows nothing.

## Authentication

The mock speaks plain HTTP, so no credentials are needed here. Against a real router the
tool can present a client certificate for mutual TLS or obtain an OIDC bearer token; the
full option table (including `--oidc-*`) is in [`../../README.md`](../../README.md).

The passphrase sources work exactly as they do for `ndmspc-ws-client`: `--key-pass`
(`NDMSPC_KEY_PASS`), or `--key-pass-file` (`NDMSPC_KEY_PASS_FILE`) holding the passphrase
base64-encoded as in `~/.globus/password.txt`. When the key is encrypted and neither source
is given, the tool prompts on the terminal before the screen starts; in a non-interactive
session it fails with an actionable error rather than blocking.

```bash
bin/ndmspc-room-tui --url "$BASE" \
    --cert ~/.globus/usercert.pem --key ~/.globus/userkey.pem \
    --ca-path /cvmfs/alice.cern.ch/etc/grid-security/certificates --allow-insecure
```
