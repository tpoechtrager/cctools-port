AC_INIT([cctools], [895], [t.poechtrager@gmail.com])

AC_CANONICAL_BUILD
AC_CANONICAL_HOST
AC_CANONICAL_TARGET

AC_ARG_PROGRAM

AM_CONDITIONAL([ISDARWIN], [false])

isdarwin=no
iscygwin=no

if test "x$CFLAGS" = "x"; then
  CFLAGS="-O3"
fi

if test "x$CXXFLAGS" = "x"; then
  CXXFLAGS="-O3"
fi

if test "x$OBJCFLAGS" = "x"; then
  OBJCFLAGS="-O3"
fi

CXXFLAGS="$CXXFLAGS -std=c++0x"

if test "x$build" = "x$host"; then
  CFLAGS="$CFLAGS -isystem /usr/local/include -isystem /usr/pkg/include"
  CXXFLAGS="$CXXFLAGS -isystem /usr/local/include -isystem /usr/pkg/include"
  LDFLAGS="$LDFLAGS -L/usr/local/lib -L/usr/pkg/lib"
fi

CWARNINGS=""
OBJCWARNINGS=""
CXXWARNINGS=""
EXTRAFLAGS=""
EXTRACXXFLAGS=""
WARNINGS=""

case $host_os in
  darwin* )
    isdarwin=yes
    AM_CONDITIONAL([ISDARWIN], [true])
  ;;
  openbsd* )
    XCC="egcc"
    XCXX="eg++"
  ;;
  cygwin* )
    iscygwin=yes
    # https://github.com/tpoechtrager/osxcross/issues/87
    EXTRAFLAGS="-D__LARGE64_FILES -D_GNU_SOURCE -fno-PIC"
    WARNINGS="-Wno-attributes"
    XCC="gcc"
    XCXX="g++"
  ;;
esac

if test "x$isdarwin" = "xno"; then
  case $host_cpu in
    arm* )
      # clang miscompiles ld64 on arm
      if test "x$isdarwin" = "xno"; then
        XCC="gcc"
        XCXX="g++"
      fi
    ;;
  esac
fi

if test "x$XCC" = "x"; then
  XCC="clang"
fi

if test "x$XCXX" = "x"; then
  XCXX="clang++"
fi

if test "x$CC" = "x"; then
  if test "x$build" != "x$host"; then
    CC="${host_alias}-${XCC}"
    which $CC 2>/dev/null 1>&2
    if test $? -ne 0; then
      CC="${host_alias}-gcc"
    fi
   else
    CC=$XCC
    which $CC 2>/dev/null 1>&2
    if test $? -ne 0; then
      CC="gcc"
    fi
  fi
fi

if test "x$CXX" = "x"; then
  if test "x$build" != "x$host"; then
    CXX="${host_alias}-${XCXX}"
    which $CXX 2>/dev/null 1>&2
    if test $? -ne 0; then
      CXX="${host_alias}-g++"
    fi
  else
    CXX=$XCXX
    which $CXX 2>/dev/null 1>&2
    if test $? -ne 0; then
      CXX="g++"
    fi
  fi
fi

CCVERSION="`$CC --version`"
CXXVERSION="`$CXX --version`"

case "$CCVERSION" in
  *clang* )
    WARNINGS="$WARNINGS -Wall -Wno-long-long -Wno-import -Wno-format -Wno-deprecated -Wno-unused-variable"
    WARNINGS="$WARNINGS -Wno-unused-private-field -Wno-unused-function -Wno-invalid-offsetof"
    WARNINGS="$WARNINGS -Wno-int-conversion -Wno-char-subscripts"
    OBJCWARNINGS="-Wall -Wno-objc-root-class -Wno-deprecated-objc-isa-usage"
  ;;
  * )
    WARNINGS="$WARNINGS -Wall -Wno-format -Wno-enum-compare -Wno-unused-result -Wno-unused-variable"
    WARNINGS="$WARNINGS -Wno-unused-but-set-variable -Wno-deprecated -Wno-deprecated-declarations"
    WARNINGS="$WARNINGS -Wno-char-subscripts -Wno-strict-aliasing"
    OBJCWARNINGS="-Wall"
    CFLAGS="$CFLAGS -std=gnu99 -D__private_extern__="
    CXXFLAGS="$CXXFLAGS -D__private_extern__="
  ;;
