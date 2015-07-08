/*
 * Copyright (c) 1999 Apple Computer, Inc. All rights reserved.
 *
 * @APPLE_LICENSE_HEADER_START@
 * 
 * This file contains Original Code and/or Modifications of Original Code
 * as defined in and that are subject to the Apple Public Source License
 * Version 2.0 (the 'License'). You may not use this file except in
 * compliance with the License. Please obtain a copy of the License at
 * http://www.opensource.apple.com/apsl/ and read it before using this
 * file.
 * 
 * The Original Code and all software distributed under the License are
 * distributed on an 'AS IS' basis, WITHOUT WARRANTY OF ANY KIND, EITHER
 * EXPRESS OR IMPLIED, AND APPLE HEREBY DISCLAIMS ALL SUCH WARRANTIES,
 * INCLUDING WITHOUT LIMITATION, ANY WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE, QUIET ENJOYMENT OR NON-INFRINGEMENT.
 * Please see the License for the specific language governing rights and
 * limitations under the License.
 * 
 * @APPLE_LICENSE_HEADER_END@
 */
#ifndef RLD
#include "stdio.h"
#endif /* !defined(RLD) */
#include "stdlib.h"
#include "string.h"
#include <mach/mach.h>
#include "stuff/openstep_mach.h"
#include "stuff/arch.h"
#include "stuff/allocate.h"

/*
 * The array of all currently know architecture flags (terminated with an entry
 * with all zeros).  Pointer to this returned with get_arch_flags().
 */
