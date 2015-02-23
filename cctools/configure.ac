AC_INIT([cctools], [862], [t.poechtrager@gmail.com])

AC_CANONICAL_BUILD
AC_CANONICAL_HOST
AC_CANONICAL_TARGET

AC_ARG_PROGRAM

case $host_os in
  darwin* )
    isdarwin=yes
    AM_CONDITIONAL([ISDARWIN], [true])
    ;;
  * )
    AM_CONDITIONAL([ISDARWIN], [false])
    ;;
esac

if test "x$CFLAGS" = "x"; then
  CFLAGS="-O3"
fi

if test "x$CXXFLAGS" = "x"; then
  CXXFLAGS="-O3"
fi

if test "x$OBJCFLAGS" = "x"; then
  OBJCFLAGS="-O3"
fi

if test "x$isdarwin" = "xyes"; then
  CXXFLAGS="$CXXFLAGS -stdlib=libc++"
fi

CXXFLAGS="$CXXFLAGS -std=c++0x"

if test "x$build" = "x$host"; then
  CC="clang"
  CXX="clang++"
  CFLAGS="$CFLAGS -isystem /usr/local/include -isystem /usr/pkg/include"
  CXXFLAGS="$CXXFLAGS -isystem /usr/local/include -isystem /usr/pkg/include"
  LDFLAGS="$LDFLAGS -L/usr/local/lib -L/usr/pkg/lib"
else
  CC="`which ${host_alias}-clang 2>/dev/null || echo clang`"
  CXX="`which ${host_alias}-clang++ 2>/dev/null || echo clang++`"
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

if test "x$isdarwin" != "xyes"; then
  AC_CHECK_HEADERS([openssl/md5.h], [], [AC_MSG_ERROR([*** md5.h not found, please install ssl dev packages according to your system])])
  CRYPTO_LIB=-lcrypto
  SSL_LIB=-lssl
  AC_SUBST([CRYPTO_LIB])
  AC_SUBST([SSL_LIB])
fi

AC_CHECK_HEADERS([uuid/uuid.h], [], [AC_MSG_ERROR([*** uuid.h not found, please install uuid dev packages according to your system])])

#for libstuff emulated.c
case $target_cpu in
     powerpc)
        AC_DEFINE(EMULATED_HOST_CPU_TYPE, 18, [Emulated CPU type])
        AC_DEFINE(EMULATED_HOST_CPU_SUBTYPE, 100, [Emulated CPU subtype])
    ;;
     powerpc64)
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
     arm64*)
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

# disable certain warnings.

WARNINGS=""
OBJCWARNINGS=""

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
CFLAGS="$CFLAGS -Wno-unused-private-field"
AC_MSG_CHECKING([if -Wno-unused-private-field is supported])
AC_COMPILE_IFELSE(
   [AC_LANG_SOURCE([[const char hw[] = "Hello, World\n";]])],
   [WARNINGS="$WARNINGS -Wno-unused-private-field"
    AC_MSG_RESULT([yes])],
   [AC_MSG_RESULT([no])]
)
CFLAGS=$ORIGCFLAGS

