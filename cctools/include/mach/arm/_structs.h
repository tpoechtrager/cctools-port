/*
 * Copyright (c) 2004-2007 Apple Inc. All rights reserved.
 */
/*
 * @OSF_COPYRIGHT@
 */
#ifndef	_MACH_ARM__STRUCTS_H_
#define	_MACH_ARM__STRUCTS_H_

#if __DARWIN_UNIX03
#define _STRUCT_ARM_EXCEPTION_STATE	struct __darwin_arm_exception_state
_STRUCT_ARM_EXCEPTION_STATE
{
	__uint32_t	__exception; /* number of arm exception taken */
	__uint32_t	__fsr; /* Fault status */
	__uint32_t	__far; /* Virtual Fault Address */
};
#else /* !__DARWIN_UNIX03 */
#define _STRUCT_ARM_EXCEPTION_STATE	struct arm_exception_state
_STRUCT_ARM_EXCEPTION_STATE
{
	__uint32_t	exception; /* number of arm exception taken */
	__uint32_t	fsr; /* Fault status */
	__uint32_t	far; /* Virtual Fault Address */
};
#endif /* __DARWIN_UNIX03 */


#if __DARWIN_UNIX03
#define _STRUCT_ARM_THREAD_STATE	struct __darwin_arm_thread_state
_STRUCT_ARM_THREAD_STATE
{
	__uint32_t	__r[13];	/* General purpose register r0-r12 */
	__uint32_t	__sp;		/* Stack pointer r13 */
	__uint32_t	__lr;		/* Link register r14 */
	__uint32_t	__pc;		/* Program counter r15 */
	__uint32_t	__cpsr;		/* Current program status register */
};
#else /* !__DARWIN_UNIX03 */
#define _STRUCT_ARM_THREAD_STATE	struct arm_thread_state
_STRUCT_ARM_THREAD_STATE
{
	__uint32_t	r[13];	/* General purpose register r0-r12 */
	__uint32_t	sp;		/* Stack pointer r13 */
	__uint32_t	lr;		/* Link register r14 */
	__uint32_t	pc;		/* Program counter r15 */
	__uint32_t	cpsr;		/* Current program status register */
};
#endif /* __DARWIN_UNIX03 */


#if __DARWIN_UNIX03
#define _STRUCT_ARM_VFP_STATE		struct __darwin_arm_vfp_state
_STRUCT_ARM_VFP_STATE
{
	__uint32_t        __r[64];
	__uint32_t        __fpscr;

};
#else /* !__DARWIN_UNIX03 */
#define _STRUCT_ARM_VFP_STATE	struct arm_vfp_state
_STRUCT_ARM_VFP_STATE
{
	__uint32_t        r[64];
	__uint32_t        fpscr;
};
#endif /* __DARWIN_UNIX03 */


#define _STRUCT_ARM_DEBUG_STATE		struct __darwin_arm_debug_state
_STRUCT_ARM_DEBUG_STATE
{
	__uint32_t        __bvr[16];
	__uint32_t        __bcr[16];
	__uint32_t        __wvr[16];
	__uint32_t        __wcr[16];
};

#endif /* _MACH_ARM__STRUCTS_H_ */
