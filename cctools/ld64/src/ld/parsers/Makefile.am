noinst_LTLIBRARIES = libParsers.la

noinst_HEADERS =  \
	archive_file.h  \
	lto_file.h  \
	macho_dylib_file.h  \
	macho_relocatable_file.h  \
	opaque_section_file.h 

libParsers_la_LIBADD =  \
	$(top_srcdir)/ld64/src/3rd/libhelper.la

libParsers_la_CXXFLAGS =  \
	-D__DARWIN_UNIX03 \
	$(WARNINGS) \
	$(LTO_DEF) \
	$(ENDIAN_FLAG) \
	-I$(top_srcdir)/include \
	-I$(top_srcdir)/include/foreign \
	-I$(top_srcdir)/ld64/src/abstraction \
	-I$(top_srcdir)/ld64/src/ld \
	-I$(top_srcdir)/ld64/src/3rd \
	-I$(top_srcdir)/ld64/src

libParsers_la_SOURCES =  \
	archive_file.cpp  \
	lto_file.cpp  \
	macho_dylib_file.cpp  \
	macho_relocatable_file.cpp  \
	opaque_section_file.cpp
