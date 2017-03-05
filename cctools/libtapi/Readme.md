# TAPI

TAPI is a __T__ext-based __A__pplication __P__rogramming __I__nterface. It
replaces the Mach-O Dynamic Library Stub files in Apple's SDKs to reduce SDK
size even further.

The text-based dynamic library stub file format (.tbd) is a human readable and
editable YAML text file. The _TAPI_ projects uses the _LLVM_ YAML parser to read
those files and provides this functionality to the linker as a dynamic library.

## Building TAPI

First obtain the matching _CLANG_ source code, which includes the _LLVM_ source
code, from [Apple Open Source](opensource.apple.com). Then place the _TAPI_
source code in _LLVM_'s projects directory.

Create a separate build directory and configure the project with CMake:

    cmake -G Ninja -C <src_dir>/projects/libtapi/cmake/caches/apple-tapi.cmake -DCMAKE_INSTALL_PREFIX=<install_dir> <src_dir>

The CMake cache file defines most of the settings for you, such as enabling LTO,
etc. It also specifies the distribution components to include all the files
needed for TAPI.

To build and install the _TAPI_ project invoke:

    ninja install-distribution

in the build directory.
