if ISDARWIN
SUBDIRS=libstuff ar as misc otool ld64
else
SUBDIRS=libstuff ar as misc libobjc2 otool ld64
endif

ACLOCAL_AMFLAGS = -I m4

DSTROOT=
RC_OS=macos

installhdrs:
	cd include; $(MAKE) DSTROOT=$(DSTROOT) RC_OS=$(RC_OS) install
