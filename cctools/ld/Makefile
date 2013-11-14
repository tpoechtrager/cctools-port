export USE_APPLE_PB_SUPPORT = all
RC_OS = macos
OFLAG = -Os
ifneq "$(RC_MAJOR_RELEASE_TRAIN)" "Tiger"
KERN = -fno-builtin -finline -fno-keep-inline-functions -fsigned-bitfields \
       -msoft-float -nostdinc \
       -iwithsysroot /System/Library/Frameworks/Kernel.framework/PrivateHeaders/libsa \
       -iwithsysroot /System/Library/Frameworks/Kernel.framework/PrivateHeaders \
       -iwithsysroot /System/Library/Frameworks/Kernel.framework/Headers/libsa \
       -iwithsysroot /System/Library/Frameworks/Kernel.framework/Headers/bsd \
       -iwithsysroot /System/Library/Frameworks/Kernel.framework/Headers \
       -DKERNEL -DKERNEL_PRIVATE -DAPPLE -DNeXT -DLIBSA_PRIVATE
else
KERN = -fno-builtin -finline -fno-keep-inline-functions -fsigned-bitfields \
       -msoft-float -nostdinc \
       -I$(NEXT_ROOT)/System/Library/Frameworks/Kernel.framework/PrivateHeaders/libsa \
       -I$(NEXT_ROOT)/System/Library/Frameworks/Kernel.framework/PrivateHeaders \
       -I$(NEXT_ROOT)/System/Library/Frameworks/Kernel.framework/Headers/libsa \
       -I$(NEXT_ROOT)/System/Library/Frameworks/Kernel.framework/Headers/bsd \
       -I$(NEXT_ROOT)/System/Library/Frameworks/Kernel.framework/Headers \
       -DKERNEL -DKERNEL_PRIVATE -DAPPLE -DNeXT -DLIBSA_PRIVATE
endif

KERN += -D__DARWIN_UNIX03=__arm__

X_CFLAGS =

ifneq "clang" "$(notdir $(CC))"
  GCC_FLAGS = $(X_CFLAGS)
  PG = -pg
endif

USE_DEPENDENCY_FILE := $(shell if [ "$(notdir $(CC))" != "mwccppc" ] && \
				  [ "$(RC_XBS)" != "YES" ]; then \
	     echo YES ; else echo NO ; \
	     fi; )

ifneq "" "$(SDKROOT)"
  SDK = -isysroot $(SDKROOT)
  CC = $(shell xcrun -find -sdk $(SDKROOT) cc)
endif
CFLAGS = -g -I$(SRCROOT) -I$(SRCROOT)/../include -I$(OFILE_DIR) -Wall \
	 $(GCC_FLAGS) $(SDK)

ifeq "macos" "$(RC_OS)"
  SYSTEMDIR := $(shell if [ "$(RC_RELEASE)" = "Beaker" ] ||    \
			  [ "$(RC_RELEASE)" = "Bunsen" ] || \
			  [ "$(RC_RELEASE)" = "Gonzo" ]; then \
	 echo /Local/Developer/System; else echo /usr/local/lib/system; fi; )
else
  SYSTEMDIR = /Local/Developer/System
endif

ifneq "" "$(wildcard /bin/mkdirs)"
  MKDIRS = /bin/mkdirs
else
  MKDIRS = /bin/mkdir -p
endif

SRCROOT = .
SYMROOT = .
OBJROOT = .
OFILE_DIRS = $(OBJROOT)/ld_dir $(OBJROOT)/shlib_obj $(OBJROOT)/profile_obj \
	     $(OBJROOT)/optimized_obj $(OBJROOT)/dynamic_obj \
	     $(OBJROOT)/ptmp_obj $(OBJROOT)/otmp_obj $(OBJROOT)/dtmp_obj \
	     $(OBJROOT)/sarld_dir $(OBJROOT)/dynamic_kld $(OBJROOT)/static_kld

