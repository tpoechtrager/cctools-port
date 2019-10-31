noinst_LTLIBRARIES = libstuff.la

libstuff_la_CFLAGS = -I$(top_srcdir)/include -I$(top_srcdir)/include/foreign -D__DARWIN_UNIX03 $(WARNINGS) $(LTO_DEF) -DPROGRAM_PREFIX="\"$(PROGRAM_PREFIX)\"" $(ENDIAN_FLAG)

libstuff_la_LDFLAGS = $(FTS_LIB)

libstuff_la_SOURCES =  \
	allocate.c  \
	apple_version.c  \
	arch.c  \
	arch_usage.c  \
	best_arch.c  \
	breakout.c  \
	bytesex.c  \
	checkout.c  \
	crc32.c  \
	dylib_roots.c  \
	dylib_table.c  \
	port.c  \
	errors.c  \
	execute.c  \
	fatal_arch.c  \
	fatals.c  \
	get_arch_from_host.c  \
	get_toc_byte_sex.c  \
	guess_short_name.c  \
	hash_string.c  \
	hppa.c  \
	llvm.c  \
	lto.c  \
	macosx_deployment_target.c  \
	ofile.c  \
	ofile_error.c  \
	ofile_get_word.c  \
	print.c  \
	reloc.c  \
	rnd.c  \
	seg_addr_table.c  \
	set_arch_flag_name.c  \
	swap_headers.c  \
	symbol_list.c  \
	SymLoc.c  \
	unix_standard_mode.c  \
	version_number.c  \
	writeout.c \
	write64.c \
	coff_bytesex.c

