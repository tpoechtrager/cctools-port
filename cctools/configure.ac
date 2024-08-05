AC_INIT([cctools], [1010.6], [t.poechtrager@gmail.com])

AC_CANONICAL_BUILD
AC_CANONICAL_HOST
AC_CANONICAL_TARGET

AC_ARG_PROGRAM

AM_CONDITIONAL([ISDARWIN], [false])

isdarwin=no

if test "x$CFLAGS" = "x"; then
  CFLAGS="-O3"
fi

if test "x$CXXFLAGS" = "x"; then
  CXXFLAGS="-O3"
fi

CXXFLAGS="$CXXFLAGS -std=c++2a"

if test "x$build" = "x$host"; then
  CFLAGS="$CFLAGS -isystem /usr/local/include -isystem /usr/pkg/include"
  CXXFLAGS="$CXXFLAGS -isystem /usr/local/include -isystem /usr/pkg/include"
  LDFLAGS="$LDFLAGS -L/usr/local/lib -L/usr/pkg/lib"
fi

CWARNINGS=""
CXXWARNINGS=""
EXTRAFLAGS=""
EXTRACXXFLAGS=""
WARNINGS=""

case $host_os in
  darwin* )
    isdarwin=yes
    AM_CONDITIONAL([ISDARWIN], [true])
  ;;
  cygwin* )
    echo "Cygwin is no longer supported." 1>&2
    exit 1
  ;;
esac

if test "x$CC" = "x"; then
  if test "x$build" != "x$host"; then
    CC="${host_alias}-clang"
   else
    CC=clang
  fi
fi

if test "x$CXX" = "x"; then
  if test "x$build" != "x$host"; then
    CXX="${host_alias}-clang++"
  else
    CXX=clang++
  fi
fi

CCVERSION="`$CC --version`"
CXXVERSION="`$CXX --version`"

case "$CCVERSION" in
  *clang* )
    WARNINGS="$WARNINGS -Wall -Wno-long-long -Wno-import -Wno-format -Wno-deprecated -Wno-unused-variable"
    WARNINGS="$WARNINGS -Wno-unused-private-field -Wno-unused-function -Wno-invalid-offsetof"
    WARNINGS="$WARNINGS -Wno-int-conversion -Wno-char-subscripts"
  ;;
  * )
    echo "You must compile this project with clang. Other compilers are no longer supported." 2>&1
    exit 1
  ;;
esac

EXTRAFLAGS="$EXTRAFLAGS -DLD64_VERSION_NUM=951.9"

case "$CXXVERSION" in
  *clang* )
    if test "x$isdarwin" = "xyes"; then
      CXXFLAGS="$CXXFLAGS -stdlib=libc++"
    fi
  ;;
esac

EXTRAFLAGS="$EXTRAFLAGS -fblocks"

case $host_cpu in
  powerpc)
    EXTRAFLAGS="$EXTRAFLAGS -D__ppc__"
  ;;
  powerpc64*)
    EXTRAFLAGS="$EXTRAFLAGS -D__ppc__ -D__ppc64__"
  ;;
  arm64*|aarch64)
    EXTRAFLAGS="$EXTRAFLAGS -D__arm64__"
  ;;
  arm*)
    EXTRAFLAGS="$EXTRAFLAGS -D__arm__"
  ;;
esac

if test "x$EXTRAFLAGS" != "x"; then
  CFLAGS="$CFLAGS $EXTRAFLAGS"
  CXXFLAGS="$CXXFLAGS $EXTRAFLAGS"
fi

if test "xEXTRACXXFLAGS" != "x"; then
  CXXFLAGS="$CXXFLAGS $EXTRACXXFLAGS"
fi

CPP="$CC -E"

# FIXME 'subdir-objects' breaks dependency tracking
AM_INIT_AUTOMAKE([1.10 no-define no-dist-gzip dist-bzip2 tar-ustar subdir-objects no-dependencies])

AC_CONFIG_MACRO_DIR([m4])
AM_MAINTAINER_MODE