BINDIR = /bin
USRBINDIR = /usr/bin
LOCLIBDIR = /usr/local/lib

ifeq "macos" "$(RC_OS)"
  SYSTEMDIR := $(shell if [ "$(RC_RELEASE)" = "Beaker" ] || \
			  [ "$(RC_RELEASE)" = "Bunsen" ] || \
			  [ "$(RC_RELEASE)" = "Gonzo" ]; then \
	 echo /Local/Developer/System; else echo /usr/local/lib/system; fi; )
else
  SYSTEMDIR = /Local/Developer/System
endif

HFILES = ld.h pass1.h objects.h sections.h cstring_literals.h symbols.h \
	 fvmlibs.h layout.h specs.h pass2.h generic_reloc.h hash_string.h \
	 sets.h 4byte_literals.h 8byte_literals.h literal_pointers.h dylibs.h \
	 indirect_sections.h mod_sections.h i860_reloc.h ppc_reloc.h \
	 m88k_reloc.h hppa_reloc.h sparc_reloc.h coalesced_sections.h \
	 live_refs.h uuid.h debugcompunit.h dwarf2.h debugline.h arm_reloc.h
CFILES = ld.c pass1.c objects.c sections.c cstring_literals.c symbols.c \
	 fvmlibs.c layout.c specs.c pass2.c generic_reloc.c rld.c sets.c \
	 4byte_literals.c 8byte_literals.c literal_pointers.c dylibs.c \
	 indirect_sections.c mod_sections.c i860_reloc.c ppc_reloc.c \
	 m88k_reloc.c hppa_reloc.c sparc_reloc.c coalesced_sections.c uuid.c \
	 debugcompunit.c debugline.c arm_reloc.c
OBJS = $(CFILES:.c=.o)
INSTALL_FILES = $(CFILES) $(HFILES) Makefile notes \
		librld.ofileList

all:	$(OFILE_DIRS) $(SYMROOT) ld_build kld_build lib_ofiles

macos_lib_ofiles:

teflon_lib_ofiles nextstep_lib_ofiles: lib_ofiles


lib_ofiles: $(OFILE_DIRS) $(SYMROOT) sarld_build \
	    $(RC_OS) dynamic_ofiles profile_ofiles

macos:

teflon: optimized_ofiles

nextstep: optimized_ofiles shlib_ofiles


ld_build:	$(OBJROOT)/ld_dir $(SYMROOT)/ld_dir
	@if [ $(SRCROOT) = . ];						\
	then								\
	    cd ld_dir; $(MAKE) -f ../Makefile ld.NEW VPATH=..		\
		OFILE_DIR=. SRCROOT=.. RC_CFLAGS="$(RC_CFLAGS)"		\
		LIBSTUFF="-L../../libstuff -lstuff"			\
		VERS_STRING_FLAGS="$(VERS_STRING_FLAGS)"		\
		MAKEINCDIR="`pwd`" COPTS="-DDEBUG" CC="$(CC)";		\
	else								\
	    cd $(OBJROOT)/ld_dir; $(MAKE) -f $(SRCROOT)/Makefile ld.NEW	\
		VPATH=$(SRCROOT) OFILE_DIR=$(OBJROOT)/ld_dir		\
		SYMROOT=$(SYMROOT)/ld_dir SRCROOT=$(SRCROOT)		\
		MAKEINCDIR="`pwd`" COPTS="-DDEBUG"			\
		LIBSTUFF="-L$(SYMROOT)/../libstuff -lstuff"		\
		VERS_STRING_FLAGS="$(VERS_STRING_FLAGS)"		\
		RC_CFLAGS="$(RC_CFLAGS)" CC="$(CC)";			\
	fi

ld_g_build: ld_g_dir
	cd ld_g_dir; $(MAKE) -f ../Makefile ld_g.NEW VPATH=.. OFILE_DIR=. \
		SRCROOT=.. MAKEINCDIR="`pwd`" COPTS="-DDEBUG" OFLAG=""	\
		LIBSTUFF="-L../../libstuff -lstuff"

