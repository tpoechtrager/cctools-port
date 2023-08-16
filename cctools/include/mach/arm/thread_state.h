/*
 * Copyright (c) 2000-2007 Apple Inc. All rights reserved.
 */
/*
 * @OSF_COPYRIGHT@
 */

#ifndef _MACH_ARM_THREAD_STATE_H_
#define _MACH_ARM_THREAD_STATE_H_

#define ARM_THREAD_STATE_MAX	(272)

#if defined (__arm__)
#undef THREAD_STATE_MAX /* cctools-port */
#define THREAD_STATE_MAX	ARM_THREAD_STATE_MAX
#endif

#if defined(__arm64__) && !defined(THREAD_STATE_MAX)
#undef THREAD_STATE_MAX /* cctools-port */
#define THREAD_STATE_MAX	ARM_THREAD_STATE_MAX
#endif

#endif	/* _MACH_ARM_THREAD_STATE_H_ */
