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

#include <sys/param.h>	/* MAXPATHLEN */
#include <stdlib.h> /* system(), free() & getenv() */
#include <stdio.h> /* snprintf() & fprintf() */

int asprintf(char **strp, const char *fmt, ...);

char *find_executable(const char *name);