ld_p_build: ld_p_dir
	cd ld_p_dir; $(MAKE) -f ../Makefile ld.NEW VPATH=.. OFILE_DIR=. \
		SRCROOT=.. MAKEINCDIR="`pwd`" COPTS="$(PG) -DDEBUG"	\
		LIBSTUFF="-L../../libstuff -lstuff_p"

shlib_ofiles: $(OBJROOT)/shlib_obj $(SYMROOT)/ld_dir
	@if [ $(SRCROOT) = . ];						\
	then								\
	    (cd shlib_obj; $(MAKE) -f ../Makefile librld.o VPATH=.. 	\
		MAKEINCDIR="`pwd`" OFILE_DIR=. SRCROOT=.. 		\
		COPTS="-static -DRLD -DSHLIB -I/LocalDeveloper/Headers/libsys" \
		LIBSTUFF="-L../../libstuff -lstuff_s"			\
		VERS_STRING_FLAGS="$(VERS_STRING_FLAGS)" CC="$(CC)"	\
		RC_CFLAGS="$(RC_CFLAGS)" RC_ARCHS="$(RC_ARCHS)");	\
	    rm -f shlib_obj/librld.ofileList;				\
	    cp librld.ofileList shlib_obj;				\
	else								\
	    (cd $(OBJROOT)/shlib_obj; $(MAKE) -f $(SRCROOT)/Makefile librld.o \
		VPATH=$(SRCROOT) OFILE_DIR=$(OBJROOT)/shlib_obj		\
		SYMROOT=$(SYMROOT)/ld_dir SRCROOT=$(SRCROOT)		\
		RC_CFLAGS="$(RC_CFLAGS)" CC="$(CC)"			\
		COPTS="-static -DRLD -DSHLIB -I/LocalDeveloper/Headers/libsys" \
		LIBSTUFF="-L$(SYMROOT)/../libstuff -lstuff_s"		\
		VERS_STRING_FLAGS="$(VERS_STRING_FLAGS)"		\
		MAKEINCDIR="`pwd`" RC_ARCHS="$(RC_ARCHS)");		\
	    rm -f $(OBJROOT)/shlib_obj/librld.ofileList;		\
	    cp librld.ofileList $(OBJROOT)/shlib_obj;			\
	fi

profile_ofiles:	$(OBJROOT)/profile_obj $(OBJROOT)/ptmp_obj $(SYMROOT)/ld_dir
	@if [ $(SRCROOT) = . ];						\
	then								\
	    (cd ptmp_obj; $(MAKE) -f ../Makefile librld_pg.a VPATH=.. 	\
		MAKEINCDIR="`pwd`" OFILE_DIR=. SRCROOT=.. 		\
		LIBSTUFF="-L../../libstuff -lstuff_p"			\
		VERS_STRING_FLAGS="$(VERS_STRING_FLAGS)" CC="$(CC)"	\
		RC_CFLAGS="$(RC_CFLAGS)" COPTS="-dynamic -DRLD $(PG)");	\
	    rm -f profile_obj/librld.o;					\
	    rm -f  profile_obj/librld.ofileList;			\
	    cp ptmp_obj/librld.o profile_obj/librld.o;			\
	    cp librld.ofileList profile_obj;				\
	    rm -f dynamic_profile_obj;					\
	    ln -s ./profile_obj dynamic_profile_obj;			\
	else								\
	    (cd $(OBJROOT)/ptmp_obj; $(MAKE) -f $(SRCROOT)/Makefile	\
	        librld_pg.a VPATH=$(SRCROOT) OFILE_DIR=$(OBJROOT)/ptmp_obj \
		SYMROOT=$(SYMROOT)/ld_dir SRCROOT=$(SRCROOT)		\
		RC_CFLAGS="$(RC_CFLAGS)" CC="$(CC)"			\
		LIBSTUFF="-L$(SYMROOT)/../libstuff -lstuff_p"		\
		VERS_STRING_FLAGS="$(VERS_STRING_FLAGS)"		\
		MAKEINCDIR="`pwd`" COPTS="-dynamic -DRLD $(PG)");	\
	    rm -f $(OBJROOT)/profile_obj/librld.o; 			\
	    rm -f $(OBJROOT)/profile_obj/librld.ofileList;		\
	    cp $(OBJROOT)/ptmp_obj/librld.o $(OBJROOT)/profile_obj/librld.o; \
	    cp librld.ofileList $(OBJROOT)/profile_obj;			\
	    rm -f $(OBJROOT)/dynamic_profile_obj;			\
	    ln -s ./profile_obj $(OBJROOT)/dynamic_profile_obj;\
	fi

