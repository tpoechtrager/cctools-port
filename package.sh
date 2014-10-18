#!/usr/bin/env bash

set -ex

DIR=`pwd`
PACKAGETMP=`mktemp -d /tmp/XXXXXXXX`

REVHASH=`git rev-parse --short HEAD`
CCTOOLSVER=`cat README.md | grep "Current Version: " | awk '{print $3}'`
LD64VER=`cat README.md | grep "Current Version: " | awk '{print $5}'`

PACKAGE=cctools-${CCTOOLSVER}-${LD64VER%?}_$REVHASH

mkdir $PACKAGETMP/$PACKAGE
cp -r . $PACKAGETMP/$PACKAGE

pushd $PACKAGETMP &>/dev/null

pushd $PACKAGE &>/dev/null
git clean -fdx &>/dev/null
rm -rf .git
rm -f cctools*.tar.*
rm -f package.sh
rm -f .gitignore
pushd cctools &>/dev/null
./autogen.sh
popd &>/dev/null
popd &>/dev/null

tar -cf - * | xz -9 -c - > $DIR/$PACKAGE.tar.xz

popd &>/dev/null

rm -rf $PACKAGETMP
