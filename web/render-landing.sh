#!/bin/sh
# Render the GitLab Pages landing page from web/index.html.in.
#
#   sh web/render-landing.sh [output]
#
# Version, date and the five most recent tags come from the checkout itself, so
# the page needs no API call and no JSON parser. A clone without tags still
# produces a valid page (a development badge and a link to all releases).
set -eu

here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
out=${1:-"$here/../public/index.html"}
repo=${CI_PROJECT_URL:-https://gitlab.com/ndmspc/ndmspc}

latest=$(git describe --tags --abbrev=0 2>/dev/null || true)

if [ -n "$latest" ]; then
  version=$latest
  image_tag=$latest
  tag_date=$(git log -1 --format=%cs "$latest" 2>/dev/null || true)
  if [ -n "$tag_date" ]; then
    release_note="released $tag_date"
  else
    release_note="tagged release"
  fi
else
  version="develop"
  image_tag="next"
  release_note="development build — no release tag in this checkout"
fi

rows=$(mktemp)
trap 'rm -f "$rows"' EXIT

if [ -n "$latest" ]; then
  git for-each-ref --sort=-v:refname \
      --format='%(refname:short)%09%(creatordate:short)' refs/tags |
    head -n 5 |
    while IFS="$(printf '\t')" read -r tag day; do
      [ -n "$tag" ] || continue
      num=${tag#v}
      extra=""
      case "$tag" in
        *-*) extra='<span class="badge pre">pre-release</span>' ;;
      esac
      printf '        <tr><td class="tag">%s%s</td><td>%s</td>' "$tag" "$extra" "$day"
      printf '<td class="actions">'
      printf '<a href="%s/-/releases/%s">notes</a>' "$repo" "$tag"
      printf '<a href="%s/-/releases/%s/downloads/ndmspc-%s-source-with-ui.tar.gz">source</a>' \
        "$repo" "$tag" "$num"
      printf '</td></tr>\n'
    done >"$rows"
else
  printf '        <tr><td colspan="3">No release tags in this checkout — see <a href="%s/-/releases">all releases</a>.</td></tr>\n' \
    "$repo" >"$rows"
fi

mkdir -p "$(dirname -- "$out")"
awk -v version="$version" -v note="$release_note" -v image="$image_tag" -v rowsfile="$rows" '
  {
    if ($0 ~ /@@RELEASES@@/) {
      while ((getline line < rowsfile) > 0) print line
      close(rowsfile)
      next
    }
    gsub(/@@VERSION@@/, version)
    gsub(/@@RELEASE_NOTE@@/, note)
    gsub(/@@IMAGE_TAG@@/, image)
    print
  }
' "$here/index.html.in" >"$out"

echo "Wrote $out ($version)"
