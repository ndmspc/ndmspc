# NDMSPC UI

The web UI is not built from sources in this repository. `ui/CMakeLists.txt`
clones [ndmspc-ui](https://gitlab.com/ndmspc/ndmspc-ui) (`main`) into
`build/ndmspc-ui-src` with `ExternalProject`, runs `npm install` and
`npm run build` there, and copies the resulting `dist/` into `build/ndmspc-ui`.
It is installed to `share/ndmspc/ndmspc-ui` by the `%files ui` subpackage.

## Prerequisites

- `nodejs` and `npm` (the RPM spec adds them as `BuildRequires` when
  `%{with_ui}` is set)
- network access to `gitlab.com` and `registry.npmjs.org` at build time

Build with UI support enabled:

```bash
scripts/make.sh ui
```

or, for the packaged build:

```bash
cmake ../ -DWITH_UI=ON ...
```

## npm flags

The `npm install` invocation in `ui/CMakeLists.txt` passes two flags that are
required for the build to work outside a developer's personal npm
configuration:

- `--include=dev` — npm omits `devDependencies` when `NODE_ENV=production` is
  exported. `tsc` and `vite` are devDependencies, so without this the build
  fails with `tsc: command not found`.
- `--legacy-peer-deps` — ndmspc-ui allows `react ^19.2.6`, which resolves to
  React 19.3, while `@react-three/fiber@9.7.0` declares
  `peer react ">=19 <19.3"`. Strict resolution fails with `ERESOLVE`.
  Locally this is often masked by `legacy-peer-deps=true` in `~/.npmrc`, which
  clean build roots such as COPR do not have.

These flags exist because of the conflict above. If ndmspc-ui pins
`react`/`react-dom` below 19.3, `--legacy-peer-deps` can be dropped.

## Build log

`npm run build` output is written to `build/npm_build.log` and echoed to the
console, so failures surface in packaging logs. The
`ndmspc-ui-src/npm_build_completed.marker` file records a successful build;
`clean_ndmspc_ui` removes the marker along with the sources and output.
