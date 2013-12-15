#!/usr/bin/env bash

set -ex

DIR=`pwd`
PACKAGETMP=`mktemp -d`

REVHASH=`git rev-parse --short HEAD`
CCTOOLSVER=`cat README.md | grep "Current Version: " | awk '{print $3}'`
LD64VER=`cat README.md | grep "Current Version: " | awk '{print $5}'`

PACKAGE=cctools-${CCTOOLSVER}-${LD64VER}_$REVHASH

mkdir $PACKAGETMP/$PACKAGE
cp -r . $PACKAGETMP/$PACKAGE

pushd $PACKAGETMP &>/dev/null

pushd $PACKAGE &>/dev/null
rm -rf .git
rm -f cctools*.tar.*
rm -f package.sh
popd &>/dev/null

XZ_OPT=-9 tar cJf $DIR/$PACKAGE.tar.xz *

popd &>/dev/null

rm -rf $PACKAGETMP
