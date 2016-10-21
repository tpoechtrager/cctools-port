bin_PROGRAMS =  \
	checksyms \
	lipo \
	size \
	strings \
	nm \
	libtool \
	redo_prebinding \
	seg_addr_table \
	seg_hack \
	install_name_tool \
	indr \
	strip \
	nmedit \
	segedit \
	pagestuff \
	ranlib \
	codesign_allocate \
	bitcode_strip \
	ctf_insert \
	check_dylib

LDADD =  \
	$(top_builddir)/libstuff/libstuff.la \
	$(DL_LIB) \
	$(LTO_RPATH)

AM_CFLAGS = -I$(top_srcdir)/include -I$(top_srcdir)/include/foreign -I$(top_srcdir)/libstuff $(WARNINGS) $(LTO_DEF) -D__DARWIN_UNIX03 $(ENDIAN_FLAG)

checksyms_SOURCES = checksyms.c
lipo_SOURCES = lipo.c
size_SOURCES = size.c
strings_SOURCES = strings.c
nm_SOURCES = nm.c
libtool_SOURCES = libtool.c
redo_prebinding_SOURCES = redo_prebinding.c
seg_addr_table_SOURCES = seg_addr_table.c
seg_hack_SOURCES = seg_hack.c
install_name_tool_SOURCES = install_name_tool.c
indr_SOURCES = indr.c
strip_SOURCES = strip.c
nmedit_SOURCES = strip.c
nmedit_CFLAGS = -DNMEDIT $(AM_CFLAGS)
segedit_SOURCES = segedit.c
pagestuff_SOURCES = pagestuff.c
ranlib_SOURCES = libtool.c
ranlib_CFLAGS = -DRANLIB $(AM_CFLAGS)
codesign_allocate_SOURCES = codesign_allocate.c
bitcode_strip_SOURCES= bitcode_strip.c
bitcode_strip_CFLAGS = -DALLOW_ARCHIVES $(AM_CFLAGS)
ctf_insert_SOURCES= ctf_insert.c
check_dylib_SOURCES= check_dylib.c
