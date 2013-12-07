# CCTOOLS Linux Port #

Current Version: 845 + ld64-136  
Originally ported by cjacker from the [ios-toolchain-for-linux](https://code.google.com/p/ios-toolchain-based-on-clang-for-linux/) project.

## INSTALLATION ##

Make sure you have the following installed on your Linux box:

`Clang 3.2+`, `libc++-devel`, `llvm-devel`, `automake`, `autogen`,..
 `libtool`, `libuuid-devel` and `openssl-devel`.

Then type:

* `cd cctools`
* `./autogen.sh`
* `./configure --prefix=<installdir> --target=<target>`
* `make`
* `make install`

target = `i386-apple-darwin11`, `x86_64-apple-darwin11`, `arm-apple-darwin11`, ...

If you get compile errors because of `unistd.h`, then please run  
`../tools/fix-unistd-issue.sh` in the cctools directory and restart compiling.
