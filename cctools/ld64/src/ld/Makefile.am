SUBDIRS = parsers passes

bin_PROGRAMS = ld
ld_LDADD =  \
    $(top_srcdir)/ld64/src/3rd/libhelper.la  \
    $(top_srcdir)/ld64/src/3rd/BlocksRuntime/libBlocksRuntime.la  \
	$(top_srcdir)/ld64/src/ld/parsers/libParsers.la \
	$(top_srcdir)/ld64/src/ld/passes/libPasses.la \
	$(DL_LIBS) \
	$(CRYPT_LIBS) \
	$(UUID_LIBS) \
	$(LTO_LIBS)

ld_LDFLAGS = $(PTHREAD_FLAGS) -lssl

ld_CXXFLAGS =  \
	-fblocks \
	-D__DARWIN_UNIX03 \
	$(WARNINGS) \
	$(LTO_DEFS) \
	$(ENDIAN_FLAG) \
	-I$(top_srcdir)/include \
	-I$(top_srcdir)/include/foreign \
	-I$(top_srcdir)/ld64/src \
	-I$(top_srcdir)/ld64/src/abstraction \
	-I$(top_srcdir)/ld64/src/3rd \
	-I$(top_srcdir)/ld64/src/3rd/BlocksRuntime \
	-I$(top_srcdir)/ld64/src/ld \
	-I$(top_srcdir)/ld64/src/ld/parsers \
    -I$(top_srcdir)/ld64/src/ld/passes \
    -DPROGRAM_PREFIX="\"$(PROGRAM_PREFIX)\"" 

ld_CFLAGS =  \
    -fblocks \
    -D__DARWIN_UNIX03 \
    $(WARNINGS) \
    $(LTO_DEFS) \
    $(ENDIAN_FLAG) \
    -I$(top_srcdir)/include \
    -I$(top_srcdir)/include/foreign \
    -I$(top_srcdir)/ld64/src \
    -I$(top_srcdir)/ld64/src/abstraction \
    -I$(top_srcdir)/ld64/src/3rd \
	  -I$(top_srcdir)/ld64/src/3rd/BlocksRuntime \
    -I$(top_srcdir)/ld64/src/ld \
    -I$(top_srcdir)/ld64/src/ld/parsers \
    -I$(top_srcdir)/ld64/src/ld/passes \
    -DPROGRAM_PREFIX="\"$(PROGRAM_PREFIX)\"" 

ld_SOURCES =  \
	debugline.c  \
	InputFiles.cpp  \
	ld.cpp  \
	Options.cpp  \
	OutputFile.cpp  \
	Resolver.cpp  \
	Snapshot.cpp  \
	SymbolTable.cpp \
	code-sign-blobs/blob.cpp
