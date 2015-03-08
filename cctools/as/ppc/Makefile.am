libexec_PROGRAMS = ppc-as

ppc_as_LDADD =  \
        $(top_srcdir)/libstuff/libstuff.la

ppc_as_CFLAGS = -I$(top_srcdir)/include -I$(top_srcdir)/include/foreign -I$(top_srcdir)/libstuff -I$(top_srcdir)/as $(WARNINGS) $(LTO_DEF) -DNeXT_MOD -DASLIBEXECDIR="\"$(ASLIBEXECDIR)/\"" -D__DARWIN_UNIX03  -DPPC $(ENDIAN_FLAG)

ppc_as_SOURCES =  ../app.c ../as.c ../atof-generic.c ../atof-ieee.c ../expr.c ../fixes.c ../flonum-const.c \
         ../flonum-copy.c ../flonum-mult.c ../frags.c ../hash.c ../hex-value.c ../input-file.c \
         ../input-scrub.c ../layout.c ../messages.c ../obstack.c ../read.c ../sections.c \
         ../symbols.c ../write_object.c ../xmalloc.c ../dwarf2dbg.c ../ppc.c
install-exec-hook:
	mkdir -p $(DESTDIR)$(libexecdir)/as/ppc
	mv $(DESTDIR)$(libexecdir)/*ppc-as* $(DESTDIR)$(libexecdir)/as/ppc/as
