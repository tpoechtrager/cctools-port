ifneq "" "$(wildcard /bin/mkdirs)"
  MKDIRS = /bin/mkdirs
else
  MKDIRS = /bin/mkdir -p
endif

DYLD_MAN1 = dyld.1
OS_MAN1 = strings.1 lipo.1
COMMON_MAN1 = as.1 gprof.1 nm.1 otool.1 ranlib.1 segedit.1 size.1 \
	      strip.1 libtool.1 cmpdylib.1 pagestuff.1 \
	      redo_prebinding.1 nmedit.1 install_name_tool.1 \
	      codesign_allocate.1 ctf_insert.1

DYLD_MAN3 = dyld.3 NSModule.3 NSObjectFileImage.3 dyld_debug.3
MAN3 = kld.3 arch.3 get_end.3 end.3 getsectbyname.3 \
       getsectbynamefromheader.3 getsectdata.3 getsectdatafromheader.3 \
       getsegbyname.3

MAN5 = Mach-O.5 a.out.5 ranlib.5 stab.5

MANL = indr.1 checksyms.1 seg_addr_table.1 check_dylib.1

EFI1 = mtoc.1

DYLD_MANL3 = NSObjectFileImage_priv.3
MANL3 = libsyminfo.3 redo_prebinding.3

INSTALL_FILES = Makefile $(OS_MAN1) $(COMMON_MAN1) $(MAN3) $(MAN5) $(MANL) \
		$(MANL3) $(DYLD_MAN1) $(DYLD_MAN3) $(DYLD_MANL3) $(EFI1) notes

MANDIR = /usr/share/man
LOCMANDIR = /usr/local/man
EFIMANDIR = /usr/local/efi/share/man
DSTDIRS_MAN1 = $(DSTROOT)$(MANDIR)/man1
DSTDIRS = $(DSTROOT)$(MANDIR)/man3 \
	  $(DSTROOT)$(MANDIR)/man5 $(DSTROOT)$(LOCMANDIR)/man1 \
	  $(DSTROOT)$(LOCMANDIR)/man3 $(DSTROOT)$(EFIMANDIR)/man1

install_os_tools: $(DSTDIRS_MAN1) $(OS_MAN1)
	install -c -m 444 $(OS_MAN1) $(DSTROOT)$(MANDIR)/man1

install: $(DSTDIRS) $(DSTDIRS_MAN1) $(COMMON_MAN1) $(MAN3) $(MAN5) $(MANL) \
	 $(RC_OS) $(MANL3) install_os_tools
	install -c -m 444 $(COMMON_MAN1) $(DSTROOT)$(MANDIR)/man1
	if [ $(OLD_DYLD_STUFF) ];					\
	then								\
	    install -c -m 444 $(DYLD_MAN1) $(DSTROOT)$(MANDIR)/man1 ;	\
	fi
	install -c -m 444 $(MAN3) $(DSTROOT)$(MANDIR)/man3
	if [ $(OLD_DYLD_STUFF) ];					\
	then								\
	    install -c -m 444 $(DYLD_MAN3) $(DSTROOT)$(MANDIR)/man3 ;	\
	fi
	install -c -m 444 $(MAN5) $(DSTROOT)$(MANDIR)/man5
	install -c -m 444 $(MANL) $(DSTROOT)$(LOCMANDIR)/man1
	install -c -m 444 $(MANL3) $(DSTROOT)$(LOCMANDIR)/man3
	install -c -m 444 $(EFI1) $(DSTROOT)$(EFIMANDIR)/man1
	if [ $(OLD_DYLD_STUFF) ];					\
	then								\
	    install -c -m 444 $(DYLD_MANL3) $(DSTROOT)$(LOCMANDIR)/man3;\
	fi

teflon macos:

nextstep:

installsrc:
	$(MKDIRS) $(SRCROOT)
	chmod 755 $(SRCROOT)
	gnutar cf - $(INSTALL_FILES) | (cd $(SRCROOT); gnutar xf -)
	chmod 444 $(SRCROOT)/*

$(DSTDIRS) $(DSTDIRS_MAN1):
	$(MKDIRS) $@

clean shlib_clean:

all:
