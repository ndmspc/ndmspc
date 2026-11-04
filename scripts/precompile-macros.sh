#!/usr/bin/env bash
#
# Precompile the read-only tool macros into ACLiC shared libraries.
#
# A room starts by executing its tool macros through cling. The installed tools never change at
# runtime, so an image can carry the result of that compile once instead of paying for it on every
# cold start. The server (NUtils::ExecMacro) loads a library when its manifest matches the running
# ROOT and the macro's size and mtime, and interprets the macro otherwise.
#
# Usage:
#   precompile-macros.sh [--strict] [--manifest-only] [macro-dir]
#
# macro-dir defaults to ${NDMSPC_ROOT:-/usr/share/ndmspc}/macros/tools. The cache is written beside
# it as .precompiled/<gROOT->GetVersionInt()>/{precompiled.json,<tool>_C.so,...}. Macros whose entry
# function is not the file stem with no arguments, or that ACLiC cannot compile, are skipped; the
# runtime then interprets them. --strict makes a skip a failure (for a release gate).
#
# --manifest-only compiles nothing: it rewrites precompiled.json from the libraries already in the
# cache and the size/mtime of the macros in macro-dir. An image build compiles in a builder stage
# (where a compiler is installed) and runs this mode after the RPMs install the macros, so the
# manifest matches the installed files without putting a compiler in the runtime image.

set -u

STRICT=0
MANIFEST_ONLY=0
MACRO_DIR=""

while [ $# -gt 0 ]; do
  case "$1" in
    --strict) STRICT=1 ;;
    --manifest-only) MANIFEST_ONLY=1 ;;
    -h|--help)
      sed -n '2,21p' "$0" | sed 's/^# \{0,1\}//'
      exit 0
      ;;
    -*)
      echo "precompile-macros.sh: unknown option '$1'" >&2
      exit 2
      ;;
    *)
      if [ -n "$MACRO_DIR" ]; then
        echo "precompile-macros.sh: more than one macro directory given" >&2
        exit 2
      fi
      MACRO_DIR="$1"
      ;;
  esac
  shift
done

if [ -z "$MACRO_DIR" ]; then
  MACRO_DIR="${NDMSPC_ROOT:-/usr/share/ndmspc}/macros/tools"
fi

if [ ! -d "$MACRO_DIR" ]; then
  echo "precompile-macros.sh: macro directory '$MACRO_DIR' does not exist" >&2
  exit 1
fi

MACRO_DIR="$(cd "$MACRO_DIR" && pwd -P)"

if ! command -v root >/dev/null 2>&1; then
  echo "precompile-macros.sh: 'root' is not in PATH" >&2
  exit 1
fi

VERSION=$(root -b -q -l -e 'printf("%d\n", gROOT->GetVersionInt());' 2>/dev/null | tr -dc '0-9')
if [ -z "$VERSION" ]; then
  echo "precompile-macros.sh: cannot read gROOT->GetVersionInt()" >&2
  exit 1
fi

PREFIX="$(cd "$MACRO_DIR/../.." && pwd -P)"
SOURCE_ROOT="$PREFIX"
# ACLiC writes the macro path relative to the directory it runs in; build from the source root so
# the dictionary's path is `macros/tools/<tool>.C`, which exists in the installed tree too. The
# manifest records this root, and ExecMacro loads the library from there.
cd "$SOURCE_ROOT" || {
  echo "precompile-macros.sh: cannot enter '$SOURCE_ROOT'" >&2
  exit 1
}
: "${ROOT_INCLUDE_PATH:=}"
: "${LD_LIBRARY_PATH:=}"
for dir in "$PREFIX/build/include" "$PREFIX/include" /usr/include; do
  if [ -d "$dir/ndmspc" ]; then
    case ":$ROOT_INCLUDE_PATH:" in
      *":$dir:"*) ;;
      *) ROOT_INCLUDE_PATH="${ROOT_INCLUDE_PATH:+$ROOT_INCLUDE_PATH:}$dir" ;;
    esac
  fi
done
export ROOT_INCLUDE_PATH

for dir in "$PREFIX/lib" "$PREFIX/lib64" /usr/lib64; do
  if [ -e "$dir/libNdmspcCore.so" ]; then
    case ":$LD_LIBRARY_PATH:" in
      *":$dir:"*) ;;
      *) LD_LIBRARY_PATH="${LD_LIBRARY_PATH:+$LD_LIBRARY_PATH:}$dir" ;;
    esac
  fi
done
export LD_LIBRARY_PATH

CACHE_DIR="$MACRO_DIR/.precompiled/$VERSION"
BUILD_DIR=$(mktemp -d "${TMPDIR:-/tmp}/ndmspc-precompile.XXXXXX")
trap 'rm -rf "$BUILD_DIR"' EXIT

mkdir -p "$CACHE_DIR"

ENTRIES=$(mktemp "${TMPDIR:-/tmp}/ndmspc-precompile-json.XXXXXX")
trap 'rm -rf "$BUILD_DIR" "$ENTRIES"' EXIT
: > "$ENTRIES"

TOTAL=0
BUILT=0
FAILED=0

