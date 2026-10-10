# `http/tui` — terminal UIs

Home for NDMSPC's terminal user interfaces. The room TUI (`ndmspc-room-tui`) that lived here was
removed — the router's web page is the rooms view now (see [`../README.md`](../README.md)).

What is left is `example.cxx`, the smallest thing that uses FTXUI. It keeps the TUI dependency
(`core/tui`'s `NdmspcTuiFtxui`, and the FTXUI fetch in `cmake/deps.cmake`) exercised, and it is the
starting point for a real TUI.

The TUI build is **off by default**. Turn it on with `-DWITH_TUI=ON`, or the `tui` argument to
`scripts/make.sh`:

```bash
./scripts/make.sh tui install      # builds and installs bin/ndmspc-tui-example
```

A TUI here is its own executable, not part of the `NdmspcHttp` library (see the glob exclusion in
`../CMakeLists.txt`): build it with `RootBin(...)` and link `NdmspcTuiFtxui` — and `NdmspcHttp` if it
talks to the server.
