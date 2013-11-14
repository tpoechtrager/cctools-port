RC_OS = macos
export USE_APPLE_PB_SUPPORT = all
OFLAG = -Os
X_CFLAGS =

ifneq "clang" "$(notdir $(CC))"
  GCC_FLAGS = -Wall $(X_CFLAGS)
endif

USE_DEPENDENCY_FILE := $(shell if [ "$(notdir $(CC))" != "clang" ] && \
				  [ "$(RC_XBS)" != "YES" ]; then \
	     echo YES ; else echo NO ; \
	     fi; )

ifneq "" "$(SDKROOT)"
  SDK = -isysroot $(SDKROOT)
  CC = $(shell xcrun -find -sdk $(SDKROOT) cc)
endif
CFLAGS = $(OFLAG) -g -I$(SRCROOT) -I$(SRCROOT)/../include -I$(OFILE_DIR) \
	$(GCC_FLAGS) -DNeXT_MOD $(SDK)

LIBSTUFF_DIR = -L$(SYMROOT)/../../libstuff
LIBSTUFF = -lstuff

ifneq "" "$(wildcard /bin/mkdirs)"
  MKDIRS = /bin/mkdirs
else
  MKDIRS = /bin/mkdir -p
endif

SRCROOT = .
SYMROOT = .
OBJROOT = .
SYM_DIRS = $(SYMROOT)/driver_dir \
	   $(SYMROOT)/a68_dir \
	   $(SYMROOT)/a88_dir \
	   $(SYMROOT)/a386_dir \
 	   $(SYMROOT)/ax86_64_dir \
	   $(SYMROOT)/appc_dir \
	   $(SYMROOT)/appc64_dir \
	   $(SYMROOT)/a860_dir \
	   $(SYMROOT)/ahppa_dir \
	   $(SYMROOT)/asparc_dir \
	   $(SYMROOT)/aarm_dir 

OFILE_DIRS = $(OBJROOT)/driver_dir \
	     $(OBJROOT)/a68_dir \
	     $(OBJROOT)/a88_dir \
	     $(OBJROOT)/a386_dir \
 	     $(OBJROOT)/ax86_64_dir \
	     $(OBJROOT)/appc_dir \
	     $(OBJROOT)/appc64_dir \
	     $(OBJROOT)/a860_dir \
	     $(OBJROOT)/ahppa_dir \
	     $(OBJROOT)/asparc_dir \
	     $(OBJROOT)/aarm_dir

BINDIR = /bin
USRBINDIR = /usr/bin
ifeq "macos" "$(RC_OS)"
  LIBDIR = /usr/libexec/as
  LOCLIBDIR = /usr/local/libexec/as
else
  LIBDIR = /usr/libexec
  LOCLIBDIR = /usr/local/libexec
endif

DWARF = dwarf2dbg.c
CFILES = app.c as.c atof-generic.c atof-ieee.c expr.c fixes.c flonum-const.c \
	 flonum-copy.c flonum-mult.c frags.c hash.c hex-value.c input-file.c \
	 input-scrub.c layout.c messages.c obstack.c read.c sections.c \
	 symbols.c write_object.c xmalloc.c $(DWARF)

CFILES_m68k = m68k.c
CFILES_m88k = m88k.c
CFILES_i860 = i860.c
CFILES_i386 = i386.c
CFILES_ppc = ppc.c
CFILES_hppa = hppa.c hppa-aux.c
CFILES_sparc = sparc.c
CFILES_arm = arm.c

HFILES = app.h as.h atof-ieee.h bignum.h expr.h fixes.h flonum.h frags.h \
	 hash.h hex_value.h i386-opcode.h i386.h i860-opcode.h input-file.h \
	 input-scrub.h layout.h m68k-opcode.h m88k-opcode.h ppc-opcode.h md.h \
	 messages.h obstack.h read.h relax.h sections.h struc-symbol.h \
	 symbols.h write_object.h xmalloc.h hppa-aux.h hppa-opcode.h \
	 sparc-opcode.h arch64_32.h dwarf2dbg.h filenames.h arm_reloc.h

CHECK_FILES = m68k-check.c m88k-check.c i860-check.c i386-check.c ppc-check.c \
	      hppa-check.c sparc-check.c arm-check.c

