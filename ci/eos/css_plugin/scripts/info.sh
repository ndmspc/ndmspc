#!/bin/bash
#echo $#
if [ $# -lt 2 ]; then
  echo "usage $0 infile outfile"
  echo "EXEC_FAILED"
  exit 1
fi
hostname >$2
whoami >>$2
uptime >>$2
if [ $? -eq 0 ]; then
  echo "EXEC_SUCCESS"
  exit 0
else
  echo "EXEC_FAILED"
  exit 1
fi