esac

case "$CXXVERSION" in
  *clang* )
    if test "x$isdarwin" = "xyes"; then
      CXXFLAGS="$CXXFLAGS -stdlib=libc++"
    fi
    # can't use -fblocks on cyggwin due to a clang driver bug
    if test "x$iscygwin" = "xno"; then
      EXTRACXXFLAGS="$EXTRACXXFLAGS -fblocks"
    else
      EXTRAFLAGS="$EXTRAFLAGS -integrated-as"
    fi
  ;;
esac

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
    case "$CXXVERSION" in
      *clang* )
        echo "" >&2
        echo "clang is known to miscompile ld64 on arm platforms:" >&2
        echo "https://github.com/tpoechtrager/cctools-port/issues/1#issuecomment-59118615" >&2
        if test "x$isdarwin" = "xno"; then
          echo "" >&2
          echo "please use gcc instead;  CC=gcc CXX=g++ ./configure" >&2
          echo "" >&2
          exit 1
        fi
        echo "" >&2
        sleep 2
      ;;
    esac
    EXTRAFLAGS="$EXTRAFLAGS -D__arm__"
  ;;
esac

if test "x$EXTRAFLAGS" != "x"; then
  CFLAGS="$CFLAGS $EXTRAFLAGS"
  OBJCFLAGS="$OBJCFLAGS $EXTRAFLAGS"
  CXXFLAGS="$CXXFLAGS $EXTRAFLAGS"
fi

if test "xEXTRACXXFLAGS" != "x"; then
  CXXFLAGS="$CXXFLAGS $EXTRACXXFLAGS"
fi

CPP="$CC -E"
OBJC="$CC"

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
AC_PROG_OBJC([clang])
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

AC_CHECK_FUNCS([strmode])

### Check for __cxa_demangle in various C++ ABI libs ###

AC_CHECK_LIB([c++abi],[__cxa_demangle],[CXXABI_LIB=-lc++abi],[
  AC_CHECK_LIB([c++],[__cxa_demangle],[CXXABI_LIB=-lc++],[
    AC_CHECK_LIB([supc++],[__cxa_demangle],[CXXABI_LIB=-lsupc++],[
      AC_CHECK_LIB([stdc++],[__cxa_demangle],[CXXABI_LIB=-lstdc++],[exit 1])
    ])
  ])
])

AC_SUBST(CXXABI_LIB)

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

AC_SUBST([WARNINGS], [$WARNINGS])
AC_SUBST([OBJCWARNINGS], [$OBJCWARNINGS])

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

### Check for libxar ###

if test "x$LLVM_CONFIG" != "xno"; then

AC_CHECK_LIB([xar],[xar_prop_create],[
  AC_CHECK_HEADERS([xar/xar.h], [XAR_LIB=-lxar])], [])
AC_SUBST(XAR_LIB)

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


AC_CONFIG_FILES([Makefile libstuff/Makefile])
AC_CONFIG_FILES([ar/Makefile])
AC_CONFIG_FILES([as/Makefile])
AC_CONFIG_FILES([as/arm/Makefile])
AC_CONFIG_FILES([as/i386/Makefile])
AC_CONFIG_FILES([as/x86_64/Makefile])
AC_CONFIG_FILES([as/ppc/Makefile])
AC_CONFIG_FILES([as/ppc64/Makefile])
#AC_CONFIG_FILES([man/Makefile])
AC_CONFIG_FILES([misc/Makefile])
AC_CONFIG_FILES([otool/Makefile])

if test "x$isdarwin" != "xyes"; then
  AC_CONFIG_FILES([libobjc2/Makefile])
fi

AC_CONFIG_FILES([ld64/Makefile])
AC_CONFIG_FILES([ld64/src/Makefile])
AC_CONFIG_FILES([ld64/src/3rd/Makefile])
AC_CONFIG_FILES([ld64/src/3rd/BlocksRuntime/Makefile])
AC_CONFIG_FILES([ld64/src/ld/Makefile])
AC_CONFIG_FILES([ld64/src/ld/parsers/Makefile])
AC_CONFIG_FILES([ld64/src/ld/passes/Makefile])
AC_CONFIG_FILES([ld64/src/other/Makefile])

AC_OUTPUT
