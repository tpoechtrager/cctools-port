AC_DEFUN([CHECK_LLVM],
[
    AC_ARG_ENABLE([lto],
    AS_HELP_STRING([--disable-lto],
                   [disable link time optimization support]))

    AC_ARG_ENABLE([ld64_liblto_proxy],
    AS_HELP_STRING([--disable-ld64-liblto-proxy],
                   [disable support for ld64 `-lto_library' (non-Apple OSs only)]))

    AC_ARG_WITH([llvm-config],
    AS_HELP_STRING([--with-llvm-config],
                   [llvm config tool]),
    [LLVM_CONFIG=$with_llvm_config], [LLVM_CONFIG=no])

    if test "x$enable_lto" != "xno"; then
        if test "x$LLVM_CONFIG" = "xno"; then
            AC_PATH_PROGS(LLVM_CONFIG,
                [llvm-config                                    \
                llvm-config-3.8 llvm-config-3.7 llvm-config-3.6 \
                llvm-config-3.5 llvm-config-3.4 llvm-config-3.3 \
                llvm-config-3.2 llvm-config-3.1                 \
                llvm-config38 llvm-config37 llvm-config36       \
                llvm-config35 llvm-config34 llvm-config33       \
                llvm-config32 llvm-config31],
            no)
        fi

        if test "x$LLVM_CONFIG" != "xno"; then
            LLVM_INCLUDE_DIR="`${LLVM_CONFIG} --includedir`"
            LLVM_LIB_DIR="`${LLVM_CONFIG} --libdir`"

            ORIGLDFLAGS=$LDFLAGS
            LDFLAGS="$LDFLAGS -L${LLVM_LIB_DIR}"

            AC_CHECK_LIB([LTO],[lto_get_version],
             [ LTO_LIB="-L${LLVM_LIB_DIR} -lLTO"
               if test "x$rpathlink" = "xyes"; then
                  LTO_RPATH="-Wl,-rpath,$LLVM_LIB_DIR,--enable-new-dtags"
               elif test "x$isdarwin" = "xyes"; then
                  LTO_LIB="-Wl,-lazy-library,${LLVM_LIB_DIR}/libLTO.dylib"
               fi
               LTO_DEF=-DLTO_SUPPORT
               # DO NOT include the LLVM include dir directly,
               # it may cause the build to fail.
               cp -f $LLVM_INCLUDE_DIR/llvm-c/lto.h `dirname ${0}`/include/llvm-c/lto.h
               AC_SUBST([LTO_DEF])
               AC_SUBST([LTO_RPATH])
               AC_SUBST([LTO_LIB]) ])

            LD64_LTO_LIB=$LTO_LIB

            if test "x$enable_ld64_liblto_proxy" = "xyes"; then
                LD64_LTO_LIB=""
            fi

            AC_SUBST([LD64_LTO_LIB])

            LDFLAGS=$ORIGLDFLAGS
        else
            AC_MSG_WARN([llvm-config not found, disabling LTO support])
        fi
    fi

    AC_SUBST(LLVM_CONFIG)
    AC_SUBST(LLVM_INCLUDE_DIR)
    AC_SUBST(LLVM_LIB_DIR)
])
