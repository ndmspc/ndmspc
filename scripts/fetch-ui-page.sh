#!/bin/bash -e
# Downloads the ndmspc-ui page into <source>/ui/ so `make dist` embeds it in the source
# tarball (and therefore in the SRPM): a build from that tarball needs no network.
#
# Candidates are passed by CMake, most preferred first, so the preference order lives in
# cmake/version.cmake only:
#   scripts/fetch-ui-page.sh v1.4-stable v1.4.5 next
PROJECT_DIR="$(dirname "$(dirname "$(readlink -m "$0")")")"
BASE_URL="https://gitlab.com/api/v4/projects/ndmspc%2Fndmspc-ui/packages/generic/ndmspc-ui-page"
DEST_DIR="$PROJECT_DIR/ui"
ARCHIVE="$DEST_DIR/ndmspc-ui-page.tar.gz"
STAMP="$DEST_DIR/ndmspc-ui-page.version"

if [ $# -eq 0 ]; then
  echo "Usage: $0 <page-version>..." >&2
  echo "  Page versions to try, most preferred first (e.g. v1.4-stable v1.4.5 next)." >&2
  exit 1
fi

# Never leave a previous run's page behind: a stale archive would be embedded silently.
rm -f "$ARCHIVE" "$STAMP"
mkdir -p "$DEST_DIR"

for version in "$@"; do
  url="$BASE_URL/$version/ndmspc-ui-page.tar.gz"
  echo "Trying ndmspc-ui page '$version'"
  if curl --fail --silent --show-error --location --retry 2 --max-time 180 \
       --output "$ARCHIVE.tmp" "$url"; then
    mv "$ARCHIVE.tmp" "$ARCHIVE"
    printf '%s\n' "$version" > "$STAMP"
    echo "Embedded ndmspc-ui page '$version'"
    exit 0
  fi
  rm -f "$ARCHIVE.tmp"
done

echo "ERROR: no ndmspc-ui page available (tried: $*)" >&2
exit 1
