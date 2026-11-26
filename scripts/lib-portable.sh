#!/bin/bash
# Portable replacements for the GNU-only tools the scripts used to call directly.
#
# Sourced, never executed: `source "$(dirname "${BASH_SOURCE[0]}")/lib-portable.sh"`.
# Every helper prefers the GNU tool and only falls back to the BSD one, so Linux
# behaviour is unchanged and macOS works instead of failing on a missing option.
#
# The helpers are prefixed `ndmspc_` because the callers are scripts of very different
# ages and some of them already define their own `die`, `run` or `usage`.

# Absolute path of an existing path, resolving symlinks and `..` without requiring the
# file itself to be a symlink: `readlink -m` on GNU, plain `cd`+`pwd -P` on Darwin (which
# has no `readlink -m`; its `readlink -f` also needs every component to exist).
ndmspc_realpath() {
  local path=$1
  if readlink -m / >/dev/null 2>&1; then
    readlink -m "$path"
    return
  fi
  # Directory or existing file: resolve it through the filesystem. A non-existent leaf is
  # split off first so resolving the directory still works.
  local dir base
  if [ -d "$path" ]; then
    (cd "$path" >/dev/null && pwd -P)
    return
  fi
  dir=$(dirname "$path")
  base=$(basename "$path")
  (cd "$dir" >/dev/null 2>&1 && printf '%s/%s\n' "$(pwd -P)" "$base")
}

# Number of CPUs, for `make -j`.
ndmspc_nproc() {
  if command -v nproc >/dev/null 2>&1; then
    nproc
  elif command -v sysctl >/dev/null 2>&1; then
    sysctl -n hw.ncpu
  else
    echo 1
  fi
}

# Lowercase hex sha256 of a file, printed bare (no filename).
ndmspc_sha256() {
  local file=$1
  if command -v sha256sum >/dev/null 2>&1; then
    sha256sum "$file" | cut -d' ' -f1
  else
    shasum -a 256 "$file" | cut -d' ' -f1
  fi
}

# "<size> <mtime>" in the `stat -c '%s %Y'` format: bytes and seconds since the epoch.
ndmspc_stat_size_mtime() {
  local file=$1
  if stat -c '%s %Y' "$file" >/dev/null 2>&1; then
    stat -c '%s %Y' "$file"
  else
    # BSD/macOS stat: %z is size, %m is mtime in seconds.
    stat -f '%z %m' "$file"
  fi
}
