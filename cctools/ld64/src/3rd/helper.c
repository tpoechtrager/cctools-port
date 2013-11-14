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
#include "helper.h"

const char ldVersionString[] = "134.9\n";


void __assert_rtn(const char *func, const char *file, int line, const char *msg)
{
    __assert(msg, file, line);
}


int _NSGetExecutablePath(char *path, unsigned int *size)
{
   int bufsize = *size;
   int ret_size;
   char *localpath = (char*)malloc(bufsize);
   bzero(localpath,bufsize);
   ret_size = readlink("/proc/self/exe", localpath, bufsize);
   if (ret_size != -1)
   {
        *size = ret_size;
        strcpy(path,localpath);
        return 0;
   }
   else
    return -1;
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
