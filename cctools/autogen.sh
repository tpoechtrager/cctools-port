#!/usr/bin/env bash

# set -x

grep -n "__block," /usr/include/unistd.h &>/dev/null
if [ $? -eq 0 ]; then
    echo "applying workaround for buggy unistd.h"
    ../tools/fix-unistd-issue.sh
fi

mkdir -p m4
aclocal
autoconf
libtoolize -c -i
automake -a -c
