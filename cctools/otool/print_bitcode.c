/*
 * Copyright © 2015 Apple Inc. All rights reserved.
 
 * @APPLE_LICENSE_HEADER_START@
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 * 
 * 1.  Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer. 
 * 2.  Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution. 
 * 3.  Neither the name of Apple Inc. ("Apple") nor the names of its
 * contributors may be used to endorse or promote products derived from this
 * software without specific prior written permission. 
 * 
 * THIS SOFTWARE IS PROVIDED BY APPLE AND ITS CONTRIBUTORS "AS IS" AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL APPLE OR ITS CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * @APPLE_LICENSE_HEADER_END@
 */
#include <stdio.h>
#include <stdlib.h>
#include <libc.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <dlfcn.h>
#include "../include/xar/xar.h" /* cctools-port: 
				   force the use of the bundled xar header */
#include "mach-o/loader.h"
#include "objc/runtime.h"       /* cctools-port:
				   objc/objc-runtime.h -> objc/runtime.h */
#include "stuff/allocate.h"
#include "stuff/bytesex.h"
#include "stuff/symbol.h"
#include "stuff/errors.h"
#include "stuff/allocate.h"
#include "dyld_bind_info.h"
#include "ofile_print.h"
#include <mach-o/dyld.h>

static enum bool tried_to_load_xar = FALSE;
static void *xar_handle = NULL;
static xar_t (*ptr_xar_open)(const char *file, int32_t flags) = NULL;
static void (*ptr_xar_serialize)(xar_t x, const char *file) = NULL;
static int (*ptr_xar_close)(xar_t x) = NULL;

void
print_bitcode_section(
char *sect,
uint64_t sect_size,
enum bool verbose,
enum bool print_xar_header)
{
    enum byte_sex host_byte_sex;
    uint32_t i, bufsize;
    char *p, *prefix, *xar_path, buf[MAXPATHLEN], resolved_name[PATH_MAX];
    struct xar_header xar_hdr;
    char xar_filename[] = "/tmp/temp.XXXXXX";
    char toc_filename[] = "/tmp/temp.XXXXXX";
    int xar_fd, toc_fd;
    xar_t xar;
    struct stat toc_stat_buf;
    char *toc;

	host_byte_sex = get_host_byte_sex();

	memset(&xar_hdr, '\0', sizeof(struct xar_header));
	if(sect_size < sizeof(struct xar_header)) {
	    printf("size of (__LLVM,__bundle) section too small (smaller "
		   "than size of struct xar_header)");
	    memcpy((char *)&xar_hdr, sect, sect_size);
	}
	else {
	    memcpy((char *)&xar_hdr, sect, sizeof(struct xar_header));
	}
#ifdef __LITTLE_ENDIAN__
	swap_xar_header(&xar_hdr, host_byte_sex);
#endif /* __LITTLE_ENDIAN__ */
	if(print_xar_header) {
	    printf("For (__LLVM,__bundle) section: xar header\n");
	    if(xar_hdr.magic == XAR_HEADER_MAGIC)
		printf("                  magic XAR_HEADER_MAGIC\n");
	    else
		printf("                  magic 0x%08x (not XAR_HEADER_MAGIC)\n"
		       , xar_hdr.magic);
	    printf("                   size %u\n", xar_hdr.size);
	    printf("                version %u\n", xar_hdr.version);
	    printf("  toc_length_compressed %llu\n",
		xar_hdr.toc_length_compressed);
	    printf("toc_length_uncompressed %llu\n",
		xar_hdr.toc_length_uncompressed);
	    printf("              cksum_alg ");
	    switch(xar_hdr.cksum_alg){
	    case XAR_CKSUM_NONE:
		printf("XAR_CKSUM_NONE\n");
		break;
	    case XAR_CKSUM_SHA1:
		printf("XAR_CKSUM_SHA1\n");
		break;
	    case XAR_CKSUM_MD5:
		printf("XAR_CKSUM_MD5\n");
		break;
	    case XAR_CKSUM_SHA256:
		printf("XAR_CKSUM_SHA512\n");
		break;
	    case XAR_CKSUM_SHA512:
		printf("XAR_CKSUM_SHA512\n");
		break;
	    default:
		printf("%u\n", xar_hdr.cksum_alg);
		break;
	    }
	}

