export USE_APPLE_PB_SUPPORT = all

DSTROOT = /
RC_OS = macos
RC_CFLAGS =

INSTALLSRC_SUBDIRS = $(COMMON_SUBDIRS) $(SUBDIRS_32) ar include efitools \
		     libmacho
COMMON_SUBDIRS = libstuff as gprof misc RelNotes man cbtlibs otool
APPLE_SUBDIRS = ar
SUBDIRS_32 = ld
EFITOOLS = efitools
SUBDIRS = $(COMMON_SUBDIRS) $(APPLE_SUBDIRS) $(EFITOOLS)

OLD_LIBKLD = NO
BUILD_DYLIBS = YES
LTO = -DLTO_SUPPORT

ifeq "macos" "$(RC_OS)"
  TRIE := $(shell if [ "$(RC_MAJOR_RELEASE_TRAIN)" = "Tiger" ] || \
		     [ "$(RC_MAJOR_RELEASE_TRAIN)" = "Leopard" ] || \
		     [ "$(RC_PURPLE)" = "YES" ]; then \
			    echo "" ; \
			else \
			    echo "-DTRIE_SUPPORT" ; fi; )
else
  TRIE =
endif

ifneq "" "$(wildcard /bin/mkdirs)"
  MKDIRS = /bin/mkdirs
else
  MKDIRS = /bin/mkdir -p
endif

all clean: $(DSTROOT)
	@if [ "$(SRCROOT)" != "" ] && \
	    [ "$(OBJROOT)" != "" ] && \
	    [ "$(SYMROOT)" != "" ];			\
	then								\
	    CWD=`pwd`; cd "$(DSTROOT)"; DSTROOT=`pwd`; cd "$$CWD";	\
	    for i in `echo $(SUBDIRS)`;					\
	      do							\
		    echo =========== $(MAKE) $@ for $$i =============;	\
		    (cd $$i; $(MAKE) RC_CFLAGS="$(RC_CFLAGS)"		\
			RC_ARCHS="$(RC_ARCHS)" RC_OS="$(RC_OS)"		\
			VERS_STRING_FLAGS="$(VERS_STRING_FLAGS)"	\
			EFITOOLS="$(EFITOOLS)" 				\
			TRIE="$(TRIE)" LTO="$(LTO)" DSTROOT=$$DSTROOT	\
			SRCROOT=$(SRCROOT)/$$i				\
			OBJROOT=$(OBJROOT)/$$i				\
			SYMROOT=$(SYMROOT)/$$i $@) || exit 1 ;		\
	      done;							\
	    SED_RC_CFLAGS=`echo "$(RC_CFLAGS)" | sed 's/-arch ppc64//'  \
 		| sed 's/-arch x86_64//' | sed 's/-arch armv5//'	\
		| sed 's/-arch armv6//' | sed 's/-arch armv7[f,k,s]*//g'`; \
	    EMPTY=`echo "$$SED_RC_CFLAGS" | sed 's/ //g'		\
		| sed 's/-pipe//'`;					\
	    if [ "$$EMPTY"x != "x" ];					\
	    then							\
	      for i in `echo $(SUBDIRS_32)`;				\
	        do							\
		    echo =========== $(MAKE) $@ for $$i =============;	\
		    (cd $$i; $(MAKE) "RC_CFLAGS=$$SED_RC_CFLAGS" 	\
			RC_ARCHS="$(RC_ARCHS)" RC_OS="$(RC_OS)"		\
			VERS_STRING_FLAGS="$(VERS_STRING_FLAGS)"	\
			EFITOOLS="$(EFITOOLS)" 				\
			TRIE="$(TRIE)" LTO="$(LTO)" DSTROOT=$$DSTROOT	\
			SRCROOT=$(SRCROOT)/$$i				\
			OBJROOT=$(OBJROOT)/$$i				\
			SYMROOT=$(SYMROOT)/$$i $@) || exit 1 ;		\
	        done;							\
	    fi								\
	else								\
	    CWD=`pwd`; cd "$(DSTROOT)"; DSTROOT=`pwd`; cd "$$CWD";	\
	    for i in `echo $(SUBDIRS)`;					\
	      do							\
		    echo =========== $(MAKE) $@ for $$i =============;	\
		    (cd $$i; $(MAKE) RC_CFLAGS="$(RC_CFLAGS)"		\
			RC_ARCHS="$(RC_ARCHS)" RC_OS="$(RC_OS)"		\
			EFITOOLS="$(EFITOOLS)" 				\
			TRIE="$(TRIE)" LTO="$(LTO)" DSTROOT=$$DSTROOT 	\
			$@) || exit 1 ; 				\
	      done;							\
	    SED_RC_CFLAGS=`echo "$(RC_CFLAGS)" | sed 's/-arch ppc64//'  \
 		| sed 's/-arch x86_64//' | sed 's/-arch armv5//'	\
		| sed 's/-arch armv6//' | sed 's/-arch armv7[f,k,s]*//g'`; \
	    EMPTY=`echo "$$SED_RC_CFLAGS" | sed 's/ //g'		\
		| sed 's/-pipe//'`;					\
	    if [ "$$EMPTY"x != "x" ];					\
	    then							\
	      for i in `echo $(SUBDIRS_32)`;				\
	        do							\
		    echo =========== $(MAKE) $@ for $$i =============;	\
		    (cd $$i; $(MAKE) RC_CFLAGS="$$SED_RC_CFLAGS"	\
			RC_ARCHS="$(RC_ARCHS)" RC_OS="$(RC_OS)"		\
			EFITOOLS="$(EFITOOLS)"				\
			TRIE="$(TRIE)" LTO="$(LTO)" DSTROOT=$$DSTROOT	\
			$@) || exit 1 ; 				\
	        done;							\
	    fi								\
	fi