optimized_ofiles: $(OBJROOT)/optimized_obj $(OBJROOT)/otmp_obj $(SYMROOT)/ld_dir
	@if [ $(SRCROOT) = . ];						\
	then								\
	    (cd otmp_obj; $(MAKE) -f ../Makefile librld_static.a VPATH=.. \
		MAKEINCDIR="`pwd`" OFILE_DIR=. SRCROOT=.. 		\
		LIBSTUFF="-L../../libstuff -lstuff" CC="$(CC)"		\
		VERS_STRING_FLAGS="$(VERS_STRING_FLAGS)"		\
		RC_CFLAGS="$(RC_CFLAGS)" COPTS="-static -DRLD");	\
	    rm -f optimized_obj/librld.o;				\
	    rm -f optimized_obj/librld.ofileList;			\
	    cp otmp_obj/librld.o optimized_obj/librld.o;		\
	    cp librld.ofileList optimized_obj;				\
	    rm -f static_obj;						\
	    ln -s ./optimized_obj static_obj;				\
	else								\
	    (cd $(OBJROOT)/otmp_obj; $(MAKE) -f $(SRCROOT)/Makefile	\
		librld_static.a VPATH=$(SRCROOT)			\
		OFILE_DIR=$(OBJROOT)/otmp_obj				\
		SYMROOT=$(SYMROOT)/ld_dir SRCROOT=$(SRCROOT)		\
		RC_CFLAGS="$(RC_CFLAGS)" CC="$(CC)"			\
		LIBSTUFF="-L$(SYMROOT)/../libstuff -lstuff"		\
		VERS_STRING_FLAGS="$(VERS_STRING_FLAGS)"		\
		MAKEINCDIR="`pwd`" COPTS="-static -DRLD");		\
	    rm -f $(OBJROOT)/optimized_obj/librld.o; 			\
	    rm -f $(OBJROOT)/optimized_obj/librld.ofileList;		\
	    cp $(OBJROOT)/otmp_obj/librld.o $(OBJROOT)/optimized_obj/librld.o;\
	    cp librld.ofileList $(OBJROOT)/optimized_obj;		\
	    rm -f $(OBJROOT)/static_obj;				\
	    ln -s ./optimized_obj $(OBJROOT)/static_obj;	\
	fi

