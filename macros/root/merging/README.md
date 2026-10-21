# Merging (NGnTree)

The two macros here are **thin wrappers**: the implementation lives in the library as
`Ndmspc::NUtils::FileToNGnTree()` and `Ndmspc::NUtils::NGnTreeMerge()` (`core/NUtils.{h,cxx}`), so
the project must be built and installed (`toolbox run -c fedora-toolbox-44 -- bash -lc 'cd ndmspc
&& ./scripts/make.sh install'`) before running them.

Both functions take the configuration as a **`json` object**. Each macro builds its default
configuration **as a visible json object at the top of the macro** (the same values as
`Ndmspc::NFileToNGnTreeDefaults` / `Ndmspc::NGnTreeMergeDefaults`, which stay available for library
users); a config path given to the macro is loaded over those values, and an empty path (`""`) uses
the visible defaults alone.

```cpp
json cfg = {
    {"outFile", "merging_ngnt.root"},
    {"files", json::array()},
    {"objects", json::array({"eventselection-run3", "phianalysis-t-hn-sparse_tpctof/*"})},
};
// library form: json cfg = Ndmspc::NFileToNGnTreeDefaults;
Ndmspc::NUtils::FileToNGnTree(cfg, "", "", "", macroDir);
```

`NFileToNGnTree` builds a 1D `NGnTree` whose single axis is **categorical** — one bin per input
file, labelled with the file name. Its process function opens the file named by each bin and writes
the selected objects into the output tree, one branch per object (one entry per file).
`NGnTreeMerge` then merges those objects (see below).

## Files

- `NFileToNGnTree.C` — thin macro wrapper for `NUtils::FileToNGnTree()`.
- `NFileToNGnTree.json` — configuration consumed by it.
- `NGnTreeMerge.C` — thin macro wrapper for `NUtils::NGnTreeMerge()`.
- `NGnTreeMerge.json` — configuration consumed by it.

## Configuration

```json
{
  "outFile": "merging_ngnt.root",
  "files": [],
  "objects": ["eventselection-run3", "phianalysis-t-hn-sparse_tpctof"]
}
```

- `files[]` — input files; order defines the axis bin order. Entries may use `*`/`?`
  wildcards (e.g. `"data/*.root"`), expanded against the filesystem and sorted. May be
  left empty when the file list is supplied by a text file (see below).
- `objects[]` — objects to look up per file. Each entry is a wildcard pattern:
  - `"*"` — every object in the file (recursively);
  - `"Data/*"` — every object under the `Data/` directory (`*` = any chars, `?` = one char);
  - a plain name/path (`"hSignal"`, `"Data/hBg"`) selects just that object.
  A plain name that is absent is still reported as `NOT FOUND`.
- `outFile` — NGnTree output file, taken relative to the current directory by default
  (used when the macro's second argument is empty).
- `split` / `splitBase` — **enabled by default**: each input file's objects are written to its own
  small file at `<splitBase>/<id>/<input file>` (default base `.ndmspc/merge`, relative to the
  current directory), together with a `manifest.json`; the aggregate `outFile` then holds no
  object branches. Set `"split": false` (CLI: `--no-split`) to write one aggregate tree instead.
- `reset` — **off by default**: a rerun **continues** an interrupted run by skipping inputs whose
  split output is already complete. Completion is recorded per input in
  `<splitBase>/<id>/.done.json` (input path, size/mtime stamp, the objects/chunk configuration and
  the files written); the entry is redone when any of those differ or when one of its files is
  missing. `"reset": true` (CLI: `--reset`) removes each entry's directory and redoes it from
  scratch. Resume applies to the split output; with `"split": false` the aggregate tree is
  rewritten on every run.
- `chunkAxes` — split heavy `THnSparse` objects into coarse sub-histograms along selected axes.
  All axes are kept, so the chunks sum back to the original. The axis binning can be given like
  any binning definition (no mapping histogram is filled):
  ```json
  [{"axis": 0, "bins": 4}]                          // 4 equal ranges
  [{"axis": 0, "bins": [1, 26, 51, 76, 101]}]       // explicit 1-based bin edges
  [{"axis": 0, "widths": [[25, 4]]}]                // width groups (as in a binning definition)
  [{"axis": 0, "bins": ["25_1_1", "25_1_2"]}]       // explicit <rebin>_<start>_<id>
  [{"axis": 0, "rebin": 25, "start": 1, "ids": [1, 2]}]
  [{"axis": 0, "groups": [                          // mixed rebin groups (like a binning)
      {"rebin": 25, "start": 1,  "ids": [1]},
      {"rebin": 10, "start": 26, "ids": [1]},
      {"rebin": 15, "start": 36, "ids": [1]}]}]
  ```
  `chunkAxes` can also be a list of **per-object rules**, first match wins (same glob syntax;
  a rule without `objects` is the default):
  ```json
  [
    {"objects": ["phianalysis-t-hn-sparse_tpctof/rotation"], "axes": [{"axis": 0, "groups": [...]}], "chunkMinBytes": 0},
    {"objects": ["*"], "axes": [{"axis": 0, "bins": 2}]}
  ]
  ```
  Rules match the source path (`a/b`), the branch (`a.b`) or the leaf name. `chunkMinBytes`
  inside a rule overrides the global one for that object.
  A chunk label is the bin's **ordinal in the axis definition** (1..K), in spec order (`1`, `1_2`
  for two axes); the `{rebin, start, id}` descriptor is recovered from the axis spec
  (`CreateChunkBinning`). The third element is the bin index inside that rebin group, while the
  label id is unique across mixed groups. To get wider chunks, increase `rebin` (or use fewer
  `bins`). Only objects at least `chunkMinBytes` (default 64 MB, `0` = every sparse) are chunked.
  Each chunk is written to `<splitBase>/<inputId>/<label>/<object>.root` and recorded in the
  manifest.

