
## Project Structure

```
ndmspc/
├── base/          # Foundation layer - utilities and base classes
├── core/          # Main analysis engine - n-dimensional operations
├── hep/           # High-Energy Physics specific tools
├── http/          # Web interface and server components
├── test/          # Comprehensive test suite
├── ci/            # Continuous Integration configuration
├── cmake/         # CMake build configuration files
├── doc/           # Documentation (Doxygen)
├── scripts/       # Build and deployment scripts
└── notes/         # Development notes and examples
```

## Core Components

### 1. Base Module (`base/`)

The foundation layer providing essential utilities:

- **NHttpRequest** - HTTP request handling
- **NWsClient** / **NWsClientInfo** - WebSocket client functionality
- **NLogger** - Logging infrastructure
- **NUtils** - General utility functions

### 2. Core Module (`core/`)

The heart of the framework, implementing n-dimensional analysis capabilities:

#### Execution Framework

- **NDimensionalExecutor** - Multi-dimensional operations executor
- **NThreadData** / **NGnThreadData** - Thread-safe data structures for parallel processing

#### Binning System

- **NBinning** - Core binning implementation
- **NBinningDef** - Binning definitions and configurations
- **NBinningPoint** - Individual points in n-dimensional space

#### Navigation and Storage

- **NGnNavigator** - Navigate through n-dimensional spaces
- **NGnTree** - Generic tree structure for n-dimensional data
- **NStorageTree** - Data storage implementation
- **NTreeBranch** - Individual branches in tree structures

#### Configuration and Monitoring

- **NParameters** - Parameter management system
- **NResourceMonitor** - Performance and resource monitoring

### 3. HEP Module (`hep/`)

High-Energy Physics specific analysis tools:

- **NAnalysisFunctions** - Common HEP analysis functions
- **NAnalysisUtils** - Utility functions for particle physics analysis

### 4. HTTP Module (`http/`)

Web-based interface and visualization. The sources are grouped by feature:

```
http/
  auth/       OIDC (Keycloak) and X509 client-certificate authentication, token clients
  room/       the room router and its client: NRoomRouter, NRoomClient, NRoomSession
  mcp/        the MCP endpoint: NMcpServer
  ngnt/       the ngnt server pieces: NWorkspace, NHistoryEntry, NRouteContext,
              NSchemaBuilder
  server/     the HTTP/WebSocket layer: NHttpServer (engine, workspace, handler map, MCP,
              rooms), NHttpRequest, NWsHandler, NWsClient, NWsClientInfo
  cli/        the executables: ndmspc-server, ndmspc-mcp, ndmspc-ws-client
  tui/        the room TUI: ndmspc-room-tui
  examples/   runnable examples (room, mcp, x509-mtls)
```

The directories are a source layout only: every header still installs flat into `include/ndmspc/http/`,
so a file in `http/room/` is included exactly as before (`#include "ndmspc/http/NRoomRouter.h"`) and no
macro or out-of-tree consumer has to change. See `http/README.md` for the mechanism.

- **NHttpServer** - the HTTP/WebSocket server: engine, workspace and history, the macro handler map, MCP and the room session
- **NWsHandler** - WebSocket handler for real-time updates
- **NRoomRouter** - the room router: one Knative Service and HTTPRoute per room, on demand

#### Room router (`NRoomRouter`)

`ndmspc-server --rooms true` (or `NDMSPC_ROOMS=1`) turns a server into the always-on
entry Service of an NDMSPC deployment on Knative, serving `/api/room/*` (and the same actions as MCP
tools). It creates one Knative Service per room on demand plus an HTTPRoute matching `?room=<id>`
aimed at that room's revision, so steady-state traffic goes gateway → room and never touches the
router again. Creating a room is slow (a revision has to become ready), so `room/open` can answer at
once with `state=preparing` and leave the work to a background thread that reports its progress on
the room's registry entry; a server without the flag never touches any of this.

Unlike the macro handlers (which are user content and stay macros), this is a compiled framework
capability, like the MCP part: every rule and the whole room lifecycle are code, exercised by
`test/test_NRoomRouter.cxx`. The state it keeps — the rooms it tracks, the configuration and the
Kubernetes access — lives in the object, and the cluster is reached through `IRoomCluster`, one seam
that a test can stand in for with an in-memory cluster. Everything above it (the Service and
HTTPRoute objects, the 404-then-POST apply, the waiting, the "no capacity" verdict, adoption, the
websocket policy) is the router's own logic and is tested through the actions themselves.

