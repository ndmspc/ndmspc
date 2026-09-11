# NDMSPC Core

`core/` builds the `NdmspcCore` library: the n-dimensional binned storage
(`NGnTree`, `NBinning`), point navigation and projection (`NGnNavigator`,
`NBinningPoint`), parameters (`NParameters`), executors and utilities. See
`include/ndmspc/core/*.h` for the public API (installed headers) and the
[Doxygen documentation](../doc) for details.

## Parameter averaging on export

`NGnNavigator::ExportToJson()` writes the navigator as a nested JSON structure.
At the deepest level each parameter carries `values` and `errors`; at every
higher level the exporter also writes, per projection cell, the mean of the
corresponding deeper-level cell's values and the error of that mean:

```
values[i] = (1/N) * sum(v_k)          over the non-zero entries of child i
errors[i] = sum(sigma_k^2) / N^2      variance of the unweighted mean
```

`errors` are stored as variances (matching the leaf `errors`, i.e. `fSumw2`), and
the average is propagated recursively up to the root.

Averaging is **on by default**. It can be turned off for large navigators, which
avoids the extra JSON size and CPU cost; higher levels then keep only the
aggregated `min`/`max`/`minE`/`maxE` (which the viewer falls back to).

Precedence, highest first:

1. `cfg["averages"]` passed to `ExportToJson()` / `Export()` — per export/request.
2. `NGnNavigator::SetAverageParameters(bool)` — per navigator tree; applies to all
   levels (descendants are updated too, and children created during `Reshape` inherit
   the parent's setting).
3. `NDMSPC_EXPORT_AVERAGES` environment variable — global default for newly
   constructed navigators (`1/true/yes/on` enable, anything else disables).

Example:

```cpp
// Default: averaging enabled
nav->Export("nested.json", {});

// Disable for a large navigator
nav->SetAverageParameters(false);
nav->Export("nested.json", {});

// One-off override via the export config
json cfg;
cfg["averages"] = false;
nav->Export("nested.json", {}, cfgFile);   // cfgFile contains {"averages": false}

// Global opt-out
// $ export NDMSPC_EXPORT_AVERAGES=0
```

When served over HTTP, `/api/ngnt/map` accepts an `averages` boolean in the
request (and exposes it as a `map` workspace property), so a client can disable
averaging for a large navigator without changing server code.
