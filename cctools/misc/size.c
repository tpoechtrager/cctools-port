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
/*	$OpenBSD: size.c,v 1.6 1997/01/28 07:12:27 deraadt Exp $	*/
/*	$NetBSD: size.c,v 1.7 1996/01/14 23:07:12 pk Exp $	*/

/*
 * Copyright (c) 1988, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
/*
 * The NeXT Computer, Inc. size(1) program that handles fat files, archives and
 * Mach-O objects files (no BSD a.out files).  A few lines of code were taken
 * and adapted from the BSD release.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "stuff/bool.h"
#include "stuff/ofile.h"
#include "stuff/errors.h"
#include "stuff/allocate.h"

char *progname = NULL;

struct flags {
    uint32_t nfiles;
    enum bool m;
    enum bool l;
    enum bool x;
    enum bool nested;
    char* indent;
};

static void usage(
    void);
static void size(
    struct ofile *ofile,
    char *arch_name,
    void *cookie);

/* apple_version is created by the libstuff/Makefile */
extern char apple_version[];
char *version = apple_version;

int
main(
int argc,
char **argv,
char **envp)
{
    int i,j;
    enum bool args_left;
    struct flags flag;
    struct arch_flag *arch_flags;
    uint32_t narch_flags;
    enum bool all_archs;
    char **files;

	progname = argv[0];
	arch_flags = NULL;
	narch_flags = 0;
	all_archs = FALSE;

	flag.nfiles = 0;
	flag.m = FALSE;
	flag.l = FALSE;
	flag.x = FALSE;
	flag.nested = FALSE;
	flag.indent = "";

	files = allocate(sizeof(char *) * argc);
	for(i = 1; i < argc; i++){
	    if(argv[i][0] == '-'){
		if(argv[i][1] == '\0' ||
		   0 == strcmp("--", argv[i])){
		    continue;
		}
		else if(strcmp(argv[i], "-arch") == 0){
		    if(i + 1 == argc){
			error("missing argument(s) to %s option", argv[i]);
			usage();
		    }
		    if(strcmp("all", argv[i+1]) == 0){
			all_archs = TRUE;
		    }
		    else{
			arch_flags = reallocate(arch_flags,
				(narch_flags + 1) * sizeof(struct arch_flag));
			if(get_arch_from_flag(argv[i+1],
					      arch_flags + narch_flags) == 0){
			    error("unknown architecture specification flag: "
				  "%s %s", argv[i], argv[i+1]);
			    arch_usage();
			    usage();
			}
			narch_flags++;
		    }
		    i++;
		    continue;
		}
		else {
		    for(j = 1; argv[i][j] != '\0'; j++){
			switch(argv[i][j]){
			    case 'l':
				flag.l = TRUE;
				break;
			    case 'm':
				flag.m = TRUE;
				break;
			    case 'x':
				flag.x = TRUE;
				break;
			    default:
				error("invalid argument -%c", argv[i][j]);
				usage();
			}
		    }
		    continue;
		}
	    }
	    files[flag.nfiles++] = argv[i];
	}

	if(flag.m == FALSE)
	    printf("__TEXT\t__DATA\t__OBJC\tothers\tdec\thex\n");

	args_left = TRUE;
	for (i = 0; i < flag.nfiles; i++) {
	    ofile_process(files[i], arch_flags, narch_flags, all_archs, FALSE,
			  TRUE, TRUE, size, &flag);
	}
	if(flag.nfiles == 0)
	    ofile_process("a.out", arch_flags, narch_flags, all_archs, FALSE,
			  TRUE, TRUE, size, &flag);

	free(files);

	if(errors == 0)
	    return(EXIT_SUCCESS);
	else
	    return(EXIT_FAILURE);
}

/*
 * usage() prints the current usage message and exits indicating failure.
 */
static
void
usage(
void)
{
	fprintf(stderr, "Usage: %s [-m] [-l] [-x] [--] "
		"[[-arch <arch_flag>] ...] [file ...]\n", progname);
	exit(EXIT_FAILURE);
}

/*
 * size() is the routine that gets called by ofile_process() to process single
 * object files.
 */
