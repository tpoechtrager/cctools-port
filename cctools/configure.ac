AC_INIT([cctools], [855], [t.poechtrager@gmail.com])

AC_CANONICAL_BUILD
AC_CANONICAL_HOST
AC_CANONICAL_TARGET

AC_ARG_PROGRAM

# force compiler to clang
CC=clang
CXX=clang++
CPP="clang -E"

CFLAGS="$CFLAGS `cat tmp/cflags 2>/dev/null`"
CFLAGS="$CFLAGS -isystem /usr/local/include -isystem /usr/pkg/include -O3"
CXXFLAGS="$CXXFLAGS -std=c++0x `cat tmp/cxxflags 2>/dev/null`"
CXXFLAGS="$CXXFLAGS -isystem /usr/local/include -isystem /usr/pkg/include -O3"

LDFLAGS="$LDFLAGS -L/usr/local/lib `cat tmp/ldflags 2>/dev/null`"

LD_LIBRARY_PATH="$LD_LIBRARY_PATH:`cat tmp/ldpath 2>/dev/null`"
export LD_LIBRARY_PATH

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

AC_CHECK_HEADERS([openssl/md5.h], [], [AC_MSG_ERROR([*** md5.h not found, Please install ssl dev packages according to your system])])
AC_CHECK_HEADERS([uuid/uuid.h], [], [AC_MSG_ERROR([*** uuid.h not found, Please install uuid dev packages according to your system])])

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
     arm)
        AC_DEFINE(EMULATED_HOST_CPU_TYPE, 12, [Emulated CPU type])
        AC_DEFINE(EMULATED_HOST_CPU_SUBTYPE, 0, [Emulated CPU subtype])
    ;;
     *)
    AC_MSG_ERROR([Unsupported target $target_cpu])
    ;;
esac

#as reported, some users may use llvm-c/Disassembler.h instead of local copy in include dir.
#add these macros for avoid compilation failed.

AC_DEFINE(__STDC_LIMIT_MACROS)
AC_DEFINE(__STDC_CONSTANT_MACROS)



#for as wrapper.
AC_SUBST([ASLIBEXECDIR], ['${libexecdir}/as'])

#for ld_class involk in ld64 and other command call.
if test "$target_alias"; then
AC_SUBST([PROGRAM_PREFIX], ['${target_alias}-'])
fi

#disable warnings.

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
CFLAGS="$CFLAGS -Wno-objc-root-class"
AC_MSG_CHECKING([if -Wno-objc-root-class is supported])
AC_COMPILE_IFELSE(
   [AC_LANG_SOURCE([[const char hw[] = "Hello, World\n";]])],
   [WARNINGS="$WARNINGS -Wno-objc-root-class"
    AC_MSG_RESULT([yes])],
   [AC_MSG_RESULT([no])]
)
CFLAGS=$ORIGCFLAGS

ORIGCFLAGS=$CFLAGS
CFLAGS="$CFLAGS -Wno-deprecated-objc-isa-usage"
AC_MSG_CHECKING([if -Wno-deprecated-objc-isa-usage is supported])
AC_COMPILE_IFELSE(
   [AC_LANG_SOURCE([[const char hw[] = "Hello, World\n";]])],
   [WARNINGS="$WARNINGS -Wno-deprecated-objc-isa-usage"
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

AC_SUBST([WARNINGS], [$WARNINGS])


AC_CHECK_LIB([dl],[dlopen],[DL_LIBS=-ldl])
AC_SUBST(DL_LIBS)

AC_CHECK_LIB([pthread],[pthread_create],[PTHREAD_FLAGS=-pthread])
AC_SUBST(PTHREAD_FLAGS)

AC_CHECK_LIB([uuid],[uuid_generate_random],[UUID_LIBS=-luuid])
AC_SUBST(UUID_LIBS)

AC_CHECK_LIB([crypto],[MD5_Init],[CRYPT_LIBS=-lcrypto])
AC_SUBST(CRYPT_LIBS)

AC_CHECK_LIB([c++abi],[__cxa_demangle],[CXXABI_LIB=-lc++abi],[
  AC_CHECK_LIB([supc++],[__cxa_demangle],[CXXABI_LIB=-lsupc++],[
    AC_CHECK_LIB([stdc++],[__cxa_demangle],[CXXABI_LIB=-lstdc++],[exit 1])
  ])
])

AC_SUBST(CXXABI_LIB)

AC_C_BIGENDIAN([AC_SUBST([ENDIAN_FLAG],[-D__BIG_ENDIAN__=1])],
      [AC_SUBST([ENDIAN_FLAG],[-D__LITTLE_ENDIAN__=1])])

AC_CHECK_FUNCS([strmode])

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
AC_CONFIG_FILES([libobjc2/Makefile])
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
