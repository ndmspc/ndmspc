# Developing

## Layout

| Path | Contents |
| --- | --- |
| `core/` | `NGnTree`, `NBinning`, `NBinningPoint`, `NParameters`, `NStorageTree`, executors |
| `http/server/` | `NHttpServer`, `NMcpServer`, `NSchemaBuilder`, `NRouteContext`, `NBaseActions` |
| `http/mcp/` | MCP transports |
| `http/room/` | `NRoomRouter`, room sessions, statistics |
| `modules/hep/`, `modules/numcal/`, `modules/taxi/` | Analysis utilities |
| `tools/` | Tool macros (`toolNgnt.C`, `toolBrowser.C`, `toolSchema.C`) |
| `macros/` | Examples and analysis macros (`macros/examples/`, `macros/physics/`) |
| `doc/` | Doxygen input (`ndmspc.Doxyfile.in`) |
| `docs/` | This documentation site |
| `http/ui/` | Fetch/embed logic for the prebuilt ndmspc-ui page |
| `http/web/` | The GitLab Pages landing page (`index.html.in` + `render-landing.sh`) |
| `ci/`, `scripts/` | Container recipes, build helpers, release scripts |

## Build and test

```bash
scripts/make.sh install                    # build + install into the tree
scripts/make.sh release test               # release build + ctest
scripts/make.sh doc install                # Doxygen HTML in share/doc/ndmspc/html
```

The CI runs `scripts/make.sh release test numcal` on AlmaLinux 9 and
`scripts/make.sh release test` on AlmaLinux 10.

Tests live beside each module — `core/test/`, `http/test/`, `modules/numcal/test/` — as gtest
binaries named after their file (e.g. `test_NRoomRouter.cxx`). Run one with
`ctest -R test_NRoomRouter --output-on-failure`, or the binary directly with
`./build/http/test/test_NRoomRouter.cxx --gtest_filter=Suite.Case --gtest_list_tests`.

In the workspace container, run builds as your user:

```bash
podman exec -u "$USER" -w "$PWD" fedora-toolbox-45 bash -lc 'WITH_TEST=true ./scripts/make.sh install'
```

## macOS CI

macOS is **not** a release platform: the RPMs, images and the pages build are Linux only. It is
covered by a manual GitHub Actions workflow instead, because GitLab's hosted macOS runners are
beta and are not available to open-source projects, while a self-hosted one would need real Mac
hardware. GitHub's standard macOS arm64 runners are free for public repositories, and
`github.com/ndmspc/ndmspc` is a public mirror of this project.

`.github/workflows/macos.yml` builds the tree and runs the tutorial macros, on two macOS
releases:

| Matrix entry | Runner | Purpose |
| --- | --- | --- |
| `sequoia` | `macos-15` | The gate. Keep it green. |
| `tahoe` | `macos-26` | Early warning about a Homebrew/runner image change; `continue-on-error`. |

To run it: Actions → **macOS (manual)** → *Run workflow*. Pick the branch in the
*Use workflow from* dropdown — that is the code that gets built — and choose `run_tutorials`
and/or `run_tests`. `test_filter` narrows the ctest run (`-R`).

The workflow file has to exist on the mirror's **default branch** for the *Run workflow* button
to be offered at all, even when you build another branch (the *Use workflow from* dropdown picks
the code). The mirror's default branch is `develop` for this reason; if it is not, set it under
Settings → General → Default branch, or push the workflow to whichever branch is default.

### ROOT comes from Homebrew, on purpose

The workflow runs `brew install root` rather than unpacking a root.cern tarball. This is not a
preference, it is the fix for the bug below: a prebuilt tarball carries `std_darwin.modulemap`
generated on the machine that built it, and cling imposes that modulemap on the SDK present at
run time. Homebrew bottles are built per macOS release, so they match the runner.

The first two runs of this workflow used a tarball and failed on both runners with
`header '__type_traits/is_generic_transparent_comparator.h' not found` (see below). If a future
Homebrew ROOT fails the same way, the cause is the same skew — check `brew list --versions root`
against `xcrun --show-sdk-path`, both printed by the *Show what ROOT we got* step.

### Why the tutorial run is the point

The job exists because of a failure users hit on macOS: every tutorial macro died in cling with
`std_darwin.modulemap: header '__locale_dir/support/liblibc.h' not found`, an
`_LIBCPP_ABI_VERSION` mismatch warning, and then a segfault or `LLVM ERROR: out of memory`.
`ndmspc-tutorials` runs each macro through `root -l -b -q` and exits non-zero listing the failed
steps, so the job reproduces exactly that report. The workflow also checks ROOT itself before
building, so a broken interpreter fails in seconds rather than mid-build.