INSTALL_FILES = Makefile notes $(CFILES) $(CFILES_m68k) $(CFILES_m88k) \
		$(CFILES_i860) $(CFILES_i386) $(CFILES_ppc) $(CFILES_hppa) \
		$(CFILES_sparc) $(CFILES_arm) $(HFILES) $(CHECK_FILES) \
		driver.c COPYING cctools.plist

OBJS = $(CFILES:.c=.o)
OBJS_m68k = $(CFILES_m68k:.c=.o)
OBJS_m88k = $(CFILES_m88k:.c=.o)
OBJS_i860 = $(CFILES_i860:.c=.o)
OBJS_i386 = $(CFILES_i386:.c=.o)
OBJS_ppc = $(CFILES_ppc:.c=.o)
OBJS_hppa = $(CFILES_hppa:.c=.o)
OBJS_sparc = $(CFILES_sparc:.c=.o)
OBJS_arm = $(CFILES_arm:.c=.o)

all:	$(OFILE_DIRS) $(SYM_DIRS) driver_build a68_build a88_build a386_build \
	ax86_64_build a860_build appc_build appc64_build ahppa_build \
	aarm_build asparc_build

all_test:	a68_test a88_test a860_test a386_test appc_test ahppa_test

driver_build:	$(OBJROOT)/driver_dir
	@if [ $(SRCROOT) = . ];						\
	then								\
	    cd driver_dir;  $(MAKE) -f ../Makefile driver VPATH=..	\
		CFLAGS="-g -I../../include $(OFLAG) $(GCC_FLAGS) $(SDK)" \
		VERS_STRING_FLAGS="$(VERS_STRING_FLAGS)" OFILE_DIR=.	\
		RC_FLAGS="$(RC_CFLAGS)" DEP_DIR=driver_dir;		\
	else								\
	    cd $(OBJROOT)/driver_dir; $(MAKE) -f $(SRCROOT)/Makefile	\
		driver VPATH=$(SRCROOT) OFILE_DIR=$(OBJROOT)/driver_dir \
		SYMROOT=$(SYMROOT)/driver_dir SRCROOT=$(SRCROOT)	\
		CFLAGS="-g -I$(SRCROOT)/../include $(OFLAG) $(GCC_FLAGS) $(SDK)" \
		VERS_STRING_FLAGS="$(VERS_STRING_FLAGS)"		\
		RC_FLAGS="$(RC_CFLAGS)" DEP_DIR=driver_dir;		\
	fi

a68_build:	$(OBJROOT)/a68_dir
	@if [ $(SRCROOT) = . ];						\
	then								\
	    cd a68_dir; $(MAKE) -f ../Makefile as VPATH=.. OFILE_DIR=.	\
		SRCROOT=.. TARGET_OBJS="$(OBJS_m68k)" DEP_DIR=a68_dir	\
		VERS_STRING_FLAGS="$(VERS_STRING_FLAGS)" OFLAG=$(OFLAG) \
		RC_CFLAGS="$(RC_CFLAGS)"				\
		COPTS="-DM68K -DBUILTIN_MMUS";				\
	else								\
	    cd $(OBJROOT)/a68_dir; $(MAKE) -f $(SRCROOT)/Makefile as	\
		VPATH=$(SRCROOT) OFILE_DIR=$(OBJROOT)/a68_dir		\
		SYMROOT=$(SYMROOT)/a68_dir SRCROOT=$(SRCROOT)		\
		TARGET_OBJS="$(OBJS_m68k)"				\
		COPTS="-DM68K -DBUILTIN_MMUS"				\
		VERS_STRING_FLAGS="$(VERS_STRING_FLAGS)"		\
		RC_CFLAGS="$(RC_CFLAGS)" DEP_DIR=a68_dir;		\
	fi

a68_test: a68_dir
	cd a68_dir; $(MAKE) -f ../Makefile m68k-check VPATH=.. OFILE_DIR=. \
		SRCROOT=.. COPTS="-DM68K -DBUILTIN_MMUS"
	cd a68_dir; $(MAKE) -f ../Makefile test