install:
	@if [ $(SRCROOT) ];						\
	then								\
	    projName=`basename $(SRCROOT) | 				\
		sed 's/-[-0-9.]*//' | sed 's/\.cvs//'`;			\
	    if [ "$$projName" = cctools ];				\
	    then							\
		target=install_tools;					\
	    elif [ "$$projName" = cctools_sdk ];			\
	    then							\
		target=install_tools;					\
	    elif [ "$$projName" = cctoolslib ];				\
	    then							\
	    	target=lib_ofiles_install;				\
	    else							\
	        echo "Unknown project name $$projName";			\
		exit 1;							\
	    fi;								\
	    CWD=`pwd`; cd "$(DSTROOT)"; DSTROOT=`pwd`; cd "$$CWD";	\
	    echo =========== $(MAKE) $$target =============;		\
	    $(MAKE) RC_CFLAGS="$(RC_CFLAGS)"				\
		RC_ARCHS="$(RC_ARCHS)" RC_OS="$(RC_OS)"			\
		VERS_STRING_FLAGS="$(VERS_STRING_FLAGS)"		\
		EFITOOLS="$(EFITOOLS)" TRIE="$(TRIE)"			\
		LTO="$(LTO)" DSTROOT=$$DSTROOT/$(INSTALL_LOCATION)	\
		SRCROOT=$(SRCROOT)					\
		OBJROOT=$(OBJROOT)					\
		SYMROOT=$(SYMROOT) $$target;				\
	else								\
	    CWD=`pwd`; cd "$(DSTROOT)"; DSTROOT=`pwd`; cd "$$CWD";	\
	    echo =========== $(MAKE) install_tools =============;	\
	    $(MAKE) RC_CFLAGS="$(RC_CFLAGS)" RC_ARCHS="$(RC_ARCHS)" 	\
		RC_OS="$(RC_OS)" SUBDIRS="$(SUBDIRS)" 			\
		SUBDIRS_32="$(SUBDIRS_32)"				\
		VERS_STRING_FLAGS="$(VERS_STRING_FLAGS)"		\
		EFITOOLS="$(EFITOOLS)" TRIE="$(TRIE)"			\
		LTO="$(LTO)" DSTROOT=$$DSTROOT install_tools 		\
		lib_ofiles_install;					\
	fi

