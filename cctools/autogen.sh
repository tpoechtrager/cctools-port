#!/bin/bash

eval `find_lto_header.sh`

echo "#include <vector>"|clang++ -stdlib=libc++ -xc++ -c -S -o- - &>/dev/null

test $? -eq 0 || { echo "libc++-devel is required" && exit 1; }

grep -n "__block," /usr/include/unistd.h &>/dev/null
if [ $? -eq 0 ]; then
    echo "applying workaround for buggy unistd.h"
    ./fix_unistd_issue.sh
fi

mkdir -p m4
aclocal
autoconf
libtoolize -c -i
autoheader
automake -a -c