a88_build:	$(OBJROOT)/a88_dir
	@if [ $(SRCROOT) = . ];						\
	then								\
	    cd a88_dir; $(MAKE) -f ../Makefile as VPATH=.. OFILE_DIR=.	\
		SRCROOT=.. TARGET_OBJS="$(OBJS_m88k)" DEP_DIR=a88_dir	\
		VERS_STRING_FLAGS="$(VERS_STRING_FLAGS)" OFLAG=$(OFLAG)	\
		COPTS="-DM88K -Dm88110" RCFLAGS=$(RCFLAGS);		\
	else								\
	    cd $(OBJROOT)/a88_dir; $(MAKE) -f $(SRCROOT)/Makefile as	\
		VPATH=$(SRCROOT) OFILE_DIR=$(OBJROOT)/a88_dir		\
		SYMROOT=$(SYMROOT)/a88_dir SRCROOT=$(SRCROOT)		\
		TARGET_OBJS="$(OBJS_m88k)" COPTS="-DM88K -Dm88110"	\
		VERS_STRING_FLAGS="$(VERS_STRING_FLAGS)"		\
		RC_CFLAGS="$(RC_CFLAGS)" DEP_DIR=a88_dir;		\
	fi

a88_test: a88_dir
	cd a88_dir; $(MAKE) -f ../Makefile m88k-check VPATH=.. OFILE_DIR=. \
		SRCROOT=.. COPTS="-DM88K -Dm88110"
	cd a88_dir; $(MAKE) -f ../Makefile test

a386_build:	$(OBJROOT)/a386_dir
	@if [ $(SRCROOT) = . ];						\
	then								\
	    cd a386_dir; $(MAKE) -f ../Makefile as VPATH=.. OFILE_DIR=.	\
		SRCROOT=.. TARGET_OBJS="$(OBJS_i386)" DEP_DIR=a386_dir	\
		VERS_STRING_FLAGS="$(VERS_STRING_FLAGS)" OFLAG=$(OFLAG)	\
		COPTS="-DI386 -Di486 -Di586 -Di686"			\
		RC_CFLAGS="$(RC_CFLAGS)";				\
	else								\
	    cd $(OBJROOT)/a386_dir; $(MAKE) -f $(SRCROOT)/Makefile as	\
		VPATH=$(SRCROOT) OFILE_DIR=$(OBJROOT)/a386_dir		\
		SYMROOT=$(SYMROOT)/a386_dir SRCROOT=$(SRCROOT)		\
		TARGET_OBJS="$(OBJS_i386)"				\
		COPTS="-DI386 -Di486 -Di586 -Di686"			\
		VERS_STRING_FLAGS="$(VERS_STRING_FLAGS)"		\
		RC_CFLAGS="$(RC_CFLAGS)"  DEP_DIR=a386_dir;		\
	fi

a386_test: a386_dir
	cd a386_dir; $(MAKE) -f ../Makefile i386-check VPATH=.. OFILE_DIR=. \
		SRCROOT=.. COPTS="-DI386 -Di486 -Di586 -Di686"
	cd a386_dir; $(MAKE) -f ../Makefile test


ax86_64_build:	$(OBJROOT)/ax86_64_dir
	@if [ $(SRCROOT) = . ];						\
 	then								\
 	    cd ax86_64_dir; $(MAKE) -f ../Makefile as VPATH=.. OFILE_DIR=. \
 		SRCROOT=.. TARGET_OBJS="$(OBJS_i386)" DEP_DIR=ax86_64_dir \
 		VERS_STRING_FLAGS="$(VERS_STRING_FLAGS)" OFLAG=$(OFLAG)	\
 		COPTS="-DI386 -Di486 -Di586 -Di686 -DARCH64"		\
 		RC_CFLAGS="$(RC_CFLAGS)" LIBSTUFF="-lstuff";		\
 	else								\
 	    cd $(OBJROOT)/ax86_64_dir; $(MAKE) -f $(SRCROOT)/Makefile as \
 		VPATH=$(SRCROOT) OFILE_DIR=$(OBJROOT)/ax86_64_dir	\
 		SYMROOT=$(SYMROOT)/ax86_64_dir SRCROOT=$(SRCROOT)	\
 		TARGET_OBJS="$(OBJS_i386)" LIBSTUFF="-lstuff"		\
 		COPTS="-DI386 -Di486 -Di586 -Di686 -DARCH64"		\
 		VERS_STRING_FLAGS="$(VERS_STRING_FLAGS)"		\
 		RC_CFLAGS="$(RC_CFLAGS)"  DEP_DIR=ax86_64_dir;		\
 	fi
 