Relative `files[]` entries are resolved against the macro directory if not found in the
current working directory. A bare file wildcard is expanded in the current working
directory first, then the macro directory.

## Text file list

An optional third argument points to a plain-text file with one input file path per line.
It **overrides** the config's `files[]`. Blank lines and `#` comments are ignored, and
entries may use wildcards:

```text
# one file per line
/data/run1/hist.root
/data/run2/hist.root
/data/run*/*.root
```

## Extra objects on the command line

An optional fourth argument is a comma-separated list of objects (wildcards allowed). Its
entries are **appended** to the config's `objects[]` (duplicates skipped), so a default
config can be extended without editing it:

```bash
root -l -q 'NFileToNGnTree.C("NFileToNGnTree.json", "", "~/merge.txt", "phianalysis-t-hn-sparse_tpctof/*")'
```

## Output tree

The output `ngnt` tree holds **one entry per input file** and **one branch per object**,
written for every entry:

- Branch names keep the file's directory structure using ROOT's dot notation
  (`phianalysis-t-hn-sparse_tpctof.unlikepm`, `…QA.Event.hVtxZ`).
- Every ancestor directory on a matched path is also a branch, because ROOT resolves a
  dotted name through its parent branch. A directory branch carries a **`TList` of that
  directory's children** (each a `TNamed` with the child's name and class) rather than the
  directory object: a `TDirectoryFile` read from an input file keeps references into that
  file, so storing it and reading it back seeks to the wrong offsets (bogus multi-GB reads,
  `std::bad_alloc`).
- Each entry also stores a log (`NLogString` under `_outputPoint`), which is what makes the
  framework fill the entry.
- An object missing from a given file leaves its branch empty for that entry (a warning is
  logged).
- The output holds the objects of **all** files, so it can be large (the sample
  `AnalysisResults.root` set produces a ~2 GB tree) — pick `objects` accordingly.

## Usage

```bash
root -l -q 'NFileToNGnTree.C'
root -l -q 'NFileToNGnTree.C("NFileToNGnTree.json")'
root -l -q 'NFileToNGnTree.C("NFileToNGnTree.json", "other_ngnt.root")'
root -l -q 'NFileToNGnTree.C("NFileToNGnTree.json", "", "~/merge.txt")'
root -l -q 'NFileToNGnTree.C("NFileToNGnTree.json", "", "~/merge.txt", "eventselection-run3/*,phianalysis-t-hn-sparse_tpctof/*")'
```

Arguments: `NFileToNGnTree(cfgFile, outFile, filesTxt, objectsCsv)` — `outFile` (when non-empty)
wins over the config's `outFile`; `filesTxt` (when non-empty) wins over the config's
`files`; `objectsCsv` (when non-empty) is appended to the config's `objects`. `cfgFile` (default
`""`) — empty uses the visible defaults as they are; a path (e.g. `"NFileToNGnTree.json"`) is
resolved relative to the run location and then to the macro directory, and merged over them.

