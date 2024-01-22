#!/bin/bash
NDMSPC_LOG_XRD=${NDMSPC_LOG_XRD-""}
NDMSPC_LOGDIR_XRD=${NDMSPC_LOGDIR_XRD-""}
NDMSPC_LOGFILE_XRD=${NDMSPC_LOGFILE_XRD-"ndmspc.log"}
NDMSPC_LOG_TYPE=${NDMSPC_LOG_TYPE-"error-only"}


# NDMSPC_LOGDIR_XRD="root://eos.ndmspc.io//eos/ndmspc/scratch/ndmspc/logs"
# NDMSPC_LOG_TYPE="always"
if [ -z "$NDMSPC_LOG_XRD" ];then
  [ -n "$NDMSPC_LOGDIR_XRD" ] &&  NDMSPC_LOG_XRD="$NDMSPC_LOGDIR_XRD"
  [ -n "$SALSA_CLUSTER_ID" ] && NDMSPC_LOG_XRD="$NDMSPC_LOGDIR_XRD/$SALSA_CLUSTER_ID"
  [ -n "$SALSA_JOB_ID" ] && NDMSPC_LOG_XRD="$NDMSPC_LOG_XRD/$SALSA_JOB_ID"
  [ -n "$SALSA_TASK_ID" ] && NDMSPC_LOG_XRD="$NDMSPC_LOG_XRD/$SALSA_TASK_ID"
  NDMSPC_LOG_XRD="$NDMSPC_LOG_XRD/$NDMSPC_LOGFILE_XRD"
fi

[ -z "$NDMSPC_LOG_XRD" ] && NDMSPC_LOG_XRD="$NDMSPC_LOGFILE_XRD"


echo $NDMSPC_LOG_XRD

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

if [ -n "$NDMSPC_LOGDIR_XRD" ];then
  if ! command -v xrdcp &> /dev/null; then
      echo "Warning: 'xrdcp' is not installed !!! Please install it first (dnf install xrootd-client) to send logs to '$NDMSPC_LOGDIR_XRD' ..."
      export NDMSPC_LOGDIR_XRD=""
  fi
fi


# deletes the temp directory
function cleanup {
  cat $LOG_FILE
  rm -rf "$WORK_DIR"
  echo "Deleted temp working directory $WORK_DIR"
}

function +() (
  set -f  # no double expand
  eval "$@" | tee >> $LOG_FILE
  RC=${PIPESTATUS[0]}
  echo "cmd='$@' rc=$RC" | tee >> $LOG_FILE
  # $@ | tee >> $LOG_FILE
  if [ -n "$NDMSPC_LOG_XRD" ];then
    
    case $NDMSPC_LOG_TYPE in
      error-only)
        [[ $RC -gt 0 ]] && export NDMSPC_LOG_COPY=1
        ;;
      always)
        export NDMSPC_LOG_COPY=1
        ;;
      *)
        export NDMSPC_LOG_COPY=0
        ;;
    esac
    if [[ $NDMSPC_LOG_COPY == 1 ]];then
      echo "Copying logs (type=$NDMSPC_LOG_TYPE) to '$NDMSPC_LOG_XRD' ..."
      NDMSPC_LOGFILE_HTTP=$(echo ${NDMSPC_LOG_XRD/root/https} | tr -s /)
      xrdcp -f $LOG_FILE $NDMSPC_LOG_XRD && echo -e "Logs:\n\t$NDMSPC_LOGFILE_HTTP\n\t$NDMSPC_LOG_XRD\n" || { echo "Warning: Problem doing 'xrdcp -f "$LOG_FILE" "$NDMSPC_LOG_XRD" !!! Log is not produced at $NDMSPC_LOG_XRD ..."; true; }
    fi
    
  fi

  return $RC # real rc
)  

# register the cleanup function to be called on the EXIT signal
trap cleanup EXIT

echo "Running '$@' ..." > $LOG_FILE
# $@ > $LOG_FILE 2>&1
+ $@