#ifdef __DYNAMIC__
static struct arch_flag arch_flags[] = {
#else
static const struct arch_flag arch_flags[] = {
#endif
    { "any",	CPU_TYPE_ANY,	  CPU_SUBTYPE_MULTIPLE },
    { "little",	CPU_TYPE_ANY,	  CPU_SUBTYPE_LITTLE_ENDIAN },
    { "big",	CPU_TYPE_ANY,	  CPU_SUBTYPE_BIG_ENDIAN },

/* 64-bit Mach-O architectures */

    /* architecture families */
    { "ppc64",     CPU_TYPE_POWERPC64, CPU_SUBTYPE_POWERPC_ALL },
    { "x86_64",    CPU_TYPE_X86_64, CPU_SUBTYPE_X86_64_ALL },
    { "x86_64h",   CPU_TYPE_X86_64, CPU_SUBTYPE_X86_64_H },
    { "arm64",     CPU_TYPE_ARM64,     CPU_SUBTYPE_ARM64_ALL },
    /* specific architecture implementations */
    { "ppc970-64", CPU_TYPE_POWERPC64, CPU_SUBTYPE_POWERPC_970 },

/* 32-bit Mach-O architectures */

    /* architecture families */
    { "ppc",    CPU_TYPE_POWERPC, CPU_SUBTYPE_POWERPC_ALL },
    { "i386",   CPU_TYPE_I386,    CPU_SUBTYPE_I386_ALL },
    { "m68k",   CPU_TYPE_MC680x0, CPU_SUBTYPE_MC680x0_ALL },
    { "hppa",   CPU_TYPE_HPPA,    CPU_SUBTYPE_HPPA_ALL },
    { "sparc",	CPU_TYPE_SPARC,   CPU_SUBTYPE_SPARC_ALL },
    { "m88k",   CPU_TYPE_MC88000, CPU_SUBTYPE_MC88000_ALL },
    { "i860",   CPU_TYPE_I860,    CPU_SUBTYPE_I860_ALL },
    { "veo",    CPU_TYPE_VEO,     CPU_SUBTYPE_VEO_ALL },
    { "arm",    CPU_TYPE_ARM,     CPU_SUBTYPE_ARM_ALL },
    /* specific architecture implementations */
    { "ppc601", CPU_TYPE_POWERPC, CPU_SUBTYPE_POWERPC_601 },
    { "ppc603", CPU_TYPE_POWERPC, CPU_SUBTYPE_POWERPC_603 },
    { "ppc603e",CPU_TYPE_POWERPC, CPU_SUBTYPE_POWERPC_603e },
    { "ppc603ev",CPU_TYPE_POWERPC,CPU_SUBTYPE_POWERPC_603ev },
    { "ppc604", CPU_TYPE_POWERPC, CPU_SUBTYPE_POWERPC_604 },
    { "ppc604e",CPU_TYPE_POWERPC, CPU_SUBTYPE_POWERPC_604e },
    { "ppc750", CPU_TYPE_POWERPC, CPU_SUBTYPE_POWERPC_750 },
    { "ppc7400",CPU_TYPE_POWERPC, CPU_SUBTYPE_POWERPC_7400 },
    { "ppc7450",CPU_TYPE_POWERPC, CPU_SUBTYPE_POWERPC_7450 },
    { "ppc970", CPU_TYPE_POWERPC, CPU_SUBTYPE_POWERPC_970 },
    { "i486",   CPU_TYPE_I386,    CPU_SUBTYPE_486 },
    { "i486SX", CPU_TYPE_I386,    CPU_SUBTYPE_486SX },
    { "pentium",CPU_TYPE_I386,    CPU_SUBTYPE_PENT }, /* same as i586 */
    { "i586",   CPU_TYPE_I386,    CPU_SUBTYPE_586 },
    { "pentpro", CPU_TYPE_I386, CPU_SUBTYPE_PENTPRO }, /* same as i686 */
    { "i686",   CPU_TYPE_I386, CPU_SUBTYPE_PENTPRO },
    { "pentIIm3",CPU_TYPE_I386, CPU_SUBTYPE_PENTII_M3 },
    { "pentIIm5",CPU_TYPE_I386, CPU_SUBTYPE_PENTII_M5 },
    { "pentium4",CPU_TYPE_I386, CPU_SUBTYPE_PENTIUM_4 },
    { "m68030", CPU_TYPE_MC680x0, CPU_SUBTYPE_MC68030_ONLY },
    { "m68040", CPU_TYPE_MC680x0, CPU_SUBTYPE_MC68040 },
    { "hppa7100LC", CPU_TYPE_HPPA,  CPU_SUBTYPE_HPPA_7100LC },
    { "veo1",   CPU_TYPE_VEO,     CPU_SUBTYPE_VEO_1 },
    { "veo2",   CPU_TYPE_VEO,     CPU_SUBTYPE_VEO_2 },
    { "veo3",   CPU_TYPE_VEO,     CPU_SUBTYPE_VEO_3 },
    { "veo4",   CPU_TYPE_VEO,     CPU_SUBTYPE_VEO_4 },
    { "armv4t", CPU_TYPE_ARM,     CPU_SUBTYPE_ARM_V4T},
    { "armv5",  CPU_TYPE_ARM,     CPU_SUBTYPE_ARM_V5TEJ},
    { "xscale", CPU_TYPE_ARM,     CPU_SUBTYPE_ARM_XSCALE},
    { "armv6",  CPU_TYPE_ARM,     CPU_SUBTYPE_ARM_V6 },
    { "armv6m", CPU_TYPE_ARM,     CPU_SUBTYPE_ARM_V6M },
    { "armv7",  CPU_TYPE_ARM,     CPU_SUBTYPE_ARM_V7 },
    { "armv7f", CPU_TYPE_ARM,     CPU_SUBTYPE_ARM_V7F },
    { "armv7s", CPU_TYPE_ARM,     CPU_SUBTYPE_ARM_V7S },
    { "armv7k", CPU_TYPE_ARM,     CPU_SUBTYPE_ARM_V7K },
    { "armv7m", CPU_TYPE_ARM,     CPU_SUBTYPE_ARM_V7M },
    { "armv7em", CPU_TYPE_ARM,    CPU_SUBTYPE_ARM_V7EM },
    { "arm64v8",CPU_TYPE_ARM64,   CPU_SUBTYPE_ARM64_V8 },
    { NULL,	0,		  0 }
};

#ifndef RLD
/*
 * get_arch_from_flag() is passed a name of an architecture flag and returns
 * zero if that flag is not known and non-zero if the flag is known.
 * If the pointer to the arch_flag is not NULL it is filled in with the
 * arch_flag struct that matches the name.
 */
__private_extern__
int
get_arch_from_flag(
char *name,
struct arch_flag *arch_flag)
{
    uint32_t i;