dynamic_ofiles: $(OBJROOT)/dynamic_obj $(OBJROOT)/dtmp_obj $(SYMROOT)/ld_dir
	@if [ $(SRCROOT) = . ];						\
	then								\
	    (cd dtmp_obj; $(MAKE) -f ../Makefile librld.a VPATH=.. 	\
		MAKEINCDIR="`pwd`" OFILE_DIR=. SRCROOT=.. 		\
		LIBSTUFF="-L../../libstuff -lstuff" CC="$(CC)"		\
		VERS_STRING_FLAGS="$(VERS_STRING_FLAGS)"		\
		RC_CFLAGS="$(RC_CFLAGS)" COPTS="-dynamic -DRLD");	\
	    rm -f dynamic_obj/librld.o;					\
	    rm -f dynamic_obj/librld.ofileList;				\
	    cp dtmp_obj/librld.o dynamic_obj/librld.o;			\
	    cp librld.ofileList dynamic_obj;				\
	else								\
	    (cd $(OBJROOT)/dtmp_obj; $(MAKE) -f $(SRCROOT)/Makefile librld.a \
		VPATH=$(SRCROOT) OFILE_DIR=$(OBJROOT)/dtmp_obj		\
		SYMROOT=$(SYMROOT)/ld_dir SRCROOT=$(SRCROOT)		\
		RC_CFLAGS="$(RC_CFLAGS)" CC="$(CC)"			\
		LIBSTUFF="-L$(SYMROOT)/../libstuff -lstuff"		\
		VERS_STRING_FLAGS="$(VERS_STRING_FLAGS)"		\
		MAKEINCDIR="`pwd`" COPTS="-dynamic -DRLD");		\
	    rm -f $(OBJROOT)/dynamic_obj/librld.o; 			\
	    rm -f $(OBJROOT)/dynamic_obj/librld.ofileList;		\
	    cp $(OBJROOT)/dtmp_obj/librld.o $(OBJROOT)/dynamic_obj/librld.o;\
	    cp librld.ofileList $(OBJROOT)/dynamic_obj;			\
	fi

sarld_build: $(OBJROOT)/sarld_dir $(SYMROOT)/ld_dir
	@if [ $(SRCROOT) = . ];						\
	then								\
	    cd sarld_dir; $(MAKE) -f ../Makefile libsarld.a VPATH=..	\
		MAKEINCDIR="`pwd`" OFILE_DIR=. SRCROOT=.. 		\
		COPTS="-static -DRLD -DSA_RLD" CC="$(CC)"		\
		LIBSTUFF="-L../../libstuff -lstuff"			\
		VERS_STRING_FLAGS="$(VERS_STRING_FLAGS)"		\
		RC_CFLAGS="$(RC_CFLAGS)" RC_ARCHS="$(RC_ARCHS)";	\
	else								\
	    cd $(OBJROOT)/sarld_dir; $(MAKE) -f $(SRCROOT)/Makefile	\
		libsarld.a VPATH=$(SRCROOT) OFILE_DIR=$(OBJROOT)/sarld_dir \
		SYMROOT=$(SYMROOT)/ld_dir SRCROOT=$(SRCROOT) CC="$(CC)"	\
		COPTS="-static -DRLD -DSA_RLD" RC_CFLAGS="$(RC_CFLAGS)"	\
		LIBSTUFF="-L$(SYMROOT)/../libstuff -lstuff"		\
		VERS_STRING_FLAGS="$(VERS_STRING_FLAGS)"		\
		MAKEINCDIR="`pwd`" RC_ARCHS="$(RC_ARCHS)";		\
	fi

