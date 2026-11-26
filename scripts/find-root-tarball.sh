#!/bin/bash -e
# Print the download URL of a prebuilt macOS arm64 ROOT tarball.
#
#   find-root-tarball.sh <version|latest> [macos-major]
#
# `version` is a release number (6.36.10) or `latest` for the newest stable release.
# `macos-major` (15, 26, ...) restricts the result to that macOS major version, which is
# what a GitHub runner label names (macos-15, macos-26). Without it, every arm64 tarball for
# the release is considered and the highest macOS version wins.
#
# The asset name cannot be built from the version: it also carries the macOS point release
# and the clang version, and both move between releases, e.g.
#   6.36.10 -> root_v6.36.10.macos-15.7-arm64-clang170.tar.gz
#   6.42.00 -> root_v6.42.00.macos-26.7-arm64-clang210.tar.gz
# There is also no arm64 build for every macOS major (6.42.00 ships 26.7 and 27.0 only), so
# the release page is the only reliable source and it is read here rather than assumed.
#
# The pages are scraped for links, so a redesign of root.cern breaks this. That is the
# deliberate trade: it fails loudly, with the assets it did find, while a hardcoded table
# would silently rot. ROOT publishes no prebuilt macOS nightlies, so `latest` means the
# newest stable release, not the development branch.

BASE_URL="https://root.cern"

usage() {
  cat <<EOF
Usage: $(basename "$0") <version|latest> [macos-major]

Prints the URL of a prebuilt macOS arm64 ROOT tarball.

  version       release number (e.g. 6.36.10) or 'latest' for the newest stable release
  macos-major   restrict to one macOS major version (e.g. 15 for a macos-15 runner)

Examples:
  $(basename "$0") 6.36.10 15    # the tarball for a macos-15 runner
  $(basename "$0") latest 26     # the newest stable release, for a macos-26 runner
EOF
}

die() {
  echo "find-root-tarball.sh: $*" >&2
  exit 1
}

# Fetch a page, failing with the reason rather than an empty string.
fetch() {
  local url=$1
  curl --fail --silent --show-error --location --retry 3 --max-time 60 "$url"
}

# Newest stable release: the release index lists newest first, but sort explicitly so the
# order of the page is not relied on. Pre-releases (6.42.00-rc1) are skipped.
latest_version() {
  fetch "$BASE_URL/install/all_releases/" |
    grep -oE 'Release 6\.[0-9]+\.[0-9]+' |
    sed 's/Release //' |
    sort -t. -k2,2n -k3,3n |
    tail -n 1
}

if [ $# -lt 1 ] || [ $# -gt 2 ]; then
  usage >&2
  exit 1
fi

case "$1" in
  -h|--help)
    usage
    exit 0
    ;;
esac

version=$1
macos_major=${2:-}

if [ "$version" = "latest" ]; then
  version=$(latest_version)
  [ -n "$version" ] || die "cannot determine the newest stable ROOT release from $BASE_URL/install/all_releases/"
fi

case "$version" in
  [0-9]*.[0-9]*.[0-9]*) ;;
  *) die "not a release version: '$version' (expected e.g. 6.36.10 or 'latest')" ;;
esac

# Release page: 6.36.10 -> /releases/release-63610/
release_id=$(printf '%s' "$version" | tr -d '.')
page_url="$BASE_URL/releases/release-$release_id/"

assets=$(fetch "$page_url" |
  grep -oE "root_v${version//./\\.}\.macos-[0-9]+\.[0-9]+-arm64-clang[0-9]+\.tar\.gz" |
  sort -u) ||
  die "cannot read $page_url"

[ -n "$assets" ] || die "no macOS arm64 tarball listed for ROOT $version (page: $page_url)"

if [ -n "$macos_major" ]; then
  filtered=$(printf '%s\n' "$assets" | grep -E "\.macos-${macos_major}\.[0-9]+-arm64-" || true)
  if [ -z "$filtered" ]; then
    # Say what the runner label would have to be instead: a bare "not found" hides that the
    # fix is a one-word change in the workflow.
    available=$(printf '%s\n' "$assets" | sed -E 's/.*\.macos-([0-9]+)\..*/\1/' | sort -un | tr '\n' ' ')
    die "ROOT $version has no macOS ${macos_major} arm64 build; it is published for macOS major(s): ${available% }. Point the runner label at one of those."
  fi
  assets=$filtered
fi

# Highest macOS point release when several are offered (26.7 over 26.3).
printf '%s\n' "$assets" |
  sort -t- -k2,2V |
  tail -n 1 |
  sed "s|^|$BASE_URL/download/|"
