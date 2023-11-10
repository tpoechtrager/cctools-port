noinst_LTLIBRARIES = libmach_o.la
noinst_HEADERS = Error.h ExportsTrie.h

libmach_o_la_CXXFLAGS =  \
	-D__DARWIN_UNIX03 \
	$(WARNINGS) \
	$(LTO_DEF) \
	$(TAPI_DEF) \
	$(ENDIAN_FLAG) \
	-I$(top_srcdir)/include \
	-I$(top_srcdir)/include/foreign \
	-I$(top_srcdir)/ld64/src \
	-I$(top_srcdir)/ld64/src/abstraction \
	-I$(top_srcdir)/ld64/src/3rd \
	-I$(top_srcdir)/ld64/src/3rd/BlocksRuntime \
	-I$(top_srcdir)/ld64/src/3rd/include \
	-I$(top_srcdir)/ld64/src/ld \
	-I$(top_srcdir)/ld64/src/ld/parsers \
	-I$(top_srcdir)/ld64/src/ld/passes \
	-I$(top_srcdir)/ld64/src/mach_o \
	-DPROGRAM_PREFIX="\"$(PROGRAM_PREFIX)\""

libmach_o_la_SOURCES =  \
	Error.cpp  \
	ExportsTrie.cpp
