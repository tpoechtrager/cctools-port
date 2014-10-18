bin_PROGRAMS = otool

if ISDARWIN
otool_LDADD = $(top_srcdir)/libstuff/libstuff.la
otool_LDFLAGS = $(PTHREAD_FLAGS) $(CXXABI_LIB) $(DL_LIB) -lobjc
else
otool_LDADD = \
$(top_srcdir)/libstuff/libstuff.la \
$(top_srcdir)/libobjc2/libobjc.la
otool_LDFLAGS = $(PTHREAD_FLAGS) $(CXXABI_LIB) $(DL_LIB)
endif

otool_CFLAGS = -I$(top_srcdir)/include -I$(top_srcdir)/include/foreign -I$(top_srcdir)/libstuff -I$(top_srcdir)/libobjc2 $(WARNINGS) $(LTO_DEF) -D_DARWIN_C_SOURCE -D__DARWIN_UNIX03 $(ENDIAN_FLAG) -DOTOOL

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
	arm64_disasm.c \
	dyld_bind_info.c
