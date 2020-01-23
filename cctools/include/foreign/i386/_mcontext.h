#ifdef __APPLE__

/*
 * This is an awful fix for #74 and should
 * cause compile errors once the struct is used
 * for real.
 */

#ifndef __M_CONTEXT_FIX__
#define __M_CONTEXT_FIX__

struct x86_thread_full_state64{};

#ifdef _STRUCT_X86_THREAD_FULL_STATE64
#error FIXME
#endif

#define _STRUCT_X86_THREAD_FULL_STATE64 struct x86_thread_full_state64

#endif

#endif /* __APPLE__ */

#include_next <i386/_mcontext.h>