	if(sect_size < sizeof(struct xar_header))
	    return;

	if(tried_to_load_xar == FALSE){
	    tried_to_load_xar = TRUE;
	    /*
	     * Construct the prefix to this executable assuming it is in a bin
	     * directory relative to a lib directory of the matching xar library
	     * and first try to load that.  If not then fall back to trying
	     * "/usr/lib/libxar.dylib". 
	     */
#ifdef __APPLE__ /* cctools-port */
	    bufsize = MAXPATHLEN;
	    p = buf;
	    i = _NSGetExecutablePath(p, &bufsize);
	    if(i == -1){
		p = allocate(bufsize);
		_NSGetExecutablePath(p, &bufsize);
	    }
	    prefix = realpath(p, resolved_name);
	    p = rindex(prefix, '/');
	    if(p != NULL)
		p[1] = '\0';
	    xar_path = makestr(prefix, "../lib/libxar.dylib", NULL);

	    xar_handle = dlopen(xar_path, RTLD_NOW);
	    if(xar_handle == NULL){
		free(xar_path);
		xar_path = NULL;
		xar_handle = dlopen("/usr/lib/libxar.dylib", RTLD_NOW);
	    }
#else
	    xar_handle = dlopen("libxar.so", RTLD_NOW);
	    if(xar_handle == NULL)
		fprintf(stderr, "Can't open libxar.so\n");
#endif /* __APPLE__ */
	    if(xar_handle == NULL)
		return;

	    ptr_xar_open = dlsym(xar_handle, "xar_open");
	    ptr_xar_serialize = dlsym(xar_handle, "xar_serialize");
	    ptr_xar_close = dlsym(xar_handle, "xar_close");
	    if(ptr_xar_open == NULL ||
	       ptr_xar_serialize == NULL ||
	       ptr_xar_close == NULL)
		return;
	}
	if(xar_handle == NULL)
	    return;

	xar_fd = mkstemp(xar_filename);
	if(write(xar_fd, sect, sect_size) != sect_size){
	    system_error("Can't write (__LLVM,__bundle) section contents "
		"to temporary file: %s\n", xar_filename);
	    close(xar_fd);
	    return;
	}
	close(xar_fd);

	if(mktemp(toc_filename) == NULL){
	    system_error("Can't create file name for xar toc\n");
	    unlink(xar_filename);
	    return;
	}
	xar = ptr_xar_open(xar_filename, READ);
	if(!xar){
	    system_error("Can't create temporary xar archive %s\n",
			 xar_filename);
	    unlink(xar_filename);
	    return;
	}
	ptr_xar_serialize(xar, toc_filename);
	ptr_xar_close(xar);
	unlink(xar_filename);

	toc_fd = open(toc_filename, O_RDONLY, 0);
	if(toc_fd == 0){
	    system_error("Can't open xar table of contents file: %s\n",
			 toc_filename);
	    unlink(toc_filename);
	    return;
	}
	if(fstat(toc_fd, &toc_stat_buf) != 0){
	    system_error("Can't fstat xar table of contents file: %s\n",
		toc_filename);
	    unlink(toc_filename);
	    return;
	}
	toc = allocate(toc_stat_buf.st_size + 1);
	toc[toc_stat_buf.st_size] = '\0';
	if(read(toc_fd, toc, toc_stat_buf.st_size) != toc_stat_buf.st_size){
	    system_error("Can't read xar table of contents file: %s\n",
			 toc_filename);
	    unlink(toc_filename);
	    return;
	}
	close(toc_fd);
	unlink(toc_filename);
	printf("For (__LLVM,__bundle) section: xar table of contents:\n");
	printf("%s\n", toc);
	free(toc);
}
