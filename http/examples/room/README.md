# Room router mock (`mock_mcp_server.py`)

A runnable stand-in for the NDMSPC **room router** — an ngnt server started with `--rooms true`,
which serves the framework's room router (`Ndmspc::NRoomRouter`, `http/room/NRoomRouter.cxx`) and
creates one Knative Service per room on demand.

## Why there is a mock here

Asking the server for rooms fails unless `KUBERNETES_SERVICE_HOST` is set: `ndmspc-server --rooms true`
logs why and exits at startup, because a room *is* a Knative Service and the real router cannot run on
a workstation. `mock_mcp_server.py` therefore answers the same **MCP endpoint** (`POST /api/mcp`, tools
`ndmspc_room_list` / `ndmspc_room_open` / `ndmspc_room_status` / `ndmspc_room_close` /
`ndmspc_room_backup` / `ndmspc_room_restore`) with the same envelopes the router produces, so any
client — `curl`, a script, or the TUI example in [`../../tui`](../../tui/README.md) — can be exercised
end to end without a cluster.

## Running it

```bash
./run-mock-server.sh            # foreground; Ctrl-C to stop
```

Then talk to it over MCP:

```bash
curl -s http://127.0.0.1:8090/api/mcp -H 'Content-Type: application/json' \
  -d '{"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"ndmspc_room_list","arguments":{}}}'
```

## What it reproduces

The mock is written against `http/room/NRoomRouter.cxx` and `http/mcp/NMcpServer.cxx`, not against any
client: the handler envelope (`{"result":"success","payload":{…}}` /
`{"result":"failure","error":"…"}` with HTTP 200 either way), the `content` / `structuredContent` /
`isError` shape, the per-action `method` validation, the
`Missing room id (send it in the body as {"room": "<id>"})` message and the `ndmspc-room-<slug>`
resource naming all come from there.

A room's access tokens (`access`: a read-write and a read-only one, with `ndmspc_room_open`'s `url`
carrying the read-write one) come from there too, with one deliberate difference: the router mints
them with a CSPRNG, while the mock derives them from the room id so a run is reproducible. Nothing
enforces them here — a room is what refuses traffic without its token, and the mock stands in for the
router, not for a room.

Ownership comes from there as well: a room belongs to whoever created it and is named after them
(`mine` is `alice@example.com-mine`), a caller that says who it is (`owner`) is shown only its own
rooms and refused the rest with `code=not_owner`, and a caller listed in `ADMINS` sees every room —
while one that says nothing about itself, which is what a script does, still sees every room. An id
that already names a room is that room, so a link keeps working and `ndmspc_room_open` answers with the
id it used plus `created`: it is ensure, and an existing room is not an error. The mock authenticates
nothing, so it only ever sees an asserted owner; the router believes a verified identity over one.

## Configuration

| Variable | Default | Meaning |
|---|---|---|
| `HOST` | `127.0.0.1` | Address the mock binds to. |
| `PORT` | `8090` | Mock port. |
| `SEED` | `demo` | Comma-separated room ids the mock pre-registers. |
| `TTL` | `60` | Idle TTL the mock reports from `ndmspc/room/list` (the router's own default). |
| `FAIL` | `0` | `1` makes every room action fail, to exercise the error path. |
| `ADMINS` | (empty) | Comma-separated owners the mock treats as admins. |

## Against the real router

Rooms need an in-cluster Kubernetes API, so use the kind deployment from
[ndmspc/devops](https://gitlab.com/ndmspc/devops) (see the *Rooms* section of the top-level
`README.md`). The router must be started with `--rooms true` / `NDMSPC_ROOMS=1`; without it the room
tools do not exist. Against a real router a client may also present a client certificate (mutual TLS)
or an OIDC bearer token — the option table is in [`../../README.md`](../../README.md).
