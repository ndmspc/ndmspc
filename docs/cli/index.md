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
| `ndmspc-cli` | Batch pipeline commands (`file`, `merge`, `binning`) and `record` (tutorial videos) |
| `ndmspc-tutorials` | Run the tutorial macros into the current directory |
| `ndmspc-chat` | Terminal chat client for the server |

## `ndmspc-server`

```bash
ndmspc-server [-p PORT] [--http-threads N] [-m macro1,macro2] [--rooms true]
```

- `--http-threads` (default 128) is the real client ceiling: each WebSocket holds one connection.
- `--mcp` / `--ws` are on by default; `--file-url` (naming a stored file by URL for exported macros
  and previews) is on by default too; `--rooms` requires Kubernetes.
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

## `ndmspc-cli record`

Records the **tool tours** declared in the tool macros (`toolXXX.C`) as tutorial videos, without the
tour overlay. It replays the same actions the tour describes: the tour itself only instructs, so
`record` performs the clicks, typing and **Save & Run** presses.

Start a server with the tools loaded (or use a room URL):

```bash
ndmspc-server -m toolNgnt.C,toolBrowser.C
```

Then:

```bash
ndmspc-cli record --list
ndmspc-cli record --tours "Project a THnSparse (cernstaff.root)" --out ./videos
ndmspc-cli record --tours "Browse cernstaff.root from Storage" --out ./videos
```

`--url` defaults to `http://localhost:8080`; point it at a room when the tour uses room-only anchors
(Storage): `--url 'http://host:8009/?room=tutorial&token=…'`.

### Prerequisites — not shipped by ndmspc

The recording step needs:

- **Node.js 20+** (`node --version`)
- **agent-browser 0.41+**: `npm install -g agent-browser`
- **ffmpeg/ffprobe** (optional): used for the resolution check and the review keyframes. Without
  them the video is still recorded; the review prompt says they were skipped.

`ndmspc-cli record` checks these before it opens a browser and prints the install command if one is
missing. It never installs anything. `--list`, `--prompt` and `--review latest` work without Node.

The recorder itself is installed under `share/ndmspc/scripts/js/record` (source:
`scripts/record`); `NDMSPC_RECORD_SCRIPT` overrides the path.

### Example session

```console
$ ndmspc-cli record --url 'http://host:8009/?room=tutorial&token=…' --list
ndmspc/browser/browse#0      Browse cernstaff.root from Storage
ndmspc/browser/browse#1      Project a THnSparse (cernstaff.root)
ndmspc/browser/browse#2      Import a THnSparse (cernstaff.root) to NGnTree (ngnt)

$ ndmspc-cli record --url 'http://host:8009/?room=tutorial&token=…' \
    --tours "Project a THnSparse (cernstaff.root)" --out ./tutorial-media
ndmspc-cli record: recording Project a THnSparse (cernstaff.root) (ndmspc/browser/browse#1)
Recorded ./tutorial-media/project-a-thnsparse-cernstaff-root.webm
Review:   ./tutorial-media/project-a-thnsparse-cernstaff-root.review.md
Report:   ./tutorial-media/project-a-thnsparse-cernstaff-root.report.json
Frames:   14
```

The clip is recorded at 1920×1080. `report.json` has per-stage timings and any failed waits;
`review.md` carries the checklist, the keyframes and the command to run again.

### Other options

```bash
ndmspc-cli record --prompt "Import a THnSparse (cernstaff.root) to NGnTree (ngnt)" > prompt.md
ndmspc-cli record --review latest
ndmspc-cli record --dry-run --tours "Project a THnSparse (cernstaff.root)"
```

After each recording, `<out>/<slug>.webm`, `<slug>.png`, `<slug>.review.md`, `<slug>.report.json`
and `<slug>.frames/` are written. The review prompt lists the stages, timings, keyframes, a
checklist, and the exact command to run again.

Scope: only tours a tool declares; the room/workflow tour is not recorded. Tour visibility is
suppressed by setting `ndmspc:tour:1:done` in `localStorage` before the page loads, so no grey tour
card appears in the video.

Exit codes: `2` missing Node/agent-browser, `3` unknown tour, `4` recording failed.

## `ndmspc-tutorials`

```bash
mkdir ndmspc-tutorials && cd ndmspc-tutorials
ndmspc-tutorials          # -l lists the steps
```

