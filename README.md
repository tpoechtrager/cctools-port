# Apple cctools and ld64 port for Linux, FreeBSD and macOS #

Current Version: cctools: 927.0.2, ld64: 450.3.  
Originally ported by [cjacker](http://ios-toolchain-based-on-clang-for-linux.googlecode.com).

## SUPPORTED HOSTS ##

**SUPPORTED OPERATING SYSTEMS:**

Linux, FreeBSD, Mac OS X, iOS,  
OpenBSD and NetBSD

**SUPPORTED HOST ARCHITECTURES:**

x86, x86_64, arm, arm64/aarch64

## SUPPORTED TARGET ARCHITECTURES ##

armv6, armv7, armv7s, arm64, arm64e (untested),  
arm64_32 (untested), i386, x86_64,  x86_64h,  
armv6m, armv7k, armv7m and armv7em

## SUPPORTED TARGET OPERATING SYSTEMS ##

Mac OS X, iOS, watchOS (untested) and tvOS (untested)

## DEPENDENCIES ##

`Clang 3.4+` 

SDKs with .tdb stubs (>= Xcode 7) require the TAPI library to be installed.  
=> https://github.com/tpoechtrager/apple-libtapi

musl-libc based systems require the musl-fts library to be installed.
=> https://github.com/pullmoll/musl-fts

Optional, but recommended:

`llvm-devel`               (For Link Time Optimization Support)  
`uuid-devel`               (For ld64 `-random_uuid` Support)  
`llvm-devel` + `xar-devel` (For ld64 `-bitcode_bundle` Support)

You can find xar [here](https://github.com/mackyle/xar).  
Do not install libxar-dev on Ubuntu, it's a different package.

## INSTALLATION ##

### Install Apple's TAPI library:
This step is only required if you intend to use SDKs with .tdb stubs.

    git clone https://github.com/tpoechtrager/apple-libtapi.git
    cd apple-libtapi
    [INSTALLPREFIX=/home/user/cctools] ./build.sh
    ./install.sh

### Install cctools and ld64:
    git clone https://github.com/tpoechtrager/cctools-port.git
    cd cctools-port/cctools
    ./configure \
        [--prefix=/home/user/cctools] \
        [--with-libtapi=/home/user/cctools] \
        [--target=<target>] \
        [--with-llvm-config=...]
    make
    make install

target = `i386-apple-darwin11`, `x86_64-apple-darwin11`, `arm-apple-darwin11`, ...

If you get compile errors because of `unistd.h`, then please run  
`../tools/fix_unistd_issue.sh` and restart compiling.

## TRAVIS CI ##

[![Build Status](https://travis-ci.org/tpoechtrager/cctools-port.svg?branch=921-ld64-409.12)](https://travis-ci.org/tpoechtrager/cctools-port)