kld_build: $(OBJROOT)/dynamic_kld $(OBJROOT)/static_kld $(SYMROOT)/ld_dir
	@if [ $(SRCROOT) = . ];						\
	then								\
	    cd dynamic_kld; $(MAKE) -f ../Makefile libkld.dylib VPATH=.. \
		MAKEINCDIR="`pwd`" OFILE_DIR=. SRCROOT=.. 		\
		COPTS="-DRLD -DKLD" CC="$(CC)"				\
		LIBSTUFF="-L../../libstuff -lstuff"			\
		VERS_STRING_FLAGS="$(VERS_STRING_FLAGS)"		\
		RC_CFLAGS="$(RC_CFLAGS)" RC_ARCHS="$(RC_ARCHS)";	\
	else								\
	    cd $(OBJROOT)/dynamic_kld; $(MAKE) -f $(SRCROOT)/Makefile	\
		libkld.dylib VPATH=$(SRCROOT) OFILE_DIR=$(OBJROOT)/dynamic_kld \
		SYMROOT=$(SYMROOT)/ld_dir SRCROOT=$(SRCROOT) CC="$(CC)"	\
		COPTS="-DRLD -DKLD" RC_CFLAGS="$(RC_CFLAGS)"		\
		LIBSTUFF="-L$(SYMROOT)/../libstuff -lstuff"		\
		VERS_STRING_FLAGS="$(VERS_STRING_FLAGS)"		\
		MAKEINCDIR="`pwd`" RC_ARCHS="$(RC_ARCHS)";		\
	fi
	@if [ $(SRCROOT) = . ];						\
	then								\
	    cd static_kld; $(MAKE) -f ../Makefile libkld.a VPATH=.. \
		MAKEINCDIR="`pwd`" OFILE_DIR=. SRCROOT=.. 		\
		COPTS="-static -DRLD -DKLD $(KERN)" CC="$(CC)"		\
		LIBSTUFF="-L../../libstuff -lstuff_kld"			\
		LIBMACHO="-L../../libmacho/ktmp_obj -lmacho_kld"	\
		VERS_STRING_FLAGS="$(VERS_STRING_FLAGS)"		\
		RC_CFLAGS="$(RC_CFLAGS)" RC_ARCHS="$(RC_ARCHS)";	\
	else								\
	    cd $(OBJROOT)/static_kld; $(MAKE) -f $(SRCROOT)/Makefile	\
		libkld.a VPATH=$(SRCROOT) OFILE_DIR=$(OBJROOT)/static_kld \
		SYMROOT=$(SYMROOT)/ld_dir SRCROOT=$(SRCROOT) CC="$(CC)"	\
		COPTS="-static -DRLD -DKLD $(KERN)" RC_CFLAGS="$(RC_CFLAGS)" \
		LIBSTUFF="-L$(SYMROOT)/../libstuff -lstuff_kld"		\
		LIBMACHO="-L$(SYMROOT)/../libmacho -lmacho_kld"		\
		VERS_STRING_FLAGS="$(VERS_STRING_FLAGS)"		\
		MAKEINCDIR="`pwd`" RC_ARCHS="$(RC_ARCHS)";		\
	fi

ld.NEW:	$(OBJS)
	$(CC) $(RC_CFLAGS) -nostdlib -r \
		$(OBJS) $(LIBSTUFF) \
		-o $(SYMROOT)/ld.private.o
	$(CC) $(RC_CFLAGS) $(SDK) -o $(SYMROOT)/ld.NEW \
		$(SYMROOT)/ld.private.o

ld_g.NEW: $(OBJS)
	$(CC) $(RC_CFLAGS) -nostdlib -r \
		$(OBJS) $(LIBSTUFF) \
		-o $(SYMROOT)/ld_g.private.o
	$(CC) $(RC_CFLAGS) $(SDK) -o $(SYMROOT)/ld_g.NEW \
		$(SYMROOT)/ld_g.private.o

librld_static.a librld_pg.a librld.a: librld.o
	libtool -static -o $(SYMROOT)/$@ librld.o

librld.o: $(OBJS)
	$(CC) $(RC_CFLAGS) -nostdlib -r \
		-o $(OFILE_DIR)/librld.o $(OBJS) $(LIBSTUFF)

libsarld.a: $(OBJS)
	$(CC) $(RC_CFLAGS) -nostdlib -r \
		-o $(SYMROOT)/libsarld.a $(OBJS) $(LIBSTUFF)

libkld.a: $(OBJS)
	$(CC) $(RC_CFLAGS) -nostdlib -r \
		-o $(SYMROOT)/prehack_libkld.a $(OBJS) $(LIBSTUFF) $(LIBMACHO)
	nmedit -p $(SYMROOT)/prehack_libkld.a
	seg_hack __KLD $(SYMROOT)/prehack_libkld.a -o $(SYMROOT)/static_libkld.o
	libtool -o $(SYMROOT)/libkld.a $(SYMROOT)/static_libkld.o

libkld.dylib: $(OBJS)
	$(CC) $(RC_CFLAGS) -nostdlib -r \
		-o $(SYMROOT)/dynamic_libkld.o $(OBJS) $(LIBSTUFF)
	nmedit -p $(SYMROOT)/dynamic_libkld.o
	libtool -o $(SYMROOT)/libkld.dylib $(SYMROOT)/dynamic_libkld.o

