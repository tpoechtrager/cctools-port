libexec_PROGRAMS = i386-as

i386_as_LDADD =  \
        $(top_srcdir)/libstuff/libstuff.la

i386_as_CFLAGS = -I$(top_srcdir)/include -I$(top_srcdir)/include/foreign -I$(top_srcdir)/libstuff -I$(top_srcdir)/as $(WARNINGS) $(LTO_DEFS) -DNeXT_MOD -DASLIBEXECDIR="\"$(ASLIBEXECDIR)/\"" -D__DARWIN_UNIX03  -DI386 -Di486 -Di586 -Di686

i386_as_SOURCES =  ../app.c ../as.c ../atof-generic.c ../atof-ieee.c ../expr.c ../fixes.c ../flonum-const.c \
         ../flonum-copy.c ../flonum-mult.c ../frags.c ../hash.c ../hex-value.c ../input-file.c \
         ../input-scrub.c ../layout.c ../messages.c ../obstack.c ../read.c ../sections.c \
         ../symbols.c ../write_object.c ../xmalloc.c ../dwarf2dbg.c ../i386.c
install-exec-hook:
	mkdir -p $(DESTDIR)/$(libexecdir)/as/i386
	mv $(DESTDIR)/$(libexecdir)/*i386-as $(DESTDIR)/$(libexecdir)/as/i386/as
