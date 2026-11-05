# NDMSPC

A ROOT framework extension for N-dimensional storage, analysis and visualization of high-dimensional
data — designed for HEP, applicable to any domain.

| | |
|---|---|
| **Project documentation** | this site (build, tutorial macros, HTTP server, CLI, development) |
| **API reference** | [Doxygen](/ndmspc/doxygen/) — every class and header |
| **User guide** | <https://ndmspc.gitlab.io/docs/> — installation, tutorial, concepts, rooms |
| **Web UI demo** | <https://ndmspc.gitlab.io/ndmspc-ui/> |

## How the repository is organised

| Directory | What lives there |
| --- | --- |
| `core/` | `NGnTree`, binning, parameters, storage, execution |
| `http/` | `NHttpServer`, the tool/handler registry, the room router, the MCP server |
| `hep/`, `numcal/`, `taxi/` | Analysis helpers, numerical tools, the taxi client |
| `macros/` | Tool macros (`macros/tools/`), examples, the CERN staff demo |
| `tutorial/` | The eight tutorial lessons — see [Tutorial macros](tutorial/index.md) |
| `doc/` | Doxygen input (`ndmspc.Doxyfile.in`) |
| `docs/` | This documentation site (Zensical, built from `mkdocs.yml`) |
| `ci/`, `scripts/` | Container recipes and build helpers |

## Requirements

- ROOT ≥ 6.32 (with the network plugins for `root://` and `http(s)://` access)
- A C++17 compiler, CMake ≥ 3.24
- Optional: Doxygen (for the API reference), Kubernetes (for rooms)

## Quick start

```bash
# container — the image already holds the tutorial macros
podman run --rm -p 8080:8080 registry.gitlab.com/ndmspc/ndmspc/base:latest
# open http://localhost:8080
```

```bash
# from a configured installation
ndmspc-server              # loads toolNgnt.C and toolBrowser.C
```

See [Install & build](install.md) for the other routes.

## License

LGPL-2.1 — see [`LGPL2_1.txt`](https://gitlab.com/ndmspc/ndmspc/-/blob/develop/LGPL2_1.txt).
