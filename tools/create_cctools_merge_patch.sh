#!/usr/bin/env bash

BASE_VERSION=1030.6.3
NEW_VERSION=1035.1.102

set -e

CDIR=`pwd`
TMPDIR=`mktemp -d`

pushd $TMPDIR

wget https://www.opensource.apple.com/tarballs/cctools/cctools-$BASE_VERSION.tar.gz
wget https://www.opensource.apple.com/tarballs/cctools/cctools-$NEW_VERSION.tar.gz

tar xzf cctools-$BASE_VERSION* &>/dev/null
tar xzf cctools-$NEW_VERSION* &>/dev/null
rm *.tar.gz

pushd cctools*$NEW_VERSION*

echo "creating patch..."
diff -Naur ../cctools-*$BASE_VERSION* . > "$CDIR/cctools-${BASE_VERSION}-${NEW_VERSION}.patch" || true
echo "done"

popd

rm -rf $TMPDIR
