#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ([2.68])
AC_INIT(ld64, 136, http://www.i-soft.com.cn)
AC_CANONICAL_BUILD
AC_CANONICAL_HOST
AC_CANONICAL_TARGET

AC_ARG_PROGRAM

AM_INIT_AUTOMAKE([1.10 no-define no-dist-gzip dist-bzip2 tar-ustar])
AC_CONFIG_MACRO_DIR([m4])
AM_MAINTAINER_MODE

CC=clang
CXX=clang++

# Checks for programs.
AC_CHECK_PROG([CC], [clang],[clang],[clang not found])
if test "$CC" = "clang not found"; then
AC_MSG_ERROR("Couldn't find clang.")
fi

AC_CHECK_PROG([CXX], [clang++],[clang++],[clang++ not found])
if test "$CXX" = "clang++ not found"; then
AC_MSG_ERROR("Couldn't find clang++.")
fi

AC_PROG_CC
AC_PROG_CXX
AM_PROG_CC_C_O

if test "$CC" != "clang"; then
AC_MSG_ERROR("You must use clang to compile it")
fi

if test "$CXX" != "clang++"; then
AC_MSG_ERROR("You must use clang to compile it")
fi

WARNINGS=""

ORIGCFLAGS=$CFLAGS
CFLAGS="$CFLAGS -Wall"
AC_MSG_CHECKING([if -Wall is supported])
AC_COMPILE_IFELSE(
   [AC_LANG_SOURCE([[const char hw[] = "Hello, World\n";]])],
   [WARNINGS="$WARNINGS -Wall"
    AC_MSG_RESULT([yes])],
   [AC_MSG_RESULT([no])]
)
CFLAGS=$ORIGCFLAGS

ORIGCFLAGS=$CFLAGS
CFLAGS="$CFLAGS -Wno-long-long"
AC_MSG_CHECKING([if -Wno-long-long is supported])
AC_COMPILE_IFELSE(
   [AC_LANG_SOURCE([[const char hw[] = "Hello, World\n";]])],
   [WARNINGS="$WARNINGS -Wno-long-long"
    AC_MSG_RESULT([yes])],
   [AC_MSG_RESULT([no])]
)
CFLAGS=$ORIGCFLAGS

ORIGCFLAGS=$CFLAGS
CFLAGS="$CFLAGS -Wno-import"
AC_MSG_CHECKING([if -Wno-import is supported])
AC_COMPILE_IFELSE(
   [AC_LANG_SOURCE([[const char hw[] = "Hello, World\n";]])],
   [WARNINGS="$WARNINGS -Wno-import"
    AC_MSG_RESULT([yes])],
   [AC_MSG_RESULT([no])]
)
CFLAGS=$ORIGCFLAGS

ORIGCFLAGS=$CFLAGS
CFLAGS="$CFLAGS -Wno-format"
AC_MSG_CHECKING([if -Wno-format is supported])
AC_COMPILE_IFELSE(
   [AC_LANG_SOURCE([[const char hw[] = "Hello, World\n";]])],
   [WARNINGS="$WARNINGS -Wno-format"
    AC_MSG_RESULT([yes])],
   [AC_MSG_RESULT([no])]
)
CFLAGS=$ORIGCFLAGS

ORIGCFLAGS=$CFLAGS
CFLAGS="$CFLAGS -Wno-deprecated"
AC_MSG_CHECKING([if -Wno-deprecated is supported])
AC_COMPILE_IFELSE(
   [AC_LANG_SOURCE([[const char hw[] = "Hello, World\n";]])],
   [WARNINGS="$WARNINGS -Wno-deprecated"
    AC_MSG_RESULT([yes])],
   [AC_MSG_RESULT([no])]
)
CFLAGS=$ORIGCFLAGS

ORIGCFLAGS=$CFLAGS
CFLAGS="$CFLAGS -Wno-unused-variable"
AC_MSG_CHECKING([if -Wno-unused-variable is supported])
AC_COMPILE_IFELSE(
   [AC_LANG_SOURCE([[const char hw[] = "Hello, World\n";]])],
   [WARNINGS="$WARNINGS -Wno-unused-variable"
    AC_MSG_RESULT([yes])],
   [AC_MSG_RESULT([no])]
)
CFLAGS=$ORIGCFLAGS

ORIGCFLAGS=$CFLAGS
CFLAGS="$CFLAGS -Wno-invalid-offsetof"
AC_MSG_CHECKING([if -Wno-invalid-offsetof is supported])
AC_COMPILE_IFELSE(
   [AC_LANG_SOURCE([[const char hw[] = "Hello, World\n";]])],
   [WARNINGS="$WARNINGS -Wno-invalid-offsetof"
    AC_MSG_RESULT([yes])],
   [AC_MSG_RESULT([no])]
)
CFLAGS=$ORIGCFLAGS


ORIGCFLAGS=$CFLAGS
CFLAGS="$CFLAGS -Wno-unused-private-field"
AC_MSG_CHECKING([if -Wno-unused-private-field is supported])
AC_COMPILE_IFELSE(
   [AC_LANG_SOURCE([[const char hw[] = "Hello, World\n";]])],
   [WARNINGS="$WARNINGS -Wno-unused-private-field"
    AC_MSG_RESULT([yes])],
   [AC_MSG_RESULT([no])]
)
CFLAGS=$ORIGCFLAGS

AC_SUBST([WARNINGS], [$WARNINGS])

# Checks for header files.
AC_CHECK_HEADERS([stddef.h stdint.h stdlib.h])

LT_PREREQ([2.2.6])
LT_INIT(disable-static)

# Checks for typedefs, structures, and compiler characteristics.
#AC_CHECK_HEADER_STDBOOL
AC_TYPE_INT16_T
AC_TYPE_INT32_T
AC_TYPE_PID_T
AC_TYPE_SIZE_T
AC_TYPE_UINT16_T
AC_TYPE_UINT32_T
AC_TYPE_UINT8_T

# Checks for library functions.
AC_FUNC_CHOWN
AC_FUNC_FORK
AC_CHECK_FUNCS([memset strdup strrchr strtoul])

AC_CHECK_LIB([dl],[dlopen],[
         DL_LIBS=-ldl
         ])
AC_SUBST(DL_LIBS)

AC_CHECK_LIB([pthread],[pthread_create],[
         PTHREAD_LDFLAG=-pthread
         ])
AC_SUBST(PTHREAD_LDFLAG)

AC_CHECK_LIB([uuid],[uuid_generate_random],[
         UUID_LIBS=-luuid
         ])
AC_SUBST(UUID_LIBS)

AC_CHECK_LIB([crypto],[MD5_Init],[
         CRYPT_LIBS=-lcrypto
         ])
AC_SUBST(CRYPT_LIBS)


# Check LTO
AC_CHECK_PROG([llvm_config], [llvm-config],[yes],[no])
if test "x$llvm-config" = "xno"; then
        AC_MSG_ERROR("Could not find llvm-config.")
else
        LLVM_LIBDIR=`llvm-config --libdir`
        if test -f $LLVM_LIBDIR/libLTO.so;then
                LTO_LIBS="-L`llvm-config --libdir` -lLTO"
                LTO_DEFS="-DLTO_SUPPORT=1"
        fi
fi

AC_SUBST(LTO_LIBS)
AC_SUBST(LTO_DEFS)


AC_C_BIGENDIAN([AC_SUBST([ENDIAN_FLAG],[-D__BIG_ENDIAN__=1])],
           [AC_SUBST([ENDIAN_FLAG],[-D__LITTLE_ENDIAN__=1])])



AC_OUTPUT([
	   Makefile
	   src/Makefile
	   src/3rd/Makefile
	   src/3rd/BlocksRuntime/Makefile
	   src/ld/Makefile
	   src/ld/parsers/Makefile
	   src/ld/passes/Makefile
       src/other/Makefile
])
