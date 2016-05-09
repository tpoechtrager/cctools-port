#!/usr/bin/env bash

BASE_VERSION=253.3
NEW_VERSION=253.9

set -e

CDIR=`pwd`
TMPDIR=`mktemp -d`

pushd $TMPDIR

wget http://www.opensource.apple.com/tarballs/ld64/ld64-$BASE_VERSION.tar.gz
wget http://www.opensource.apple.com/tarballs/ld64/ld64-$NEW_VERSION.tar.gz

tar xzf ld64-$BASE_VERSION.tar.gz &>/dev/null
tar xzf ld64-$NEW_VERSION.tar.gz &>/dev/null

pushd ld64-$NEW_VERSION*

echo "creating patch..."
diff -Naur ../ld64-$BASE_VERSION . > "$CDIR/ld64-${BASE_VERSION}-${NEW_VERSION}.patch" || true
echo "done"

popd

rm -rf $TMPDIR
