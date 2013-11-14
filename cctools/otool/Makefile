RC_OS = macos
export USE_APPLE_PB_SUPPORT = all
OFLAG = -Os
LEGACY = -D_MACH_I386_THREAD_STATUS_FPSTATE_LEGACY_FIELD_NAMES_ \
	 -D_ARCHITECTURE_I386_FPU_FPSTATE_LEGACY_FIELD_NAMES_
X_CFLAGS =

ifeq "macos" "$(RC_OS)"
  STATIC := $(shell if [ "$(RC_RELEASE)" = "Beaker"    ] || \
		     [ "$(RC_RELEASE)" = "Bunsen"    ] || \
		     [ "$(RC_RELEASE)" = "Gonzo"     ] || \
		     [ "$(RC_RELEASE)" = "Kodiak"    ] || \
		     [ "$(RC_RELEASE)" = "Cheetah"   ] || \
		     [ "$(RC_RELEASE)" = "Puma"      ] || \
		     [ "$(RC_RELEASE)" = "Jaguar"    ] || \
		     [ "$(RC_RELEASE)" = "Panther"   ] || \
		     [ "$(RC_RELEASE)" = "MuonPrime" ] || \
		     [ "$(RC_RELEASE)" = "MuonSeed"  ] || \
		     [ "$(RC_RELEASE)" = "SUPanWheat" ]; then \
		    echo "-static" ; \
	    else if [ "$(RC_RELEASE)" = "Tiger" ]; then \
		    echo "-static" ; \
	    else \
		    echo "" ; \
	  fi; fi; )
else
  STATIC = -static
endif

ifeq "$(EFITOOLS)" "efitools"
	EFI_SUPPORT = -DEFI_SUPPORT
endif

ifneq "" "$(SDKROOT)"
  SDK = -isysroot $(SDKROOT)
  CC = $(shell xcrun -find -sdk $(SDKROOT) cc)
  CXX = $(shell xcrun -find -sdk $(SDKROOT) c++)
endif

ifeq "clang" "$(notdir $(CC))"
  CFLAGS = $(STATIC) $(OFLAG) -g -Wall -I$(SRCROOT)/../include \
	   -I$(SRCROOT) $(LEGACY) $(EFI_SUPPORT) $(SDK)
else
  CFLAGS = $(STATIC) $(OFLAG) -g -Wall -I$(SRCROOT)/../include \
	   -I$(SRCROOT) $(X_CFLAGS) $(LEGACY) $(EFI_SUPPORT) $(SDK)
endif

USE_DEPENDENCY_FILE := $(shell if [ "$(notdir $(CC))" != "mwccppc" ] && \
				  [ "$(RC_XBS)" != "YES" ]; then \
	     echo YES ; else echo NO ; \
	     fi; )

LIBSTUFF = -L$(SYMROOT)/../libstuff -lstuff

ifeq "macos" "$(RC_OS)"
  SYSTEMDIR := $(shell if [ "$(RC_RELEASE)" = "Beaker" ] || \
			  [ "$(RC_RELEASE)" = "Bunsen" ] || \
			  [ "$(RC_RELEASE)" = "Gonzo" ]; then \
	 echo /Local/Developer/System; else echo /usr/local/lib/system; fi; )
else
  SYSTEMDIR = /Local/Developer/System
endif
ifeq "macos" "$(RC_OS)"
  LIBS := $(shell if [ "$(RC_RELEASE)" = "Beaker"    ] || \
		     [ "$(RC_RELEASE)" = "Bunsen"    ] || \
		     [ "$(RC_RELEASE)" = "Gonzo"     ] || \
		     [ "$(RC_RELEASE)" = "Kodiak"    ] || \
		     [ "$(RC_RELEASE)" = "Cheetah"   ] || \
		     [ "$(RC_RELEASE)" = "Puma"      ] || \
		     [ "$(RC_RELEASE)" = "Jaguar"    ] || \
		     [ "$(RC_RELEASE)" = "Panther"   ] || \
		     [ "$(RC_RELEASE)" = "MuonPrime" ] || \
		     [ "$(RC_RELEASE)" = "MuonSeed"  ] || \
		     [ "$(RC_RELEASE)" = "SUPanWheat" ]; then \
		    echo "-L$(NEXT_ROOT)$(SYSTEMDIR) -lc_static" ; \
	    else if [ "$(RC_RELEASE)" = "Tiger" ]; then \
		    echo "-L$(NEXT_ROOT)$(SYSTEMDIR) -lc_static -lm_static" ; \
	    else \
		    echo "" ; \
	  fi; fi; )
