if ISDARWIN
SUBDIRS=libstuff ar as misc otool ld64 man
else
SUBDIRS=libobjc2 libstuff ar as misc otool ld64 man
endif

ACLOCAL_AMFLAGS = -I m4

DSTROOT=
RC_OS=macos

installhdrs:
	cd include; $(MAKE) DSTROOT=$(DSTROOT) RC_OS=$(RC_OS) install
