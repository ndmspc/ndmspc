#!/usr/bin/env bash
#
# Report the OpenSSL libraries linked by ndmspc and ROOT.
#
# Usage: scripts/check-openssl.sh [3|1.1]
#
#   no argument   print the NEEDED entries and whether both versions coexist
#   3 / 1.1       fail when the ndmspc libraries do not use that version
#
# A v3 build records libssl.so.3/libcrypto.so.3 and can run with an OpenSSL 1.1
# directory in LD_LIBRARY_PATH (ROOT keeps using libssl.so.1.1): the loader matches
# the exact SONAME, so both libraries are mapped side by side.
set -euo pipefail

root_dir=$(cd "$(dirname "$0")/.." && pwd)
lib_dir="${root_dir}/lib"
[ -d "$lib_dir" ] || lib_dir="${root_dir}/lib64"

needed() {
  local file=$1
  [ -f "$file" ] || return 0
  if command -v readelf >/dev/null 2>&1; then
    readelf -d "$file" 2>/dev/null | sed -n 's/.*Shared library: \[\(.*\)\]/\1/p'
  else
    objdump -p "$file" 2>/dev/null | sed -n 's/^ *NEEDED *//p'
  fi
}

show() {
  local label=$1 file=$2 libs
  libs=$(needed "$file" | grep -E '^lib(ssl|crypto)\.so' | tr '\n' ' ' || true)
  libs=${libs% }
  printf '%-32s %s\n' "$label" "${libs:-<none>}"
}

echo "ndmspc:"
for file in "$lib_dir/libNdmspcHttp.so" "$lib_dir/libNdmspcCore.so" \
            "$root_dir/bin/ndmspc-server" "$root_dir/bin/ndmspc-ws-client" \
            "$root_dir/bin/ndmspc-cli"; do
  [ -f "$file" ] && show "  $(basename "$file")" "$file"
done

root_config=""
if [ -n "${ROOTSYS:-}" ] && [ -x "$ROOTSYS/bin/root-config" ]; then
  root_config="$ROOTSYS/bin/root-config"
elif command -v root-config >/dev/null 2>&1; then
  root_config=$(command -v root-config)
fi

if [ -n "$root_config" ]; then
  show "  ROOT libRIO.so" "$("$root_config" --libdir)/libRIO.so"
else
  printf '%-32s %s\n' "  ROOT libRIO.so" "<root-config not found>"
fi

expected=${1:-}
if [ -n "$expected" ]; then
  libs=$(needed "$lib_dir/libNdmspcHttp.so" | grep -E '^libssl\.so' || true)
  case "$expected" in
    3)
      [[ "$libs" == *"libssl.so.3"* ]] || { echo "ERROR: expected OpenSSL 3, got: ${libs:-none}"; exit 1; }
      ;;
    1.1)
      [[ "$libs" == *"libssl.so.1.1"* ]] || { echo "ERROR: expected OpenSSL 1.1, got: ${libs:-none}"; exit 1; }
      ;;
    *)
      echo "usage: $0 [3|1.1]" >&2
      exit 2
      ;;
  esac
  echo "OK: ndmspc uses OpenSSL $expected"
fi
