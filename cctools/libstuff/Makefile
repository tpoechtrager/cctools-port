RC_OS = macos
OFLAG = -Os
LTO = -DLTO_SUPPORT
LEGACY = -D_MACH_I386_THREAD_STATUS_FPSTATE_LEGACY_FIELD_NAMES_ \
	 -D_ARCHITECTURE_I386_FPU_FPSTATE_LEGACY_FIELD_NAMES_

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

X_CFLAGS =

USE_DEPENDENCY_FILE := $(shell if [ "$(notdir $(CC))" != "mwccppc" ] && \
				  [ "$(RC_XBS)" != "YES" ]; then \
	     echo YES ; else echo NO ; \
	     fi; )
	
ifneq "" "$(SDKROOT)"
  SDK = -isysroot $(SDKROOT)
  CC = $(shell xcrun -find -sdk $(SDKROOT) cc)
endif

G = -g
ifeq "clang" "$(notdir $(CC))"
  CFLAGS = $(OFLAG) $(LTO) $(G) -I$(SRCROOT)/../include -Wall \
	   $(LEGACY) $(SDK)
  PG =
else
  CFLAGS = $(OFLAG) $(LTO) $(G) -I$(SRCROOT)/../include -Wall \
	   $(X_CFLAGS) $(LEGACY) $(SDK)
  PG = -pg
endif

ifneq "" "$(wildcard /bin/mkdirs)"
  MKDIRS = /bin/mkdirs
else
  MKDIRS = /bin/mkdir -p
endif

SRCROOT = .
SYMROOT = .
OBJROOT = .
OFILE_DIRS = $(OBJROOT)/static_obj $(OBJROOT)/dynamic_obj \
	     $(OBJROOT)/profile_obj $(OBJROOT)/shlib_obj \
	     $(OBJROOT)/static_kld_obj
OFILE_DIR = $(OBJROOT)
VPATH = $(OFILE_DIR)

ifeq "$(EFITOOLS)" "efitools"
	COFF_BYTESEX = coff_bytesex.c
else
	COFF_BYTESEX =
endif
CFILES = allocate.c errors.c arch.c execute.c bytesex.c rnd.c hppa.c \
	  swap_headers.c reloc.c version_number.c get_toc_byte_sex.c \
	  best_arch.c fatals.c arch_usage.c ofile.c ofile_error.c print.c \
	  set_arch_flag_name.c seg_addr_table.c dylib_table.c \
	  breakout.c writeout.c checkout.c fatal_arch.c ofile_get_word.c \
	  vm_flush_cache.c hash_string.c dylib_roots.c guess_short_name.c \
	  SymLoc.c get_arch_from_host.c crc32.c macosx_deployment_target.c \
	  symbol_list.c unix_standard_mode.c lto.c llvm.c $(COFF_BYTESEX)
OBJS = $(CFILES:.c=.o) apple_version.o
INSTALL_FILES = $(CFILES) Makefile notes

ifeq "$(OLD_LIBKLD)" "YES"
	STATIC_KLD = static_kld
else
	STATIC_KLD = 
endif

#all: $(OFILE_DIRS) $(SYMROOT) static dynamic profile $(STATIC_KLD) $(RC_OS)
all: $(OFILE_DIRS) $(SYMROOT) dynamic profile $(STATIC_KLD) $(RC_OS)

lib_ofiles: $(OFILE_DIRS) $(SYMROOT) dynamic

lib_ofiles_install: lib_ofiles

teflon macos:

nextstep: shlib

static: $(OBJROOT)/static_obj
	@if [ $(SRCROOT) = . ];						\
	then								\
	    cd static_obj;						\
		$(MAKE) -f ../Makefile libstuff.a			\
		PRODUCT=libstuff.a					\
		VPATH=.. 						\
		OFILE_DIR=.						\
		SRCROOT=..						\
		SYMROOT=..						\
		COPTS="-static"						\
		OFLAG="$(OFLAG)"					\
		CFILES="$(CFILES)"					\
		RC_CFLAGS="$(RC_CFLAGS)"				\
		RC_ARCHS="$(RC_ARCHS)";					\
	else								\
	    cd $(OBJROOT)/static_obj;					\
		$(MAKE) -f $(SRCROOT)/Makefile libstuff.a		\
		PRODUCT=libstuff.a					\
		VPATH=$(SRCROOT)					\
		OFILE_DIR=$(OBJROOT)/static_obj				\
		SRCROOT=$(SRCROOT)					\
		SYMROOT=$(SYMROOT)					\
		COPTS="-static"						\
		OFLAG="$(OFLAG)"					\
		CFILES="$(CFILES)"					\
		RC_CFLAGS="$(RC_CFLAGS)"				\
		RC_ARCHS="$(RC_ARCHS)";					\
	fi

