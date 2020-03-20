bin_PROGRAMS = otool

if ISDARWIN
otool_LDADD = \
$(top_builddir)/libstuff/libstuff.la \
$(PTHREAD_FLAGS) $(CXXABI_LIB) $(DL_LIB) -lobjc
else
otool_LDADD = \
$(top_builddir)/libstuff/libstuff.la \
$(top_builddir)/libobjc2/libobjc.la \
$(PTHREAD_FLAGS) $(CXXABI_LIB) $(DL_LIB) $(REALLOCF_LIB) $(GCC_LIB)
endif

if ISDARWIN
otool_CFLAGS = -I$(top_srcdir)/include -I$(top_srcdir)/include/foreign -I$(top_srcdir)/libstuff $(WARNINGS) $(LTO_DEF) -D_DARWIN_C_SOURCE -D__DARWIN_UNIX03 $(ENDIAN_FLAG) -DOTOOL -DEFI_SUPPORT
else
otool_CFLAGS = -I$(top_srcdir)/include -I$(top_srcdir)/include/foreign -I$(top_srcdir)/libstuff -I$(top_srcdir)/libobjc2 $(WARNINGS) $(LTO_DEF) -D_DARWIN_C_SOURCE -D__DARWIN_UNIX03 $(ENDIAN_FLAG) -DOTOOL -DEFI_SUPPORT
endif

otool_SOURCES =  \
	main.c \
	$(top_srcdir)/libstuff/ofile.c \
	ofile_print.c \
	m68k_disasm.c \
	i860_disasm.c \
	m88k_disasm.c \
	i386_disasm.c \
	ppc_disasm.c \
	hppa_disasm.c \
	sparc_disasm.c \
	arm_disasm.c \
	print_objc.c \
	print_objc2_32bit.c \
	print_objc2_64bit.c \
	print_bitcode.c \
	coff_print.c \
	arm64_disasm.c \
	dyld_bind_info.c