## Merge step: `NGnTreeMerge.C`

Takes the tree produced above (`merging_ngnt.root`, bins = input files, one branch per
object) and merges each requested object across **all** entries.

**One NGnTree process with the object names as its axis** — each bin merges one object and
writes that object's partial directly, so no result is kept in memory after its bin and the
final file is just the combination of the partials. With several processes configured, the
bins (objects) run in parallel; each worker holds at most one merged object at a time.

Progress is printed up front and per object/file — the file name comes from the
`_outputPoint` log that `NFileToNGnTree` stored for each entry, and every line carries the
object's `[X/Y]`:

```
NGnTreeMerge: 2 object(s) requested:
NGnTreeMerge:   'phianalysis-t-hn-sparse_tpctof.unlikepm'
NGnTreeMerge:   'phianalysis-t-hn-sparse_tpctof.unlikemp'
NGnTreeMerge: 2 found, 0 missing - to process:
NGnTreeMerge:   [1/2] 'phianalysis-t-hn-sparse_tpctof.unlikepm' [THnSparseT<TArrayF>]
NGnTreeMerge:   [2/2] 'phianalysis-t-hn-sparse_tpctof.unlikemp' [THnSparseT<TArrayF>]
NGnTreeMerge: [1/2] run for '…unlikepm' [THnBase] -> 'merged_ngnt.…unlikepm.root'
NGnTreeMerge: [1/2] '…unlikepm' file 1/7: /data/.../AnalysisResults.root
...
NGnTreeMerge: [1/2] '…unlikepm' merged 7/7 file(s) [THnBase] in 93.2 s
NGnTreeMerge: [1/2] done '…unlikepm' -> 'merged_ngnt.…unlikepm.root'
```
Requested names that are not in the input are reported (`branch '…' not found … - skipped`)
before the found/missing summary.

`NGnTreeMerge.json`:
```json
{
  "inputFile": ".ndmspc/merge",
  "outFile": "merged_ngnt.root",
  "objects": ["*"],
  "excludeObjects": [],
  "mergeTypes": ["TH1", "TH2", "TH3", "TProfile", "THnSparse", "TEfficiency"],
  "onUnsupported": "keepFirst",
  "force": false,
  "reset": false,
  "combine": true,
  "processes": 2,
  "mode": "ipc",
  "port": 5555,
  "stallTimeout": 3600
}
```

```bash
root -l -q 'NGnTreeMerge.C("NGnTreeMerge.json")'
root -l -q 'NGnTreeMerge.C("NGnTreeMerge.json", "merged_ngnt.root", "hSignal,Data.h1")'
# force a fresh run: delete the requested objects' partials (and the combined file) first
root -l -q 'NGnTreeMerge.C("NGnTreeMerge.json", "", "", true)'
```

Arguments: `NGnTreeMerge(cfgFile, outFile, objectsCsv, deletePartials)` — `deletePartials` is the
4th argument and defaults to `false`; when `true` it removes `<partial>` / `<partial>.part` for the
requested objects plus the combined file before starting. `cfgFile` (default `""`) — empty uses the
visible defaults as they are; a path (e.g. `"NGnTreeMerge.json"`) is resolved relative to the run
location and then to the macro directory, and merged over them.

- `objects[]` are the **branch names** from the input tree (dotted, as printed by
  `NFileToNGnTree`). Entries may be patterns (`*`, `?`) matched against the branch names — `"*"`
  means **all mergeable leaf objects**, and e.g. `"phianalysis-t-hn-sparse_tpctof.*"` selects that
  family. Directory placeholders (`TList`) are skipped; a plain name that is missing is reported.
- `excludeObjects[]` — blacklist applied after matching, same syntax as `objects`. Use it to take
  a whole family and drop a few: `"objects": ["phianalysis-t-hn-sparse_tpctof/*"]` with
  `"excludeObjects": ["phianalysis-t-hn-sparse_tpctof/rotation"]`. For chunked objects the base
  name is matched as well, so excluding an object removes all of its chunks. CLI:
  `--exclude-objects name1,name2`.
