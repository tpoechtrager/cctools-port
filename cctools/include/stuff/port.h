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

#define ARCHS_CONTAIN(archs, narchs, arch_wanted) \
({ \
	int result; \
	result = 0; \
	do { \
		uint32_t i; \
		for (i = 0; i < narchs; i++) { \
			cpu_type_t cputype; \
			if (archs[i].object == NULL) { \
				continue; \
			} \
			if (archs[i].object->mh != NULL) { \
				cputype = archs[i].object->mh->cputype; \
			} else { \
				cputype = archs[i].object->mh64->cputype; \
			} \
			if (cputype == arch_wanted) { \
				result = 1; \
				break; \
			} \
		} \
	} while (0); \
	result; \
})

#define FAKE_SIGN_BINARY(filename, verbose) \
do { \
	const char *codesign_debug; \
	char codesign_command[MAXPATHLEN]; \
	char *codesign; \
	codesign_debug = getenv("CODESIGN_DEBUG"); \
	codesign = find_executable("codesign"); \
	if (!codesign) { \
		if (codesign_debug) { \
			fprintf(stderr, "[cctools-port]: " \
			                "cannot find 'codesign' executable in PATH\n", codesign_command); \
		} \
		break; \
	} \
	snprintf(codesign_command, sizeof(codesign_command), "%s -s - -f %s", codesign, filename); \
	if (codesign_debug || verbose) { \
		fprintf(stderr, "[cctools-port]: " \
		                "generating fake signature for '%s'\n", filename); \
		if (codesign_debug) { \
			fprintf(stderr, "[cctools-port]: %s\n", codesign_command); \
		} \
	} \
	system(codesign_command); \
	free(codesign); \
} while (0)

#define FAKE_SIGN_ARM_BINARY(archs, narchs, filename) \
do { \
	uint32_t i; \
	enum bool is_archive; \
	is_archive = FALSE; \
	if (getenv("NO_CODESIGN")) { \
		break; \
	} \
	for (i = 0; i < narchs; i++) { \
		if (archs[i].type == OFILE_ARCHIVE) { \
			is_archive = TRUE; \
			break; \
		} \
	} \
	if (!is_archive && \
	    (ARCHS_CONTAIN(archs, narchs, CPU_TYPE_ARM) || \
	     ARCHS_CONTAIN(archs, narchs, CPU_TYPE_ARM64) || \
	     ARCHS_CONTAIN(archs, narchs, CPU_TYPE_ARM64_32))) { \
	    FAKE_SIGN_BINARY(filename, 1); \
	} \
} while (0)
