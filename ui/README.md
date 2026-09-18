# NDMSPC UI

The web UI is not built from sources in this repository. The
[ndmspc-ui](https://gitlab.com/ndmspc/ndmspc-ui) project publishes a
ready-to-serve page tarball (`index.html` + `assets/`) to its GitLab Generic
Package Registry on every build. `ui/CMakeLists.txt` downloads the requested
version at configure time and installs it to `share/ndmspc/ndmspc-ui` (the
`%files ui` subpackage).

## Version

`NDMSPC_UI_VERSION` selects what to download (default `next`):

- `next` — the latest `main` build of ndmspc-ui, republished on every push.
- a tag such as `v1.2.3` — a fixed release.

It can be set the usual ways:

```bash
scripts/make.sh ui=v1.2.3 install
NDMSPC_UI_VERSION=v1.2.3 scripts/make.sh ui install
cmake ../ -DWITH_UI=ON -DNDMSPC_UI_VERSION=v1.2.3 ...
```

The version maps to a file in the ndmspc-ui package registry:

```
https://gitlab.com/api/v4/projects/ndmspc%2Fndmspc-ui/packages/generic/ndmspc-ui-page/<version>/ndmspc-ui-page.tar.gz
```

## Prerequisites

- network access to `gitlab.com` when configuring with `WITH_UI=ON`
- no `nodejs`/`npm` — the page is downloaded prebuilt

Build with UI support enabled:

```bash
scripts/make.sh ui
```

or, for the packaged build:

```bash
cmake ../ -DWITH_UI=ON ...
```

## Refresh

The page is re-downloaded on every cmake configure, so re-configuring (for
example with `scripts/make.sh ui`) picks up the current `next`. The
`clean_ndmspc_ui` target removes the extracted page and the downloaded archive:

```bash
make clean_ndmspc_ui
```
