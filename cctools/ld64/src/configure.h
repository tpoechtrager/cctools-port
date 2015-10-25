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

#define SUPPORT_ARCH_arm_any 1
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
#define SUPPORT_ARCH_ppc 1
#define SUPPORT_ARCH_ppc750 1
#define SUPPORT_ARCH_ppc7400 1
#define SUPPORT_ARCH_ppc7450 1
#define SUPPORT_ARCH_ppc970 1
#define SUPPORT_ARCH_ppc64 1

#define SUPPORT_APPLE_TV 1
#define ALL_SUPPORTED_ARCHS  "armv4t armv5 armv6 armv7 armv7f armv7k armv7s armv6m armv7m armv7em armv8 arm64 arm64v8 i386 x86_64 x86_64h ppc ppc750 ppc7400 ppc7450 ppc970 ppc64 (tvOS)"

#define BITCODE_XAR_VERSION "1.0"

#ifndef HW_NCPU
#define HW_NCPU 3
#endif

#ifndef CTL_HW
#define CTL_HW  6
#endif

#ifndef ARG_MAX
#define ARG_MAX 31072
#endif

#endif
