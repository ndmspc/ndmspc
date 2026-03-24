#!/bin/bash


SLURM_ARRAY_TASK_ID=${SLURM_ARRAY_TASK_ID-1}
JOB_ID=${SLURM_ARRAY_JOB_ID-"$SLURM_JOBID"}

curl -X PATCH http://localhost:8080/api/jobs  -H "Content-Type: application/json"   -d '{"name":"job_'$JOB_ID'","task":'$SLURM_ARRAY_TASK_ID',"action":"R","rc":0}'
export
hostname
date
$*
date
curl -X PATCH http://localhost:8080/api/jobs  -H "Content-Type: application/json"   -d '{"name":"job_'$JOB_ID'","task":'$SLURM_ARRAY_TASK_ID',"action":"D", "rc":0}'