ifeq "NO" "$(USE_DEPENDENCY_FILE)"
.c.o:
	$(CC) $(CFLAGS) $(COPTS) $(OFLAG) $(RC_CFLAGS) -c \
		-o $(OFILE_DIR)/$*.o $<
else
.c.o:
	$(CC) $(CFLAGS) $(COPTS) $(OFLAG) $(RC_CFLAGS) -c -MD \
		-dependency-file $(OFILE_DIR)/$*.d -o $(OFILE_DIR)/$*.o $<
	md -d -u $(OFILE_DIR)/Makedep $(OFILE_DIR)/$*.d
endif

clean:
	-rm -r -f $(OFILE_DIRS) ld_g_dir ld_p_dir make.out

shlib_clean:
	-rm -r -f $(OBJROOT)/ld_dir $(OBJROOT)/ptmp_obj $(OBJROOT)/otmp_obj \
		$(OBJROOT)/dtmp_obj $(OBJROOT)/sarld_dir ld_g_dir ld_p_dir
	-cd $(OBJROOT)/shlib_obj; rm -f $(OBJS) Makedep 


install: $(OFILE_DIRS) $(SYMROOT) ld_build $(RC_OS)_lib_ofiles $(RC_OS)_install

macos_install:
	$(MKDIRS) $(DSTROOT)$(USRBINDIR)
	install -c -s -m 555 $(SYMROOT)/ld_dir/ld.NEW \
		$(DSTROOT)$(USRBINDIR)/ld_classic

teflon_install: common_install
	$(MKDIRS) $(DSTROOT)$(USRBINDIR)
	install -c -s -m 555 $(SYMROOT)/ld_dir/ld.NEW $(DSTROOT)$(USRBINDIR)/ld

nextstep_install: common_install
	$(MKDIRS) $(DSTROOT)$(BINDIR)
	install -c -s -m 555 $(SYMROOT)/ld_dir/ld.NEW $(DSTROOT)$(BINDIR)/ld

common_install:
	$(MKDIRS) $(DSTROOT)$(LOCLIBDIR)
	-install -p -c -m 444 $(SYMROOT)/ld_dir/libsarld.a \
		$(DSTROOT)$(LOCLIBDIR)/libsarld.a

lib_ofiles_install: $(RC_OS)_lib_ofiles_install

ifeq "macos" "$(RC_OS)"
  KLD_BUILD := $(shell if [ "$(RC_RELEASE)" = "Beaker" ] ||    \
			  [ "$(RC_RELEASE)" = "Bunsen" ] || \
			  [ "$(RC_RELEASE)" = "Gonzo" ] || \
			  [ "$(RC_RELEASE)" = "Kodiak" ] || \
			  [ "$(OLD_LIBKLD)" = "NO" ]; then \
	 echo ""; else echo kld_build; fi; )
  X_LIB_OFILES := $(shell if [ "$(RC_RELEASE)" = "Beaker" ] ||    \
			     [ "$(RC_RELEASE)" = "Bunsen" ] || \
			     [ "$(RC_RELEASE)" = "Gonzo" ] || \
			     [ "$(RC_RELEASE)" = "Kodiak" ] || \
			     [ "$(RC_RELEASE)" = "Cheetah" ]; then \
	 echo "x_lib_ofiles"; else echo ""; fi; )
else
  KLD_BUILD = 
  X_LIB_OFILES = 
endif

x_lib_ofiles: lib_ofiles common_install

