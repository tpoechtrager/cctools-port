SUBDIRS=libstuff libmacho ar as misc otool efitools ld64 man

ACLOCAL_AMFLAGS = -I m4

DSTROOT=
RC_OS=macos

installhdrs:
	cd include; $(MAKE) DSTROOT=$(DSTROOT) RC_OS=$(RC_OS) install
