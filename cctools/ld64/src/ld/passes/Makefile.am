noinst_LTLIBRARIES = libPasses.la
noinst_HEADERS =  \
	branch_island.h  \
	branch_shim.h  \
	compact_unwind.h  \
	dtrace_dof.h  \
	dylibs.h  \
	got.h  \
	huge.h  \
	objc.h  \
	order.h  \
	tlvp.h \
	bitcode_bundle.h \
	code_dedup.h


libPasses_la_CXXFLAGS = \
	-D__DARWIN_UNIX03 \
	$(WARNINGS) \
	$(LTO_DEF) \
	$(ENDIAN_FLAG) \
	-I$(top_srcdir)/include \
	-I$(top_srcdir)/include/foreign \
	-I$(top_srcdir)/ld64/src/abstraction \
	-I$(top_srcdir)/ld64/src/ld \
	-I$(top_srcdir)/ld64/src/ld/parsers \
	-I$(top_srcdir)/ld64/src/3rd \
	-I$(top_srcdir)/ld64/src

libPasses_la_SOURCES =  \
	branch_island.cpp  \
	branch_shim.cpp  \
	compact_unwind.cpp  \
	dtrace_dof.cpp  \
	dylibs.cpp  \
	got.cpp  \
	huge.cpp  \
	objc.cpp  \
	order.cpp  \
	tlvp.cpp \
	stubs/stubs.cpp \
	bitcode_bundle.cpp \
	code_dedup.cpp \
	thread_starts.cpp \
	inits.cpp \
	objc_constants.cpp \
	objc_stubs.cpp
