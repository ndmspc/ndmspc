# Merging + analysis test — how to run

Pipeline in this directory:
1. `file` — ROOT files -> split NGnTree (with per-object chunking),
2. `merge` — split NGnTree -> per-object / per-chunk merged results,
3. `binning` — selected objects + several binnings -> per-bin projections in an NGnTree.

Existing results from earlier runs (`.ndmspc/merge`, `merged_ngnt*`) are not touched; this test
uses `.ndmspc/chunktest`, `chunk_merged*` and `binning_ngnt.root`.

## Files
| File | Purpose |
| --- | --- |
| `run.sh` | runs the three steps from scratch |
| `NFileToNGnTree.json` | step 1: split output + chunk rule |
| `NGnTreeMerge.json` | step 2: objects + exclusions |
| `NGnTreeBinning.json` | step 3: axes, binnings, projection axis |
| `files.txt` | list of input `AnalysisResults.root` files |

## Run everything (resume by default)
```bash
cd /var/home/mvala/MVALA/Work/git/gitlab.com/ndmspc/ndmspc/macros/ndmspc/physics/rsn
./run.sh                # continue; console logging follows NDMSPC_LOG_RUN
./run.sh -v             # continue with console logging (verbose)
./run.sh reset          # start everything from scratch
./run.sh reset-file     # redo only the file step (split output)
./run.sh reset-merge    # redo only the merge step (partials + combined file)
./run.sh -v reset       # combine options in any order
```

By default every config runs with `"processes": 1` and `"mode": "ipc"` (fork-based local
workers).

`run.sh` is (it assumes `ndmspc-cli` is in `PATH` and the ndmspc/ROOT environment is already
set up):
```bash
VERBOSE=false
RESET=""
for arg in "$@"; do
  case "$arg" in
    -v|--verbose) VERBOSE=true ;;
    reset|reset-file|reset-merge) RESET="$arg" ;;
    *) echo "usage: $0 [-v|--verbose] [reset|reset-file|reset-merge]" >&2; exit 2 ;;
  esac
done

FILE_ARGS=()
MERGE_ARGS=()
case "$RESET" in
  reset)       rm -rf .ndmspc/chunktest chunk_merging_ngnt.root chunk_merged* binning_ngnt.root
               FILE_ARGS=(--reset); MERGE_ARGS=(--reset) ;;
  reset-file)  rm -rf .ndmspc/chunktest chunk_merging_ngnt.root
               FILE_ARGS=(--reset) ;;
  reset-merge) rm -rf chunk_merged*
               MERGE_ARGS=(--reset) ;;
esac

V=()
if [ "$VERBOSE" = true ]; then V=(-v); fi

ndmspc-cli file    -c NFileToNGnTree.json -f files.txt --run "${FILE_ARGS[@]}" "${V[@]}"
ndmspc-cli merge   -c NGnTreeMerge.json --run "${MERGE_ARGS[@]}" "${V[@]}"
ndmspc-cli binning -c NGnTreeBinning.json --run "${V[@]}"
```

`file` skips inputs whose split entry is already complete (`.done.json` inside each entry
directory); the entry is redone when the input, the objects/chunk configuration, or one of its
files changes. `merge` skips complete partials. `reset-file`/`reset-merge` delete and redo only
that step; the other steps continue normally (the binning output is always rewritten).

## Run the steps separately
```bash
cd /var/home/mvala/MVALA/Work/git/gitlab.com/ndmspc/ndmspc/macros/ndmspc/physics/rsn

# 1. ROOT files -> split NGnTree
ndmspc-cli file -c NFileToNGnTree.json -f files.txt --run -v

# 2. split NGnTree -> partials + per-chunk outputs
ndmspc-cli merge -c NGnTreeMerge.json --run -v

# 3. objects + binnings -> per-bin projections
ndmspc-cli binning -c NGnTreeBinning.json --run -v

# dry run (prints what would happen, writes nothing): omit --run
```

## TCP workers
Each step's config owns its execution settings: `processes` is the number of workers, and
`"mode": "tcp"` makes the step wait for workers started elsewhere — the supervisor spawns none.
The port is not a config key: set `NDMSPC_TCP_PORT` in the environment (default 5555).

```bash
# on the worker machine(s), pointing at the supervisor and the same port
ndmspc-worker -e tcp://<supervisor-host>:5555

# on the supervisor, with "mode": "tcp" and "processes": 4 in the step's config
NDMSPC_TCP_PORT=5555 ndmspc-cli merge -c NGnTreeMerge.json --run -v
NDMSPC_TCP_PORT=5555 ndmspc-cli binning -c NGnTreeBinning.json --run -v
```

