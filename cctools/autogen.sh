#!/usr/bin/env bash

# set -x

../tools/find_lto_header.sh || echo "llvm-devel seems not to be installed - disabling LTO support"

echo "#include <vector>"|clang++ -stdlib=libc++ -xc++ -c -S -o- - &>/dev/null

if [ $? -ne 0 ]; then

    if [ ! -f tmp/have_libcxx ]; then
        echo ""
        echo "no working libc++ found, will build it from source ..."
        echo ""
        sleep 2
        ../tools/build_libcxx.sh || exit 1
    fi
fi

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