static_kld: $(OBJROOT)/static_kld_obj
	@if [ $(SRCROOT) = . ];						\
	then								\
	    cd static_kld_obj;						\
		$(MAKE) -f ../Makefile libstuff_kld.a			\
		PRODUCT=libstuff_kld.a					\
		VPATH=.. 						\
		OFILE_DIR=.						\
		SRCROOT=..						\
		SYMROOT=..						\
		COPTS="-static -DRLD -DSA_RLD $(KERN)"			\
		OFLAG="$(OFLAG)"					\
		CFILES="$(CFILES)"					\
		RC_CFLAGS="$(RC_CFLAGS)"				\
		RC_ARCHS="$(RC_ARCHS)";					\
	else								\
	    cd $(OBJROOT)/static_kld_obj;				\
		$(MAKE) -f $(SRCROOT)/Makefile libstuff_kld.a		\
		PRODUCT=libstuff_kld.a					\
		VPATH=$(SRCROOT)					\
		OFILE_DIR=$(OBJROOT)/static_kld_obj			\
		SRCROOT=$(SRCROOT)					\
		SYMROOT=$(SYMROOT)					\
		COPTS="-static -DRLD -DSA_RLD $(KERN)"			\
		OFLAG="$(OFLAG)"					\
		CFILES="$(CFILES)"					\
		RC_CFLAGS="$(RC_CFLAGS)"				\
		RC_ARCHS="$(RC_ARCHS)";					\
	fi

dynamic: $(OBJROOT)/dynamic_obj
	@if [ $(SRCROOT) = . ];						\
	then								\
	    cd dynamic_obj;						\
		$(MAKE) -f ../Makefile libstuff.dylib			\
		PRODUCT=libstuff.dylib					\
		VPATH=.. 						\
		OFILE_DIR=.						\
		SRCROOT=..						\
		SYMROOT=..						\
		COPTS="$(DYNAMIC)"					\
		OFLAG="$(OFLAG)"					\
		CFILES="$(CFILES)"					\
		RC_CFLAGS="$(RC_CFLAGS)"				\
		RC_ARCHS="$(RC_ARCHS)";					\
	else								\
	    cd $(OBJROOT)/dynamic_obj;					\
		$(MAKE) -f $(SRCROOT)/Makefile libstuff.dylib		\
		PRODUCT=libstuff.dylib					\
		VPATH=$(SRCROOT)					\
		OFILE_DIR=$(OBJROOT)/dynamic_obj			\
		SRCROOT=$(SRCROOT)					\
		SYMROOT=$(SYMROOT)					\
		COPTS="$(DYNAMIC)"					\
		OFLAG="$(OFLAG)"					\
		CFILES="$(CFILES)"					\
		RC_CFLAGS="$(RC_CFLAGS)"				\
		RC_ARCHS="$(RC_ARCHS)";					\
	fi

profile: $(OBJROOT)/profile_obj
	@if [ $(SRCROOT) = . ];						\
	then								\
	    cd profile_obj;						\
		$(MAKE) -f ../Makefile libstuff_p.a			\
		PRODUCT=libstuff_p.a					\
		VPATH=.. 						\
		OFILE_DIR=.						\
		SRCROOT=..						\
		SYMROOT=..						\
		COPTS="$(DYNAMIC) $(PG)"				\
		OFLAG="$(OFLAG)"					\
		RC_CFLAGS="$(RC_CFLAGS)"				\
		RC_ARCHS="$(RC_ARCHS)";					\
	else								\
	    cd $(OBJROOT)/profile_obj;					\
		$(MAKE) -f $(SRCROOT)/Makefile libstuff_p.a		\
		PRODUCT=libstuff_p.a					\
		VPATH=$(SRCROOT)					\
		OFILE_DIR=$(OBJROOT)/profile_obj			\
		SRCROOT=$(SRCROOT)					\
		SYMROOT=$(SYMROOT)					\
		COPTS="$(DYNAMIC) $(PG)"				\
		OFLAG="$(OFLAG)"					\
		RC_CFLAGS="$(RC_CFLAGS)"				\
		RC_ARCHS="$(RC_ARCHS)";					\
	fi

