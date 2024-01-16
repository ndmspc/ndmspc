#!/bin/bash

# the directory of the script
# DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
DIR=/tmp/ndmspc

[ -d $DIR ] || mkdir -p $DIR

# the temp directory used, within $DIR
# omit the -p parameter to create a temporal directory in the default location
WORK_DIR=`mktemp -d -p "$DIR"`
LOG_FILE="$WORK_DIR/ndmspc.log"

# check if tmp dir was created
if [[ ! "$WORK_DIR" || ! -d "$WORK_DIR" ]]; then
  echo "Could not create temp dir"
  exit 1
fi

# deletes the temp directory
function cleanup {
  echo $LOG_FILE
  cat $LOG_FILE
  rm -rf "$WORK_DIR"
  echo "Deleted temp working directory $WORK_DIR"
}

function +() (
  set -f  # no double expand
  eval "$@" | tee >> $LOG_FILE
  # $@ | tee >> $LOG_FILE
  return ${PIPESTATUS[0]}  # real rc
)  

# register the cleanup function to be called on the EXIT signal
trap cleanup EXIT

echo "Running '$@' ..." > $LOG_FILE
# $@ > $LOG_FILE 2>&1
+ $@
