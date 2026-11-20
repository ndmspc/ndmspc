#!/bin/bash -e
# Prints the ndmspc-ui version a built page came from, or nothing when it cannot be told.
#
# A page is published under a slot (`v1.4-stable`, `next`) that is republished, so the slot
# says nothing about which tag/commit the bytes are. The build stamps its version into the
# page (`VITE_APP_VERSION`, from the tag in a release pipeline, `git describe` otherwise) and
# the user menu shows it; that stamp is what this reads back.
#
# Arguments:
#   ui-page-tag.sh <page-archive.tar.gz>

if [ $# -ne 1 ]; then
  echo "Usage: $0 <page-archive.tar.gz>" >&2
  exit 1
fi

archive="$1"
[ -f "$archive" ] || exit 0

tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

# Only the JS bundles are needed; a page that does not extract yields no answer, not an error.
tar xzf "$archive" -C "$tmp" --wildcards './assets/*' 2>/dev/null || true

# Vite substitutes import.meta.env.VITE_APP_VERSION at build time, so the key appears verbatim
# in the bundle even though everything around it is minified. Several bundles carry it; they
# all agree, and the most frequent value wins if one ever does not.
grep -rhoE 'VITE_APP_VERSION:"[^"]+"' "$tmp" 2>/dev/null |
  sed -E 's/.*:"(.*)"/\1/' |
  sort | uniq -c | sort -rn |
  sed -n '1{s/^ *[0-9]* *//;p}' || true
