bin_PROGRAMS =  \
	dyldinfo \
	ObjectDump \
	unwinddump \
	machocheck

AM_CXXFLAGS = \
	-D__DARWIN_UNIX03 \
	$(WARNINGS) \
	-Wno-switch \
	$(LTO_DEF) \
	$(ENDIAN_FLAG) \
	-I$(top_srcdir)/include \
	-I$(top_srcdir)/include/foreign \
	-I$(top_srcdir)/ld64/src \
	-I$(top_srcdir)/ld64/src/3rd \
	-I$(top_srcdir)/ld64/src/abstraction \
	-I$(top_srcdir)/ld64/src/ld \
	-I$(top_srcdir)/ld64/src/ld/parsers \
	-I$(top_srcdir)/ld64/src/ld/passes

AM_CFLAGS = \
    -D__DARWIN_UNIX03 \
    $(WARNINGS) \
    $(LTO_DEF) \
    $(ENDIAN_FLAG) \
    -I$(top_srcdir)/include \
    -I$(top_srcdir)/include/foreign \
    -I$(top_srcdir)/ld64/src \
    -I$(top_srcdir)/ld64/src/3rd \
    -I$(top_srcdir)/ld64/src/abstraction \
    -I$(top_srcdir)/ld64/src/ld \
    -I$(top_srcdir)/ld64/src/ld/parsers \
    -I$(top_srcdir)/ld64/src/ld/passes 

unwinddump_SOURCES = unwinddump.cpp
machocheck_SOURCES = machochecker.cpp
ObjectDump_SOURCES = \
	ObjectDump.cpp \
	$(top_srcdir)/ld64/src/ld/debugline.c 
ObjectDump_LDADD =  \
	$(top_srcdir)/ld64/src/ld/parsers/libParsers.la \
	$(LTO_LIB)


dyldinfo_SOURCES =  dyldinfo.cpp
dyldinfo_LDADD =  \
	$(top_srcdir)/ld64/src/3rd/libhelper.la