static
void
size(
struct ofile *ofile,
char *arch_name,
void *cookie)
{
    struct flags *flag;
    uint64_t seg_sum, sect_sum;
    uint32_t i, j;
    struct load_command *lc;
    struct segment_command *sg;
    struct segment_command_64 *sg64;
    struct fileset_entry_command *fse;
    struct section *s;
    struct section_64 *s64;
    uint64_t text, data, objc, others, sum;
    uint32_t ncmds;

	flag = (struct flags *)cookie;
	if(ofile->mh != NULL)
	    ncmds = ofile->mh->ncmds;
	else
	    ncmds = ofile->mh64->ncmds;
	if(flag->m == TRUE){
	    if(flag->nfiles > 1 || ofile->member_ar_hdr != NULL ||
	       arch_name != NULL){
		if(ofile->member_ar_hdr != NULL){
		    printf("%s%s(%.*s)", flag->indent, ofile->file_name,
			   (int)ofile->member_name_size,
			   ofile->member_name);
		}
		else{
		    printf("%s%s", flag->indent, ofile->file_name);
		}
		if(arch_name != NULL)
		    printf(" (for architecture %s):\n", arch_name);
		else
		    printf(":\n");
	    }
	    lc = ofile->load_commands;
	    seg_sum = 0;
	    for(i = 0; i < ncmds; i++,
		    lc = (struct load_command *)((char *)lc + lc->cmdsize)){
		if(lc->cmd == LC_SEGMENT){
		    sg = (struct segment_command *)lc;
		    if (flag->nested && !strcmp(sg->segname, "__LINKEDIT")) continue;
		    printf("%sSegment %.16s: ", flag->indent, sg->segname);
		    if(flag->x == TRUE)
			printf("0x%x", (unsigned int)sg->vmsize);
		    else
			printf("%u", sg->vmsize);
			if (strcmp(sg->segname, "__PAGEZERO") == 0)
				printf(" (zero fill) ");
		    if(sg->flags & SG_FVMLIB)
			printf(" (fixed vm library segment)\n");
		    else{
			if(flag->l == TRUE)
			    printf(" (vmaddr 0x%x fileoff %u)\n",
				    (unsigned int)sg->vmaddr, sg->fileoff);
			else
			    printf("\n");
		    }
		    seg_sum += sg->vmsize;
		    s = (struct section *)((char *)sg +
			    sizeof(struct segment_command));
		    sect_sum = 0;
		    for(j = 0; j < sg->nsects; j++){
			if(ofile->mh_filetype == MH_OBJECT)
			    printf("%s\tSection (%.16s, %.16s): ", flag->indent,
				   s->segname, s->sectname);
			else
			    printf("%s\tSection %.16s: ", flag->indent, s->sectname);
			if(flag->x == TRUE)
			    printf("0x%x", (unsigned int)s->size);
			else
			    printf("%u", s->size);
			enum bool zero_filled = ((s->flags & SECTION_TYPE) == S_THREAD_LOCAL_ZEROFILL) ||
					((s->flags & SECTION_TYPE) == S_ZEROFILL);
			if(flag->l == TRUE) {
				if (!zero_filled) {
					printf(" (addr 0x%x offset %u)\n",
						   (unsigned int)s->addr, s->offset);
				}
				else {
					printf(" (addr 0x%x zerofill)\n",
						   (unsigned int)s->addr);
				}
			}
			else {
				if (!zero_filled) {
					printf("\n");
				}
				else {
					printf(" (zerofill)\n");
				}
			}
			sect_sum += s->size;
			s++;
		    }
		    if(sg->nsects > 0){
			if(flag->x == TRUE)
			    printf("%s\ttotal 0x%llx\n", flag->indent, sect_sum);
			else
			    printf("%s\ttotal %llu\n", flag->indent, sect_sum);
		    }
		}
		else if(lc->cmd == LC_SEGMENT_64){
		    sg64 = (struct segment_command_64 *)lc;
		    if (flag->nested && !strcmp(sg64->segname, "__LINKEDIT")) continue;
		    printf("%sSegment %.16s: ", flag->indent, sg64->segname);
		    if(flag->x == TRUE)
			printf("0x%llx", sg64->vmsize);
		    else
			printf("%llu", sg64->vmsize);
			if (strcmp(sg64->segname, "__PAGEZERO") == 0)
				printf(" (zero fill) ");
		    if(sg64->flags & SG_FVMLIB)
			printf(" (fixed vm library segment)\n");
		    else{
			if(flag->l == TRUE)
			    printf(" (vmaddr 0x%llx fileoff %llu)\n",
				    sg64->vmaddr, sg64->fileoff);
			else
			    printf("\n");
		    }
		    seg_sum += sg64->vmsize;
		    s64 = (struct section_64 *)((char *)sg64 +
			    sizeof(struct segment_command_64));
		    sect_sum = 0;
		    for(j = 0; j < sg64->nsects; j++){
			if(ofile->mh_filetype == MH_OBJECT)
			    printf("%s\tSection (%.16s, %.16s): ", flag->indent,
				   s64->segname, s64->sectname);
			else
			    printf("%s\tSection %.16s: ", flag->indent, s64->sectname);
			if(flag->x == TRUE)
			    printf("0x%llx", s64->size);
			else
			    printf("%llu", s64->size);
			enum bool zero_filled = ((s64->flags & SECTION_TYPE) == S_THREAD_LOCAL_ZEROFILL) ||
				((s64->flags & SECTION_TYPE) == S_ZEROFILL);
			if(flag->l == TRUE) {
				if (!zero_filled) {
					printf(" (addr 0x%llx offset %u)\n",
						   s64->addr,
						   s64->offset);
				}
				else {
					printf(" (addr 0x%llx zerofill)\n",
						   s64->addr);
				}
			}
			else {
				if (!zero_filled) {
					printf("\n");
				}
				else {
					printf(" (zerofill)\n");
				}
			}
			sect_sum += s64->size;
			s64++;
		    }
		    if(sg64->nsects > 0){
			if(flag->x == TRUE)
			    printf("%s\ttotal 0x%llx\n", flag->indent, sect_sum);
			else
			    printf("%s\ttotal %llu\n", flag->indent, sect_sum);
		    }
		}
		else if(lc->cmd == LC_FILESET_ENTRY){
		    fse = (struct fileset_entry_command *)lc;
		    printf("%sFileset entry %s: ", flag->indent,
			    (char*)lc + fse->entry_id.offset);
		    if(flag->l == TRUE)
			printf(" (vmaddr 0x%llx fileoff %llu)\n",
				fse->vmaddr, fse->fileoff);
		    else
			printf("\n");
		    uint32_t magic;
		    char *addr = ofile->object_addr + fse->fileoff;
		    memcpy(&magic, addr, sizeof(uint32_t));
		    if(magic == MH_MAGIC || magic == SWAP_INT(MH_MAGIC) ||
			    magic == MH_MAGIC_64 || magic == SWAP_INT(MH_MAGIC_64)) {
			struct ofile fse_ofile = *ofile;
			fse_ofile.object_addr = addr;
			if (magic == MH_MAGIC || magic == SWAP_INT(MH_MAGIC)) {
			    fse_ofile.mh = (struct mach_header *)addr;
			    fse_ofile.mh_filetype = fse_ofile.mh->filetype;
			    fse_ofile.load_commands = (struct load_command *)(addr +
						sizeof(struct mach_header));
			} else {
			    fse_ofile.mh64 = (struct mach_header_64 *)addr;
			    fse_ofile.mh_filetype = fse_ofile.mh64->filetype;
			    fse_ofile.load_commands = (struct load_command *)(addr +
						sizeof(struct mach_header_64));
			}
		        struct flags fseflag = *flag;
			fseflag.nested = TRUE;
			fseflag.indent = "\t|";
			size(&fse_ofile, arch_name, &fseflag);
		    }
		}
	    }
	    if(flag->x == TRUE)
		printf("%stotal 0x%llx\n", flag->indent, seg_sum);
	    else
		printf("%stotal %llu\n", flag->indent, seg_sum);
	}
	else{
	    text = 0;
	    data = 0;
	    objc = 0;
	    others = 0;
	    lc = ofile->load_commands;
	    for(i = 0; i < ncmds; i++){
		if(lc->cmd == LC_SEGMENT){
		    sg = (struct segment_command *)lc;
		    if(ofile->mh_filetype == MH_OBJECT){
			s = (struct section *)((char *)sg +
				sizeof(struct segment_command));
			for(j = 0; j < sg->nsects; j++){
			    if(strcmp(s->segname, SEG_TEXT) == 0)
				text += s->size;
			    else if(strcmp(s->segname, SEG_DATA) == 0)
				data += s->size;
			    else if(strcmp(s->segname, SEG_OBJC) == 0)
				objc += s->size;
			    else
				others += s->size;
			    s++;
			}
		    }
		    else{
			if(strcmp(sg->segname, SEG_TEXT) == 0)
			    text += sg->vmsize;
			else if(strcmp(sg->segname, SEG_DATA) == 0)
			    data += sg->vmsize;
			else if(strcmp(sg->segname, SEG_OBJC) == 0)
			    objc += sg->vmsize;
			else
			    others += sg->vmsize;
		    }
		}
		else if(lc->cmd == LC_SEGMENT_64){
		    sg64 = (struct segment_command_64 *)lc;
		    if(ofile->mh_filetype == MH_OBJECT){
			s64 = (struct section_64 *)((char *)sg64 +
				sizeof(struct segment_command_64));
			for(j = 0; j < sg64->nsects; j++){
			    if(strcmp(s64->segname, SEG_TEXT) == 0)
				text += s64->size;
			    else if(strcmp(s64->segname, SEG_DATA) == 0)
				data += s64->size;
			    else if(strcmp(s64->segname, SEG_OBJC) == 0)
				objc += s64->size;
			    else
				others += s64->size;
			    s64++;
			}
		    }
		    else{
			if(strcmp(sg64->segname, SEG_TEXT) == 0)
			    text += sg64->vmsize;
			else if(strcmp(sg64->segname, SEG_DATA) == 0)
			    data += sg64->vmsize;
			else if(strcmp(sg64->segname, SEG_OBJC) == 0)
			    objc += sg64->vmsize;
			else
			    others += sg64->vmsize;
		    }
		}
		lc = (struct load_command *)((char *)lc + lc->cmdsize);
	    }
	    printf("%llu\t%llu\t%llu\t%llu\t", text, data, objc, others);
	    sum = text + data + objc + others;
	    printf("%llu\t%llx", sum, sum);
	    if(flag->nfiles > 1 || ofile->member_ar_hdr != NULL ||
	       arch_name != NULL){
		if(ofile->member_ar_hdr != NULL){
		    printf("\t%s(%.*s)", ofile->file_name,
			   (int)ofile->member_name_size,
			   ofile->member_name);
		}
		else{
		    printf("\t%s", ofile->file_name);
		}
		if(arch_name != NULL)
		    printf(" (for architecture %s)", arch_name);
	    }
	    printf("\n");
	}
}
