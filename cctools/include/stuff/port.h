#ifndef DISABLE_CLANG_AS
char *find_clang();
#endif /* !DISABLE_CLANG_AS */

#ifndef HAVE_UTIMENS
#include <time.h>
int utimens(const char *path, const struct timespec times[2]);
#endif /* !HAVE_UTIMENS */

#ifndef HAVE_STRMODE
void strmode(/* mode_t */ int mode, char *p);
#endif /* !__APPLE__ */

#ifndef HAVE_REALLOCF
void *reallocf(void *ptr, size_t size);
#elif defined(HAVE_BSD_STDLIB_H)
#include <bsd/stdlib.h>
#endif /* !HAVE_REALLOCF */

int asprintf(char **strp, const char *fmt, ...);
