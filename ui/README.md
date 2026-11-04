# NDMSPC UI

The web UI is not built from sources in this repository. The
[ndmspc-ui](https://gitlab.com/ndmspc/ndmspc-ui) project publishes a
ready-to-serve page tarball (`index.html` + `assets/`) to its GitLab Generic
Package Registry on every build. `ui/CMakeLists.txt` downloads the requested
version at configure time and installs it to `share/ndmspc/ndmspc-ui` (the
`%files ui` subpackage).

## Version

What to download is resolved in `cmake/version.cmake` and tried in order until a page
exists:

1. `v<major>.<minor>-stable` — the newest page of that series, e.g. `v1.4-stable`
2. `v<version>` — the page built from the exact ndmspc-ui tag, e.g. `v1.4.3`
3. `next` — the latest development build of ndmspc-ui, republished on every push

A build from a tag (pre-releases included) starts at 1, so a rebuilt `v1.4.3` picks up UI
fixes published after that release; branches start at 3, so they track development. Step
2 covers a series whose stable slot does not exist yet, and step 3 is the last resort
(the configure log names what was tried). A pinned version skips the fallbacks entirely.

Set an explicit version the usual ways (these are used as-is, no fallback — a missing
page fails the configure):

```bash
NDMSPC_UI_VERSION=v1.4.3 scripts/make.sh install
cmake ../ -DNDMSPC_UI_VERSION=v1.4.3 ...
```

Pinning matters when a byte-identical page is required: `v1.4-stable` is over-written by
every 1.4.x ndmspc-ui release, so rebuilding the same ndmspc tag later can pick a newer
page.

## Embedded page (source tarballs)

`make dist` (`make srpm`, the source tarball attached to a tag's Release) embeds the page
it would have downloaded into `ui/ndmspc-ui-page.tar.gz`, with the chosen version in
`ui/ndmspc-ui-page.version`. A build from that tarball — no `.git`, page present — uses it
and needs no network; the configure log says `Using the ndmspc-ui page embedded …`.

Two switches control it:

- `-DNDMSPC_UI_EMBED=OFF` — produce a source tarball without the page (offline `make
  dist`); builds from it download as usual.
- `-DNDMSPC_UI_FETCH=ON` — ignore an embedded page and download the current one (the
  candidate list below). A pinned `NDMSPC_UI_VERSION` also ignores it and downloads exactly
  that version.

GitLab's own "Download source code" archive is a plain `git archive` of the tag: it cannot
contain the page (nothing runs when it is generated), so use the tarball attached to the
tag's Release instead.

The version maps to a file in the ndmspc-ui package registry:

```
https://gitlab.com/api/v4/projects/ndmspc%2Fndmspc-ui/packages/generic/ndmspc-ui-page/<version>/ndmspc-ui-page.tar.gz
```

## Prerequisites

- network access to `gitlab.com` when the page has to be downloaded (a source tarball
  from `make dist` already carries it)
- no `nodejs`/`npm` — the page is downloaded prebuilt

The page is installed together with the server (`WITH_HTTP`, on by default), so a normal
build includes it.

```bash
scripts/make.sh install
```

## A page built locally

When the page being served is not published yet — a working tree in
[ndmspc-ui](https://gitlab.com/ndmspc/ndmspc-ui) — that build's page can be put in
the same place on top of a released image, without rebuilding ndmspc at all. The
page directory is what `npm run build:page` writes (`page-dist/`: `index.html` +
`assets/`), and [`ci/Dockerfile.ui-local`](../ci/Dockerfile.ui-local) is the
overlay that copies it to `/usr/share/ndmspc/ndmspc-ui`:

```bash
(cd ../ndmspc-ui && npm run build:page)
podman build -f ci/Dockerfile.ui-local \
  --build-arg BASE_IMAGE=localhost:5001/ndmspc/base:latest \
  -t localhost:5001/ndmspc/base:local ../ndmspc-ui/page-dist
```

The tag it produces is what `-e ndmspc_image=…` points a deployment at, and the
entry and every room serve that page from then on.

## Refresh

The page is re-downloaded on every cmake configure, so re-configuring (`scripts/make.sh`
re-runs cmake) picks up the current `v1.4-stable` or `next`. The `clean_ndmspc_ui` target
removes the extracted page and the downloaded archive:

```bash
make clean_ndmspc_ui
```
