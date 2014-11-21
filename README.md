# Apple cctools port for Linux, FreeBSD and NetBSD #

Current Version: 862 + ld64-134.9.  
Originally ported by [cjacker](http://ios-toolchain-based-on-clang-for-linux.googlecode.com).

## SUPPORTED HOSTS ##

Linux  
FreeBSD  
NetBSD  
Mac OS X  
iOS (partially)

## SUPPORTED TARGETS ##

armv4t, armv5, armv6, armv7, armv7f, armv7k, armv7s, i386 and x86_64.

## DEPENDENCIES ##

`Clang 3.2+`, `automake`, `autogen` and `libtool`.

On Linux / FreeBSD / NetBSD you also need to install:

`uuid-devel` and `openssl-devel`.

Optional, but recommended deps:

`llvm-devel` (Enables Link Time Optimization)

## INSTALLATION ##

* `cd cctools`
* `./autogen.sh`
* `./configure --prefix=<installdir> --target=<target>`
* `make`
* `make install`

target = `i386-apple-darwin11`, `x86_64-apple-darwin11`, `arm-apple-darwin11`, ...

If you get compile errors because of `unistd.h`, then please run  
`../tools/fix_unistd_issue.sh` and restart compiling.