ax86_64_test: ax86_64_dir
	cd ax86_64_dir; $(MAKE) -f ../Makefile x86_64-check VPATH=.. \
	    OFILE_DIR=. SRCROOT=.. COPTS="-DI386 -Di486 -Di586 -Di686 -DARCH64"
	cd ax86_64_dir; $(MAKE) -f ../Makefile test

a860_build:	$(OBJROOT)/a860_dir
	@if [ $(SRCROOT) = . ];						\
	then								\
	    cd a860_dir; $(MAKE) -f ../Makefile as VPATH=.. OFILE_DIR=.	\
		SRCROOT=.. TARGET_OBJS="$(OBJS_i860)" DEP_DIR=a860_dir	\
		VERS_STRING_FLAGS="$(VERS_STRING_FLAGS)" OFLAG=$(OFLAG)	\
		RC_CFLAGS="$(RC_CFLAGS)" COPTS="-DI860";		\
	else								\
	    cd $(OBJROOT)/a860_dir; $(MAKE) -f $(SRCROOT)/Makefile as	\
		VPATH=$(SRCROOT) OFILE_DIR=$(OBJROOT)/a860_dir		\
		SYMROOT=$(SYMROOT)/a860_dir SRCROOT=$(SRCROOT)		\
		TARGET_OBJS="$(OBJS_i860)" COPTS="-DI860"		\
		VERS_STRING_FLAGS="$(VERS_STRING_FLAGS)"		\
		RC_CFLAGS="$(RC_CFLAGS)"  DEP_DIR=a860_dir;		\
	fi

a860_test: a860_dir
	cd a860_dir; $(MAKE) -f ../Makefile i860-check VPATH=.. OFILE_DIR=. \
		SRCROOT=.. COPTS="-DI860"
	cd a860_dir; $(MAKE) -f ../Makefile test ASFLAGS=-W

appc_build:	$(OBJROOT)/appc_dir
	@if [ $(SRCROOT) = . ];						\
	then								\
	    cd appc_dir; $(MAKE) -f ../Makefile as VPATH=.. OFILE_DIR=.	\
		SRCROOT=.. TARGET_OBJS="$(OBJS_ppc)" DEP_DIR=appc_dir	\
		VERS_STRING_FLAGS="$(VERS_STRING_FLAGS)" OFLAG=$(OFLAG) \
		RC_CFLAGS="$(RC_CFLAGS)" COPTS="-DPPC";			\
	else								\
	    cd $(OBJROOT)/appc_dir; $(MAKE) -f $(SRCROOT)/Makefile as	\
		VPATH=$(SRCROOT) OFILE_DIR=$(OBJROOT)/appc_dir		\
		SYMROOT=$(SYMROOT)/appc_dir SRCROOT=$(SRCROOT)		\
		TARGET_OBJS="$(OBJS_ppc)" COPTS="-DPPC"			\
		VERS_STRING_FLAGS="$(VERS_STRING_FLAGS)"		\
		RC_CFLAGS="$(RC_CFLAGS)" DEP_DIR=appc_dir;		\
	fi

appc64_build:	$(OBJROOT)/appc64_dir
	@if [ $(SRCROOT) = . ];						\
	then								\
	    cd appc64_dir; $(MAKE) -f ../Makefile as VPATH=.. OFILE_DIR=. \
		SRCROOT=.. TARGET_OBJS="$(OBJS_ppc)" DEP_DIR=appc64_dir	\
		VERS_STRING_FLAGS="$(VERS_STRING_FLAGS)"		\
		RC_CFLAGS="$(RC_CFLAGS)" OFLAG=$(OFLAG)			\
		COPTS="-DPPC -DARCH64" LIBSTUFF="-lstuff";		\
	else								\
	    cd $(OBJROOT)/appc64_dir; $(MAKE) -f $(SRCROOT)/Makefile as	\
		VPATH=$(SRCROOT) OFILE_DIR=$(OBJROOT)/appc64_dir	\
		SYMROOT=$(SYMROOT)/appc64_dir SRCROOT=$(SRCROOT)	\
		TARGET_OBJS="$(OBJS_ppc)" COPTS="-DPPC -DARCH64"	\
		VERS_STRING_FLAGS="$(VERS_STRING_FLAGS)"		\
		RC_CFLAGS="$(RC_CFLAGS)" DEP_DIR=appc64_dir		\
		LIBSTUFF="-lstuff";					\
	fi

