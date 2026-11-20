#!/bin/bash -e
# Vendors the pinned third-party archives into <dest>/<name>/ so a source tarball built by
# `make dist` can be configured without network access: ndmspc_find_or_fetch() then reads
# the local archive instead of downloading it, and verifies it against the same hash.
#
# Each archive is checked against the sha256 pinned in cmake/deps.cmake before it is kept, so
# a stale or tampered copy in <dest> is replaced (and a bad download fails here, not later).
#
# Arguments, from cmake/base.cmake's `dist` target (NDMSPC_DEPS_VENDOR_ARGS):
#   fetch-deps.sh <dest-dir> <name> <url> SHA256=<hex> [<name> <url> SHA256=<hex> ...]

if [ $# -lt 4 ]; then
  echo "Usage: $0 <dest-dir> <name> <url> SHA256=<hex> [<name> <url> SHA256=<hex> ...]" >&2
  exit 1
fi

dest="$1"
shift
if [ $(( $# % 3 )) -ne 0 ]; then
  echo "ERROR: arguments must be <name> <url> SHA256=<hex> triples" >&2
  exit 1
fi

while [ $# -gt 0 ]; do
  name="$1"
  url="$2"
  expected="${3#SHA256=}"
  shift 3

  if [ -z "$expected" ]; then
    echo "ERROR: $name has no SHA256 to verify against; pin one in cmake/deps.cmake" >&2
    exit 1
  fi

  # The directory is the lowercased NAME: that is the FetchContent name
  # ndmspc_find_or_fetch() looks the archive up under (<deps-dir>/<name>/<archive>).
  dir="$dest/$(printf '%s' "$name" | tr '[:upper:]' '[:lower:]')"
  archive="$dir/$(basename "$url")"
  mkdir -p "$dir"

  if [ -f "$archive" ] && [ "$(sha256sum "$archive" | cut -d' ' -f1)" = "$expected" ]; then
    echo "Kept vendored $name ($(basename "$archive"), sha256 $expected)"
    continue
  fi

  rm -f "$archive" "$archive.tmp"
  curl --fail --silent --show-error --location --retry 3 --max-time 300 \
    --output "$archive.tmp" "$url"
  actual="$(sha256sum "$archive.tmp" | cut -d' ' -f1)"
  if [ "$actual" != "$expected" ]; then
    rm -f "$archive.tmp"
    echo "ERROR: $name: $url" >&2
    echo "  expected sha256 $expected" >&2
    echo "  actual   sha256 $actual" >&2
    exit 1
  fi
  mv "$archive.tmp" "$archive"
  echo "Vendored $name ($(basename "$archive"), sha256 $expected)"
done
