#ifndef _QSORT_R_H
#define _QSORT_R_H
#include <sys/types.h>

#if defined(__cplusplus)
extern "C" {
#endif
void qsort_r_local(void *base, size_t nmemb, size_t size, void *thunk, int (*compar)(void *, const void *, const void *));
#if defined(__cplusplus)
};
#endif

#endif
