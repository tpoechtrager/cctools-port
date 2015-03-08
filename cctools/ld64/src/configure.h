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

#ifdef __GLIBCXX__
#include <algorithm>
#endif

#define CPU_SUBTYPE_X86_ALL     ((cpu_subtype_t)3)

#define SUPPORT_ARCH_armv4t 1
#define SUPPORT_ARCH_armv5 1
#define SUPPORT_ARCH_armv6 1
#define SUPPORT_ARCH_armv7 1
#define SUPPORT_ARCH_armv7f 1
#define SUPPORT_ARCH_armv7k 1
#define SUPPORT_ARCH_armv7s 1
#define SUPPORT_ARCH_armv6m 1
#define SUPPORT_ARCH_armv7m 1
#define SUPPORT_ARCH_armv7em 1
#define SUPPORT_ARCH_armv8 1
#define SUPPORT_ARCH_arm64 1
#define SUPPORT_ARCH_arm64v8 1
#define SUPPORT_ARCH_i386 1
#define SUPPORT_ARCH_x86_64 1
#define SUPPORT_ARCH_x86_64h 1

#define ALL_SUPPORTED_ARCHS  "armv4t armv5 armv6 armv7 armv7f armv7k armv7s armv6m armv7m armv7em armv8 arm64 arm64v8 i386 x86_64 x86_64h"

#define HW_NCPU      3
#define CTL_HW      6

#undef ARG_MAX
#define ARG_MAX       131072

#endif
