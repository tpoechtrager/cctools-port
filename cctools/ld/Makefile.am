bin_PROGRAMS = ld_classic 
ld_classic_LDADD =  \
        $(top_builddir)/libstuff/libstuff.la   $(UUID_LIB)

ld_classic_CFLAGS = -D__DARWIN_UNIX03 -I$(top_srcdir)/include -I$(top_srcdir)/include/foreign -I$(top_srcdir)/libstuff $(WARNINGS) $(LTO_DEF) $(ENDIAN_FLAG) -DDEBUG

ld_classic_SOURCES =  \
	ld.c pass1.c objects.c sections.c cstring_literals.c symbols.c \
	fvmlibs.c layout.c specs.c pass2.c generic_reloc.c rld.c sets.c \
	4byte_literals.c 8byte_literals.c literal_pointers.c dylibs.c \
	indirect_sections.c mod_sections.c i860_reloc.c ppc_reloc.c \
	m88k_reloc.c hppa_reloc.c sparc_reloc.c coalesced_sections.c uuid.c \
	debugcompunit.c debugline.c arm_reloc.c