	for(i = 0; arch_flags[i].name != NULL; i++){
	    if(strcmp(arch_flags[i].name, name) == 0){
		if(arch_flag != NULL)
		    *arch_flag = arch_flags[i];
		return(1);
	    }
	}
	if(arch_flag != NULL)
	    memset(arch_flag, '\0', sizeof(struct arch_flag));
	return(0);
}

/*
 * get_arch_flags() returns a pointer to an array of all currently know
 * architecture flags (terminated with an entry with all zeros).
 */
__private_extern__
const struct arch_flag *
get_arch_flags(
void)
{
	return(arch_flags);
}
#endif /* !defined(RLD) */

/*
 * get_arch_name_from_types() returns the name of the architecture for the
 * specified cputype and cpusubtype if known.  If unknown it returns a pointer
 * to the an allocated string "cputype X cpusubtype Y" where X and Y are decimal
 * values.
 */
__private_extern__
const char *
get_arch_name_from_types(
cpu_type_t cputype,
cpu_subtype_t cpusubtype)
{
    uint32_t i;
    char *p;

	for(i = 0; arch_flags[i].name != NULL; i++){
	    if(arch_flags[i].cputype == cputype &&
	       (arch_flags[i].cpusubtype & ~CPU_SUBTYPE_MASK) ==
	       (cpusubtype & ~CPU_SUBTYPE_MASK))
		return(arch_flags[i].name);
	}
#ifndef RLD
	p = savestr("cputype 1234567890 cpusubtype 1234567890");
	if(p != NULL)
	    sprintf(p, "cputype %u cpusubtype %u", cputype,
		    cpusubtype & ~CPU_SUBTYPE_MASK);
#else
	/* there is no sprintf() in the rld kernel API's */
	p = savestr("cputype ?? cpusubtype ??");
#endif
	return(p);
}

/*
 * get_arch_family_from_cputype() returns the family architecture for the
 * specified cputype if known.  If unknown it returns NULL.
 */
__private_extern__
const struct arch_flag *
get_arch_family_from_cputype(
cpu_type_t cputype)
{
    uint32_t i;

	for(i = 0; arch_flags[i].name != NULL; i++){
	    if(arch_flags[i].cputype == cputype)
		return(arch_flags + i);
	}
	return(NULL);
}

/*
 * get_byte_sex_from_flag() returns the byte sex of the architecture for the
 * specified cputype and cpusubtype if known.  If unknown it returns
 * UNKNOWN_BYTE_SEX.  If the bytesex can be determined directly as in the case
 * of reading a magic number from a file that should be done and this routine
 * should not be used as it could be out of date.
 */
__private_extern__
enum byte_sex
get_byte_sex_from_flag(
const struct arch_flag *flag)
{
   if(flag->cputype == CPU_TYPE_MC680x0 ||
      flag->cputype == CPU_TYPE_MC88000 ||
      flag->cputype == CPU_TYPE_POWERPC ||
      flag->cputype == CPU_TYPE_POWERPC64 ||
      flag->cputype == CPU_TYPE_HPPA ||
      flag->cputype == CPU_TYPE_SPARC ||
      flag->cputype == CPU_TYPE_I860 ||
      flag->cputype == CPU_TYPE_VEO)
        return BIG_ENDIAN_BYTE_SEX;
    else if(flag->cputype == CPU_TYPE_I386 ||
	    flag->cputype == CPU_TYPE_X86_64 ||
	    flag->cputype == CPU_TYPE_ARM64 ||
	    flag->cputype == CPU_TYPE_ARM)
        return LITTLE_ENDIAN_BYTE_SEX;
    else
        return UNKNOWN_BYTE_SEX;
}

#ifndef RLD
/*
 * get_stack_direction_from_flag() returns the direction the stack grows as
 * either positive (+1) or negative (-1) of the architecture for the
 * specified cputype and cpusubtype if known.  If unknown it returns 0.
 */
__private_extern__
int
get_stack_direction_from_flag(
const struct arch_flag *flag)
{
   if(flag->cputype == CPU_TYPE_MC680x0 ||
      flag->cputype == CPU_TYPE_MC88000 ||
      flag->cputype == CPU_TYPE_POWERPC ||
      flag->cputype == CPU_TYPE_I386 ||
      flag->cputype == CPU_TYPE_SPARC ||
      flag->cputype == CPU_TYPE_I860 ||
      flag->cputype == CPU_TYPE_VEO ||
      flag->cputype == CPU_TYPE_ARM)
        return(-1);
    else if(flag->cputype == CPU_TYPE_HPPA)
        return(+1);
    else
        return(0);
}

/*
 * get_stack_addr_from_flag() returns the default starting address of the user
 * stack.  This should be in the header file <bsd/XXX/vmparam.h> as USRSTACK.
 * Since some architectures have come and gone and come back and because you
 * can't include all of these headers in one source the constants have been
 * copied here.
 */
__private_extern__
uint64_t
get_stack_addr_from_flag(
const struct arch_flag *flag)
{
    switch(flag->cputype){
    case CPU_TYPE_MC680x0:
	return(0x04000000);
    case CPU_TYPE_MC88000:
	return(0xffffe000);
    case CPU_TYPE_POWERPC:
    case CPU_TYPE_VEO:
    case CPU_TYPE_I386:
	return(0xc0000000);
    case CPU_TYPE_ARM:
	return(0x30000000);
    case CPU_TYPE_SPARC:
	return(0xf0000000);
    case CPU_TYPE_I860:
	return(0);
    case CPU_TYPE_HPPA:
	return(0xc0000000-0x04000000);
    case CPU_TYPE_POWERPC64:
	return(0x7ffff00000000LL);
    case CPU_TYPE_X86_64:
	return(0x7fff5fc00000LL);
    default:
	return(0);
    }
}

/*
 * get_stack_size_from_flag() returns the default size of the userstack.  This
 * should be in the header file <bsd/XXX/vmparam.h> as MAXSSIZ. Since some
 * architectures have come and gone and come back, you can't include all of
 * these headers in one source and some of the constants covered the whole
 * address space the common value of 64meg was chosen.
 */
__private_extern__
uint32_t
get_stack_size_from_flag(
const struct arch_flag *flag)
{
#ifdef __MWERKS__
    const struct arch_flag *dummy;
	dummy = flag;
#endif

    return(64*1024*1024);
}
#endif /* !defined(RLD) */

/*
 * get_segalign_from_flag() returns the default segment alignment (page size).
 */
__private_extern__
uint32_t
get_segalign_from_flag(
const struct arch_flag *flag)
{
        if(flag->cputype == CPU_TYPE_ARM ||
           flag->cputype == CPU_TYPE_ARM64)
	    return(0x4000); /* 16K */

	if(flag->cputype == CPU_TYPE_POWERPC ||
	   flag->cputype == CPU_TYPE_POWERPC64 ||
	   flag->cputype == CPU_TYPE_VEO ||
	   flag->cputype == CPU_TYPE_I386 ||
	   flag->cputype == CPU_TYPE_X86_64)
	    return(0x1000); /* 4K */
	else
	    return(0x2000); /* 8K */
}

/*
 * get_segprot_from_flag() returns the default segment protection.
 */
__private_extern__
vm_prot_t
get_segprot_from_flag(
const struct arch_flag *flag)
{
	if(flag->cputype == CPU_TYPE_I386)
	    return(VM_PROT_READ | VM_PROT_WRITE);
	else
	    return(VM_PROT_READ | VM_PROT_WRITE | VM_PROT_EXECUTE);
}

/*
 * get_shared_region_size_from_flag() returns the default shared
 * region size.
 */
__private_extern__
uint32_t
get_shared_region_size_from_flag(
const struct arch_flag *flag)
{
	if(flag->cputype == CPU_TYPE_ARM)
	   return (0x08000000);
	else
	   return (0x10000000);
}

/*
 * force_cpusubtype_ALL_for_cputype() takes a cputype and returns TRUE if for
 * that cputype the cpusubtype should always be forced to the ALL cpusubtype,
 * otherwise it returns FALSE.
 */
__private_extern__
enum bool
force_cpusubtype_ALL_for_cputype(
cpu_type_t cputype)
{
	if(cputype == CPU_TYPE_I386)
	    return(TRUE);
	else
	    return(FALSE);
}
