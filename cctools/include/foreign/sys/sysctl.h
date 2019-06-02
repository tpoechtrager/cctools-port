#if defined(__APPLE__) || defined(__GLIBC__) || defined(__FreeBSD__)
#include_next <sys/sysctl.h>
#else
#ifndef __SYSCTL_H__
#define __SYSCTL_H__

#include <errno.h>
#include <stdio.h> /* stderr */

#define CTL_KERN 1
#define KERN_OSRELEASE 2

static inline
int sysctl(const int *name, u_int namelen, void *oldp,	size_t *oldlenp,
           const void *newp, size_t newlen)
{
    /* fprintf(stderr, "sysctl() not implented\n"); */
    errno = EINVAL;
    return -1;
}
#endif /* __SYSCTL_H__ */
#endif /* __APPLE__ || __GLIBC__ */