install_tools: installhdrs
	@if [ $(SRCROOT) ];						\
	then								\
	    CWD=`pwd`; cd "$(DSTROOT)"; DSTROOT=`pwd`; cd "$$CWD";	\
	    for i in `echo $(SUBDIRS)`;					\
	      do							\
		    echo ======== $(MAKE) install for $$i ============;	\
		    (cd $$i; $(MAKE) RC_CFLAGS="$(RC_CFLAGS)"		\
			RC_ARCHS="$(RC_ARCHS)" RC_OS="$(RC_OS)"		\
			VERS_STRING_FLAGS="$(VERS_STRING_FLAGS)"	\
			EFITOOLS="$(EFITOOLS)" 				\
			TRIE="$(TRIE)" LTO="$(LTO)" DSTROOT=$$DSTROOT	\
			SRCROOT=$(SRCROOT)/$$i				\
			OBJROOT=$(OBJROOT)/$$i				\
			SYMROOT=$(SYMROOT)/$$i install) || exit 1;	\
	      done;							\
	    SED_RC_CFLAGS=`echo "$(RC_CFLAGS)" | sed 's/-arch ppc64//'  \
 		| sed 's/-arch x86_64//' | sed 's/-arch armv5//'	\
		| sed 's/-arch armv6//' | sed 's/-arch armv7[f,k,s]*//g'`; \
	    EMPTY=`echo "$$SED_RC_CFLAGS" | sed 's/ //g'		\
		| sed 's/-pipe//'`;					\
	    if [ "$$EMPTY"x != "x" ];					\
	    then							\
	      for i in `echo $(SUBDIRS_32)`;				\
	        do							\
		    echo ======== $(MAKE) install for $$i ============;	\
		    (cd $$i; $(MAKE) RC_CFLAGS="$$SED_RC_CFLAGS"	\
			RC_ARCHS="$(RC_ARCHS)" RC_OS="$(RC_OS)"		\
			VERS_STRING_FLAGS="$(VERS_STRING_FLAGS)"	\
			EFITOOLS="$(EFITOOLS)"				\
			TRIE="$(TRIE)" LTO="$(LTO)" DSTROOT=$$DSTROOT	\
			SRCROOT=$(SRCROOT)/$$i				\
			OBJROOT=$(OBJROOT)/$$i				\
			SYMROOT=$(SYMROOT)/$$i install) || exit 1;	\
	        done;							\
	    fi								\
	else								\
	    CWD=`pwd`; cd "$(DSTROOT)"; DSTROOT=`pwd`; cd "$$CWD";	\
	    for i in `echo $(SUBDIRS)`;					\
	      do							\
		    echo ========= $(MAKE) install for $$i ===========;	\
		    (cd $$i; $(MAKE) RC_CFLAGS="$(RC_CFLAGS)"		\
			RC_ARCHS="$(RC_ARCHS)" RC_OS="$(RC_OS)"		\
			VERS_STRING_FLAGS="$(VERS_STRING_FLAGS)"	\
			EFITOOLS="$(EFITOOLS)"				\
			TRIE="$(TRIE)" LTO="$(LTO)" DSTROOT=$$DSTROOT 	\
			install) || exit 1;				\
	      done;							\
	    SED_RC_CFLAGS=`echo "$(RC_CFLAGS)" | sed 's/-arch ppc64//'  \
 		| sed 's/-arch x86_64//' | sed 's/-arch armv5//'	\
		| sed 's/-arch armv6//' | sed 's/-arch armv7[f,k,s]*//g'`; \
	    EMPTY=`echo "$$SED_RC_CFLAGS" | sed 's/ //g'		\
		| sed 's/-pipe//'`;					\
	    if [ "$$EMPTY"x != "x" ];					\
	    then							\
	      for i in `echo $(SUBDIRS_32)`;				\
	        do							\
		    echo ========= $(MAKE) install for $$i ===========;	\
		    (cd $$i; $(MAKE) RC_CFLAGS="$$SED_RC_CFLAGS"	\
			RC_ARCHS="$(RC_ARCHS)" RC_OS="$(RC_OS)"		\
			VERS_STRING_FLAGS="$(VERS_STRING_FLAGS)"	\
			EFITOOLS="$(EFITOOLS)"				\
			TRIE="$(TRIE)" LTO="$(LTO)" DSTROOT=$$DSTROOT 	\
			install) || exit 1;				\
	        done;							\
	    fi								\
	fi

ofiles_install:
	@ export RC_FORCEHDRS=YES;					\
	$(MAKE) RC_CFLAGS="$(RC_CFLAGS)"				\
		RC_ARCHS="$(RC_ARCHS)"					\
		RC_OS="$(RC_OS)"					\
		DSTROOT=$$DSTROOT/$(INSTALL_LOCATION)			\
		SRCROOT=$(SRCROOT)					\
		OBJROOT=$(OBJROOT)					\
		SYMROOT=$(SYMROOT)					\
		EFITOOLS="$(EFITOOLS)" TRIE="$(TRIE)"			\
		LTO="$(LTO)" lib_ofiles_install

