SUBDIRS = 
noinst_LTLIBRARIES = libhelper.la
noinst_HEADERS = helper.h strlcat.h strlcpy.h qsort_r.h md5.h mkpath_np.h
libhelper_la_CFLAGS=-I$(top_srcdir)/include -I$(top_srcdir)/include/foreign $(ENDIAN_FLAG) $(WARNINGS) -D__DARWIN_UNIX03

libhelper_la_SOURCES =  \
	helper.c  \
	qsort_r.c  \
	strlcat.c  \
	strlcpy.c \
	eprintf.c \
	md5.c \
	sha1.c \
	sha256.c \
	mkpath_np.c \
	reallocf.c
