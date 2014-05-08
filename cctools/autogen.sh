#!/usr/bin/env bash

# set -x

test -n "$DISABLE_LTO_SUPPORT" && rm -rf tmp

../tools/find_lto_header.sh || echo "llvm-devel seems not to be installed - disabling LTO support"

grep -n "__block," /usr/include/unistd.h &>/dev/null
if [ $? -eq 0 ]; then
    echo "applying workaround for buggy unistd.h"
    ../tools/fix_unistd_issue.sh
fi

mkdir -p m4
aclocal
autoconf
libtoolize -c -i
automake -a -c
