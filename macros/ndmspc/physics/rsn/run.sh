#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"

VERBOSE=false
RESET=""
for arg in "$@"; do
  case "$arg" in
    -v|--verbose) VERBOSE=true ;;
    reset|reset-file|reset-merge) RESET="$arg" ;;
    *) echo "usage: $0 [-v|--verbose] [reset|reset-file|reset-merge]" >&2; exit 2 ;;
  esac
done

FILE_ARGS=()
MERGE_ARGS=()
case "$RESET" in
  reset)       rm -rf .ndmspc/chunktest chunk_merging_ngnt.root chunk_merged* binning_ngnt.root
               FILE_ARGS=(--reset); MERGE_ARGS=(--reset) ;;
  reset-file)  rm -rf .ndmspc/chunktest chunk_merging_ngnt.root
               FILE_ARGS=(--reset) ;;
  reset-merge) rm -rf chunk_merged*
               MERGE_ARGS=(--reset) ;;
  "")          ;;
esac

V=()
if [ "$VERBOSE" = true ]; then V=(-v); fi

ndmspc-cli file    -c NFileToNGnTree.json -f files.txt --run "${FILE_ARGS[@]}" "${V[@]}"
ndmspc-cli merge   -c NGnTreeMerge.json --run "${MERGE_ARGS[@]}" "${V[@]}"
ndmspc-cli binning -c NGnTreeBinning.json --run "${V[@]}"
