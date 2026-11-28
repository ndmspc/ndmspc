#!/bin/bash -e
# Downloads the ndmspc-ui page into <source>/http/ui/ so `make dist` embeds it in the source
# tarball (and therefore in the SRPM): a build from that tarball needs no network.
#
# The archive's sha256 is recorded next to it (http/ui/ndmspc-ui-page.sha256) and travels inside
# the source tarball, so a build from the tarball can verify the embedded page and a consumer
# can check the download by hand (`sha256sum -c ndmspc-ui-page.sha256`).
#
# Candidates are passed by CMake, most preferred first, so the preference order lives in
# cmake/version.cmake only:
#   scripts/fetch-ui-page.sh v1.4-stable v1.4.5 next
# shellcheck source=lib-portable.sh disable=SC1091
source "$(dirname "${BASH_SOURCE[0]}")/lib-portable.sh"

PROJECT_DIR="$(dirname "$(dirname "$(ndmspc_realpath "$0")")")"
BASE_URL="https://gitlab.com/api/v4/projects/ndmspc%2Fndmspc-ui/packages/generic/ndmspc-ui-page"
DEST_DIR="$PROJECT_DIR/http/ui"
ARCHIVE="$DEST_DIR/ndmspc-ui-page.tar.gz"
STAMP="$DEST_DIR/ndmspc-ui-page.version"
CHECKSUM="$DEST_DIR/ndmspc-ui-page.sha256"

if [ $# -eq 0 ]; then
  echo "Usage: $0 <page-version>..." >&2
  echo "  Page versions to try, most preferred first (e.g. v1.4-stable v1.4.5 next)." >&2
  exit 1
fi

# Never leave a previous run's page behind: a stale archive would be embedded silently.
rm -f "$ARCHIVE" "$STAMP" "$CHECKSUM"
mkdir -p "$DEST_DIR"

for version in "$@"; do
  url="$BASE_URL/$version/ndmspc-ui-page.tar.gz"
  echo "Trying ndmspc-ui page '$version'"
  if curl --fail --silent --show-error --location --retry 2 --max-time 180 \
       --output "$ARCHIVE.tmp" "$url"; then
    mv "$ARCHIVE.tmp" "$ARCHIVE"
    # A slot (`v1.4-stable`, `next`) is republished, so name the build it actually holds.
    build="$("$PROJECT_DIR/scripts/ui-page-tag.sh" "$ARCHIVE" || true)"
    if [ -n "$build" ]; then
      # "<slot> (<build>)": the slot says what was asked for, the build what arrived.
      printf '%s (%s)\n' "$version" "$build" > "$STAMP"
    else
      printf '%s\n' "$version" > "$STAMP"
    fi
    # sha256sum's own format, so `cd http/ui && sha256sum -c ndmspc-ui-page.sha256` works by hand.
    (cd "$DEST_DIR" && printf '%s  %s\n' "$(ndmspc_sha256 ndmspc-ui-page.tar.gz)" ndmspc-ui-page.tar.gz) > "$CHECKSUM"
    if [ -n "$build" ]; then
      echo "Embedded ndmspc-ui page '$version' (build $build, $(cut -d' ' -f1 "$CHECKSUM"))"
    else
      echo "Embedded ndmspc-ui page '$version' ($(cut -d' ' -f1 "$CHECKSUM"))"
    fi
    exit 0
  fi
  rm -f "$ARCHIVE.tmp"
done

echo "ERROR: no ndmspc-ui page available (tried: $*)" >&2
exit 1
