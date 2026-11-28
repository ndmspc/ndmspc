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
| `ndmspc-cli` | Batch pipeline commands (`file`, `merge`, `binning`, `combo`, `stress`) and `record` (tutorial videos) |
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
ndmspc-mcp                                        # toolNgnt.C from $NDMSPC_ROOT/tools
ndmspc-mcp --rooms                                # the room router instead
ndmspc-mcp -m /path/toolNgnt.C --all-tools
```

JSON-RPC on stdout; logs go to stderr.

## `ndmspc-cli combo`

Precaches a processed NGnTree file: reshape the combinations you choose and store them **inside the file**, so
whoever opens it later gets those navigators without reshaping. Optionally their map JSON is stored too, which
makes the first map a file read instead of an export.

A processed file holds only the tree and its outputs. `combo` is what adds combinations to it:

```bash
# store two combinations, with their map JSON
ndmspc-cli combo -i data.root --levels '[[0,2],[1]];[[0,1,2]]' --map --run

# what the file offers now
ndmspc-cli combo -i data.root --list
```

- `--levels` takes several combinations separated by `;`. Each is JSON (`[[0,2],[1]]`) or the plain spelling
  `0,2:1` (`:` between layers, `,` between axes) — the same habit as `stress --axes`. Without it, every axis
  is stored in two layers.
- `--map` also stores each combination's exported JSON.
- `--refresh` rebuilds a combination that is already stored; by default a stored one is left alone, since
  rebuilding it costs the reshape it was meant to save.
- `--list` prints the stored entries as JSON and exits.

A combination that the tree cannot take (levels that miss an axis) is reported and not stored — a file is
never left advertising something it cannot serve.

**If you precache the same file from several jobs at once**, they are serialised by an advisory lock on a
`<file>.lock` sidecar: ROOT's `UPDATE` open rewrites the whole file, so two writers would otherwise drop each
other's entries while both reported success. Readers need no lock — a session opening the file is unaffected.

### From ROOT/Cling

`Ndmspc::NCombo` is a library class, so the same precache can be driven from a session:

```cpp
Ndmspc::NGnTree * t = Ndmspc::NGnTree::Open("data.root");
std::vector<Ndmspc::NCombo::Result> results;
Ndmspc::NCombo::Store(t, {{{0, 2}, {1}}, {{0, 1, 2}}}, "", /*withMap=*/true, /*refresh=*/false, results);
delete t;
```

## `ndmspc-cli stress`

Builds a big NGnTree out of one source file and measures reshape on it, with and without the cache — for
sizing a real analysis, or for checking that the cache is worth its keep on your data.

```bash
# build a grid of 10 x 20 copies of data.root, then measure
ndmspc-cli stress --source data.root --axes a1=10,a2=20 --workdir /tmp/stress --run
```

The grid is a directory per bin holding a real copy, which the directory importer turns back into a tree.
The directories are only bin labels: the axis names and their order come from the copy tree, so
`--axes a2=2,a1=3` is what decides that `a2` is the first axis. The copies are made by `NGnTree::Process`,
so `--mode`/`-n` select how they are spread (`ipc`, `tcp`, `thread`) like everywhere else.

`--source` may be omitted, in which case a synthetic tree is generated (`--bins`, `--entries`).

`--axes` takes `:` between axes and `,` between labels, so a name and a count can be mixed freely:

```bash
--axes a1=label1,label2:a2=2:a3=alpha,beta:a4=2
```

A bare number is a **count** of numbered bins (`a4=2` is `1,2`); anything else is a label, which becomes the
directory name. The older single-comma form (`a1=x,a2=2`) is still accepted, since existing commands use it.

A ready-made source for stressing reshape is `NCpuStress.C`, whose `nParamCopies` argument widens each
point's `_params` branch. That is the knob that matters: reshape disables the `_outputPoint` branch
(`NGnNavigator.cxx:472`) and reads `_params` bin by bin, so extra objects per point cost nothing while
extra parameters cost a great deal. Measured here, one 64-point source:

| `nParamCopies` | reshape |
| --- | --- |
| 0 | 0.004 s |
| 500 | 0.36 s |
| 2000 | 4.1 s |

```bash
ndmspc-run -n 22 tutorials/99_stress/NCpuStress.C --macro-params '"stress.root",20000,1,1,8,8,2000'
```

### What it measures

| phase | what it is |
| --- | --- |
| `cold cache-off` | the honest baseline: `NDMSPC_RESHAPE_CACHE=0` |
| `cold cache-on` | the first build plus what storing it costs (runs once, so the median cannot hide it) |
| `warm cache-hit` | the same combination again — what a revisit costs |

A phase that cannot reshape is reported as having **no timing** rather than as a fast one: a failed
reshape returns instantly, and would otherwise read as an enormous speed-up.

### Several runs, one tree

`--out-results` writes the runs as an ordinary NGnTree — one point per run and phase, with `seconds` and
`ratio` parameters and a readable log per point. Measuring two storages therefore gives two runs in one
tree, which can be reshaped and drawn like any other:

```bash
ndmspc-cli stress --axes a1=10,a2=20 --workdir /tmp/stress --label local --out-results local.root --run
ndmspc-cli stress --axes a1=10,a2=20 --workdir /eos/stress --label eos  --out-results eos.root   --run
```

### From ROOT/Cling

`Ndmspc::NStress` is a library class, so the same measurement can be driven from a session and the runs
compared there:

```cpp
Ndmspc::NStress s;
s.SetWorkdir("/tmp/stress");
s.SetAxes("a1=10,a2=20");
s.SetSource("data.root");     // omit to generate
s.Run("local");
s.SetWorkdir("/eos/stress");  // same measurement, another storage
s.Run("eos");
s.GetResults("both.root");    // both runs, one tree
```

A note on cold measurements: entries are stored inside the copied files, so they survive between runs. A
`cold` phase on a workdir that was measured before is really a cache hit, and the tool says so when it
detects it — rebuild the tree for a true baseline.

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

