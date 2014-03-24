#!/usr/bin/env bash

test -n "$DISABLE_LTO_SUPPORT" && exit 0

function try()
{
    LLVM_CONFIG="llvm-config$1"
    which $LLVM_CONFIG &>/dev/null

    if [ $? -eq 0 ]; then
        set -e
        LLVM_INC_DIR=`$LLVM_CONFIG --includedir`
        LLVM_LIB_DIR=`$LLVM_CONFIG --libdir`
        ln -sf "$LLVM_INC_DIR/llvm-c/lto.h" "include/llvm-c/lto.h"
        mkdir -p tmp
        echo -n "-L$LLVM_LIB_DIR -lLTO " > tmp/ldflags
        echo -n "-DLTO_SUPPORT=1 " > tmp/cflags
        echo -n "-DLTO_SUPPORT=1 " > tmp/cxxflags
        echo -n "$LLVM_LIB_DIR" > tmp/ldpath
        exit 0
    fi
}

try ""
try "-3.2"
try "-3.3"
try "-3.4"
try "-3.5"
try "-devel"

try "32"
try "33"
try "34"
try "35"

exit 1
