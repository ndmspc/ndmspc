#!/bin/bash

# shellcheck source=lib-portable.sh disable=SC1091
source "$(dirname "${BASH_SOURCE[0]}")/lib-portable.sh"

PROJECT_DIR="$(dirname "$(dirname "$(ndmspc_realpath "${BASH_SOURCE[0]}")")")"

export NDMSPC_DIR=$PROJECT_DIR
export NDMSPC_ROOT=$PROJECT_DIR/share/ndmspc
export NDMSPC_MACRO_DIR=$PROJECT_DIR/macros
export NDMSPC_TOOL_DIR=$PROJECT_DIR/tools
export PATH="$PROJECT_DIR/bin:$PATH"
# macOS resolves installed libraries through DYLD_LIBRARY_PATH, not LD_LIBRARY_PATH.
if [ "$(uname -s)" = "Darwin" ]; then
  export DYLD_LIBRARY_PATH="$PROJECT_DIR/lib:$PROJECT_DIR/lib64:$DYLD_LIBRARY_PATH"
else
  export LD_LIBRARY_PATH="$PROJECT_DIR/lib:$PROJECT_DIR/lib64:$LD_LIBRARY_PATH"
fi
export CPLUS_INCLUDE_PATH="$PROJECT_DIR/include/ndmspc:$PROJECT_DIR/include:$CPLUS_INCLUDE_PATH"
# export ROOT_INCLUDE_PATH="$PROJECT_DIR/include/ndmspc:$PROJECT_DIR/include:$ROOT_INCLUDE_PATH"
