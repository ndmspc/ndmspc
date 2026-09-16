# `http/tui` — terminal UIs for the HTTP services

Home of the FTXUI-based terminal user interfaces for the NDMSPC HTTP services.

## Layout

| File | Purpose |
|---|---|
| `ndmspc-room-tui.cxx` | The room-management CLI: options, TLS/OIDC setup, the MCP handshake, the headless `--list/--open/--status/--close/--backup/--restore` actions, and the hand-off to the screen. Includes no FTXUI header. |
| `room_mgm_ui.cxx` / `.h` | The interactive room screen for that binary: room table, detail pane, dialogs and the worker thread. This is the only file that includes FTXUI. |

The FTXUI implementation itself is **not** here — it is generic TUI infrastructure and
lives in [`core/tui`](../../core/tui/README.md), which compiles the amalgamated
`ftxui_all.hpp` once and hands its include directories to whatever links
`NdmspcTuiFtxui`. A new TUI in this directory therefore only needs
`NdmspcTuiFtxui` in its `RootBin` link list.

## Building

`WITH_HTTP` (the default) builds `ndmspc-room-tui`; the FTXUI dependency is fetched by
`cmake/deps.cmake` and installed into `build/_deps/ftxui-src`.

```bash
./scripts/make.sh install     # -> bin/ndmspc-room-tui
```

## Notes for changes here

- Room calls never run on the render thread. `room_open` blocks until the router's Knative
  revision is ready (tens of seconds by default), so actions go through the worker's queue
  and the screen only reads a locked snapshot of the shared state.
- The worker's `busy` flag means "an action is in flight" and gates both the periodic
  refresh and the dialogs' submit. Do not set it for anything else (an initial "connecting"
  state used to stall the first room-list load).
- NLogger's console output is switched off while the screen owns the terminal;
  diagnostics are surfaced in the UI instead.
- `http/examples/room/` runs the tool against a mock of the router's MCP endpoint, which is
  the only way to exercise it away from a cluster.
