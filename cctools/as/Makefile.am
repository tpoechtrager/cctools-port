SUBDIRS=arm i386 x86_64 ppc ppc64

bin_PROGRAMS = as 
as_LDADD =  \
        $(top_srcdir)/libstuff/libstuff.la

as_CFLAGS = -I$(top_srcdir)/include -I$(top_srcdir)/include/foreign -I$(top_srcdir)/libstuff $(WARNINGS) $(LTO_DEFS) -DNeXT_MOD -DASLIBEXECDIR="\"$(ASLIBEXECDIR)/\"" -D__DARWIN_UNIX03 $(ENDIAN_FLAG)

as_SOURCES = driver.c 
