#!/usr/bin/env bash

grep -n "__block," "$TCROOT/usr/include/unistd.h" &>/dev/null

if [ $? -ne 0 ]; then
    echo "no fix needed"
    exit
else
    echo "applying fix for broken unistd.h" 1>&2
fi

pushd "${0%/*}/../cctools" &>/dev/null
set -x

find . -type f \( -name "*.c" -o -name "*.cpp" -o -name "*.cc" -o -name "*.h" -o -name "*.hpp" \) -print0 | \
xargs -0 sed -i "s/#include <unistd.h>/#undef __block\n#include <unistd.h>\n#define __block __attribute__((__blocks__(byref)))/g"
