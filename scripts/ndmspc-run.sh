#!/bin/bash
NDMSPC_DEBUG=${NDMSPC_DEBUG-0}
[[ $NDMSPC_DEBUG == 1 ]] && set -x
NDMSPC_LOGDIR_XRD=${NDMSPC_LOGDIR_XRD-"root://eos.ndmspc.io//eos/ndmspc/scratch/ndmspc/logs"}
NDMSPC_LOG_TYPE=${NDMSPC_LOG_TYPE-"always"}
NDMSPC_SALSA_URL=${NDMSPC_SALSA_URL-"tcp://localhost:41000"}
NDMSPC_SALSA_ARGS=${NDMSPC_SALSA_ARGS-""}
NDMSPC_JOB_FILE=${NDMSPC_JOB_FILE-"/tmp/ndmspc-jobs-$$.txt"}
NDMSPC_RUN_SCRIPT=${NDMSPC_RUN_SCRIPT-"ndmspc"}
NDMSPC_RUN_CONFIG=${1-"myAnalysis.json"}
NDMSPC_DRY_RUN=${NDMSPC_DRY_RUN-0}
NDMSPC_POINT_RUN_MACRO=${NDMSPC_POINT_RUN_MACRO-"$NDMSPC_MACRO_DIR/NdmspcPointRun.C"}



# env

# Generate multiple jobs to jobs.txt
root -b -q -l $NDMSPC_MACRO_DIR/NdmspcGenerateJobs.C'("'$NDMSPC_RUN_CONFIG'","'$NDMSPC_JOB_FILE'" )'
NLINES=$(cat $NDMSPC_JOB_FILE | wc -l)

# exit 1

if [[ $NLINES == 0 ]];then
  echo "Error: No jobs generated !!!"
  exit 1
fi


# Setup salsa

# Run

if [[ $NDMSPC_SALSA_URL != "tcp://localhost:41000" ]];then
  NDMSPC_SALSA_ARGS="$NDMSPC_SALSA_ARGS --batch"
  export NDMSPC_POINT_RUN_MACRO="/usr/share/ndmspc/macros/NdmspcPointRun.C"
fi

# echo "NDMSPC_MACRO_DIR=$NDMSPC_MACRO_DIR"
export SALSA_ENV_NAMES="ROOT_INCLUDE_PATH NDMSPC_POINT_RUN_MACRO NDMSPC_LOGDIR_XRD NDMSPC_LOG_TYPE"
# export SALSA_ENV_NAMES="NDMSPC_LOGDIR_XRD NDMSPC_LOG_TYPE"
for e in $SALSA_ENV_NAMES;do
#   echo "exporting $e ..."
  export $e
done


if [[ $NDMSPC_DRY_RUN == 0 ]];then
  salsa-feeder -s $NDMSPC_SALSA_URL -f $NDMSPC_JOB_FILE $NDMSPC_SALSA_ARGS
else
  echo ""
  echo "Warning: Job is not executed because running in 'dryrun' mode !!!"
  echo "  run 'NDMSPC_DRY_RUN=0 ndmspc-run.sh'"
  echo ""
fi