for macro in $(find "$MACRO_DIR" -maxdepth 1 -type f -name '*.C' | sort); do
  name=$(basename "$macro")
  stem=${name%.C}
  TOTAL=$((TOTAL + 1))

  if ! grep -Eq "^[[:space:]]*(void|int)[[:space:]]+$stem[[:space:]]*\([[:space:]]*\)" "$macro"; then
    echo "precompile-macros.sh: skipping '$name' (no no-argument '$stem()' entry)" >&2
    FAILED=$((FAILED + 1))
    continue
  fi

  if [ "$MANIFEST_ONLY" -eq 1 ]; then
    if [ ! -f "$CACHE_DIR/${stem}_C.so" ]; then
      echo "precompile-macros.sh: skipping '$name': no precompiled library in $CACHE_DIR" >&2
      FAILED=$((FAILED + 1))
      continue
    fi
  else
    # ACLiC adds the macro's own path under the build directory; compile into a throwaway directory
    # and copy the flat library out, so the cache has a stable layout.
    LOG="$BUILD_DIR/$stem.log"
    if ! root -b -q -l -e "gSystem->CompileMacro(\"$macro\",\"k\",\"\",\"$BUILD_DIR\")" >"$LOG" 2>&1; then
      # ACLiC's return value is 1 even on a successful build (it propagates the expression's value),
      # so the produced library, not the exit status, decides.
      :
    fi

    so=$(find "$BUILD_DIR" -type f -name "${stem}_C.so" -print -quit)
    if [ -z "$so" ]; then
      echo "precompile-macros.sh: skipping '$name': ACLiC did not produce ${stem}_C.so" >&2
      tail -n 20 "$LOG" >&2
      FAILED=$((FAILED + 1))
      continue
    fi

    cp "$so" "$CACHE_DIR/${stem}_C.so"
    pcm=$(find "$BUILD_DIR" -type f -name "${stem}_C_ACLiC_dict_rdict.pcm" -print -quit)
    if [ -n "$pcm" ]; then
      cp "$pcm" "$CACHE_DIR/"
    fi
  fi

  read -r size mtime <<< "$(stat -c '%s %Y' "$macro")"
  if [ "$BUILT" -gt 0 ]; then
    printf ',\n' >> "$ENTRIES"
  fi
  printf '    "%s": {"library": "%s_C.so", "entry": "%s", "size": %s, "mtime": %s}' \
    "$name" "$stem" "$stem" "$size" "$mtime" >> "$ENTRIES"
  BUILT=$((BUILT + 1))
  if [ "$MANIFEST_ONLY" -eq 1 ]; then
    echo "precompile-macros.sh: recorded '$name' -> ${stem}_C.so"
  else
    echo "precompile-macros.sh: compiled '$name' -> ${stem}_C.so"
  fi
done

MANIFEST_TMP="$CACHE_DIR/precompiled.json.tmp"
{
  printf '{\n'
  printf '  "version": 1,\n'
  printf '  "root": %s,\n' "$VERSION"
  printf '  "sourceRoot": "%s",\n' "$SOURCE_ROOT"
  printf '  "createdAt": %s,\n' "$(date +%s)"
  printf '  "macros": {\n'
  cat "$ENTRIES"
  printf '\n  }\n}\n'
} > "$MANIFEST_TMP"
mv "$MANIFEST_TMP" "$CACHE_DIR/precompiled.json"

chmod 0755 "$CACHE_DIR"
find "$CACHE_DIR" -maxdepth 1 -type f -exec chmod 0644 {} +

if [ "$MANIFEST_ONLY" -eq 1 ]; then
  echo "precompile-macros.sh: manifest refreshed for $BUILT/$TOTAL macro(s) in $CACHE_DIR (ROOT $VERSION)"
  # The MCP tool catalog: what `tools/list` serves without loading a macro (see
  # NMcpServer::BuildTools). Written here, where the installed macros and the server binary are both
  # present; the runtime falls back to loading the macros when the file is absent.
  if command -v ndmspc-server >/dev/null 2>&1; then
    CATALOG="$CACHE_DIR/tools.catalog.json"
    # Every installed tool: the catalog is served to every room, and a room may name only a subset on
    # its command line, so a subset here would list tools its own group cannot offer.
    if NDMSPC_PRELOAD_TOOLS=all ndmspc-server --write-tool-catalog "$CATALOG" >/dev/null 2>&1; then
      echo "precompile-macros.sh: tool catalog written to $CATALOG"
    else
      rm -f "$CATALOG"
      echo "precompile-macros.sh: warning: could not write the tool catalog; tools/list will load macros" >&2
      if [ "$STRICT" -eq 1 ]; then
        FAILED=$((FAILED + 1))
      fi
    fi
  fi
else
  echo "precompile-macros.sh: $BUILT/$TOTAL macro(s) precompiled into $CACHE_DIR (ROOT $VERSION)"
fi

if [ "$STRICT" -eq 1 ] && [ "$FAILED" -gt 0 ]; then
  echo "precompile-macros.sh: $FAILED macro(s) skipped (--strict)" >&2
  exit 1
fi

exit 0
