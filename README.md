# Apple cctools and ld64 port for Linux, *BSD and macOS #

Current Version: cctools: 986, ld64: 711.  
Originally ported by [cjacker](http://ios-toolchain-based-on-clang-for-linux.googlecode.com).

## SUPPORTED HOSTS ##

**SUPPORTED OPERATING SYSTEMS:**

Linux, Android (Termux), FreeBSD,  
Mac OS X, iOS, OpenBSD and NetBSD

**SUPPORTED HOST ARCHITECTURES:**

x86, x86_64, arm, arm64/AArch64, PowerPC/PowerPC64

## SUPPORTED TARGET ARCHITECTURES ##

armv6, armv7, armv7s, arm64, arm64e,  
arm64_32 (untested), i386, x86_64, x86_64h,  
armv6m, armv7k, armv7m and armv7em

arm64e requires [Apple LLVM/Clang](https://github.com/apple/llvm-project).

## SUPPORTED TARGET OPERATING SYSTEMS ##

macOS, iOS, tvOS, watchOS, bridgeOS, Mac Catalyst,  
iOS Simluator, watchOS Simulator, DriverKit

Not all of the targets have been tested.

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
`libdispatch`              (For parallelism in ld64 / libcodedirectory.c)

You can find xar [here](https://github.com/tpoechtrager/xar).  
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

## TRAVIS CI ##

[![Build Status](https://travis-ci.org/tpoechtrager/cctools-port.svg?branch=973.0.1-ld64-609)](https://travis-ci.org/tpoechtrager/cctools-port)
