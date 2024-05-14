#!/bin/bash

PROJECT_DIR="$(dirname $(dirname $(readlink -m ${BASH_ARGV[0]})))"

export NDMSPC_DIR=$PROJECT_DIR
export NDMSPC_MACRO_DIR=$PROJECT_DIR/macros
export PATH="$PROJECT_DIR/bin:$PATH"
export LD_LIBRARY_PATH="$PROJECT_DIR/lib:$PROJECT_DIR/lib64:$LD_LIBRARY_PATH"
export CPLUS_INCLUDE_PATH="$PROJECT_DIR/include/ndmspc:$PROJECT_DIR/include:$CPLUS_INCLUDE_PATH"
export ROOT_INCLUDE_PATH="$PROJECT_DIR/include/ndmspc:$PROJECT_DIR/include:$ROOT_INCLUDE_PATH"