appc_test: appc_dir
	cd appc_dir; $(MAKE) -f ../Makefile ppc-check VPATH=.. OFILE_DIR=. \
		SRCROOT=.. COPTS="-DPPC"
	cd appc_dir; $(MAKE) -f ../Makefile test

ahppa_build:	$(OBJROOT)/ahppa_dir
	@if [ $(SRCROOT) = . ];						\
	then								\
	    cd ahppa_dir; $(MAKE) -f ../Makefile as VPATH=.. OFILE_DIR=.\
		SRCROOT=.. TARGET_OBJS="$(OBJS_hppa)" DEP_DIR=ahppa_dir	\
		VERS_STRING_FLAGS="$(VERS_STRING_FLAGS)" OFLAG=$(OFLAG)	\
		RC_CFLAGS="$(RC_CFLAGS)" COPTS="-DHPPA";		\
	else								\
	    cd $(OBJROOT)/ahppa_dir; $(MAKE) -f $(SRCROOT)/Makefile as	\
		VPATH=$(SRCROOT) OFILE_DIR=$(OBJROOT)/ahppa_dir		\
		SYMROOT=$(SYMROOT)/ahppa_dir SRCROOT=$(SRCROOT)		\
		TARGET_OBJS="$(OBJS_hppa)" COPTS="-DHPPA"		\
		VERS_STRING_FLAGS="$(VERS_STRING_FLAGS)"		\
		RC_CFLAGS="$(RC_CFLAGS)" DEP_DIR=ahppa_dir;		\
	fi

ahppa_test: ahppa_dir
	cd ahppa_dir; $(MAKE) -f ../Makefile hppa-check VPATH=.. OFILE_DIR=. \
		SRCROOT=.. COPTS="-DHPPA"
	cd ahppa_dir; $(MAKE) -f ../Makefile test

asparc_build:	$(OBJROOT)/asparc_dir
	@if [ $(SRCROOT) = . ];						\
	then								\
	    cd asparc_dir; $(MAKE) -f ../Makefile as VPATH=.. OFILE_DIR=.\
		SRCROOT=.. TARGET_OBJS="$(OBJS_sparc)" DEP_DIR=asparc_dir\
		VERS_STRING_FLAGS="$(VERS_STRING_FLAGS)" OFLAG=$(OFLAG)	\
		RC_CFLAGS="$(RC_CFLAGS)" COPTS="-DSPARC";		\
	else								\
	    cd $(OBJROOT)/asparc_dir; $(MAKE) -f $(SRCROOT)/Makefile as	\
		VPATH=$(SRCROOT) OFILE_DIR=$(OBJROOT)/asparc_dir	\
		SYMROOT=$(SYMROOT)/asparc_dir SRCROOT=$(SRCROOT)	\
		TARGET_OBJS="$(OBJS_sparc)" COPTS="-DSPARC"		\
		VERS_STRING_FLAGS="$(VERS_STRING_FLAGS)"		\
		RC_CFLAGS="$(RC_CFLAGS)" DEP_DIR=asparc_dir;		\
	fi

asparc_test: asparc_dir
	cd asparc_dir; $(MAKE) -f ../Makefile sparc-check VPATH=.. \
		OFILE_DIR=. SRCROOT=.. COPTS="-DSPARC"
	cd asparc_dir; $(MAKE) -f ../Makefile test

aarm_build:	$(OBJROOT)/aarm_dir
	@if [ $(SRCROOT) = . ];						\
	then								\
	    cd aarm_dir; $(MAKE) -f ../Makefile as VPATH=.. OFILE_DIR=. \
		SRCROOT=.. TARGET_OBJS="$(OBJS_arm)" DEP_DIR=aarm_dir	\
		VERS_STRING_FLAGS="$(VERS_STRING_FLAGS)" OFLAG=$(OFLAG)	\
		RC_CFLAGS="$(RC_CFLAGS)" COPTS="-DARM";			\
	else								\
	    cd $(OBJROOT)/aarm_dir; $(MAKE) -f $(SRCROOT)/Makefile as	\
		VPATH=$(SRCROOT) OFILE_DIR=$(OBJROOT)/aarm_dir		\
		SYMROOT=$(SYMROOT)/aarm_dir SRCROOT=$(SRCROOT)		\
		TARGET_OBJS="$(OBJS_arm)" COPTS="-DARM"			\
		VERS_STRING_FLAGS="$(VERS_STRING_FLAGS)"		\
		RC_CFLAGS="$(RC_CFLAGS)" DEP_DIR=aarm_dir;		\
	fi

