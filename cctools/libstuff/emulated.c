#ifndef __APPLE__
#include <mach/mach.h>
#include <mach/mach_error.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/mman.h>
//#include <sys/attr.h>
#include <errno.h>
#include <inttypes.h>
#include <mach/mach_time.h>
#include <mach/mach_host.h>
#include <mach/host_info.h>
#include <sys/time.h>

int _NSGetExecutablePath(char *path, unsigned int *size)
{
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
}

kern_return_t mach_timebase_info( mach_timebase_info_t info) {
   info->numer = 1;
   info->denom = 1;
   return 0;
}

char *mach_error_string(mach_error_t error_value)
{
  return "Unknown mach error";
}

mach_port_t mach_host_self(void)
{
  return 0;
}

kern_return_t host_info
(
 host_t host,
 host_flavor_t flavor,
 host_info_t host_info_out,
 mach_msg_type_number_t *host_info_outCnt
 )
{
  if(flavor == HOST_BASIC_INFO) {
    host_basic_info_t      basic_info;

    basic_info = (host_basic_info_t) host_info_out;
    memset(basic_info, 0x00, sizeof(*basic_info));
    basic_info->cpu_type = EMULATED_HOST_CPU_TYPE;
    basic_info->cpu_subtype = EMULATED_HOST_CPU_SUBTYPE;
  }

  return 0;
}

mach_port_t mach_task_self_ = 0;

kern_return_t mach_port_deallocate
(
 ipc_space_t task,
 mach_port_name_t name
 )
{
  return 0;
}

kern_return_t vm_allocate
(
 vm_map_t target_task,
 vm_address_t *address,
 vm_size_t size,
        int flags
 )
{

  vm_address_t addr = 0;

  addr = (vm_address_t)calloc(size, sizeof(char));
  if(addr == 0)
    return 1;

  *address = addr;

  return 0;
}

kern_return_t vm_deallocate
(
 vm_map_t target_task,
 vm_address_t address,
        vm_size_t size
 )
{
  //  free((void *)address); leak it here

  return 0;
}
kern_return_t host_statistics ( host_t host_priv, host_flavor_t flavor, host_info_t host_info_out, mach_msg_type_number_t *host_info_outCnt)
{
 return ENOTSUP;
}
kern_return_t map_fd(
                     int fd,
                     vm_offset_t offset,
                     vm_offset_t *va,
                     boolean_t findspace,
                     vm_size_t size)
{
  void *addr = NULL;
  addr = mmap(0, size, PROT_READ|PROT_WRITE,
	      MAP_PRIVATE|MAP_FILE, fd, offset);
  if(addr == (void *)-1) {
    return 1;
  }
  *va = (vm_offset_t)addr;
  return 0;
}

uint64_t  mach_absolute_time(void) {
  uint64_t t = 0;
  struct timeval tv;
  if (gettimeofday(&tv,NULL)) return t;
  t = ((uint64_t)tv.tv_sec << 32)  | tv.tv_usec;
  return t;
}


#ifndef HAVE_STRMODE
#include <sys/cdefs.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>

void
strmode(/* mode_t */ int mode, char *p)
{
     /* print type */
    switch (mode & S_IFMT) {
    case S_IFDIR:           /* directory */
        *p++ = 'd';
        break;
    case S_IFCHR:           /* character special */
        *p++ = 'c';
        break;
    case S_IFBLK:           /* block special */
        *p++ = 'b';
        break;
    case S_IFREG:           /* regular */
        *p++ = '-';
        break;
    case S_IFLNK:           /* symbolic link */
        *p++ = 'l';
        break;
    case S_IFSOCK:          /* socket */
        *p++ = 's';
        break;
#ifdef S_IFIFO
    case S_IFIFO:           /* fifo */
        *p++ = 'p';
        break;
#endif
#ifdef S_IFWHT
    case S_IFWHT:           /* whiteout */
        *p++ = 'w';
        break;
#endif
    default:            /* unknown */
        *p++ = '?';
        break;
    }
    /* usr */
    if (mode & S_IRUSR)
        *p++ = 'r';
    else
        *p++ = '-';
    if (mode & S_IWUSR)
        *p++ = 'w';
    else
        *p++ = '-';
    switch (mode & (S_IXUSR | S_ISUID)) {
    case 0:
        *p++ = '-';
        break;
    case S_IXUSR:
        *p++ = 'x';
        break;
    case S_ISUID:
        *p++ = 'S';
        break;
    case S_IXUSR | S_ISUID:
        *p++ = 's';
        break;
    }
    /* group */
    if (mode & S_IRGRP)
        *p++ = 'r';
    else
        *p++ = '-';
    if (mode & S_IWGRP)
        *p++ = 'w';
    else
        *p++ = '-';
    switch (mode & (S_IXGRP | S_ISGID)) {
    case 0:
        *p++ = '-';
        break;
    case S_IXGRP:
        *p++ = 'x';
        break;
    case S_ISGID:
        *p++ = 'S';
        break;
    case S_IXGRP | S_ISGID:
        *p++ = 's';
        break;
    }
    /* other */
    if (mode & S_IROTH)
        *p++ = 'r';
    else
        *p++ = '-';
    if (mode & S_IWOTH)
        *p++ = 'w';
    else
        *p++ = '-';
    switch (mode & (S_IXOTH | S_ISVTX)) {
    case 0:
        *p++ = '-';
        break;
    case S_IXOTH:
        *p++ = 'x';
        break;
    case S_ISVTX:
        *p++ = 'T';
        break;
    case S_IXOTH | S_ISVTX:
        *p++ = 't';
        break;
    }
    *p++ = ' ';
    *p = '\0';
}
#endif

int getattrlist(const char* a,void* b,void* c,size_t d,unsigned int e)
{
  errno = ENOTSUP;
  return -1;
}

vm_size_t       vm_page_size = 4096; // hardcoded to match expectations of darwin



/*      $OpenBSD: strlcpy.c,v 1.11 2006/05/05 15:27:38 millert Exp $        */

/*
 * Copyright (c) 1998 Todd C. Miller <Todd.Miller@courtesan.com>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/types.h>
#include <string.h>


/*
 * Copy src to string dst of size siz.  At most siz-1 characters
 * will be copied.  Always NUL terminates (unless siz == 0).
 * Returns strlen(src); if retval >= siz, truncation occurred.
 */
size_t
strlcpy(char *dst, const char *src, size_t siz)
{
        char *d = dst;
        const char *s = src;
        size_t n = siz;

        /* Copy as many bytes as will fit */
        if (n != 0) {
                while (--n != 0) {
                        if ((*d++ = *s++) == '\0')
                                break;
                }
        }

        /* Not enough room in dst, add NUL and traverse rest of src */
        if (n == 0) {
                if (siz != 0)
                        *d = '\0';                /* NUL-terminate dst */
                while (*s++)
                        ;
        }

        return(s - src - 1);        /* count does not include NUL */
}

#endif /* __APPLE__ */