shlib: $(OBJROOT)/shlib_obj
	@if [ $(SRCROOT) = . ];						\
	then								\
	    cd shlib_obj;						\
		$(MAKE) -f ../Makefile libstuff_s.a			\
		PRODUCT=libstuff_s.a					\
		VPATH=.. 						\
		OFILE_DIR=.						\
		SRCROOT=..						\
		SYMROOT=..						\
		COPTS="-static -DSHLIB -I/LocalDeveloper/Headers/libsys" \
		OFLAG="$(OFLAG)"					\
		RC_CFLAGS="$(RC_CFLAGS)"				\
		RC_ARCHS="$(RC_ARCHS)";					\
	else								\
	    cd $(OBJROOT)/shlib_obj;					\
		$(MAKE) -f $(SRCROOT)/Makefile libstuff_s.a		\
		PRODUCT=libstuff_s.a					\
		VPATH=$(SRCROOT)					\
		OFILE_DIR=$(OBJROOT)/shlib_obj				\
		SRCROOT=$(SRCROOT)					\
		SYMROOT=$(SYMROOT)					\
		COPTS="-static -DSHLIB -I/LocalDeveloper/Headers/libsys" \
		OFLAG="$(OFLAG)"					\
		RC_CFLAGS="$(RC_CFLAGS)"				\
		RC_ARCHS="$(RC_ARCHS)";					\
	fi

$(PRODUCT):	$(OFILE_DIR) $(SYMROOT) $(OBJS)
	libtool -static -o $(SYMROOT)/$(PRODUCT) $(OBJS)

apple_version.c:
	echo "const char apple_version[]=" > $(OFILE_DIR)/$@
	echo "\"cctools-$(RC_ProjectSourceVersion)\";" >> $(OFILE_DIR)/$@

ifeq "NO" "$(USE_DEPENDENCY_FILE)"
.c.o:
	$(CC) $(CFLAGS) $(RC_CFLAGS) $(COPTS) -c -o $(OFILE_DIR)/$*.o $<

lto.o: lto.c
	$(CC) -std=c99 $(CFLAGS) $(RC_CFLAGS) $(COPTS) -c \
		-I/Developer/usr/local/include \
		-I$(DT_TOOLCHAIN_DIR)/usr/local/include \
		-o $(OFILE_DIR)/$*.o $<
else
.c.o:
	$(CC) $(CFLAGS) $(RC_CFLAGS) $(COPTS) -c \
		-MD -dependency-file $(OFILE_DIR)/$*.d \
		-o $(OFILE_DIR)/$*.o $<
	md -d -u $(OFILE_DIR)/Makedep $(OFILE_DIR)/$*.d

lto.o: lto.c
	$(CC) -std=c99 $(CFLAGS) $(RC_CFLAGS) $(COPTS) -c \
		-I/Developer/usr/local/include \
		-I$(DT_TOOLCHAIN_DIR)/usr/local/include \
		-MD -dependency-file $(OFILE_DIR)/$*.d \
		-o $(OFILE_DIR)/$*.o $<
	md -d -u $(OFILE_DIR)/Makedep $(OFILE_DIR)/$*.d
endif

clean shlib_clean:
	-rm -r -f $(OFILE_DIRS)
	-rm -f libstuff.a libstuff.dylib libstuff_p.a libstuff_s.a

install: all

installsrc:
	$(MKDIRS) $(SRCROOT)
	chmod 755 $(SRCROOT)
	gnutar cf - $(INSTALL_FILES) | (cd $(SRCROOT); gnutar xf -)
	chmod 444 $(SRCROOT)/*

installGASsrc:
	$(MKDIRS) $(SRCROOT)
	chmod 755 $(SRCROOT)
	gnutar cf - Makefile bytesex.c rnd.c | (cd $(SRCROOT); gnutar xf -)
	chmod 444 $(SRCROOT)/*

fromGASsrc: $(OBJROOT)/dynamic_obj
	@cd dynamic_obj;						\
	    $(MAKE) -f ../Makefile libstuff.dylib			\
	    PRODUCT=libstuff.dylib					\
	    VPATH=.. 							\
	    OFILE_DIR=.							\
	    SRCROOT=..							\
	    SYMROOT=..							\
	    COPTS="$(DYNAMIC)"						\
	    OFLAG="$(OFLAG)"						\
	    CFILES="bytesex.c rnd.c"					\
	    RC_CFLAGS="$(RC_CFLAGS)"					\
	    RC_ARCHS="$(RC_ARCHS)";

$(OFILE_DIRS) $(SYMROOT):
	$(MKDIRS) $@

-include $(OFILE_DIR)/Makedep