aarm_test: aarm_dir
	cd aarm_dir; $(MAKE) -f ../Makefile arm-check VPATH=.. \
		OFILE_DIR=. SRCROOT=.. COPTS="-DARM"
	cd aarm_dir; $(MAKE) -f ../Makefile test

driver:	driver.o
	$(CC) $(RC_CFLAGS) $(SDK) -o $(SYMROOT)/driver \
		$(OFILE_DIR)/driver.o $(LIBSTUFF_DIR) $(LIBSTUFF)

as:	$(OBJS) $(TARGET_OBJS)
	$(CC) $(RC_CFLAGS) -nostdlib -r \
		-o $(SYMROOT)/as.private.o $(OBJS) $(TARGET_OBJS) \
		$(LIBSTUFF_DIR) $(LIBSTUFF)
	$(CC) $(RC_CFLAGS) $(SDK) -o $(SYMROOT)/as $(SYMROOT)/as.private.o

m68k-check:
	$(CC) $(COPTS) $(CFLAGS) -o check ../m68k-check.c

m88k-check:
	$(CC) $(COPTS) $(CFLAGS) -o check ../m88k-check.c

i386-check:
	$(CC) $(COPTS) $(CFLAGS) -o check ../i386-check.c

x86_64-check:
	$(CC) $(COPTS) $(CFLAGS) -DARCH64 -o check ../i386-check.c

i860-check:
	$(CC) $(COPTS) $(CFLAGS) -o check ../i860-check.c

ppc-check:
	$(CC) $(COPTS) $(CFLAGS) -o check ../ppc-check.c

hppa-check:
	$(CC) $(COPTS) $(CFLAGS) -o check ../hppa-check.c

sparc-check:
	$(CC) $(COPTS) $(CFLAGS) -o check ../sparc-check.c

arm-check:
	$(CC) $(COPTS) $(CFLAGS) -o check ../arm-check.c

test:
	./check $(LLVM_MC) > check.s
	./as -W -force_cpusubtype_ALL $(ASFLAGS) -o check.o check.s
	otool -X -tV $(LLVM_MC) check.o > check1.s
	./as -force_cpusubtype_ALL $(ASFLAGS) -o check1.o check1.s
	otool -X -tV $(LLVM_MC) check1.o > check2.s
	diff check1.s check2.s > check.out
	cmp check.o check1.o

ifeq "NO" "$(USE_DEPENDENCY_FILE)"
.c.o:
	$(CC) $(COPTS) $(CFLAGS) $(RC_CFLAGS) -c -o $(OFILE_DIR)/$*.o $<
else
.c.o:
	$(CC) $(COPTS) $(CFLAGS) $(RC_CFLAGS) -c -MD \
		-dependency-file $(OFILE_DIR)/$*.d -o $(OFILE_DIR)/$*.o $<
	md -d -u $(OFILE_DIR)/Makedep $(OFILE_DIR)/$*.d
endif

clean shlib_clean:
	-rm -r -f $(OFILE_DIRS) make.out