lib_ofiles lib_ofiles_install: installhdrs
	@if [ $(SRCROOT) ];						\
	then								\
	    CWD=`pwd`; cd "$(DSTROOT)"; DSTROOT=`pwd`; cd "$$CWD";	\
	    SED_RC_CFLAGS=`echo "$(RC_CFLAGS)" | sed 's/-arch ppc64//'  \
 		| sed 's/-arch x86_64//'`;				\
	    echo =========== $(MAKE) $@ for libstuff =============;	\
	    (cd libstuff; $(MAKE) "RC_CFLAGS=$(RC_CFLAGS)"		\
		RC_ARCHS="$(RC_ARCHS)" RC_OS="$(RC_OS)"			\
		DSTROOT=$$DSTROOT					\
		SRCROOT=$(SRCROOT)/libstuff				\
		OBJROOT=$(OBJROOT)/libstuff				\
		SYMROOT=$(SYMROOT)/libstuff $@) || exit 1;		\
	    echo =========== $(MAKE) all for libstuff =============;	\
	    (cd libstuff; $(MAKE) "RC_CFLAGS=$$SED_RC_CFLAGS"		\
		RC_ARCHS="$(RC_ARCHS)" RC_OS="$(RC_OS)"			\
		OLD_LIBKLD="$(OLD_LIBKLD)" 				\
		DSTROOT=$$DSTROOT					\
		SRCROOT=$(SRCROOT)/libstuff				\
		OBJROOT=$(OBJROOT)/libstuff				\
		SYMROOT=$(SYMROOT)/libstuff all) || exit 1;		\
	    if [ $(BUILD_DYLIBS) = "YES" ];				\
	    then							\
	        echo =========== $(MAKE) $@ for libmacho =============;	\
	        (cd libmacho; $(MAKE) RC_CFLAGS="$(RC_CFLAGS)"		\
		    RC_ARCHS="$(RC_ARCHS)" RC_OS="$(RC_OS)"		\
		    OLD_LIBKLD="$(OLD_LIBKLD)"				\
		    DSTROOT=$$DSTROOT					\
		    SRCROOT=$(SRCROOT)/libmacho				\
		    OBJROOT=$(OBJROOT)/libmacho				\
		    SYMROOT=$(SYMROOT)/libmacho $@) || exit 1;		\
	    fi;								\
	    echo =========== $(MAKE) $@ for ld =============;		\
	    (cd ld; $(MAKE) "RC_CFLAGS=$$SED_RC_CFLAGS"			\
		RC_ARCHS="$(RC_ARCHS)" RC_OS="$(RC_OS)"			\
		OLD_LIBKLD="$(OLD_LIBKLD)"				\
		DSTROOT=$$DSTROOT					\
		SRCROOT=$(SRCROOT)/ld					\
		OBJROOT=$(OBJROOT)/ld					\
		SYMROOT=$(SYMROOT)/ld $@) || exit 1;			\
	    echo =========== $(MAKE) $@ for misc =============;	\
	    (cd misc; $(MAKE) "RC_CFLAGS=$(RC_CFLAGS)"			\
		RC_ARCHS="$(RC_ARCHS)" RC_OS="$(RC_OS)"			\
		TRIE="$(TRIE)" LTO="$(LTO)"				\
		DSTROOT=$$DSTROOT					\
		SRCROOT=$(SRCROOT)/misc					\
		OBJROOT=$(OBJROOT)/misc					\
		SYMROOT=$(SYMROOT)/misc $@) || exit 1;			\
	    echo =========== $(MAKE) $@ for cbtlibs =============;	\
	    (cd cbtlibs; $(MAKE) "RC_CFLAGS=$(RC_CFLAGS)"		\
		RC_ARCHS="$(RC_ARCHS)" RC_OS="$(RC_OS)"			\
		DSTROOT=$$DSTROOT					\
		SRCROOT=$(SRCROOT)/cbtlibs				\
		OBJROOT=$(OBJROOT)/cbtlibs				\
		SYMROOT=$(SYMROOT)/cbtlibs $@) || exit 1;		\
	else								\
	    CWD=`pwd`; cd "$(DSTROOT)"; DSTROOT=`pwd`; cd "$$CWD";	\
	    SED_RC_CFLAGS=`echo "$(RC_CFLAGS)" | sed 's/-arch ppc64//'  \
 		| sed 's/-arch x86_64//'`;				\
	    echo =========== $(MAKE) $@ for libstuff =============;	\
	    (cd libstuff; $(MAKE) "RC_CFLAGS=$(RC_CFLAGS)"		\
		RC_ARCHS="$(RC_ARCHS)" RC_OS="$(RC_OS)"			\
		DSTROOT=$$DSTROOT $@) || exit 1;			\
	    echo =========== $(MAKE) all for libstuff =============;	\
	    (cd libstuff; $(MAKE) "RC_CFLAGS=$$SED_RC_CFLAGS"		\
		RC_ARCHS="$(RC_ARCHS)" RC_OS="$(RC_OS)"			\
		DSTROOT=$$DSTROOT all) || exit 1;			\
	    if [ $(BUILD_DYLIBS) = "YES" ];				\
	    then							\
	        echo =========== $(MAKE) $@ for libmacho =============;	\
	        (cd libmacho; $(MAKE) RC_CFLAGS="$(RC_CFLAGS)"		\
		    RC_ARCHS="$(RC_ARCHS)" RC_OS="$(RC_OS)"		\
		    DSTROOT=$$DSTROOT $@) || exit 1;			\
	    fi;								\
	    echo =========== $(MAKE) $@ for ld =============;		\
	    (cd ld; $(MAKE) "RC_CFLAGS=$$SED_RC_CFLAGS"			\
		RC_ARCHS="$(RC_ARCHS)" RC_OS="$(RC_OS)"			\
		OLD_LIBKLD="$(OLD_LIBKLD)"				\
		DSTROOT=$$DSTROOT $@) || exit 1;			\
	    echo =========== $(MAKE) $@ for misc =============;		\
	    (cd misc; $(MAKE) "RC_CFLAGS=$(RC_CFLAGS)"			\
		RC_ARCHS="$(RC_ARCHS)" RC_OS="$(RC_OS)"			\
		TRIE="$(TRIE)" LTO="$(LTO)"				\
		DSTROOT=$$DSTROOT $@) || exit 1;			\
	    (cd cbtlibs; $(MAKE) "RC_CFLAGS=$(RC_CFLAGS)"		\
		RC_ARCHS="$(RC_ARCHS)" RC_OS="$(RC_OS)"			\
		DSTROOT=$$DSTROOT $@) || exit 1;			\
	fi