The scripts under `scripts/` are POSIX-portable (`scripts/lib-portable.sh` replaces GNU-only
`readlink -m`, `nproc`, `sha256sum` and `stat -c` on macOS), so the same commands work on both
platforms.

### Root cause of the macOS cling failure

**It is a ROOT/cling bug, not an ndmspc bug.** No ndmspc code is involved; the reason it looks
like one is that it hits every macro, immediately.

1. ROOT generates `etc/cling/std_darwin.modulemap` **at build time** from the libc++ modulemap of
   the SDK on the build machine: `cmake/scripts/std_modulemap_darwin_fix.py` (called from
   `core/clingutils/CMakeLists.txt`) runs `xcrun --show-sdk-path`, reads
   `usr/include/c++/v1/module.modulemap` and wraps it in `module std [system] { … }`.
2. **At run time cling forces that baked modulemap onto the live SDK** — `CIFactory` finds the
   include dir containing `cassert` and mounts `std_darwin.modulemap` there with
   `AllowModulemapOverride=false`, so the SDK's own modulemap is deliberately ignored.
3. When the two SDKs differ, the modulemap lists headers the run-time libc++ does not have, and
   `std` cannot be built. Two observed instances of the same skew:

   | Reported by | Missing header | ROOT built against | Running on |
   | --- | --- | --- | --- |
   | user, aliBuild tree | `__locale_dir/support/liblibc.h` | macOS 26 SDK (first to ship it) | macOS 15 SDK |
   | this workflow's first runs | `__type_traits/is_generic_transparent_comparator.h` | macOS 26.7 (asset `macos-26.7-arm64-clang210`) | Xcode 26.6 / macOS 26.6 |

4. Everything else follows. `std` never builds, so `CheckABICompatibility()` cannot extract
   `_LIBCPP_ABI_VERSION` (that is the "Failed to extract …" / "Possible mismatch" warning), and
   codegen then dies in `llvm::DataLayout::operator=` (segfault / bus error) or runs out of memory.

So a prebuilt ROOT is only self-consistent while the SDK present at run time still has what the
SDK at build time had. That breaks whenever Xcode is updated or `xcode-select` changes after ROOT
was built. The robust answers are: use a ROOT built for the platform (Homebrew bottles are built
per macOS release — what this workflow does), rebuild ROOT against the active SDK
(`brew reinstall root`, or a fresh aliBuild build), or build ROOT from source on the machine that
runs it.

To tell this apart from an ndmspc problem, run ROOT alone — no ndmspc code is involved:

```bash
root -l -b -q -e 'printf("%d\n", gROOT->GetVersionInt());'
```

If that fails, the interpreter is broken and nothing built on top of it can run. To confirm the
skew: compare `xcode-select -p` / `xcrun --show-sdk-path` with the SDK ROOT was built against.

## Conventions

- Document every public class and member — the Doxygen build treats warnings as errors when
  `-DDOCUMENTATION_WARN_AS_ERROR=ON`.
- Tool keys follow `namespace/group[/subgroup]/action`; a non-conforming key throws at registration.
- Tool macros are annotated, not compiled: descriptions, schemas, dependencies and tours live in the
  macro beside the handler.
- A tool macro keeps everything but its entry point in a namespace of its own
  (`namespace Ndmspc::FooTools { … }`). Macros share one Cling session, so a helper defined at file
  scope in two macros collides — and Cling crashes in its unloader rather than reporting the clash.
  An anonymous namespace does not help: all of them are the same namespace.

## Documentation

Two separate builds, on purpose:

| Directory | Output | What it is |
| --- | --- | --- |
| `doc/` | `/doxygen/` | API reference (Doxygen, `ndmspc.Doxyfile.in`) |
| `docs/` | `/docs/` | This project documentation (Zensical, `mkdocs.yml`) |
| `http/web/` | `/` | The landing page (plain HTML rendered by `http/web/render-landing.sh`) |

`mkdocs.yml` stays compatible with MkDocs; Zensical is what CI runs:

```bash
zensical build --clean --strict
mkdocs build --strict          # fallback, same config
```

## Release

Tags drive everything: `base`, `slurmd` and the full image are published from a tag, the source
tarball embeds the `ndmspc-ui` page, and the RPM goes to COPR `testing` for `-rc` tags and `stable`
otherwise. See `ci/` and `scripts/`.

The Pages landing page lists the five most recent tags and links each to its Release and source
tarball; it is rendered at deploy time, so it needs no update per release.
