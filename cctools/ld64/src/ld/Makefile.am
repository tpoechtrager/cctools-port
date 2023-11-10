SUBDIRS = parsers passes

bin_PROGRAMS = ld
ld_LDADD =  \
	$(top_builddir)/ld64/src/3rd/libhelper.la  \
	$(top_builddir)/ld64/src/3rd/BlocksRuntime/libBlocksRuntime.la  \
	$(top_builddir)/ld64/src/ld/parsers/libParsers.la \
	$(top_builddir)/ld64/src/ld/passes/libPasses.la \
        $(top_builddir)/ld64/src/mach_o/libmach_o.la \
	$(UUID_LIB) \
	$(LTO_RPATH) \
	$(XAR_LIB) \
	$(DL_LIB) \
	$(TAPI_LIB) \
	$(REALLOCF_LIB) \
	$(DISPATCH_LIB)

ld_LDFLAGS = $(PTHREAD_FLAGS) $(EXECINFO_LIB)

ld_CXXFLAGS =  \
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

ld_CFLAGS =  \
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
	-I$(top_srcdir)/ld64/src/ld \
	-I$(top_srcdir)/ld64/src/ld/parsers \
	-I$(top_srcdir)/ld64/src/ld/passes \
	-I$(top_srcdir)/ld64/src/mach_o \
	-DPROGRAM_PREFIX="\"$(PROGRAM_PREFIX)\"" 

ld_SOURCES =  \
	debugline.c  \
	libcodedirectory.c \
	InputFiles.cpp  \
	ld.cpp  \
	Options.cpp  \
	OutputFile.cpp  \
	Resolver.cpp  \
	Snapshot.cpp  \
	SymbolTable.cpp \
	PlatformSupport.cpp \
	ResponseFiles.cpp \
	FatFile.cpp \
	Mangling.cpp \
	code-sign-blobs/blob.cpp