AC_PREREQ([2.57])
AC_REVISION([$Revision: 1.37 $])

AC_CONFIG_SRCDIR([APPLE_LICENSE])
LT_PREREQ([2.2.6])
LT_INIT(disable-static)

AC_PROG_INSTALL

AC_PROG_CC([clang])
AC_PROG_CXX([clang++])
AM_PROG_CC_C_O
AM_PROG_AS([clang])

AC_CHECK_TOOL([HOST_RANLIB], [ranlib], :)
AC_CHECK_TOOL([HOST_AR], [ar], :)

#for libstuff emulated.c
case $target_cpu in
     powerpc)
        AC_DEFINE(EMULATED_HOST_CPU_TYPE, 18, [Emulated CPU type])
        AC_DEFINE(EMULATED_HOST_CPU_SUBTYPE, 100, [Emulated CPU subtype])
    ;;
     powerpc64*)
        AC_DEFINE(EMULATED_HOST_CPU_TYPE, 16777234, [Emulated CPU type])
        AC_DEFINE(EMULATED_HOST_CPU_SUBTYPE, 0, [Emulated CPU subtype])
       ;;
     i?86)
        AC_DEFINE(EMULATED_HOST_CPU_TYPE, 7, [Emulated CPU type])
        AC_DEFINE(EMULATED_HOST_CPU_SUBTYPE, 10, [Emulated CPU subtype])
    ;;
     x86_64|amd64)
        AC_DEFINE(EMULATED_HOST_CPU_TYPE, 16777223, [Emulated CPU type])
        AC_DEFINE(EMULATED_HOST_CPU_SUBTYPE, 3, [Emulated CPU subtype])
    ;;
     arm64*|aarch64)
        AC_DEFINE(EMULATED_HOST_CPU_TYPE, 16777228, [Emulated CPU type])
        AC_DEFINE(EMULATED_HOST_CPU_SUBTYPE, 0, [Emulated CPU subtype])
    ;;
     arm*)
        AC_DEFINE(EMULATED_HOST_CPU_TYPE, 12, [Emulated CPU type])
        AC_DEFINE(EMULATED_HOST_CPU_SUBTYPE, 0, [Emulated CPU subtype])
    ;;
     *)
    AC_MSG_ERROR([Unsupported target $target_cpu])
    ;;
esac

# as reported, some users may use llvm-c/Disassembler.h instead of a local copy in include dir.
# add these macros to avoid compilation failures.

AC_DEFINE(__STDC_LIMIT_MACROS)
AC_DEFINE(__STDC_CONSTANT_MACROS)

AC_SUBST([ASLIBEXECDIR], ['${libexecdir}/as'])

# set PROGRAM PREFIX
if test "$target_alias"; then
  AC_SUBST([PROGRAM_PREFIX], ['${target_alias}-'])
fi

AC_CHECK_LIB([dl],[dlopen],[DL_LIB=-ldl])
AC_SUBST(DL_LIB)

AC_CHECK_LIB([pthread],[pthread_create],[PTHREAD_FLAGS=-pthread])
AC_SUBST(PTHREAD_FLAGS)

AC_CHECK_HEADERS([execinfo.h], [
  AC_CHECK_LIB([execinfo],[backtrace],[EXECINFO_LIB=-lexecinfo])], [])
AC_SUBST(EXECINFO_LIB)

AC_CHECK_LIB([uuid],[uuid_generate_random],[
  AC_CHECK_HEADERS([uuid/uuid.h], [UUID_LIB=-luuid])], [])
AC_SUBST(UUID_LIB)

AC_CHECK_FUNCS([strmode utimens utimensat])
AC_CHECK_FUNCS([bcopy bcmp bzero index rindex])

