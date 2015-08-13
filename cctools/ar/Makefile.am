bin_PROGRAMS = ar 
ar_LDADD =  \
        $(top_builddir)/libstuff/libstuff.la

ar_CFLAGS = -D__DARWIN_UNIX03 -I$(top_srcdir)/include -I$(top_srcdir)/include/foreign -I$(top_srcdir)/libstuff $(WARNINGS) $(LTO_DEF) $(ENDIAN_FLAG)

ar_SOURCES =  \
	append.c  \
	ar.c  \
	archive.c  \
	contents.c  \
	delete.c  \
	extract.c  \
	misc.c  \
    move.c  \
	print.c  \
	replace.c