`processes` is the maximum number of worker slots, not a required count: the run starts when the
first worker connects and uses the others as they join.

There is **no local fallback** in TCP mode: with no worker connected the run waits
`NDMSPC_WORKER_TIMEOUT` seconds (default 300) and then fails with
`failed to start workers: Timeout: no TCP workers connected.` Use
`NDMSPC_WORKER_TIMEOUT=30 ndmspc-cli merge ...` to fail fast, or leave the configs at their
defaults (`ipc`, 1 process) when no worker is available.

## What the configs do
`NFileToNGnTree.json`
- `objects: ["phianalysis-t-hn-sparse_tpctof/*"]`
- `splitBase: ".ndmspc/chunktest"`
- chunk rule: only `phianalysis-t-hn-sparse_tpctof/rotation` is chunked, along axis 2 into
  4 chunks (`"axes": [{"axis": 0, "bins": 4}]`, `"chunkMinBytes": 0` in the rule).
  Only `THnSparse` objects are chunked; `TH1/2/3`, `TProfile`, `TEfficiency`, `TList` stay as-is.

`NGnTreeMerge.json`
- `inputFile: ".ndmspc/chunktest"` (split input auto-detected from `manifest.json`)
- `objects: ["phianalysis-t-hn-sparse_tpctof/*"]`, `excludeObjects: [".../unlikemp"]`
- chunked objects keep one file per chunk (`combineChunks: false`)

`NGnTreeBinning.json`
- `inputFile: ".ndmspc/chunktest"` — the project step reads the split output directly and adds
  the chunks / per-input files of each object (cached per worker)
- `axes`: `pt` (source `axis1-pt`) and `ce` (source `axis2-ce`)
- `binnings.default`: width groups per axis, as in `NAliRsnStep1.C`
- `objects`: `phianalysis-t-hn-sparse_tpctof/unlikepm` (only `THnSparse` objects are used;
  empty objects such as `unlikemp` in this sample simply give empty projections)
- `projectionAxis: 0` — the invariant-mass axis
- for every bin of every definition: axis ranges are set, each object is projected onto
  `projectionAxis`, and the projection is stored in the entry's `_outputPoint`; the object's
  entries are stored as a parameter in `_params`.

## Expected output
```
.ndmspc/chunktest/<inputId>/AnalysisResults.root             # unchunked objects
.ndmspc/chunktest/<inputId>/1/... (2/3/4)                   # chunk 1..4 of rotation
.ndmspc/chunktest/manifest.json

chunk_merged.<object>.root                                   # unchunked objects (minus excluded)
chunk_merged.phianalysis-...rotation.chunk_1.root ...        # one partial per chunk
chunk_merged.phianalysis-...rotation/chunk_1.root ...        # per-chunk output files
chunk_merged.chunks.json                                     # chunk -> file mapping

binning_ngnt.root                                            # one entry per bin
  ngnt tree: _outputPoint = projections per object, _params = per-object entries
```

## Common changes
- **Exclusions at merge**: edit `excludeObjects` in `NGnTreeMerge.json` or use
  `--exclude-objects 'a/b, c/*'`.
- **No blacklist**: `ndmspc-cli merge -c NGnTreeMerge.json --run -v -j '{"excludeObjects": []}'`.
- **One object from its chunks**: add `--combine-chunks` to the merge.
- **Different binnings for the analysis**: edit `binnings` in `NGnTreeBinning.json`
  (`{"pt": [[width,count],...], "ce": [...]}`); add more definitions as extra keys.
- **Different axes / projection**: edit `axes` (`name` + source axis name/index) and
  `projectionAxis` (or `projectionName`).
- **Chunk another object**: edit the `chunkAxes` rule in `NFileToNGnTree.json`; re-run `file`.
- **Plain ROOT file input** for `binning`: set `inputFile` to the file and `objectDirectory` to
  the directory holding the objects (e.g. `merged.Data.sparse.root` + `"Data"`).

## Notes
- Chunk labels are the bin ordinal in the axis definition (`1`..`4`; `1_2` for two chunk axes).
- `binning` runs all binning definitions in one `NGnTree::Process`; bins shared by definitions
  are processed once.
- A fresh run is independent: `./run.sh reset` (or delete `.ndmspc/chunktest`, `chunk_merged*`
  and `binning_ngnt.root`) starts over.
