#!/bin/bash

CLASSNAME=${1-""}
CLASSNAME_DERIVATIVE=${2-"NObject"}

[ -n "$CLASSNAME" ] || {
  echo "Set the class name as first argument !!!"
  exit 1
}
WKDIR=$(pwd)
PROJECT_DIR="$(dirname $(dirname $(readlink -m $0)))"
echo $PROJECT_DIR

TEMPLATE_DIR="$PROJECT_DIR/etc/template"
cp -a $TEMPLATE_DIR/${CLASSNAME_DERIVATIVE}.h.tmpl $WKDIR/${CLASSNAME}.h
cp -a $TEMPLATE_DIR/${CLASSNAME_DERIVATIVE}.cxx.tmpl $WKDIR/${CLASSNAME}.cxx

sed -i "s/__CLASSNAME__/$CLASSNAME/g" $WKDIR/$1.h
sed -i "s/__CLASSNAME_DERIVATIVE__/$CLASSNAME_DERIVATIVE/g" $WKDIR/${CLASSNAME}.h
sed -i "s/__CLASSNAME__/$CLASSNAME/g" $WKDIR/$1.cxx
sed -i "s/__CLASSNAME_DERIVATIVE__/$CLASSNAME_DERIVATIVE/g" $WKDIR/${CLASSNAME}.cxx
