bin_PROGRAMS =  \
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
	-I$(top_srcdir)/ld64/src/ld/passes \
        -DBUILDING_TOOLS

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
machocheck_LDADD = $(top_builddir)/ld64/src/3rd/libhelper.la

ObjectDump_SOURCES = \
	ObjectDump.cpp \
	$(top_srcdir)/ld64/src/ld/PlatformSupport.cpp \
	$(top_srcdir)/ld64/src/ld/debugline.c \
	$(top_srcdir)/ld64/src/ld/SymbolTable.cpp

ObjectDump_LDADD =  \
	$(top_builddir)/ld64/src/ld/parsers/libParsers.la \
	$(top_builddir)/ld64/src/3rd/BlocksRuntime/libBlocksRuntime.la  \
	$(LTO_RPATH) \
	$(LTO_LIB) \
	$(DL_LIB) 



