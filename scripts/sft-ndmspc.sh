#!/bin/bash

MYTAG="v1.3.3-rc3"

BASEDIR=/tmp/sft-ndmspc

rm -rf $BASEDIR
mkdir -p $BASEDIR
cd $BASEDIR
cp -a /var/home/mvala/git/gitlab.com/ndmspc/ndmspc $BASEDIR/ndmspc
#git clone https://gitlab.com/ndmspc/ndmspc.git || exit 1
cd $BASEDIR/ndmspc
git checkout $MYTAG
rm -rf build
mkdir build
cd build
cmake -DCMAKE_INSTALL_PREFIX=$BASEDIR/ndmspc_bin ..
make -j20 install

cd $BASEDIR

# remove source directory (avoid picking up headers from CMAKE_SOURCE_DIR)
rm -rf $BASEDIR/ndmspc
export PATH=$BASEDIR/ndmspc_bin/bin:$PATH
export LD_LIBRARY_PATH=$BASEDIR/ndmspc_bin/lib:$LD_LIBRARY_PATH
export ROOT_INCLUDE_DIR=$BASEDIR/ndmspc_bin/include

ndmspc-run $BASEDIR/ndmspc_bin/tutorial/05_single_bining/NSingleBinning01Gaus.C
