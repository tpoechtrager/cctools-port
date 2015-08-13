libexec_PROGRAMS = arm-as

arm_as_LDADD =  \
        $(top_builddir)/libstuff/libstuff.la

arm_as_CFLAGS = -I$(top_srcdir)/include -I$(top_srcdir)/include/foreign -I$(top_srcdir)/libstuff -I$(top_srcdir)/as $(WARNINGS) $(LTO_DEF) -DNeXT_MOD -DASLIBEXECDIR="\"$(ASLIBEXECDIR)/\"" -D__DARWIN_UNIX03 -DARM $(ENDIAN_FLAG)

arm_as_SOURCES =  ../app.c ../as.c ../atof-generic.c ../atof-ieee.c ../expr.c ../fixes.c ../flonum-const.c \
         ../flonum-copy.c ../flonum-mult.c ../frags.c ../hash.c ../hex-value.c ../input-file.c \
         ../input-scrub.c ../layout.c ../messages.c ../obstack.c ../read.c ../sections.c \
         ../symbols.c ../write_object.c ../xmalloc.c ../dwarf2dbg.c ../arm.c
install-exec-hook:
	mkdir -p $(DESTDIR)$(libexecdir)/as/arm
	mv $(DESTDIR)$(libexecdir)/*arm-as* $(DESTDIR)$(libexecdir)/as/arm/as
