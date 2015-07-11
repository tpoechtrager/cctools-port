SUBDIRS = BlocksRuntime
noinst_LTLIBRARIES = libhelper.la
noinst_HEADERS = helper.h strlcat.h strlcpy.h qsort_r.h
libhelper_la_CFLAGS=-I$(top_srcdir)/include -I$(top_srcdir)/include/foreign $(ENDIAN_FLAG)

libhelper_la_SOURCES =  \
	helper.c  \
	qsort_r.c  \
	strlcat.c  \
	strlcpy.c \
	eprintf.c \
	md5.c
