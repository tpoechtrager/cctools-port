#ifndef _CONFIGURE_H
#define _CONFIGURE_H
#include <sys/param.h>
#include <limits.h>
#include <unistd.h>
#include <stddef.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>

#include "strlcat.h"
#include "strlcpy.h"
#include "helper.h"

#define CPU_SUBTYPE_X86_ALL     ((cpu_subtype_t)3)


#define SUPPORT_ARCH_i386  1
#define SUPPORT_ARCH_x86_64  1
#define SUPPORT_ARCH_armv4t  1
#define SUPPORT_ARCH_armv5  1
#define SUPPORT_ARCH_armv6  1
#define SUPPORT_ARCH_armv7  1
#define SUPPORT_ARCH_armv7f  1
#define SUPPORT_ARCH_armv7k  1
#define SUPPORT_ARCH_armv7s  1
#define ALL_SUPPORTED_ARCHS  "i386 x86_64 armv4t armv5 armv6 armv7 armv7f armv7k armv7s"

#define HW_NCPU      3
#define CTL_HW      6

#undef ARG_MAX
#define ARG_MAX       131072

#endif
