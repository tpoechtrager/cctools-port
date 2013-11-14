/*
 * Copyright (c) 2007 Apple Inc. All rights reserved.
 */
/*
 * FILE_ID: thread_status.h
 */


#ifndef _ARM_THREAD_STATUS_H_
#define _ARM_THREAD_STATUS_H_

#include <mach/arm/_structs.h>
#include <mach/message.h>
#include <mach/arm/thread_state.h>

/*
 *    Support for determining the state of a thread
 */


/*
 * Flavors
 */

#define ARM_THREAD_STATE		1
#define ARM_VFP_STATE			2
#define ARM_EXCEPTION_STATE		3
#define ARM_DEBUG_STATE			4
#define THREAD_STATE_NONE		5

#ifdef XNU_KERNEL_PRIVATE
#define THREAD_STATE_LAST		8
#endif

#define VALID_THREAD_STATE_FLAVOR(x)\
((x == ARM_THREAD_STATE) 		||	\
 (x == ARM_VFP_STATE) 			||	\
 (x == ARM_EXCEPTION_STATE) 	||	\
 (x == ARM_DEBUG_STATE) 		||	\
 (x == THREAD_STATE_NONE))

typedef _STRUCT_ARM_THREAD_STATE		arm_thread_state_t;
typedef _STRUCT_ARM_VFP_STATE			arm_vfp_state_t;
typedef _STRUCT_ARM_EXCEPTION_STATE		arm_exception_state_t;
typedef _STRUCT_ARM_DEBUG_STATE			arm_debug_state_t;

#define ARM_THREAD_STATE_COUNT ((mach_msg_type_number_t) \
   (sizeof (arm_thread_state_t)/sizeof(uint32_t)))

#define ARM_VFP_STATE_COUNT ((mach_msg_type_number_t) \
   (sizeof (arm_vfp_state_t)/sizeof(uint32_t)))

#define ARM_EXCEPTION_STATE_COUNT ((mach_msg_type_number_t) \
   (sizeof (arm_exception_state_t)/sizeof(uint32_t)))

#define ARM_DEBUG_STATE_COUNT ((mach_msg_type_number_t) \
   (sizeof (arm_debug_state_t)/sizeof(uint32_t)))

/*
 * Largest state on this machine:
 */
#define THREAD_MACHINE_STATE_MAX	THREAD_STATE_MAX

#ifdef XNU_KERNEL_PRIVATE

#if defined(__arm__)

#define ARM_SAVED_STATE			THREAD_STATE_NONE + 1

struct arm_saved_state {
	uint32_t	r[13];		/* General purpose register r0-r12 */
	uint32_t	sp;			/* Stack pointer r13 */
	uint32_t	lr;			/* Link register r14 */
	uint32_t	pc;			/* Program counter r15 */
	uint32_t	cpsr;		/* Current program status register */
	uint32_t	fsr;		/* Fault status */
	uint32_t	far;		/* Virtual Fault Address */
	uint32_t	exception;	/* exception number */
};
typedef struct arm_saved_state arm_saved_state_t;

#ifdef XNU_KERNEL_PRIVATE
typedef struct arm_saved_state arm_saved_state32_t;

static inline arm_saved_state32_t*
saved_state32(arm_saved_state_t *iss)
{
	return iss;
}

static inline boolean_t
is_saved_state32(arm_saved_state_t *iss __unused)
{
	return TRUE;
}

#endif

struct arm_saved_state_tagged {
	uint32_t					tag;
	struct arm_saved_state		state;
};
typedef struct arm_saved_state_tagged arm_saved_state_tagged_t;

#define ARM_SAVED_STATE32_COUNT ((mach_msg_type_number_t) \
		(sizeof (arm_saved_state_t)/sizeof(unsigned int)))

#else
#error Unknown arch
#endif

#endif /* XNU_KERNEL_PRIVATE */

#endif    /* _ARM_THREAD_STATUS_H_ */
