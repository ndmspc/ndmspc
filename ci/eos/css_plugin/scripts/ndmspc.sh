#!/bin/bash
#echo $#
if [ $# -lt 2 ]; then
  echo "usage $0 infile outfile"
  echo "EXEC_FAILED"
  exit 1
fi
output_file=$(mktemp --suffix=.txt)
while IFS= read -r line; do
  echo "Executing: $line" >/dev/null 2>&1
  eval "sbatch /usr/bin/ndmspc-run $line"
done <"$1" >"$output_file"
cat $output_file >$2
if [ $? -eq 0 ]; then
  echo "EXEC_SUCCESS"
  exit 0
else
  echo "EXEC_FAILED"
  exit 1
fi
