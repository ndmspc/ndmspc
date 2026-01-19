
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
- **NCloudEvent** - Cloud events support for distributed systems
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

- **AnalysisFunctions** - Common HEP analysis functions
- **AnalysisUtils** - Utility functions for particle physics analysis

### 4. HTTP Module (`http/`)

Web-based interface and visualization:

- **NHttpServer** - HTTP server implementation
- **NWsHandler** - WebSocket handler for real-time updates
- **NStressHistograms** - Performance testing for histogram operations

## Key Features

### Multi-Dimensional Analysis

- Support for arbitrary n-dimensional spaces
- Efficient binning and navigation algorithms
- Parallel execution capabilities

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
- `scripts/ndmspc-run` - Runtime launcher

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


