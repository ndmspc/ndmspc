# `core/tui` — shared FTXUI support for NDMSPC terminal UIs

Compiles FTXUI's amalgamated header-only implementation exactly once and exposes it as the
static library `NdmspcTuiFtxui`, so a terminal UI only has to link it.

## Why it lives in `core`

The FTXUI implementation is generic — it knows nothing about rooms, the HTTP server or any
other service — so it belongs with the reusable infrastructure rather than inside one
service's UI directory. It is a static library of its own rather than part of `NdmspcCore`
so that FTXUI stays out of the ROOT-dictionary'd core library and out of the link line of
that library's other consumers.

## How it is wired

- `cmake/deps.cmake` pins `FTXUI_VERSION` and fetches the amalgamated release archive
  (`ftxui-amalgamated.zip`) via `ndmspc_find_or_fetch(... NO_ADD_SUBDIRECTORY)`. Because
  the archive is fetched without adding a CMake subproject, FTXUI registers no `install()`
  rules of its own — the same rule `httplib` and `jwt-cpp` follow, which matters because
  NDMSPC's install prefix defaults to the source root. The fetch is unconditional, because
  `core/` is always built.
- `CMakeLists.txt` builds `ftxui_impl.cxx` into `NdmspcTuiFtxui` and carries FTXUI's include
  directories, as system includes, plus `Threads::Threads`, as usage requirements.

## What a consumer does

Add the target to the `RootBin` link list and nothing else:

```cmake
set(MY_EXTERNAL_LIBS
  NdmspcHttp        # or whatever the UI talks to
  NdmspcTuiFtxui
)
RootBin(my-tui "my-tui.cxx" "${MY_EXTERNAL_LIBS}")
```

FTXUI's headers then arrive as `-isystem .../ftxui-src`, so their warnings cannot reach the
consumer's build even when `ENABLE_STRICT_WARNINGS` is ON. `INTERFACE_SYSTEM_INCLUDE_DIRECTORIES`
is set explicitly because `target_include_directories(... SYSTEM PUBLIC ...)` only
propagates that to consumers from CMake 3.25, while this project declares a 3.24 floor.

## Rules

- `FTXUI_IMPLEMENTATION` must be defined in exactly one translation unit, and that is
  `ftxui_impl.cxx`. Nothing else may define it — a second definition gives duplicate
  symbols at link time.
- `ftxui_impl.cxx` must include no ROOT and no NDMSPC header. It exists to compile FTXUI's
  implementation in isolation from everything else in the binary.

## See also

- [`http/tui`](../../http/tui/README.md) — the first consumer, `ndmspc-room-tui`.
