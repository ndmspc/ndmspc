#!/bin/bash
set -x

NDMSPC_LOGDIR_XRD=${NDMSPC_LOGDIR_XRD-"root://eos.ndmspc.io//eos/ndmspc/scratch/ndmspc/logs"}
NDMSPC_LOG_TYPE=${NDMSPC_LOG_TYPE-"always"}
NDMSPC_SALSA_URL=${NDMSPC_SALSA_URL-"tcp://localhost:41000"}
NDMSPC_JOB_FILE=${NDMSPC_JOB_FILE-"/tmp/ndmspc-jobs-$$.txt"}
NDMSPC_RUN_SCRIPT=${NDMSPC_RUN_SCRIPT-"ndmspc"}
NDMSPC_RUN_CONFIG=${NDMSPC_RUN_CONFIG-"$NDMSPC_MACRO_DIR/test/rsn/myAnalysis.json"}
export SALSA_ENV_NAMES="ROOT_INCLUDE_PATH NDMSPC_MACRO_DIR NDMSPC_LOGDIR_XRD NDMSPC_LOG_TYPE PATH"
for e in $SALSA_ENV_NAMES;do
#   echo "exporting $e ..."
  export $e
done

# env

# Generate multiple jobs to jobs.txt
root -b -q -l $NDMSPC_MACRO_DIR/NdmspcGenerateJobs.C'("'$NDMSPC_RUN_CONFIG'","'$NDMSPC_JOB_FILE'" )'
# cat $NDMSPC_JOB_FILE


# Setup salsa

# Run

salsa-feeder -s $NDMSPC_SALSA_URL -f $NDMSPC_JOB_FILE $1
# salsa-feeder -s $NDMSPC_SALSA_URL -t "sleep 1:1" $1
