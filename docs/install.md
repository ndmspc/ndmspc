# Install & build

Four routes, in the order most people want them. The user-facing installation guide (with the
container details, COPR versions and aliBuild variants) is the
[central installation page](https://ndmspc.gitlab.io/docs/ndmspc/installation/); this page is the
repo-level quick reference.

## Container

```bash
podman run --rm --name ndmspc -p 8080:8080 \
  registry.gitlab.com/ndmspc/ndmspc/base:latest
```

Tags: `latest` (stable), `next` (pre-release), `v<version>`. The image includes ROOT, the tutorial
macros and a precompiled macro cache.

## RPM (COPR)

```bash
sudo dnf copr enable ndmspc/stable -y
sudo dnf install ndmspc
```

## aliBuild

The recipe is [`ci/ndmspc.sh`](https://gitlab.com/ndmspc/ndmspc/-/blob/develop/ci/ndmspc.sh):

```bash
aliBuild build ndmspc
alienv enter ndmspc/latest
```

`NDMSPC_OPENSSL_MODE` selects the OpenSSL the recipe links against: `v1` (default, from
`OPENSSL_ROOT`), `system` (system OpenSSL ≥ 3) or `env` (OpenSSL ≥ 3 from `OPENSSL_ROOT`).

## From source

```bash
git clone https://gitlab.com/ndmspc/ndmspc.git
cd ndmspc
scripts/make.sh install          # add `test` to build and run the suite
source scripts/env.sh
```

Useful targets:

| Command | What it does |
| --- | --- |
| `scripts/make.sh install` | Build and install into the build tree |
| `scripts/make.sh release test` | Release build + `ctest` |
| `scripts/make.sh doc install` | Build the Doxygen HTML (`share/doc/ndmspc/html`) |
| `scripts/make.sh rpm` / `srpm` | Package |

CMake options worth knowing: `-DWITH_TEST=ON`, `-DWITH_AI=OFF`, `-DNDMSPC_OPENSSL=auto|3|1.1`,
`-DDOCUMENTATION_WARN_AS_ERROR=ON`.

## Environment

| Variable | Meaning |
| --- | --- |
| `NDMSPC_DIR` / `NDMSPC_ROOT` | Installation prefix (macros, tutorial) |
| `NDMSPC_HTTP_THREADS` | Connections served at once (default 128) |
| `PORT` | Server port (default 8080) |
| `ROOT_MAX_THREADS` | ROOT worker threads |
| `NDMSPC_MAX_PROCESSES` | NDMSPC worker processes |
| `NDMSPC_EXECUTION_MODE` | `thread`, `ipc` (`process`) or `tcp` |
