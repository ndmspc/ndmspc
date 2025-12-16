#!/bin/bash

export

echo "hostname: $(hostname)"
echo "user: $(whoami)"
echo "date: $(date)"
export ROOT_INCLUDE_PATH="$ROOT_INCLUDE_PATH:/usr/include/ndmspc"
echo "Running '$*' ..."
$*
echo "date: $(date)"

echo "Finished '$*' ..."