install_dev_tools:
	@ export RC_FORCEHDRS=YES;					\
	$(MAKE) RC_CFLAGS="$(RC_CFLAGS)"				\
		RC_ARCHS="$(RC_ARCHS)"					\
		RC_OS="$(RC_OS)"					\
		INSTALL_LOCATION=$(INSTALL_LOCATION)			\
		DSTROOT=$(DSTROOT)					\
		SRCROOT=$(SRCROOT)					\
		OBJROOT=$(OBJROOT)					\
		SYMROOT=$(SYMROOT)					\
		EFITOOLS="$(EFITOOLS)" TRIE="$(TRIE)"			\
		LTO="$(LTO)" install

install_os_tools: installhdrs
	@if [ $(SRCROOT) ];						\
	then								\
	    CWD=`pwd`; cd "$(DSTROOT)"; DSTROOT=`pwd`; cd "$$CWD";	\
	    echo =========== $(MAKE) all for libstuff =============;	\
	    (cd libstuff; $(MAKE) "RC_CFLAGS=$(RC_CFLAGS)"		\
		RC_ARCHS="$(RC_ARCHS)" RC_OS="$(RC_OS)"			\
		OLD_LIBKLD="$(OLD_LIBKLD)" 				\
		DSTROOT=$$DSTROOT					\
		SRCROOT=$(SRCROOT)/libstuff				\
		OBJROOT=$(OBJROOT)/libstuff				\
		SYMROOT=$(SYMROOT)/libstuff all) || exit 1;		\
	    echo =========== $(MAKE) $@ for misc =============;	\
	    (cd misc; $(MAKE) "RC_CFLAGS=$(RC_CFLAGS)"			\
		RC_ARCHS="$(RC_ARCHS)" RC_OS="$(RC_OS)"			\
		TRIE="$(TRIE)" LTO="$(LTO)"				\
		DSTROOT=$$DSTROOT					\
		SRCROOT=$(SRCROOT)/misc					\
		OBJROOT=$(OBJROOT)/misc					\
		SYMROOT=$(SYMROOT)/misc $@) || exit 1;			\
	    echo =========== $(MAKE) $@ for man =============;	\
	    (cd man; $(MAKE) "RC_CFLAGS=$(RC_CFLAGS)"			\
		RC_ARCHS="$(RC_ARCHS)" RC_OS="$(RC_OS)"			\
		DSTROOT=$$DSTROOT					\
		SRCROOT=$(SRCROOT)/man					\
		OBJROOT=$(OBJROOT)/man					\
		SYMROOT=$(SYMROOT)/man $@) || exit 1;			\
	else								\
	    CWD=`pwd`; cd "$(DSTROOT)"; DSTROOT=`pwd`; cd "$$CWD";	\
	    echo =========== $(MAKE) all for libstuff =============;	\
	    (cd libstuff; $(MAKE) "RC_CFLAGS=$(RC_CFLAGS)"		\
		RC_ARCHS="$(RC_ARCHS)" RC_OS="$(RC_OS)"			\
		DSTROOT=$$DSTROOT all) || exit 1;			\
	    echo =========== $(MAKE) $@ for misc =============;		\
	    (cd misc; $(MAKE) "RC_CFLAGS=$(RC_CFLAGS)"			\
		RC_ARCHS="$(RC_ARCHS)" RC_OS="$(RC_OS)"			\
		TRIE="$(TRIE)" LTO="$(LTO)"				\
		DSTROOT=$$DSTROOT $@) || exit 1;			\
	    echo =========== $(MAKE) $@ for man =============;		\
	    (cd man; $(MAKE) "RC_CFLAGS=$(RC_CFLAGS)"			\
		RC_ARCHS="$(RC_ARCHS)" RC_OS="$(RC_OS)"			\
		DSTROOT=$$DSTROOT $@) || exit 1;			\
	fi