macos_lib_ofiles_install: $(KLD_BUILD) $(X_LIB_OFILES)
	$(MKDIRS) $(DSTROOT)$(SYSTEMDIR)
	if [ "$(X_LIB_OFILES)" = x_lib_ofiles ];			\
	then								\
	    if [ $(SRCROOT) = . ];					\
	    then							\
		install -p -c -m 444 dtmp_obj/librld.a			\
			 $(DSTROOT)$(LOCLIBDIR)/librld.a ;		\
		ranlib $(DSTROOT)$(LOCLIBDIR)/librld.a ;		\
		install -p -c -m 444 dtmp_obj/librld.a			\
			 $(DSTROOT)$(SYSTEMDIR)/librld.a ;		\
		ranlib $(DSTROOT)$(SYSTEMDIR)/librld.a ;		\
		(cd $(DSTROOT)$(SYSTEMDIR);				\
		rm -f librld_debug.a ; ln -s librld.a librld_debug.a) ;	\
		install -p -c -m 444 ptmp_obj/librld_pg.a		\
			 $(DSTROOT)$(LOCLIBDIR)/librld_profile.a ;	\
		ranlib $(DSTROOT)$(LOCLIBDIR)/librld_profile.a ;	\
		install -p -c -m 444 ptmp_obj/ld_dir/librld_pg.a	\
			 $(DSTROOT)$(SYSTEMDIR)/librld_profile.a ;	\
		ranlib $(DSTROOT)$(SYSTEMDIR)/librld_profile.a ;	\
	    else							\
		install -p -c -m 444 $(SYMROOT)/ld_dir/librld.a		\
			 $(DSTROOT)$(LOCLIBDIR)/librld.a ;		\
		ranlib $(DSTROOT)$(LOCLIBDIR)/librld.a ;		\
		install -p -c -m 444 $(SYMROOT)/ld_dir/librld.a		\
			 $(DSTROOT)$(SYSTEMDIR)/librld.a ;		\
		ranlib $(DSTROOT)$(SYSTEMDIR)/librld.a ;		\
		(cd $(DSTROOT)$(SYSTEMDIR);				\
		rm -f librld_debug.a ; ln -s librld.a librld_debug.a) ;	\
		install -p -c -m 444 $(SYMROOT)/ld_dir/librld_pg.a	\
			 $(DSTROOT)$(LOCLIBDIR)/librld_profile.a ;	\
		ranlib $(DSTROOT)$(LOCLIBDIR)/librld_profile.a ;	\
		install -p -c -m 444 $(SYMROOT)/ld_dir/librld_pg.a	\
			 $(DSTROOT)$(SYSTEMDIR)/librld_profile.a ;	\
		ranlib $(DSTROOT)$(SYSTEMDIR)/librld_profile.a ;	\
	    fi								\
	fi
	$(MKDIRS) $(DSTROOT)$(LOCLIBDIR)
	if [ "$(KLD_BUILD)" = kld_build ];				\
	then								\
	    if [ $(SRCROOT) = . ];					\
	    then							\
		install -p -c -m 444 static_kld/libkld.a		\
			$(DSTROOT)$(LOCLIBDIR)/libkld.a ;		\
		install -p -c -m 444 dynamic_kld/libkld.dylib		\
			$(DSTROOT)$(LOCLIBDIR)/libkld.dylib ;		\
	    else							\
		install -p -c -m 444 $(SYMROOT)/ld_dir/libkld.a		\
			$(DSTROOT)$(LOCLIBDIR)/libkld.a ;		\
		install -p -c -m 444 $(SYMROOT)/ld_dir/libkld.dylib	\
			$(DSTROOT)$(LOCLIBDIR)/libkld.dylib ;		\
	    fi								\
	fi

teflon_lib_ofiles_install nextstep_lib_ofiles_install:

installsrc:
	$(MKDIRS) $(SRCROOT)
	chmod 755 $(SRCROOT)
	gnutar cf - $(INSTALL_FILES) | (cd $(SRCROOT); gnutar xf -)
	chmod 444 $(SRCROOT)/*

$(OFILE_DIRS) $(SYMROOT) $(SYMROOT)/ld_dir ld_g_dir ld_p_dir:
	$(MKDIRS) $@

-include $(MAKEINCDIR)/Makedep
