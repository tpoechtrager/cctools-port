#if defined(__CYGWIN__)
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
#elif defined(__linux__)
#include <linux/sysctl.h>
#else
#include_next <sys/sysctl.h>
#endif
