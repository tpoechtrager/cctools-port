const char ldVersionString[] = "241.9\n";

#ifndef __APPLE__

#include <unistd.h> 
#include <mach/mach.h>
#include <mach/mach_error.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/attr.h>
#include <errno.h>
#include <inttypes.h>
#include <mach/mach_time.h>
#include <mach/mach_host.h>
#include <mach/host_info.h>
#include <sys/time.h>
#include <assert.h>
 
#if defined(__FreeBSD__) || defined(__NetBSD__) || defined(__OpenBSD__)
#include <sys/sysctl.h>
#endif

#include "helper.h"

void __assert_rtn(const char *func, const char *file, int line, const char *msg)
{
#if defined(__FreeBSD__) || defined(__DragonFly__)
    __assert(msg, file, line, func);
#elif defined(__NetBSD__) || defined(__OpenBSD__) || defined(__CYGWIN__)
    __assert(msg, line, file);
#else
    __assert(msg, file, line);
#endif /* __FreeBSD__ */
}

int _NSGetExecutablePath(char *path, unsigned int *size)
{
#ifdef __FreeBSD__
   int mib[4];
   mib[0] = CTL_KERN;
   mib[1] = KERN_PROC;
   mib[2] = KERN_PROC_PATHNAME;
   mib[3] = -1;
   size_t cb = *size;
   if (sysctl(mib, 4, path, &cb, NULL, 0) != 0)
    return -1;
   *size = cb;
   return 0;
#else
   int bufsize = *size;
   int ret_size;
   ret_size = readlink("/proc/self/exe", path, bufsize-1);
   if (ret_size != -1)
   {
        *size = ret_size;
        path[ret_size]=0;
        return 0;
   }
   else
    return -1;
#endif
}

int _dyld_find_unwind_sections(void* i, struct dyld_unwind_sections* sec)
{
    return 0;
}

mach_port_t mach_host_self(void)
{
  return 0;
}

kern_return_t host_statistics ( host_t host_priv, host_flavor_t flavor, host_info_t host_info_out, mach_msg_type_number_t *host_info_outCnt)
{
 return ENOTSUP;
}

uint64_t  mach_absolute_time(void) {
  uint64_t t = 0;
  struct timeval tv;
  if (gettimeofday(&tv,NULL)) return t;
  t = ((uint64_t)tv.tv_sec << 32)  | tv.tv_usec;
  return t;
}

kern_return_t     mach_timebase_info( mach_timebase_info_t info) {
   info->numer = 1;
   info->denom = 1;
   return 0;
}

int32_t OSAtomicAdd32( int32_t theAmount, volatile int32_t *theValue )
{
   __sync_fetch_and_add(theValue, theAmount);
   return *theValue; 
}
int64_t OSAtomicAdd64(int64_t theAmount, volatile int64_t *theValue) {
   __sync_fetch_and_add(theValue, theAmount);
   return *theValue; 
}

#endif /* __APPLE__ */
