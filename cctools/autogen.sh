#!/usr/bin/env bash

# set -x

# LIBTOOLIZE=libtoolize
which glibtoolize &>/dev/null
if [ $? -eq 0 ]; then
    LIBTOOLIZE=glibtoolize
else
    LIBTOOLIZE=libtoolize
fi

export LIBTOOLIZE
mkdir -p m4

$LIBTOOLIZE -c -i --force
aclocal -I m4
autoconf

automake -a -c

