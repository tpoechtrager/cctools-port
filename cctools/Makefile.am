SUBDIRS=libstuff ar as misc otool ld64 man

if !ISDARWIN
SUBDIRS+=libobjc2
endif

ACLOCAL_AMFLAGS = -I m4

DSTROOT=
RC_OS=macos

installhdrs:
	cd include; $(MAKE) DSTROOT=$(DSTROOT) RC_OS=$(RC_OS) install
