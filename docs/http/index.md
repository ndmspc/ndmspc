# HTTP server & tools

`ndmspc-server` serves the web UI, the HTTP API, the WebSocket and the MCP endpoint. Every capability
is a **tool**: a handler registered under a key, annotated by a ROOT macro.

The full reference is [`http/README.md`](https://gitlab.com/ndmspc/ndmspc/-/blob/develop/http/README.md)
(server flags, authentication, rooms, MCP, precompiled macros). This page is the short version.

## Tool keys

```
<namespace>/<group>[/<subgroup>]/<action>
```

With `ndmspc` as the platform namespace; at most one subgroup. Everything else is derived:

| From `ndmspc/ngnt/open` | Is |
| --- | --- |
| MCP tool name | `ndmspc_ngnt_open` |
| HTTP route | `POST /api/ndmspc/ngnt/open` |
| Group | `ndmspc/ngnt` (everything before the last `/`) |

A key that does not conform is logged and **throws**, so the macro fails to load instead of
publishing a tool nothing can route.

## Registering a tool

```cpp
#include <ndmspc/http/NHttpServer.h>

void toolMyTutorial()
{
  auto & handlers = *(Ndmspc::gNdmspcHttpHandlers);

  Ndmspc::RegisterMcpTool("mytutorial/summary/show", {
      .description = "Show a short summary of what is open.",
      .dependsOn   = {"ndmspc/ngnt/open"},
      .order       = 2,
  });

  handlers["mytutorial/summary/show"] =
      [](std::string method, json & in, json & out, json & wsOut,
         std::map<std::string, TObject *> & objects) {
        Ndmspc::NRouteContext ctx(method, in, out, wsOut, objects);
        ctx.Show("# Summary", "markdown", "pad1");
        ctx.Success();
      };
}
```

`inputSchema` (built with `Ndmspc::NSchemaBuilder`) is what the UI renders as the tool's form;
`_meta["ndmspc.io/*"]` carries the rest (group label, prerequisites, tours).

## Route families

| Family | Routes |
| --- | --- |
| Platform | `/api/ndmspc/{health,heartbeat,state,group,reload,pads,session,macro,logs}` |
| NGnTree | `/api/ndmspc/ngnt/{open,reshape,map,spectra,point}` |
| ROOT browser | `/api/ndmspc/browser/{open,browse}` + hidden `ndmspc/browser/*` |
| Rooms | `/api/ndmspc/room/{open,status,upgrade,list,capacity,close,clear,watch,state,backup,config,restore,import}` |
| Statistics | `/api/ndmspc/stats/{summary,users,export,presence}` |
| MCP | `POST /api/mcp` |

## Loading macros

```bash
ndmspc-server -m "macros/tools/toolNgnt.C,toolBrowser.C"
```

A bare name inherits the directory of the entry before it; a list that begins with a bare name falls
back on `$NDMSPC_DIR/macros/tools`. `toolNgnt.C` and `toolBrowser.C` load automatically: they are the
defaults, and `-m` names what a run **adds** to them rather than what replaces them. `--no-default-tools`
(or `NDMSPC_NO_DEFAULT_TOOLS=1`) loads only the macros named, which is what a deployment shipping a
single tool of its own wants:

```bash
ndmspc-server -m ~/my-tools/toolMyThing.C                     # ngnt + browser + my tool
ndmspc-server --no-default-tools -m ~/my-tools/toolMyThing.C  # my tool only
```

## Rooms

`--rooms true` (or `NDMSPC_ROOMS=1`) turns the server into a room router: one Knative Service per
room, routed by `?room=<id>`, created on demand. It requires Kubernetes and loads no macro.

```bash
curl -s -X POST -H 'Content-Type: application/json' \
  -d '{"room":"demo"}' "$BASE/api/ndmspc/room/open"
curl -s "$BASE/api/ndmspc/room/list"
```

## MCP

```bash
ndmspc-mcp -m "$NDMSPC_DIR/macros/tools/toolNgnt.C"    # stdio
curl -s localhost:8080/api/mcp -H 'Content-Type: application/json' \
  -d '{"jsonrpc":"2.0","id":1,"method":"tools/list","params":{}}'
```

Disable the HTTP endpoint with `--mcp false` or `NDMSPC_MCP=0`.