### Check how to access struct stat's tv_nsec field.
AC_MSG_CHECKING(for st_mtimespec.tv_nsec in struct stat)
AC_COMPILE_IFELSE(
  [AC_LANG_PROGRAM(
    [[#include <sys/stat.h>]],
    [[
    struct stat st;
    st.st_mtimespec.tv_nsec = 0;
    ]])],
  [AC_DEFINE(HAVE_STAT_ST_MTIMESPEC, 1, [struct stat has st_mtimespec.tv_nsec])
   AC_MSG_RESULT([yes])],
  [AC_MSG_RESULT([no])])

AC_MSG_CHECKING(for st_mtim.tv_nsec in struct stat)
AC_COMPILE_IFELSE(
  [AC_LANG_PROGRAM(
    [[#include <sys/stat.h>]],
    [[
    struct stat st;
    st.st_mtim.tv_nsec = 0;
    ]])],
  [AC_DEFINE(HAVE_STAT_ST_MTIM, 1, [struct stat has st_mtim.tv_nsec])
   AC_MSG_RESULT([yes])],
  [AC_MSG_RESULT([no])])

### Check for log2 in -lm

AC_CHECK_LIB([m], [log2], [
  MATH_LIB="-lm"
])

AC_SUBST(MATH_LIB)

### Check for reallocf in -lc, -lbsd

AC_CHECK_HEADERS([bsd/stdlib.h], [])

AC_CHECK_LIB([c],[reallocf],[
    REALLOCF_LIB=""
    AC_DEFINE(HAVE_REALLOCF, 1)
  ],[
  AC_CHECK_LIB([bsd],[reallocf],[
    REALLOCF_LIB=-lbsd
    AC_DEFINE(HAVE_REALLOCF, 1)
  ],[])
])

AC_SUBST(REALLOCF_LIB)

### Check for clock_gettime

AC_CHECK_FUNC([clock_gettime],[AC_DEFINE(HAVE_CLOCK_GETTIME, 1)])

### Check for write64

AC_CHECK_FUNC([write64],[AC_DEFINE(HAVE_WRITE64, 1)])

### Check for __cxa_demangle in various C++ ABI libs ###

AC_CHECK_LIB([c++abi],[__cxa_demangle],[CXXABI_LIB=-lc++abi],[
  AC_CHECK_LIB([c++],[__cxa_demangle],[CXXABI_LIB=-lc++],[
    AC_CHECK_LIB([supc++],[__cxa_demangle],[CXXABI_LIB=-lsupc++],[
      AC_CHECK_LIB([stdc++],[__cxa_demangle],[CXXABI_LIB=-lstdc++],[exit 1])
    ])
  ])
])

AC_SUBST(CXXABI_LIB)

### Check for __gcc_personality_v0 in -lgcc (NetBSD needs this)

AC_CHECK_LIB([gcc], [__gcc_personality_v0], [
  GCC_LIB="-lgcc"
])

AC_SUBST(GCC_LIB)

### Check for supported warning options ###

ORIGCFLAGS=$CFLAGS
CFLAGS="$CFLAGS -Werror -Wno-shift-negative-value"
AC_MSG_CHECKING([whether $CC supports -Wno-shift-negative-value])
AC_COMPILE_IFELSE(
  [AC_LANG_SOURCE([[int dummy;]])],
  [WARNINGS="$WARNINGS -Wno-shift-negative-value"
   AC_MSG_RESULT([yes])],
  [AC_MSG_RESULT([no])]
)
CFLAGS=$ORIGCFLAGS

ORIGCFLAGS=$CFLAGS
CFLAGS="$CFLAGS -Werror -Wno-misleading-indentation"
AC_MSG_CHECKING([whether $CC supports -Wno-misleading-indentation])
AC_COMPILE_IFELSE(
  [AC_LANG_SOURCE([[int dummy;]])],
  [WARNINGS="$WARNINGS -Wno-misleading-indentation"
   AC_MSG_RESULT([yes])],
  [AC_MSG_RESULT([no])]
)
CFLAGS=$ORIGCFLAGS

ORIGCFLAGS=$CFLAGS
CFLAGS="$CFLAGS -Werror -Wno-gnu-folding-constant"
AC_MSG_CHECKING([whether $CC supports -Wno-gnu-folding-constant])
AC_COMPILE_IFELSE(
  [AC_LANG_SOURCE([[int dummy;]])],
  [WARNINGS="$WARNINGS -Wno-gnu-folding-constant"
   AC_MSG_RESULT([yes])],
  [AC_MSG_RESULT([no])]
)
CFLAGS=$ORIGCFLAGS

ORIGCFLAGS=$CFLAGS
CFLAGS="$CFLAGS -Werror -Wno-unused-but-set-variable"
AC_MSG_CHECKING([whether $CC supports -Wno-unused-but-set-variable])
AC_COMPILE_IFELSE(
  [AC_LANG_SOURCE([[int dummy;]])],
  [WARNINGS="$WARNINGS -Wno-unused-but-set-variable"
   AC_MSG_RESULT([yes])],
  [AC_MSG_RESULT([no])]
)
CFLAGS=$ORIGCFLAGS

ORIGCFLAGS=$CFLAGS
CFLAGS="$CFLAGS -Werror -Wno-deprecated-non-prototype"
AC_MSG_CHECKING([whether $CC supports -Wno-deprecated-non-prototype])
AC_COMPILE_IFELSE(
  [AC_LANG_SOURCE([[int dummy;]])],
  [WARNINGS="$WARNINGS -Wno-deprecated-non-prototype"
   AC_MSG_RESULT([yes])],
  [AC_MSG_RESULT([no])]
)
CFLAGS=$ORIGCFLAGS

ORIGCFLAGS=$CFLAGS
CFLAGS="$CFLAGS -Werror -Wno-vla-cxx-extension"
AC_MSG_CHECKING([whether $CC supports -Wno-vla-cxx-extension])
AC_COMPILE_IFELSE(
  [AC_LANG_SOURCE([[int dummy;]])],
  [WARNINGS="$WARNINGS -Wno-vla-cxx-extension"
   AC_MSG_RESULT([yes])],
  [AC_MSG_RESULT([no])]
)
CFLAGS=$ORIGCFLAGS

AC_SUBST([WARNINGS], [$WARNINGS])

### Check for libLTO ###

ORIGLDFLAGS=$LDFLAGS
LDFLAGS="$LDFLAGS -Wl,-rpath,/usr/local/lib,--enable-new-dtags"
AC_MSG_CHECKING([whether ld supports -rpath,<path>,--enable-new-dtags])
AC_LINK_IFELSE(
  [AC_LANG_SOURCE([[int main(){}]])],
  [rpathlink=yes
  AC_MSG_RESULT([yes])],
  [AC_MSG_RESULT([no])]
)
LDFLAGS=$ORIGLDFLAGS

CHECK_LLVM
LDFLAGS="$LDFLAGS $LTO_RPATH"

### Check for Blocks Runtime library ###

AC_ARG_WITH([libblocksruntime],
    AS_HELP_STRING([--with-libblocksruntime],
                  [path to the blocks runtime library]),
    [BLOCKS_RUNTIME_DIR=$with_libblocksruntime], [BLOCKS_RUNTIME_DIR=""])

if test "x$BLOCKS_RUNTIME_DIR" = "xno"; then
  BLOCKS_RUNTIME_DIR=""
fi

if test "x$BLOCKS_RUNTIME_DIR" != "x"; then
  test -d "$BLOCKS_RUNTIME_DIR/lib" && LDFLAGS="$LDFLAGS -L$BLOCKS_RUNTIME_DIR/lib"
  test -d "$BLOCKS_RUNTIME_DIR/lib64" && LDFLAGS="$LDFLAGS -L$BLOCKS_RUNTIME_DIR/lib64"
  test -d "$BLOCKS_RUNTIME_DIR/lib32" && LDFLAGS="$LDFLAGS -L$BLOCKS_RUNTIME_DIR/lib32"
  if test "x$rpathlink" = "xyes"; then
    BLOCKS_RUNTIME_RPATH="-Wl,-rpath,$BLOCKS_RUNTIME_DIR/lib,--enable-new-dtags"
    BLOCKS_RUNTIME_RPATH="$BLOCKS_RUNTIME_RPATH -Wl,-rpath,$BLOCKS_RUNTIME_DIR/lib64,--enable-new-dtags"
    BLOCKS_RUNTIME_RPATH="$BLOCKS_RUNTIME_RPATH -Wl,-rpath,$BLOCKS_RUNTIME_DIR/lib32,--enable-new-dtags"
    LDFLAGS="$LDFLAGS $BLOCKS_RUNTIME_RPATH"
  fi
  if test "x$isdarwin" = "xyes"; then
    BLOCKS_RUNTIME_RPATH="-Wl,-rpath,$BLOCKS_RUNTIME_DIR/lib"
    LDFLAGS="$LDFLAGS $BLOCKS_RUNTIME_RPATH"
  fi
fi

AC_CHECK_LIB([BlocksRuntime], [_Block_release], [BLOCKS_RUNTIME_LIB=-lBlocksRuntime], [
    AC_CHECK_LIB([c], [_Block_release], [], [
      echo "Required library libblocksruntime not installed (install libdispatch!)" >&2
      exit 1
    ])
    AC_SUBST(BLOCKS_RUNTIME_LIB)
  ])
AC_SUBST(BLOCKS_RUNTIME_LIB)

### Check for dispatch library ###

AC_ARG_WITH([libdispatch],
    AS_HELP_STRING([--with-libdispatch],
                  [path to the dispatch library]),
    [LIBDISPATCH_DIR=$with_libdispatch], [LIBDISPATCH_DIR=""])

if test "x$LIBDISPATCH_DIR" = "xno"; then
  LIBDISPATCH_DIR=""
fi

if test "x$LIBDISPATCH_DIR" != "x"; then
  CXXFLAGS="$CXXFLAGS -isystem $LIBDISPATCH_DIR/include"
  CPPFLAGS="$CPPFLAGS -isystem $LIBDISPATCH_DIR/include"
  test -d "$LIBDISPATCH_DIR/lib" && LDFLAGS="$LDFLAGS -L$LIBDISPATCH_DIR/lib"
  test -d "$LIBDISPATCH_DIR/lib64" && LDFLAGS="$LDFLAGS -L$LIBDISPATCH_DIR/lib64"
  test -d "$LIBDISPATCH_DIR/lib32" && LDFLAGS="$LDFLAGS -L$LIBDISPATCH_DIR/lib32"
  if test "x$rpathlink" = "xyes"; then
    LIBDISPATCH_RPATH="-Wl,-rpath,$LIBDISPATCH_DIR/lib,--enable-new-dtags"
    LIBDISPATCH_RPATH="$LIBDISPATCH_RPATH -Wl,-rpath,$LIBDISPATCH_DIR/lib64,--enable-new-dtags"
    LIBDISPATCH_RPATH="$LIBDISPATCH_RPATH -Wl,-rpath,$LIBDISPATCH_DIR/lib32,--enable-new-dtags"
    LDFLAGS="$LDFLAGS $LIBDISPATCH_RPATH"
  fi
  if test "x$isdarwin" = "xyes"; then
    LIBDISPATCH_RPATH="-Wl,-rpath,$LIBDISPATCH_DIR/lib"
    LDFLAGS="$LDFLAGS $LIBDISPATCH_RPATH"
  fi
fi

# FreeBSD requires -lBlocksRuntime for -ldispatch
if test "x$isdarwin" != "xyes"; then
  ORIGLDFLAGS=$LDFLAGS
  LDFLAGS="$LDFLAGS -lBlocksRuntime"
fi

AC_CHECK_LIB([dispatch], [dispatch_once], [
  AC_CHECK_HEADERS([dispatch/dispatch.h], [DISPATCH_LIB=-ldispatch])], [
    AC_CHECK_LIB([c], [dispatch_once], [
      AC_CHECK_HEADERS([dispatch/dispatch.h], [])], [
        echo "Required library libdispatch not installed" >&2
        exit 1
      ])
    AC_SUBST(DISPATCH_LIB)
  ])
AC_SUBST(DISPATCH_LIB)

if test "x$isdarwin" != "xyes"; then
  LDFLAGS=$ORIGLDFLAGS
fi

### Check for libfts (musl) ###

AC_CHECK_LIB([fts],[fts_open],[
  AC_CHECK_HEADERS([fts.h], [FTS_LIB=-lfts])], [])
AC_SUBST(FTS_LIB)

### Check for XAR ###

AC_ARG_ENABLE([xar-support],
AS_HELP_STRING([--enable-xar-support],
                [enable xar support]),
[], [enable_xar_support=yes])

if test "x$LTO_LIB" != "x"; then
  if test "x$enable_xar_support" = "xyes"; then
    AC_ARG_WITH([libxar],
        AS_HELP_STRING([--with-libxar],
                      [path to the XAR library]),
        [XAR_DIR=$with_libxar], [XAR_DIR=""])

    if test "x$XAR_DIR" = "xno"; then
      XAR_DIR=""
    fi

    if test "x$XAR_DIR" != "x"; then
      CXXFLAGS="$CXXFLAGS -isystem $XAR_DIR/include"
      CPPFLAGS="$CPPFLAGS -isystem $XAR_DIR/include"
      test -d "$XAR_DIR/lib" && LDFLAGS="$LDFLAGS -L$XAR_DIR/lib"
      test -d "$XAR_DIR/lib64" && LDFLAGS="$LDFLAGS -L$XAR_DIR/lib64"
      test -d "$XAR_DIR/lib32" && LDFLAGS="$LDFLAGS -L$XAR_DIR/lib32"
      if test "x$rpathlink" = "xyes"; then
        XAR_RPATH="-Wl,-rpath,$XAR_DIR/lib,--enable-new-dtags"
        XAR_RPATH="$XAR_RPATH -Wl,-rpath,$XAR_DIR/lib64,--enable-new-dtags"
        XAR_RPATH="$XAR_RPATH -Wl,-rpath,$XAR_DIR/lib32,--enable-new-dtags"
        LDFLAGS="$LDFLAGS $XAR_RPATH"
      fi
      if test "x$isdarwin" = "xyes"; then
        XAR_RPATH="-Wl,-rpath,$XAR_DIR/lib"
        LDFLAGS="$LDFLAGS $XAR_RPATH"
      fi
    fi

    XAR_LIB=""

    AC_CHECK_LIB([xar],[xar_prop_create],[
      AC_CHECK_HEADERS([xar/xar.h], [XAR_LIB=-lxar])], [])
    AC_SUBST(XAR_LIB)
  fi
fi

### Check for Apple TAPI library ###

AC_ARG_ENABLE([tapi-support],
AS_HELP_STRING([--enable-tapi-support],
                [enable tapi support]),
[], [enable_tapi_support=yes])

if test "x$enable_tapi_support" = "xyes"; then
  AC_ARG_WITH([libtapi],
      AS_HELP_STRING([--with-libtapi],
                    [path to the apple tapi library]),
      [LIBTAPI_DIR=$with_libtapi], [LIBTAPI_DIR=""])

  if test "x$LIBTAPI_DIR" = "xno"; then
    LIBTAPI_DIR=""
  fi
  if test "x$LIBTAPI_DIR" != "x"; then
    CXXFLAGS="$CXXFLAGS -isystem $LIBTAPI_DIR/include"
    CPPFLAGS="$CPPFLAGS -isystem $LIBTAPI_DIR/include"
    test -d "$LIBTAPI_DIR/lib" && LDFLAGS="$LDFLAGS -L$LIBTAPI_DIR/lib"
    test -d "$LIBTAPI_DIR/lib64" && LDFLAGS="$LDFLAGS -L$LIBTAPI_DIR/lib64"
    test -d "$LIBTAPI_DIR/lib32" && LDFLAGS="$LDFLAGS -L$LIBTAPI_DIR/lib32"
    if test "x$rpathlink" = "xyes"; then
      LIBTAPI_RPATH="-Wl,-rpath,$LIBTAPI_DIR/lib,--enable-new-dtags"
      LIBTAPI_RPATH="$LIBTAPI_RPATH -Wl,-rpath,$LIBTAPI_DIR/lib64,--enable-new-dtags"
      LIBTAPI_RPATH="$LIBTAPI_RPATH -Wl,-rpath,$LIBTAPI_DIR/lib32,--enable-new-dtags"
      LDFLAGS="$LDFLAGS $LIBTAPI_RPATH"
    fi
    if test "x$isdarwin" = "xyes"; then
      LIBTAPI_RPATH="-Wl,-rpath,$LIBTAPI_DIR/lib"
      LDFLAGS="$LDFLAGS $LIBTAPI_RPATH"
    fi
  fi

  TAPI_LIB=""
  AC_CACHE_CHECK([tapi/tapi.h and libtapi support],[cctools_cv_tapi_support],
    AC_LANG_PUSH([C++])
    save_LIBS="$LIBS"
    LIBS="$LIBS -ltapi"
    AC_TRY_LINK([#include <tapi/tapi.h>],
                [return tapi::APIVersion::getMajor();],
                [cctools_cv_tapi_support=yes],
                [cctools_cv_tapi_support=no])
    LIBS="$save_LIBS"
    AC_LANG_POP([C++])
  )

  if test $cctools_cv_tapi_support = yes; then
    TAPI_LIB=-ltapi
    TAPI_DEF=-DTAPI_SUPPORT
  else
    if test "x$LIBTAPI_DIR" != "x"; then
      AC_MSG_ERROR([-- ERROR: Failed TAPI checks in $LIBTAPI_DIR])
      exit 1
    fi
    echo ""
    echo "### Building without TAPI support ###"
    echo "### See README.md -> DEPENDENCIES ###"
    echo ""
  fi

  AC_SUBST(TAPI_LIB)
  AC_SUBST(TAPI_DEF)
fi

### Check whether we want to use clang as assembler ###

AC_ARG_ENABLE([clang-as],
AS_HELP_STRING([--disable-clang-as],
               [do not use clang for assembling]),
[], [enable_clang_as=yes])

if test "x$enable_clang_as" != "xyes"; then
  CFLAGS="$CFLAGS -DDISABLE_CLANG_AS"
fi

### Check endianness ###

AC_C_BIGENDIAN([AC_SUBST([ENDIAN_FLAG],[-D__BIG_ENDIAN__=1])],
      [AC_SUBST([ENDIAN_FLAG],[-D__LITTLE_ENDIAN__=1])])


AC_CONFIG_FILES([Makefile libstuff/Makefile libmacho/Makefile])
AC_CONFIG_FILES([ar/Makefile])
AC_CONFIG_FILES([as/Makefile])
AC_CONFIG_FILES([as/arm/Makefile])
AC_CONFIG_FILES([as/i386/Makefile])
AC_CONFIG_FILES([as/x86_64/Makefile])
AC_CONFIG_FILES([as/ppc/Makefile])
AC_CONFIG_FILES([as/ppc64/Makefile])
AC_CONFIG_FILES([man/Makefile])
AC_CONFIG_FILES([misc/Makefile])
AC_CONFIG_FILES([otool-wrapper/Makefile])
AC_CONFIG_FILES([efitools/Makefile])
AC_CONFIG_FILES([ld64/Makefile])
AC_CONFIG_FILES([ld64/doc/man/Makefile])
AC_CONFIG_FILES([ld64/src/Makefile])
AC_CONFIG_FILES([ld64/src/3rd/Makefile])
AC_CONFIG_FILES([ld64/src/mach_o/Makefile])
AC_CONFIG_FILES([ld64/src/ld/Makefile])
AC_CONFIG_FILES([ld64/src/ld/parsers/Makefile])
AC_CONFIG_FILES([ld64/src/ld/passes/Makefile])
AC_CONFIG_FILES([ld64/src/other/Makefile])

AC_OUTPUT