## Key Features

### Multi-Dimensional Analysis

- Support for arbitrary n-dimensional spaces
- Efficient binning and navigation algorithms
- Parallel execution capabilities

### Execution Modes

`NGnTree::Process` supports three execution modes — see the
[Execution Modes](https://ndmspc.gitlab.io/docs/ndmspc/core/execution-modes/)
documentation for full details.

- **thread** (default) — ROOT IMT thread pool, single process
- **ipc / process** — fork-based local multi-process via ZeroMQ IPC
- **tcp** — distributed workers on separate machines via ZeroMQ TCP; workers
  are started with the `ndmspc-worker` binary

### Distributed TCP Processing

The `tcp` mode allows analysis to be spread across workers on different
machines.  Two binaries are involved:

| Binary | Role |
|---|---|
| `ndmspc-run` | Loads and runs a macro (supervisor). Sets execution mode, directories, and port via CLI flags. |
| `ndmspc-worker` | Connects to the supervisor, receives its configuration automatically, and processes tasks. |

#### Quick start

**Supervisor machine:**
```bash
ndmspc-run --mode tcp -n 4 \
  --tcp-port 5555 \
  --tmp-dir /tmp \
  --results-dir /shared/results \
  myanalysis.C
```

**Supervisor machine (auto-spawn local workers):**
```bash
ndmspc-run --mode tcp -n 4 \
  --spawn-workers 4 \
  --tcp-port 5555 \
  --tmp-dir /tmp \
  --results-dir /shared/results \
  myanalysis.C
```

**Worker machine(s)** — no extra configuration needed:
```bash
ndmspc-worker -e tcp://supervisor-host:5555
```

**Worker machine (spawn N local workers on that machine):**
```bash
ndmspc-worker -e tcp://supervisor-host:5555 --spawn-workers 8
```

Workers connect, send a `BOOTSTRAP` message, and receive their assigned index,
macro URL, and directory paths from the supervisor automatically.

#### `ndmspc-run` options

| Flag | Env var set | Description |
|---|---|---|
| `macro` (positional) | `NDMSPC_MACRO` | Macro file(s) or URL(s) to run (comma-separated) |
| `--macro-params` | `NDMSPC_MACRO_PARAMS` | Parameter list forwarded to `TMacro::Exec(params)` |
| `--mode` | `NDMSPC_EXECUTION_MODE` | `ipc`, `tcp`, or `thread` |
| `-n / --processes` | `NDMSPC_MAX_PROCESSES` | Number of worker processes/slots |
| `--tcp-port` | `NDMSPC_TCP_PORT` | TCP port the supervisor binds (default: 5555) |
| `--spawn-workers` | — | In TCP mode, spawn N local `ndmspc-worker` processes |
| `--worker-bin` | — | Worker executable for `--spawn-workers` (default: `ndmspc-worker`) |
| `--worker-endpoint` | — | Endpoint used by spawned workers (default: `tcp://localhost:<tcp-port>`) |
| `--tmp-dir` | `NDMSPC_TMP_DIR` | Local scratch directory for temporary files |
| `--results-dir` | `NDMSPC_TMP_RESULTS_DIR` | Shared directory where workers deposit finished files |
| `-v / --verbose` | — | Enable verbose console logging |

All flags set the corresponding environment variable only if it is not already
present in the shell environment, so existing exports take priority.

#### `ndmspc-worker` options

| Flag | Description |
|---|---|
| `-e / --endpoint` (required) | Supervisor ZeroMQ endpoint, e.g. `tcp://host:5555` |
| `-i / --index` | Worker index (auto-assigned by supervisor when omitted) |
| `-m / --macro` | Macro to run (sent by supervisor via bootstrap when omitted) |
| `--macro-params` | Parameter list forwarded to `TMacro::Exec(params)` |
| `--spawn-workers` | Spawn N local worker processes from this host (spawner mode) |
| `--worker-bin` | Worker executable used by spawner mode (default: current executable) |
| `-v / --verbose` | Enable verbose console logging |

#### Bootstrap protocol

When a worker starts without `--index` or `--macro` it sends a `BOOTSTRAP`
message to the supervisor.  The supervisor replies with a `CONFIG` frame
containing the assigned worker index, macro list, `NDMSPC_TMP_DIR`, and
`NDMSPC_TMP_RESULTS_DIR`.  The worker applies these before loading the macro,
so it requires zero manual configuration beyond the endpoint.

#### Using `SetWorkerMacro()` for custom macro paths

By default the supervisor forwards the same macro it is running.  If workers
should load a different macro (e.g. a pre-installed path on each worker node),
call `SetWorkerMacro()` before `Process()` inside the supervisor macro:

```cpp
// myanalysis.C
void myanalysis() {
  auto * tree = NGnTree::Open("input.root");
  // Workers will load this path instead of myanalysis.C
  tree->SetWorkerMacro("/opt/analysis/worker.C");
  tree->Process(MyFunc);
}
```

### Thread Safety

- Thread-safe data structures
- Parallel processing support
- Resource monitoring for concurrent operations

### Web Interface

- Real-time data visualization via HTTP/WebSocket
- Interactive analysis monitoring
- Remote access capabilities

### ROOT Integration

- Seamless integration with ROOT framework
- ROOT dictionary generation for reflection
- Compatible with ROOT's TTree and histogram systems

### Observability

- OpenTelemetry integration for distributed tracing
- Resource monitoring capabilities
- Performance profiling support

## Build System

### CMake Configuration

- Modern CMake (3.x+) with presets support
- Modular build system (base, core, hep, http)
- Automatic ROOT dictionary generation
- CPack for packaging

### Packaging

- RPM spec files for Fedora/RHEL
- COPR integration for automated package building
- Docker support for containerized deployments

### Scripts

- `scripts/make.sh` - Build automation
- `scripts/env.sh` - Environment setup
- `scripts/gen.sh` - Code generation utilities
- `scripts/ndmspc-run` - Legacy shell wrapper (sets ROOT_INCLUDE_PATH before running a command)

## Testing

Comprehensive test suite in `test/` directory:

- `test_00_NGnTree.cxx` - Tree structure tests
- `test_01_NExecutor.cxx` - Executor functionality tests
- `test_02_NStorage.cxx` - Storage system tests
- `test_03_NParameter.cxx` - Parameter management tests
- `test_04_NCustomization.cxx` - Customization tests

Example ROOT files demonstrate various dimensional analyses:

- `NExecutor1D.root` - 1-dimensional examples
- `NExecutor2D.root` - 2-dimensional examples
- `NExecutor5D.root` - 5-dimensional examples

## CI/CD Pipeline

GitLab CI/CD configuration (`.gitlab-ci.yml`):

- Automated building and testing
- Docker image creation
- Package generation
- Documentation deployment

## Technologies

### Core Technologies

- **C++** (C++11 or later)
- **ROOT** (CERN Data Analysis Framework)
- **CMake** (Build system)

### Additional Dependencies

- **OpenTelemetry C++** - Observability and tracing
- **WebSocket libraries** - Real-time communication
- **Doxygen** - Documentation generation

### Target Platforms

- Linux (primary target)
- AlmaLinux 9 (specific Docker support)
- Fedora/RHEL (via COPR)

## Development Tools

### Code Quality

- `.clang-format` - Code formatting rules
- `.clangd` - Language server configuration
- VS Code integration (`.vscode/` directory)

### Documentation

- Doxygen configuration for API documentation
- Markdown notes in `notes/` directory
- Example code and tutorials

## License

Licensed under **LGPL 2.1** (Lesser General Public License 2.1)

- See `LGPL2_1.txt` and `LICENSE` for full terms
- Allows linking with proprietary software
- Requires sharing modifications to the library itself

## Use Cases

This framework is designed for:

1. **High-Energy Physics Analysis**
   - Multi-dimensional particle event analysis
   - Complex histogram operations
   - Large-scale data processing

2. **Scientific Computing**
   - N-dimensional data exploration
   - Statistical analysis across multiple variables
   - Parallel computation of complex operations

3. **Real-time Monitoring**
   - Live data visualization
   - Performance monitoring
   - Distributed system observability

## Getting Started

### Building

```bash
# Setup environment
source scripts/env.sh

# Build the project
./scripts/make.sh

# Run tests
cd build && ctest
```

### Running

```bash
# Start the HTTP server
./bin/ndmspc-server

# Terminal UI for the room router (rooms need an in-cluster Kubernetes API)
./bin/ndmspc-room-tui --url http://localhost:8080
```

## Project Status

The project includes:

- ✅ Complete core functionality
- ✅ Web interface implementation
- ✅ Comprehensive test suite
- ✅ CI/CD pipeline
- ✅ Documentation framework
- ✅ Example analyses (1D, 2D, 5D)

## Repository

Hosted on GitLab: `gitlab.com/ndmspc/ndmspc`

```