- `outFile` is the **base name** of the partials: an object's result goes to
  `<outFile without .root>.<object name>.root`
  (`merged_ngnt.phianalysis-t-hn-sparse_tpctof.unlikepm.root`).
- `inputSplit` — when `inputFile` is a **split output** directory (written with `split: true`),
  set this to true (or leave it false: a `manifest.json` next to the input enables it
  automatically). The objects are then read from the per-input files listed in the manifest.
  `inputFile` defaults to `.ndmspc/merge`, the split output of the step above.
- `serializeBytes` — objects whose stored size is at least this many bytes are merged **one at
  a time** across workers, so two multi-GB merged sparses never coexist (default 64 MB, `0`
  disables). Light objects still run in parallel.
- `combineChunks` — for chunked objects, keep one file per chunk under `<outFile>.<object>/`
  (default) or add the chunks back into one `THnSparse` (`true`, CLI: `--combine-chunks`). A
  `<outFile>.chunks.json` sidecar records the chunk → file mapping.
- Inside a partial the **source file's directory structure is restored**: the dotted name is
  turned back into directories (`phianalysis-t-hn-sparse_tpctof.unlikepm` →
  `phianalysis-t-hn-sparse_tpctof/unlikepm`), so the result looks like the original file.
- After all objects are done, the partials are **combined** into one `merged_ngnt.root` (the
  `outFile`), with the same directory layout. The partials are **kept** — they are the
  resumable per-object results. Set `"combine": false` to skip the combined file.
- `processes` / `mode` / `port` control the execution: `processes` workers (`NDMSPC_MAX_PROCESSES`),
  transport `ipc` (default) or `tcp` (binds `NDMSPC_TCP_PORT` from `port`). The object bins run in
  parallel; each worker holds one merged object at a time because the partial is written as soon as
  the object is done.

```cpp
TFile * f = TFile::Open("merged_ngnt.phianalysis-t-hn-sparse_tpctof.unlikepm.root");
THnSparse * hs = (THnSparse *)f->Get("phianalysis-t-hn-sparse_tpctof/unlikepm");
```

### Merge strategies — `mergeTypes`
The list decides *how* a type is merged (matched in order; a class matches an entry when it is
that class or derives from it, so `TH1D`/`TH2D`/`TProfile` → `TH1`):

| configured type (and derived) | operation |
| --- | --- |
| `TH1` (TH1D/TH1F, TH2, TH3, TProfile, …) | `TH1::Add(const TH1*)` |
| `THnBase` / `THnSparse` | `THnBase::Add(const THnBase*)` |
| `TEfficiency` | `TEfficiency::Add(const TEfficiency&)` |
| `TList` | merge by name (existing entry merged, new entry cloned) |

Default when `mergeTypes` is absent: `["TH1", "THnBase"]`.

### Types outside the list — `onUnsupported`
- `"keepFirst"` (default) — run anyway, keep the first entry, warn;
- `"skip"` — no run and no partial, warn;
- `"fail"` — count the object as failed, no partial.

### Resume — `force` and provenance
A partial counts as complete only if it exists, holds the object at its restored path, and its
`provenance` key matches the current input (path, size, mtime) and object name. Complete
partials are **skipped**, so a re-run only redoes what is missing (or what changed). Delete a
partial, change the input, or set `"force": true` to redo it. The temporary `<partial>.part`
file is removed after each run; a failed run leaves neither a partial nor a temp file.

The `provenance` marker lives in the **partials only** — it is what the resume check reads, and
the combined `merged_ngnt.root` contains just the merged objects.

A rerun where every requested object is already complete skips the merge phase and, when the
combined output is newer than all partials, also the combine stage. Re-running the `file` step
does not rewrite an unchanged `manifest.json` (its stamp is the merge input stamp), so it does
not invalidate the partials; a changed input or chunk/object configuration does.

`"reset": true` is the "start from scratch" form (CLI `--reset`, or the macro's 4th argument):
it removes the requested objects' partials and the combined file before running. `"force": true`
keeps the existing files and redoes only complete partials (useful when the merge strategy
changed but the inputs did not).

- Large merges take minutes; the IPC watchdog is raised by default (`stallTimeout`,
  seconds) so they do not abort with "No IPC/TCP ACK progress". Set
  `NDMSPC_IPC_STALL_TIMEOUT` in the environment to override.
