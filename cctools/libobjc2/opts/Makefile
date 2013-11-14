LEVEL = ../../../
LIBRARYNAME = libGNUObjCRuntime
LOADABLE_MODULE = 1
USEDLIBS =
CPPFLAGS += `llvm-config --version | sed 's/\([0-9]*\).\([0-9]*\).*/-DLLVM_MAJOR=\1 -DLLVM_MINOR=\2/'`
CPPFLAGS += -Wno-variadic-macros

include $(LEVEL)/Makefile.common
