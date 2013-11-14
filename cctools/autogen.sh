#!/bin/sh
mkdir -p m4
aclocal
autoconf
libtoolize -c -i
autoheader
automake -a -c
