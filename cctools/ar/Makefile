ifneq "" "$(SDKROOT)"
  SDK = -isysroot $(SDKROOT)
  CC = $(shell xcrun -find -sdk $(SDKROOT) cc)
endif
OFLAG = -Os
ifeq "mwccppc" "$(notdir $(CC))"
  CFLAGS = -g $(OFLAG) -I$(SRCROOT)/../include $(SDK)
else
  CFLAGS = -g $(OFLAG) -Wall -I$(SRCROOT)/../include $(SDK)
endif

LIBSTUFF = -L$(SYMROOT)/../libstuff -lstuff

USE_DEPENDENCY_FILE := $(shell if [ "$(notdir $(CC))" != "mwccppc" ] && \
				  [ "$(RC_XBS)" != "YES" ]; then \
	     echo YES ; else echo NO ; \
	     fi; )

ifneq "" "$(wildcard /bin/mkdirs)"
  MKDIRS = /bin/mkdirs
else
  MKDIRS = /bin/mkdir -p
endif

SRCROOT = .
OBJROOT = .
SYMROOT = .
OFILE_DIR = $(OBJROOT)
VPATH = $(OFILE_DIR)

BINDIR = /usr/bin
MANDIR = /usr/share/man
DSTDIRS = $(DSTROOT)$(BINDIR) $(DSTROOT)$(MANDIR)/man1 $(DSTROOT)$(MANDIR)/man5

HFILES = archive.h extern.h pathnames.h
CFILES = append.c ar.c archive.c contents.c delete.c extract.c misc.c move.c \
	 print.c replace.c 
MAN1 = ar.1
MAN5 = ar.5
OBJS = $(CFILES:.c=.o)
INSTALL_FILES = $(CFILES) $(HFILES) $(MAN1) $(MAN5) Makefile notes

PRODUCT = ar

all: $(OFILE_DIR) $(SYMROOT) $(PRODUCT).NEW

$(PRODUCT).NEW:	$(OFILE_DIR) $(SYMROOT) $(OBJS)
	$(CC) $(RC_CFLAGS) $(SDK) -o $(SYMROOT)/$@ $(OBJS) $(LIBSTUFF)

ifeq "NO" "$(USE_DEPENDENCY_FILE)"
.c.o:
	$(CC) $(CFLAGS) $(RC_CFLAGS) $(COPTS) -c -o $(OFILE_DIR)/$*.o $<
else
.c.o:
	$(CC) $(CFLAGS) $(RC_CFLAGS) -c -MD -dependency-file $(OFILE_DIR)/$*.d \
		-o $(OFILE_DIR)/$*.o $<
	md -d -u $(OFILE_DIR)/Makedep $(OFILE_DIR)/$*.d
endif

clean: shlib_clean
	-rm -f $(SYMROOT)/$(PRODUCT).NEW

shlib_clean:
	-cd $(OFILE_DIR); rm -f $(OBJS)
	-rm -f $(OFILE_DIR)/Makedep make.out

install: $(RC_OS)

teflon macos: all $(DSTDIRS)
	install -c -s -m 555 $(SYMROOT)/$(PRODUCT).NEW \
		$(DSTROOT)$(BINDIR)/$(PRODUCT)
	install -c -m 444 $(MAN1) $(DSTROOT)$(MANDIR)/man1
	install -c -m 444 $(MAN5) $(DSTROOT)$(MANDIR)/man5

nextstep:

installsrc:
	$(MKDIRS) $(SRCROOT)
	chmod 755 $(SRCROOT)
	gnutar cf - $(INSTALL_FILES) | (cd $(SRCROOT); gnutar xf -)
	chmod 444 $(SRCROOT)/*

$(OFILE_DIR) $(SYMROOT) $(DSTDIRS):
	$(MKDIRS) $@

-include $(OFILE_DIR)/Makedep