else
  LIBS = -lc_static
endif

ifneq "" "$(wildcard /bin/mkdirs)"
  MKDIRS = /bin/mkdirs
else
  MKDIRS = /bin/mkdir -p
endif

SRCROOT = .
SYMROOT = .
OBJROOT = .
OFILE_DIR = $(OBJROOT)
VPATH = $(OFILE_DIR)

BINDIR = /bin
USRBINDIR = /usr/bin

CFILES = main.c ofile_print.c m68k_disasm.c i860_disasm.c \
	 m88k_disasm.c i386_disasm.c ppc_disasm.c hppa_disasm.c \
	 sparc_disasm.c print_objc.c notify.c arm_disasm.c \
	 print_objc2_64bit.c print_objc2_32bit.c
ifeq "$(EFITOOLS)" "efitools"
	CFILES += coff_print.c
endif
HFILES = otool.h ofile_print.h m68k_disasm.h i860_disasm.h \
	 sparc_disasm.h m88k_disasm.h i386_disasm.h ppc_disasm.h hppa_disasm.h \
	 notify.h arm_disasm.h
OBJS = $(CFILES:.c=.o) ofile.o
INSTALL_FILES = $(CFILES) $(HFILES) Makefile notes

PRODUCT = otool
all: $(OFILE_DIR) $(SYMROOT) $(PRODUCT).NEW

$(PRODUCT).NEW:	$(OFILE_DIR) $(SYMROOT) $(OBJS)
	$(CC) $(RC_CFLAGS) -nostdlib -r -o $(OBJROOT)/private.o \
		$(OBJS) $(LIBSTUFF)
	$(CXX) $(RC_CFLAGS) $(SDK) -o $(SYMROOT)/$@ $(OBJROOT)/private.o \
		$(LIBSTUFF) $(LIBS)

ifeq "NO" "$(USE_DEPENDENCY_FILE)"
.c.o:
	$(CC) $(CFLAGS) $(RC_CFLAGS) -c -o $(OFILE_DIR)/$*.o $<
else
.c.o:
	$(CC) $(CFLAGS) $(RC_CFLAGS) -c -MD -dependency-file $(OFILE_DIR)/$*.d \
		-o $(OFILE_DIR)/$*.o $<
	md -d -u $(OFILE_DIR)/Makedep $(OFILE_DIR)/$*.d
endif

ofile.o: $(SRCROOT)/../libstuff/ofile.c
ifeq "NO" "$(USE_DEPENDENCY_FILE)"
	$(CC) $(CFLAGS) $(RC_CFLAGS) -c -o $(OFILE_DIR)/$*.o \
		$(SRCROOT)/../libstuff/ofile.c -DOTOOL
else
	$(CC) $(CFLAGS) $(RC_CFLAGS) -c -MD -dependency-file $(OFILE_DIR)/$*.d \
		-o $(OFILE_DIR)/$*.o $(SRCROOT)/../libstuff/ofile.c -DOTOOL
	md -d -u $(OFILE_DIR)/Makedep $(OFILE_DIR)/$*.d
endif

clean: shlib_clean
	-rm -f $(SYMROOT)/$(PRODUCT).NEW

shlib_clean:
	-cd $(OFILE_DIR); rm -f $(OBJS) private.o
	-rm -f $(OFILE_DIR)/Makedep make.out

install: all $(RC_OS)_install

teflon_install macos_install:
	$(MKDIRS) $(DSTROOT)$(USRBINDIR)
	install -c -s -m 555 $(SYMROOT)/$(PRODUCT).NEW \
		$(DSTROOT)$(USRBINDIR)/$(PRODUCT)

nextstep_install:
	$(MKDIRS) $(DSTROOT)$(BINDIR)
	install -c -s -m 555 $(SYMROOT)/$(PRODUCT).NEW \
		$(DSTROOT)$(BINDIR)/$(PRODUCT)

installsrc:
	$(MKDIRS) $(SRCROOT)
	chmod 755 $(SRCROOT)
	gnutar cf - $(INSTALL_FILES) | (cd $(SRCROOT); gnutar xf -)
	chmod 444 $(SRCROOT)/*

$(OFILE_DIR) $(SYMROOT):
	$(MKDIRS) $@

-include $(OFILE_DIR)/Makedep
