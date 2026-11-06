# Developing

## Layout

| Path | Contents |
| --- | --- |
| `core/` | `NGnTree`, `NBinning`, `NBinningPoint`, `NParameters`, `NStorageTree`, executors |
| `http/server/` | `NHttpServer`, `NMcpServer`, `NSchemaBuilder`, `NRouteContext`, `NBaseActions` |
| `http/mcp/` | MCP transports |
| `http/room/` | `NRoomRouter`, room sessions, statistics |
| `hep/`, `numcal/`, `taxi/` | Analysis utilities |
| `macros/` | Tool macros and examples (`macros/tools/toolNgnt.C`, `toolBrowser.C`, `toolSchema.C`) |
| `doc/` | Doxygen input (`ndmspc.Doxyfile.in`) |
| `docs/` | This documentation site |
| `web/` | The GitLab Pages landing page (`index.html.in` + `render-landing.sh`) |
| `ci/`, `scripts/` | Container recipes, build helpers, release scripts |

## Build and test

```bash
scripts/make.sh install                    # build + install into the tree
scripts/make.sh release test               # release build + ctest
scripts/make.sh doc install                # Doxygen HTML in share/doc/ndmspc/html
```

The CI runs `scripts/make.sh release test numcal` on AlmaLinux 9 and
`scripts/make.sh release test` on AlmaLinux 10.

In the workspace container, run builds as your user:

```bash
podman exec -u "$USER" -w "$PWD" fedora-toolbox-45 bash -lc 'WITH_TEST=true ./scripts/make.sh install'
```

## Conventions

- Document every public class and member — the Doxygen build treats warnings as errors when
  `-DDOCUMENTATION_WARN_AS_ERROR=ON`.
- Tool keys follow `namespace/group[/subgroup]/action`; a non-conforming key throws at registration.
- Tool macros are annotated, not compiled: descriptions, schemas, dependencies and tours live in the
  macro beside the handler.

## Documentation

Two separate builds, on purpose:

| Directory | Output | What it is |
| --- | --- | --- |
| `doc/` | `/doxygen/` | API reference (Doxygen, `ndmspc.Doxyfile.in`) |
| `docs/` | `/docs/` | This project documentation (Zensical, `mkdocs.yml`) |
| `web/` | `/` | The landing page (plain HTML rendered by `web/render-landing.sh`) |

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
