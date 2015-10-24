# Apple cctools port for Linux, *BSD and Windows (Cygwin) #

Current Version: 877.5 + ld64-253.3.  
Originally ported by [cjacker](http://ios-toolchain-based-on-clang-for-linux.googlecode.com).

## SUPPORTED HOSTS ##

**SUPPORTED OPERATING SYSTEMS:**

Linux, FreeBSD, NetBSD, OpenBSD, DragonFlyBSD,  
Windows (Cygwin), Mac OS X and iOS

**SUPPORTED HOST ARCHITECTURES:**

x86, x86_64, arm

Untested, but compiles:

aarch64, ppc, ppc64

## SUPPORTED TARGET ARCHITECTURES ##

armv4t, armv5, armv6, armv7, armv7f, armv7k, armv7s, armv6m  
armv7m, armv7em, armv8, arm64, arm64v8, i386, x86_64 and x86_64h.

## SUPPORTED TARGET OPERATING SYSTEMS ##

Mac OS X, iOS, watchOS (untested) and tvOS (untested)

## DEPENDENCIES ##

`Clang 3.2+ or gcc/g++/gcc-objc 4.7+`, `automake`, `autogen` and `libtool`.

Optional, but recommended:

`llvm-devel`               (For Link Time Optimization Support)  
`uuid-devel`               (For ld64 `-random_uuid` Support)  
`llvm-devel` + `xar-devel` (For ld64 `-bitcode_bundle` Support)

You can find xar [here](https://github.com/mackyle/xar).  
Do not install libxar-dev on Ubuntu, it's a different package.

## INSTALLATION ##

* `cd cctools`
* `./autogen.sh`
* `./configure --prefix=<installdir> --target=<target> [--with-llvm-config=...]`
* `make`
* `make install`

target = `i386-apple-darwin11`, `x86_64-apple-darwin11`, `arm-apple-darwin11`, ...

If you get compile errors because of `unistd.h`, then please run  
`../tools/fix_unistd_issue.sh` and restart compiling.

## TRAVIS CI ##

[![Build Status](https://travis-ci.org/tpoechtrager/cctools-port.svg?branch=master)](https://travis-ci.org/tpoechtrager/cctools-port)
