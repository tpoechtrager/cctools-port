if !ISDARWIN
LIBOBJC2_SUBDIR=libobjc2
endif
if ENABLE_LIBTAPI
LIBTAPI_SUBDIR=libtapi
endif

SUBDIRS=libstuff ar as misc $(LIBOBJC2_SUBDIR) $(LIBTAPI_SUBDIR) otool ld64

ACLOCAL_AMFLAGS = -I m4

DSTROOT=
RC_OS=macos

installhdrs:
	cd include; $(MAKE) DSTROOT=$(DSTROOT) RC_OS=$(RC_OS) install
