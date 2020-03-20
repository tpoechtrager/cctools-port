noinst_LTLIBRARIES = libmacho.la

libmacho_la_CFLAGS = -I$(top_srcdir)/include -I$(top_srcdir)/include/foreign -D__DARWIN_UNIX03 $(WARNINGS) $(LTO_DEF) -DPROGRAM_PREFIX="\"$(PROGRAM_PREFIX)\"" $(ENDIAN_FLAG)

libmacho_la_SOURCES =  \
	arch.c
