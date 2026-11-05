# Command-line tools

All binaries install next to each other and share the same environment (`NDMSPC_DIR`,
`ROOT_MAX_THREADS`, `NDMSPC_MAX_PROCESSES`, `NDMSPC_EXECUTION_MODE`).

| Binary | Purpose |
| --- | --- |
| `ndmspc-server` | Serve the web UI, HTTP API, WebSocket and MCP |
| `ndmspc-run` | Run a macro over N workers (`thread`, `ipc`, `tcp`) |
| `ndmspc-worker` | A worker that attaches to an IPC/TCP supervisor |
| `ndmspc-mcp` | MCP server over stdin/stdout |
| `ndmspc-ws-client` | WebSocket client for scripts (OIDC or X509) |
| `ndmspc-cli` | Batch pipeline commands (`file`, `merge`, `binning`) |
| `ndmspc-tutorials` | Run the tutorial macros into the current directory |
| `ndmspc-chat` | Terminal chat client for the server |

## `ndmspc-server`

```bash
ndmspc-server [-p PORT] [--http-threads N] [-m macro1,macro2] [--rooms true]
```

- `--http-threads` (default 128) is the real client ceiling: each WebSocket holds one connection.
- `--mcp` / `--ws` are on by default; `--rooms` requires Kubernetes.
- `--write-tool-catalog <file>` writes the MCP tool list and exits (used by the image build).

The complete flag and environment table is in
[`http/README.md`](https://gitlab.com/ndmspc/ndmspc/-/blob/develop/http/README.md).

## `ndmspc-run`

```bash
ndmspc-run --mode ipc -n 4 myMacro.C
ndmspc-run --mode tcp -n 2 --spawn-workers 2 myMacro.C
ndmspc-run --mode tcp -n 2 myMacro.C      # then: ndmspc-worker -e tcp://host:5555
```

`--spawn-workers` is only valid with `--mode tcp`. `process` is an alias for `ipc`.

## `ndmspc-worker`

```bash
ndmspc-worker -e tcp://127.0.0.1:5555
ndmspc-worker -e tcp://host:5555 --index 0 --macro myMacro.C   # expert override
```

Usually started without `--index`/`--macro`: the supervisor hands both over in the bootstrap frame.

## `ndmspc-mcp`

```bash
ndmspc-mcp                                        # toolNgnt.C from $NDMSPC_DIR/macros/tools
ndmspc-mcp --rooms                                # the room router instead
ndmspc-mcp -m /path/toolNgnt.C --all-tools
```

JSON-RPC on stdout; logs go to stderr.

## `ndmspc-tutorials`

```bash
mkdir ndmspc-tutorials && cd ndmspc-tutorials
ndmspc-tutorials          # -l lists the steps
```
