# CCTOOLS Linux Port #

Current Version: 845 + ld64-134.9  
Originally ported by cjacker from the [ios-toolchain-for-linux](https://code.google.com/p/ios-toolchain-based-on-clang-for-linux/) project.

## INSTALLATION ##

Make sure you have got `clang` installed on your system, then type:

* `cd cctools`
* `./configure --prefix=<installdir> --target=<target>`
* `make`
* `make install`

target = `i386-apple-darwin11`, `x86_64-apple-darwin11`, `arm-apple-darwin11`, ...
