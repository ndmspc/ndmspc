#!/bin/bash
#echo $#
if [ $# -lt 2 ];then
	echo "usage $0 infile outfile"
	echo "EXEC_FAILED"
	exit 1
fi
/usr/bin/cat $1 > $2  
if  [ $? -eq 0 ];then
	echo "EXEC_SUCCESS"
	exit 0
else
	echo "EXEC_FAILED"
	exit 1
fi
