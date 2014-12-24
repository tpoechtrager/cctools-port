#!/usr/bin/env bash

# set -x

grep -n "__block," /usr/include/unistd.h &>/dev/null
if [ $? -eq 0 ]; then
    echo "applying workaround for buggy unistd.h"
    ../tools/fix_unistd_issue.sh
fi

# LIBTOOLIZE=libtoolize
which glibtoolize &>/dev/null
if [ $? -eq 0 ]; then
    LIBTOOLIZE=glibtoolize
else
    LIBTOOLIZE=libtoolize
fi

export LIBTOOLIZE
mkdir -p m4

$LIBTOOLIZE -c -i
aclocal -I m4
autoconf

automake -a -c

