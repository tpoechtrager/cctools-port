SUBDIRS=libstuff ar as misc libobjc2 otool ld64 $(LD_CLASSIC)
ACLOCAL_AMFLAGS = -I m4

install-exec-hook:
	(test -f tmp/install_libcxx.sh && mkdir -p $(DESTDIR)$(libdir) && ./tmp/install_libcxx.sh $(DESTDIR)$(libdir)) || true
