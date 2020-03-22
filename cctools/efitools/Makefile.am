bin_PROGRAMS =  \
	makerelocs \
	mtoc \
	mtor

LDADD =  \
	$(top_builddir)/libstuff/libstuff.la \
	$(REALLOCF_LIB)

AM_CFLAGS = -I$(top_srcdir)/include -I$(top_srcdir)/include/foreign -I$(top_srcdir)/libstuff $(WARNINGS) $(LTO_DEF) -D__DARWIN_UNIX03 $(ENDIAN_FLAG)

makerelocs_SOURCES= makerelocs.c
mtoc_SOURCES= mtoc.c
mtor_SOURCES= mtor.c
