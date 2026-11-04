#!/bin/bash -e
# Writes the version into <source>/VERSION, where cmake/version.cmake reads it when the
# tree has no .git: the CPack source tarball (make dist / make srpm) and the Docker build
# context (ci/Dockerfile.base, .dockerignore strips .git). Uses the same `git describe`
# input as the CMake resolver, so the two agree.
PROJECT_DIR="$(dirname "$(dirname "$(readlink -m "$0")")")"

for arg in "$@"; do
  case $arg in
    -h|--help)
      echo "Usage: $0 [-h]"
      echo "Writes the git describe version to $PROJECT_DIR/VERSION."
      exit 0
      ;;
    *)
      echo "Unknown argument: $arg" >&2
      exit 1
      ;;
  esac
done

desc=""
if [ -d "$PROJECT_DIR/.git" ]; then
  desc=$(cd "$PROJECT_DIR" &&
    { git describe --tags --long --dirty 2>/dev/null || git describe --always 2>/dev/null; } || true)
fi

if [ -z "$desc" ]; then
  # No git metadata (Docker build context, unpacked tarball): keep the version the tree
  # already carries rather than clobbering it with the fallback.
  if [ -s "$PROJECT_DIR/VERSION" ]; then
    echo "Kept VERSION: $(head -n 1 "$PROJECT_DIR/VERSION")"
    exit 0
  fi
  desc="${MY_PROJECT_VER:-9999.0.0}"
fi

printf '%s\n' "$desc" > "$PROJECT_DIR/VERSION"
echo "Wrote VERSION: $desc"
