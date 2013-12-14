#!/usr/bin/env bash

dir=`pwd`
packagetmp=`mktemp -d`
cp -r . $packagetmp || exit $?
pushd $packagetmp >/dev/null
./cleanup.sh 2>/dev/null
rm -rf .git 2>/dev/null
rm cctools*.tar.* 2>/dev/null
tar -pczf $dir/cctools-XXX-ld64-XXX.tar.gz * || exit $?
popd >/dev/null
rm -rf $packagetmp || exit $?
