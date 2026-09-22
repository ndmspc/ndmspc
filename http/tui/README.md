# `http/tui` — terminal UIs for the HTTP services

Home of the FTXUI-based terminal user interfaces for the NDMSPC HTTP services.

## Layout

| File | Purpose |
|---|---|
| `ndmspc-room-tui.cxx` | The room-management CLI: options, TLS/OIDC setup, the MCP handshake, the headless `--list/--open/--status/--close/--backup/--restore` actions, and the hand-off to the screen. Includes no FTXUI header. |
| `room_ui.cxx` / `.h` | The interactive room screen for that binary: room table, detail pane, dialogs and the worker thread. This is the only file that includes FTXUI. |

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

- Room calls never run on the render thread: they go through the worker's queue and the screen
  only reads a locked snapshot of the shared state. They are quick now - `room_open` is called with
  `wait=false`, so the router does the creation off its request path and answers at once - but the
  worker and the snapshot are what make that answer meet the screen safely.
- There is no local "creating" state: a room being created is the router's own `preparing` entry
  (`state`, `phase`, `startedAt` in `room/list`), so the row appears as soon as the worker refreshes
  the list after `room_open` and keeps the elapsed time itself. The screen therefore has nothing to
  invent and nothing to clear if a call fails, and several rooms can be created in a row.
- Anything drawn while an action is in flight - and every `preparing` row - has to be animated by
  `RunRoomUi`'s ticker, not by the worker: the worker's loop *is* the action until it answers, so
  its `fWake()` cannot run then, and the screen would otherwise not be redrawn until the action
  finished or the next refresh came round.
- The worker's `busy` flag means "an action is in flight" and gates both the periodic
  refresh and the dialogs' submit. Do not set it for anything else (an initial "connecting"
  state used to stall the first room-list load). While it is set, the keys that start an action
  (enter, `r`, and the dialogs' submit) are refused with a message naming what is still running:
  actions run one at a time, so queueing them would only hide the wait.
- NLogger's console output is switched off while the screen owns the terminal;
  diagnostics are surfaced in the UI instead.
- A room's access tokens (`NRoomAccess`, reported by the router as `access`) are shown in the
  detail pane: the three URLs carry the token of the level being looked at, and `t` switches
  between the read-write and the read-only one. `NRoomInfo` carries them (`tokenRw`/`tokenRo`), so
  `--list` reports them too; a room the router reports no tokens for keeps the plain URLs, which is
  also how an older room is told apart from one that enforces access.
- A room's owner (`owner`, kept on the room's Service as `ndmspc.io/room-owner`) is shown as an
  `OWNER` column and, for the selected room, in the detail pane; `--list` reports it per row. The
  tool asserts who it is with `--owner` (`NDMSPC_ROOM_OWNER`), which the client adds to every
  request, and the router then answers with that owner's rooms only and refuses the rest with
  `not_owner`. Nothing is asserted by default, so an operator's run still sees every room.
- `http/examples/room/` runs the tool against a mock of the router's MCP endpoint, which is
  the only way to exercise it away from a cluster.