installsrc installGASsrc:
	$(MKDIRS) $(SRCROOT)
	chmod 755 $(SRCROOT)
	gnutar cf - $(INSTALL_FILES) | (cd $(SRCROOT); gnutar xf -)
	chmod 444 $(SRCROOT)/*

install: all $(RC_OS)_install

macos_install: common_install xcommon_install
	$(MKDIRS) $(DSTROOT)$(LIBDIR)/i386
	install -c -s -m 555 $(SYMROOT)/a386_dir/as \
		$(DSTROOT)$(LIBDIR)/i386/as
	$(MKDIRS) $(DSTROOT)$(LIBDIR)/x86_64
	install -c -s -m 555 $(SYMROOT)/ax86_64_dir/as \
		$(DSTROOT)$(LIBDIR)/x86_64/as

teflon_install: common_install xcommon_install
	$(MKDIRS) $(DSTROOT)$(LIBDIR)/i386
	install -c -s -m 555 $(SYMROOT)/a386_dir/as \
		$(DSTROOT)$(LIBDIR)/i386/as

xcommon_install:
	$(MKDIRS) $(DSTROOT)$(USRBINDIR)
	install -c -s -m 555 $(SYMROOT)/driver_dir/driver \
		$(DSTROOT)$(USRBINDIR)/as	
	$(MKDIRS) $(DSTROOT)/usr/local/OpenSourceVersions/
	install -c -s -m 444 $(SRCROOT)/cctools.plist \
		$(DSTROOT)/usr/local/OpenSourceVersions/cctools.plist
	$(MKDIRS) $(DSTROOT)/usr/local/OpenSourceLicenses/
	install -c -s -m 444 $(SRCROOT)/COPYING \
		$(DSTROOT)/usr/local/OpenSourceLicenses/cctools.txt
	$(MKDIRS) $(DSTROOT)$(LOCLIBDIR)/ppc
	install -c -s -m 555 $(SYMROOT)/appc_dir/as \
		$(DSTROOT)$(LOCLIBDIR)/ppc/as
	$(MKDIRS) $(DSTROOT)$(LOCLIBDIR)/ppc64
	install -c -s -m 555 $(SYMROOT)/appc64_dir/as \
		$(DSTROOT)$(LOCLIBDIR)/ppc64/as
	$(MKDIRS) $(DSTROOT)$(LOCLIBDIR)/m68k
	install -c -s -m 555 $(SYMROOT)/a68_dir/as \
		$(DSTROOT)$(LOCLIBDIR)/m68k/as
	$(MKDIRS) $(DSTROOT)$(LOCLIBDIR)/sparc 
	install -c -s -m 555 $(SYMROOT)/asparc_dir/as \
		$(DSTROOT)$(LOCLIBDIR)/sparc/as
	$(MKDIRS) $(DSTROOT)$(LIBDIR)/arm
	install -c -s -m 555 $(SYMROOT)/aarm_dir/as \
		$(DSTROOT)$(LIBDIR)/arm/as

nextstep_install: common_install
	$(MKDIRS) $(DSTROOT)$(BINDIR)
	install -c -s -m 555 $(SYMROOT)/driver_dir/driver \
		$(DSTROOT)$(BINDIR)/as	
	$(MKDIRS) $(DSTROOT)$(LIBDIR)/m68k
	install -c -s -m 555 $(SYMROOT)/a68_dir/as \
		$(DSTROOT)$(LIBDIR)/m68k/as
	$(MKDIRS) $(DSTROOT)$(LIBDIR)/i386
	install -c -s -m 555 $(SYMROOT)/a386_dir/as \
		$(DSTROOT)$(LIBDIR)/i386/as
	$(MKDIRS) $(DSTROOT)$(LIBDIR)/sparc
	install -c -s -m 555 $(SYMROOT)/asparc_dir/as \
		$(DSTROOT)$(LIBDIR)/sparc/as
	$(MKDIRS) $(DSTROOT)$(LOCLIBDIR)/ppc
	install -c -s -m 555 $(SYMROOT)/appc_dir/as \
		$(DSTROOT)$(LOCLIBDIR)/ppc/as

common_install:
	$(MKDIRS) $(DSTROOT)$(LOCLIBDIR)/m88k
	install -c -s -m 555 $(SYMROOT)/a88_dir/as \
		$(DSTROOT)$(LOCLIBDIR)/m88k/as
	$(MKDIRS) $(DSTROOT)$(LOCLIBDIR)/hppa
	install -c -s -m 555 $(SYMROOT)/ahppa_dir/as \
		$(DSTROOT)$(LOCLIBDIR)/hppa/as
	$(MKDIRS) $(DSTROOT)$(LOCLIBDIR)/i860
	install -s -m 555 $(SYMROOT)/a860_dir/as \
		$(DSTROOT)$(LOCLIBDIR)/i860/as

$(OFILE_DIRS) $(SYM_DIRS):
	$(MKDIRS) $@

-include $(DEP_DIR)/Makedep
