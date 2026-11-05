# Tutorial macros

Eight lessons under `tutorial/`, each a self-contained ROOT macro. Run any of them from a writable
directory with a configured NDMSPC:

```bash
root -l -b -q $NDMSPC_DIR/tutorial/01_executor/NExecutor1D.C
```

| Directory | Macro | What it shows |
| --- | --- | --- |
| `01_executor` | `NExecutor1D.C`, `NExecutor2D.C`, `NExecutor5D.C` | Axes, binning definitions, the process function |
| `02_storage` | `NStorage01Gaus.C` | Filling every point with objects |
| `03_parameter` | `NParameter01Gaus.C` | Storing fit results as per-point parameters |
| `04_customization` | `NCustomization01Gaus.C` | Passing a JSON configuration into the process function |
| `05_single_bining` | `NSingleBinning01Gaus.C` | The fully parameterised example (3 axes → 150 points) |
| `06_binnings` | `NBinnings01Gaus.C`, `NBinnings01GausStress.C` | Several binning definitions in one run |
| `07_nested_processing` | `NNestedProcessing01Gaus.C` | One tree per point, folded back with `ImportNgnt` |
| `08_systematics` | `NSystematics01Gaus.C`, `NSystematics02GausCfg.C` | Reference vs variations, driven by `sys.json` |
| `99_stress` | `NCpuStress.C` | Load testing the executor |

The step-by-step walkthrough for every lesson — setup, expected output and exercises — is the
[Tutorial section of the user documentation](https://ndmspc.gitlab.io/docs/ndmspc/quickstart/).

## Generate all outputs at once

The installed helper runs the lessons and writes their files into the current directory:

```bash
mkdir ndmspc-tutorials && cd ndmspc-tutorials
ndmspc-tutorials          # -l lists the steps without running them
```

## Running with workers

```bash
NDMSPC_MAX_PROCESSES=4 root -l -b -q $NDMSPC_DIR/tutorial/06_binnings/NBinnings01Gaus.C
ndmspc-run --mode ipc -n 4 $NDMSPC_DIR/tutorial/06_binnings/NBinnings01Gaus.C
ndmspc-run --mode tcp -n 2 NBinnings01Gaus.C      # then: ndmspc-worker -e tcp://host:5555
```

See [Execution modes](https://ndmspc.gitlab.io/docs/ndmspc/userguide/execution-modes/) for the
environment variables and the worker protocol.