installsrc: SRCROOT
	$(MKDIRS) $(SRCROOT)
	cp Makefile APPLE_LICENSE PB.project $(SRCROOT)
	for i in `echo $(INSTALLSRC_SUBDIRS)`; \
	  do \
		echo =========== $(MAKE) $@ for $$i =============;	\
		(cd $$i; $(MAKE) SRCROOT=$$SRCROOT/$$i 			\
		 EFITOOLS="$(EFITOOLS)" $@) || exit 1;	\
	  done

installGASsrc: SRCROOT
	$(MKDIRS) $(SRCROOT)
	cp Makefile $(SRCROOT)
	@for i in as libstuff include ; \
	  do \
		echo =========== $(MAKE) $@ for $$i =============;	\
		(cd $$i; $(MAKE) SRCROOT=$$SRCROOT/$$i $@) || exit 1;	\
	  done

fromGASsrc:
	@CWD=`pwd`; cd "$(DSTROOT)"; DSTROOT=`pwd`; cd "$$CWD";		\
	echo =========== $(MAKE) fromGASsrc for libstuff =============;	\
	(cd libstuff; $(MAKE) RC_CFLAGS="$(RC_CFLAGS)"			\
	    RC_ARCHS="$(RC_ARCHS)" RC_OS="$(RC_OS)"			\
	    DSTROOT=$$DSTROOT fromGASsrc) || exit 1;			\
	echo =========== $(MAKE) appc_build for as =============;	\
	(cd as; $(MAKE) RC_CFLAGS="$(RC_CFLAGS)"			\
	    RC_ARCHS="$(RC_ARCHS)" RC_OS="$(RC_OS)"			\
	    DSTROOT=$$DSTROOT appc_build) || exit 1;			\

installhdrs: $(DSTROOT)
	@if [ $(SRCROOT) ];						\
	then								\
	    projName=`basename $(SRCROOT) | sed 's/-[0-9.]*//'`;	\
	    if [ "$$projName" = cctools -a $(RC_OS) = macos ] &&	\
	       [ "$(RC_FORCEHDRS)" != "YES" ];				\
	    then							\
	    	echo === cctools does not install headers for macos ===;\
	    else							\
		(cd include; $(MAKE) DSTROOT=$(DSTROOT)			\
			RC_OS="$(RC_OS)" install) || exit 1;		\
	    fi;								\
	else								\
	    (cd include; $(MAKE) DSTROOT=$(DSTROOT) RC_OS=$(RC_OS) 	\
		install) || exit 1;					\
	fi

$(DSTROOT):
	$(MKDIRS) $@

SRCROOT:
	@if [ -n "${$@}" ]; \
	then \
		exit 0; \
	else \
		echo Must define $@; \
		exit 1; \
	fi