ORIGCFLAGS=$CFLAGS
CFLAGS="$CFLAGS -Wno-unused-function"
AC_MSG_CHECKING([if -Wno-unused-function is supported])
AC_COMPILE_IFELSE(
   [AC_LANG_SOURCE([[const char hw[] = "Hello, World\n";]])],
   [WARNINGS="$WARNINGS -Wno-unused-function"
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
CFLAGS="$CFLAGS -Wno-int-conversion"
AC_MSG_CHECKING([if -Wno-int-conversion is supported])
AC_COMPILE_IFELSE(
   [AC_LANG_SOURCE([[const char hw[] = "Hello, World\n";]])],
   [WARNINGS="$WARNINGS -Wno-int-conversion"
    AC_MSG_RESULT([yes])],
   [AC_MSG_RESULT([no])]
)
CFLAGS=$ORIGCFLAGS

ORIGCFLAGS=$CFLAGS
CFLAGS="$CFLAGS -Wno-char-subscripts"
AC_MSG_CHECKING([if -Wno-char-subscripts is supported])
AC_COMPILE_IFELSE(
   [AC_LANG_SOURCE([[const char hw[] = "Hello, World\n";]])],
   [WARNINGS="$WARNINGS -Wno-char-subscripts"
    AC_MSG_RESULT([yes])],
   [AC_MSG_RESULT([no])]
)
CFLAGS=$ORIGCFLAGS

ORIGCFLAGS=$CFLAGS
CFLAGS="$CFLAGS -Wno-objc-root-class"
AC_MSG_CHECKING([if -Wno-objc-root-class is supported])
AC_COMPILE_IFELSE(
   [AC_LANG_SOURCE([[const char hw[] = "Hello, World\n";]])],
   [OBJCWARNINGS="$OBJCWARNINGS -Wno-objc-root-class"
    AC_MSG_RESULT([yes])],
   [AC_MSG_RESULT([no])]
)
CFLAGS=$ORIGCFLAGS

ORIGCFLAGS=$CFLAGS
CFLAGS="$CFLAGS -Wno-deprecated-objc-isa-usage"
AC_MSG_CHECKING([if -Wno-deprecated-objc-isa-usage is supported])
AC_COMPILE_IFELSE(
   [AC_LANG_SOURCE([[const char hw[] = "Hello, World\n";]])],
   [OBJCWARNINGS="$OBJCWARNINGS -Wno-deprecated-objc-isa-usage"
    AC_MSG_RESULT([yes])],
   [AC_MSG_RESULT([no])]
)
CFLAGS=$ORIGCFLAGS

AC_SUBST([WARNINGS], [$WARNINGS])
AC_SUBST([OBJCWARNINGS], [$OBJCWARNINGS])

AC_CHECK_LIB([dl],[dlopen],[DL_LIB=-ldl])
AC_SUBST(DL_LIB)

AC_CHECK_LIB([pthread],[pthread_create],[PTHREAD_FLAGS=-pthread])
AC_SUBST(PTHREAD_FLAGS)

AC_CHECK_LIB([uuid],[uuid_generate_random],[UUID_LIB=-luuid])
AC_SUBST(UUID_LIB)

AC_CHECK_FUNCS([strmode])

### Check for __cxa_demangle in various C++ ABI libs ###

AC_CHECK_LIB([c++abi],[__cxa_demangle],[CXXABI_LIB=-lc++abi],[
  AC_CHECK_LIB([supc++],[__cxa_demangle],[CXXABI_LIB=-lsupc++],[
    AC_CHECK_LIB([stdc++],[__cxa_demangle],[CXXABI_LIB=-lstdc++],[exit 1])
  ])
])

AC_SUBST(CXXABI_LIB)

### Check for libLTO ###

ORIGLDFLAGS=$LDFLAGS
LDFLAGS="$LDFLAGS -Wl,-rpath,/usr/local/lib,--enable-new-dtags"
AC_MSG_CHECKING([if -Wl,-rpath,<path>,--enable-new-dtags is supported])
AC_LINK_IFELSE(
  [AC_LANG_SOURCE([[int main(){}]])],
  [rpathlink=yes
  AC_MSG_RESULT([yes])],
  [AC_MSG_RESULT([no])]
)
LDFLAGS=$ORIGLDFLAGS

CHECK_LLVM

LDFLAGS="$LDFLAGS $LTO_RPATH"

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

AC_CONFIG_FILES([ld/Makefile])

AC_CONFIG_FILES([ld64/Makefile])
AC_CONFIG_FILES([ld64/src/Makefile])
AC_CONFIG_FILES([ld64/src/3rd/Makefile])
AC_CONFIG_FILES([ld64/src/3rd/BlocksRuntime/Makefile])
AC_CONFIG_FILES([ld64/src/ld/Makefile])
AC_CONFIG_FILES([ld64/src/ld/parsers/Makefile])
AC_CONFIG_FILES([ld64/src/ld/passes/Makefile])
AC_CONFIG_FILES([ld64/src/other/Makefile])

AC_OUTPUT
