#!/bin/bash

[ -n "$1" ] || {
  echo "Set name of the class name as first argument !!!"
  exit 1
}

PROJECT_DIR="$(dirname $(readlink -m ${BASH_ARGV[0]}))"
echo $PROJECT_DIR

TEMPLATE_DIR="$PROJECT_DIR/etc/template"
cp -a $TEMPLATE_DIR/Example.h $1.h
cp -a $TEMPLATE_DIR/Example.cxx $1.cxx

sed -i "s/Example/$1/g" $1.h
sed -i "s/NdmspcExample_H/Ndmspc$1_H/g" $1.h
sed -i "s/Example/$1/g" $1.cxx
