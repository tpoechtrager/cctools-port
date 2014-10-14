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
	codesign_allocate

LDADD =  \
	$(top_srcdir)/libstuff/libstuff.la \
	$(DL_LIB)

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
pagestuff_SOURCES=pagestuff.c

ranlib_SOURCES = libtool.c
ranlib_CFLAGS = -DRANLIB $(AM_CFLAGS)
codesign_allocate_SOURCES = codesign_allocate.c

