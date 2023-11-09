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
#include <mach-o/ldsyms.h>
#ifndef CCTB_DRIVERKIT_ENABLED
#include <mach-o/swap.h>
#endif /* !defined(CCTB_DRIVERKIT_ENABLED) */
#include <string.h>
#ifdef __DYNAMIC__
#include <mach-o/dyld.h> /* defines _dyld_lookup_and_bind() */
#endif /* defined(__DYNAMIC__) */
#if !defined(__OPENSTEP__) && __has_include(<crt_externs.h>)
#include <crt_externs.h>
#else /* defined(__OPENSTEP__) */

#if !defined(__DYNAMIC__)
#define DECLARE_VAR(var, type) \
extern type var
#define SETUP_VAR(var)
#define USE_VAR(var) var
#else
#define STRINGIFY(a) # a
#define DECLARE_VAR(var, type)	\
static type * var ## _pointer = NULL
#define SETUP_VAR(var)						\
if ( var ## _pointer == NULL) {				\
    _dyld_lookup_and_bind( STRINGIFY(_ ## var),		\
                           (uint32_t *) & var ## _pointer, NULL);	\
}
#define USE_VAR(var) (* var ## _pointer)
#endif
#endif /* __OPENSTEP__ */

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"

#ifndef CCTB_DRIVERKIT_ENABLED

/*
 * This routine returns the section structure for the named section in the
 * named segment for the mach_header pointer passed to it if it exist.
 * Otherwise it returns zero.
 */
const struct section *
getsectbynamefromheader(
struct mach_header *mhp,
const char *segname,
const char *sectname)
{
	struct segment_command *sgp;
	struct section *sp;
	uint32_t i, j;
        
	sgp = (struct segment_command *)
	      ((char *)mhp + sizeof(struct mach_header));
	for(i = 0; i < mhp->ncmds; i++){
	    if(sgp->cmd == LC_SEGMENT)
		if(strncmp(sgp->segname, segname, sizeof(sgp->segname)) == 0 ||
		   mhp->filetype == MH_OBJECT){
		    sp = (struct section *)((char *)sgp +
			 sizeof(struct segment_command));
		    for(j = 0; j < sgp->nsects; j++){
			if(strncmp(sp->sectname, sectname,
			   sizeof(sp->sectname)) == 0 &&
			   strncmp(sp->segname, segname,
			   sizeof(sp->segname)) == 0)
			    return(sp);
			sp = (struct section *)((char *)sp +
			     sizeof(struct section));
		    }
		}
	    sgp = (struct segment_command *)((char *)sgp + sgp->cmdsize);
	}
	return((struct section *)0);
}

/*
 * This routine returns the section structure for the named section in the
 * named segment for the mach_header_64 pointer passed to it if it exist.
 * Otherwise it returns zero.
 */
const struct section_64 *
getsectbynamefromheader_64(
struct mach_header_64 *mhp,
const char *segname,
const char *sectname)
{
	struct segment_command_64 *sgp;
	struct section_64 *sp;
	uint32_t i, j;
        
	sgp = (struct segment_command_64 *)
	      ((char *)mhp + sizeof(struct mach_header_64));
	for(i = 0; i < mhp->ncmds; i++){
	    if(sgp->cmd == LC_SEGMENT_64)
		if(strncmp(sgp->segname, segname, sizeof(sgp->segname)) == 0 ||
		   mhp->filetype == MH_OBJECT){
		    sp = (struct section_64 *)((char *)sgp +
			 sizeof(struct segment_command_64));
		    for(j = 0; j < sgp->nsects; j++){
			if(strncmp(sp->sectname, sectname,
			   sizeof(sp->sectname)) == 0 &&
			   strncmp(sp->segname, segname,
			   sizeof(sp->segname)) == 0)
			    return(sp);
			sp = (struct section_64 *)((char *)sp +
			     sizeof(struct section_64));
		    }
		}
	    sgp = (struct segment_command_64 *)((char *)sgp + sgp->cmdsize);
	}
	return((struct section_64 *)0);
}

/*
 * This routine returns the section structure for the named section in the
 * named segment for the mach_header pointer passed to it if it exist.
 * Otherwise it returns zero.  If fSwap == YES (the mach header has been
 * swapped to the endiannes of the current machine, but the segments and
 * sections are different) then the segment and sections are swapped.
 */
const struct section *
getsectbynamefromheaderwithswap(
    struct mach_header *mhp,
    const char *segname,
    const char *sectname, 
    int fSwap)
{
	struct segment_command *sgp;
	struct section *sp;
	uint32_t i, j;

	sgp = (struct segment_command *)
	      ((char *)mhp + sizeof(struct mach_header));
	for(i = 0; i < mhp->ncmds; i++){
	    if(sgp->cmd == (fSwap ? OSSwapInt32(LC_SEGMENT) : LC_SEGMENT)) {
	    
		if (fSwap) {
#ifdef __LITTLE_ENDIAN__
		    swap_segment_command(sgp, NX_BigEndian);
#else
		    swap_segment_command(sgp, NX_LittleEndian);
#endif /* __LITTLE_ENDIAN__ */
		}
	    
		if(strncmp(sgp->segname, segname, sizeof(sgp->segname)) == 0 ||
		   mhp->filetype == MH_OBJECT){
		    sp = (struct section *)((char *)sgp +
			 sizeof(struct segment_command));
		
		    if (fSwap) {
#ifdef __LITTLE_ENDIAN__
			swap_section(sp, sgp->nsects, NX_BigEndian);
#else
			swap_section(sp, sgp->nsects, NX_LittleEndian);
#endif /* __LITTLE_ENDIAN__ */
		    }
		
		    for(j = 0; j < sgp->nsects; j++){
			if(strncmp(sp->sectname, sectname,
			   sizeof(sp->sectname)) == 0 &&
			   strncmp(sp->segname, segname,
			   sizeof(sp->segname)) == 0)
			    return(sp);
			sp = (struct section *)((char *)sp +
			     sizeof(struct section));
		    }
		}
		sgp = (struct segment_command *)((char *)sgp + sgp->cmdsize);
	    } else {
		sgp = (struct segment_command *)((char *)sgp +
		    (fSwap ? OSSwapInt32(sgp->cmdsize) : sgp->cmdsize));
	    }
	}
	return((struct section *)0);
}

/*
 * This routine returns the section_64 structure for the named section in the
 * named segment for the mach_header_64 pointer passed to it if it exist.
 * Otherwise it returns zero.  If fSwap == YES (the mach header has been
 * swapped to the endiannes of the current machine, but the segments and
 * sections are different) then the segment and sections are swapped.
 */
const struct section_64 *
getsectbynamefromheaderwithswap_64(
    struct mach_header_64 *mhp,
    const char *segname,
    const char *sectname, 
    int fSwap)
{
	struct segment_command_64 *sgp;
	struct section_64 *sp;
	uint32_t i, j;

	sgp = (struct segment_command_64 *)
	      ((char *)mhp + sizeof(struct mach_header_64));
	for(i = 0; i < mhp->ncmds; i++){
	    if(sgp->cmd == (fSwap ? OSSwapInt32(LC_SEGMENT) : LC_SEGMENT)) {
	    
		if (fSwap) {
#ifdef __LITTLE_ENDIAN__
		    swap_segment_command_64(sgp, NX_BigEndian);
#else
		    swap_segment_command_64(sgp, NX_LittleEndian);
#endif /* __LITTLE_ENDIAN__ */
		}
	    
		if(strncmp(sgp->segname, segname, sizeof(sgp->segname)) == 0 ||
		   mhp->filetype == MH_OBJECT){
		    sp = (struct section_64 *)((char *)sgp +
			 sizeof(struct segment_command_64));
		
		    if (fSwap) {
#ifdef __LITTLE_ENDIAN__
			swap_section_64(sp, sgp->nsects, NX_BigEndian);
#else
			swap_section_64(sp, sgp->nsects, NX_LittleEndian);
#endif /* __LITTLE_ENDIAN__ */
		    }
		
		    for(j = 0; j < sgp->nsects; j++){
			if(strncmp(sp->sectname, sectname,
			   sizeof(sp->sectname)) == 0 &&
			   strncmp(sp->segname, segname,
			   sizeof(sp->segname)) == 0)
			    return(sp);
			sp = (struct section_64 *)((char *)sp +
			     sizeof(struct section_64));
		    }
		}
		sgp = (struct segment_command_64 *)((char *)sgp + sgp->cmdsize);
	    } else {
		sgp = (struct segment_command_64 *)((char *)sgp +
		    (fSwap ? OSSwapInt32(sgp->cmdsize) : sgp->cmdsize));
	    }
	}
	return((struct section_64 *)0);
}

/*
 * This routine returns the a pointer the section structure of the named
 * section in the named segment if it exist in the mach executable it is
 * linked into.  Otherwise it returns zero.
 */
#ifndef __LP64__

const struct section *
getsectbyname(
const char *segname,
const char *sectname)
{
#ifndef __OPENSTEP__
    struct mach_header *mhp = _NSGetMachExecuteHeader();
#else /* defined(__OPENSTEP__) */
    static struct mach_header *mhp = NULL;
	DECLARE_VAR(_mh_execute_header, struct mach_header);
        SETUP_VAR(_mh_execute_header);
	mhp = (struct mach_header *)(& USE_VAR(_mh_execute_header));
#endif /* __OPENSTEP__ */
	return(getsectbynamefromheader(mhp, segname, sectname));
}

#else /* defined(__LP64__) */

const struct section_64 *
getsectbyname(
const char *segname,
const char *sectname)
{
    struct mach_header_64 *mhp = _NSGetMachExecuteHeader();

	return(getsectbynamefromheader_64(mhp, segname, sectname));
}

#endif /* defined(__LP64__) */

/*
 * This routine returns the a pointer to the data for the named section in the
 * named segment if it exist in the mach executable it is linked into.  Also
 * it returns the size of the section data indirectly through the pointer size.
 * Otherwise it returns zero for the pointer and the size.
 */
char *
getsectdata(
const char *segname,
const char *sectname,
unsigned long *size)
{
#ifndef __LP64__
    const struct section *sp;
#else /* defined(__LP64__) */
    const struct section_64 *sp;
#endif /* defined(__LP64__) */

	sp = getsectbyname(segname, sectname);
	if(sp == NULL){
	    *size = 0;
	    return(NULL);
	}
	*size = sp->size;
	return((char *)(sp->addr));
}

#endif /* !defined(CCTB_DRIVERKIT_ENABLED) */

/*
 * This routine returns the a pointer to the section contents of the named
 * section in the named segment if it exists in the image pointed to by the
 * mach header.  Otherwise it returns zero.
 */
uint8_t*
getsectiondata(
#if __LP64__
  const struct mach_header_64* mh,
#else
  const struct mach_header* mh,
#endif
  const char* segname, const char* sectname, unsigned long* size)
{
    unsigned startOffset = (mh->magic == MH_MAGIC_64) ? sizeof(struct mach_header_64) : sizeof(struct mach_header);
    struct load_command* lc = (struct load_command*)((uint8_t*)mh + startOffset);
    uintptr_t slide = 0;
    for (int i=0; i < mh->ncmds; ++i){
        if (lc->cmd == LC_SEGMENT_64) {
            const struct segment_command_64* seg = (struct segment_command_64*)lc;
            if (strcmp(seg->segname, "__TEXT") == 0) {
                slide = (uintptr_t)mh - seg->vmaddr;
            }
            if (strncmp(seg->segname, segname, 16) == 0) {
                const struct section_64* sect = (struct section_64*)((uint8_t*)seg + sizeof(struct segment_command_64));
                for (int j = 0; j < seg->nsects; ++j){
                    if ((strncmp(sect[j].sectname, sectname, 16) == 0) && (strncmp(sect[j].segname, segname, 16) == 0)) {
                        *size = sect[j].size;
                        uint8_t* result = (uint8_t*)(sect[j].addr + slide);
                        return result;
                    }
                }
            }
        }
        else if (lc->cmd == LC_SEGMENT) {
            const struct segment_command* seg = (struct segment_command*)lc;
            if (strcmp(seg->segname, "__TEXT") == 0) {
                slide = (uintptr_t)mh - seg->vmaddr;
            }
            if (strncmp(seg->segname, segname, 16) == 0) {
                const struct section* sect = (struct section*)((uint8_t*)seg + sizeof(struct segment_command));
                for (int j = 0; j < seg->nsects; ++j){
                    if ((strncmp(sect[j].sectname, sectname, 16) == 0) && (strncmp(sect[j].segname, segname, 16) == 0)) {
                        *size = sect[j].size;
                        return (uint8_t*)(sect[j].addr + slide);
                    }
                }
            }
        }
        lc = (struct load_command*)((uint8_t*)lc + lc->cmdsize);
    }
    return NULL;
}

#ifndef __LP64__
uint8_t *
getsegmentdata(
const struct mach_header *mhp,
const char *segname,
unsigned long *size)
{
    struct segment_command *sgp;
    uintptr_t slide;
    uint32_t i;

	slide = 0;
	sgp = (struct segment_command *)
	      ((char *)mhp + sizeof(struct mach_header));
	for(i = 0; i < mhp->ncmds; i++){
	    if(sgp->cmd == LC_SEGMENT){
		if(strcmp(sgp->segname, "__TEXT") == 0){
		    slide = (uintptr_t)mhp - sgp->vmaddr;
		}
		if(strncmp(sgp->segname, segname, sizeof(sgp->segname)) == 0){
		    *size = sgp->vmsize;
		    return((uint8_t *)(sgp->vmaddr + slide));
		}
	    }
	    sgp = (struct segment_command *)((char *)sgp + sgp->cmdsize);
	}
	return(0);
}

#else /* defined(__LP64__) */


uint8_t * 
getsegmentdata(
const struct mach_header_64 *mhp,
const char *segname,
unsigned long *size)
{
    struct segment_command_64 *sgp;
    intptr_t slide;
    uint32_t i;

	slide = 0;
	sgp = (struct segment_command_64 *)
	      ((char *)mhp + sizeof(struct mach_header_64));
	for(i = 0; i < mhp->ncmds; i++){
	    if(sgp->cmd == LC_SEGMENT_64){
		if(strcmp(sgp->segname, "__TEXT") == 0){
		    slide = (uintptr_t)mhp - sgp->vmaddr;
		}
		if(strncmp(sgp->segname, segname, sizeof(sgp->segname)) == 0){
		    *size = sgp->vmsize;
		    return((uint8_t *)(sgp->vmaddr + slide));
		}
	    }
	    sgp = (struct segment_command_64 *)((char *)sgp + sgp->cmdsize);
	}
	return(0);
}

#endif /* defined(__LP64__) */

#ifndef CCTB_DRIVERKIT_ENABLED

/*
 * This routine returns the a pointer to the data for the named section in the
 * named segment if it exist in the mach header passed to it.  Also it returns
 * the size of the section data indirectly through the pointer size.  Otherwise
 * it returns zero for the pointer and the size.
 */
char *
getsectdatafromheader(
struct mach_header *mhp,
const char *segname,
const char *sectname,
uint32_t *size)
{
    const struct section *sp;

	sp = getsectbynamefromheader(mhp, segname, sectname);
	if(sp == NULL){
	    *size = 0;
	    return(NULL);
	}
	*size = sp->size;
	return((char *)((uintptr_t)(sp->addr)));
}

/*
 * This routine returns the a pointer to the data for the named section in the
 * named segment if it exist in the 64-bit mach header passed to it.  Also it
 * returns the size of the section data indirectly through the pointer size.
 * Otherwise it returns zero for the pointer and the size.
 */
char *
getsectdatafromheader_64(
struct mach_header_64 *mhp,
const char *segname,
const char *sectname,
uint64_t *size)
{
    const struct section_64 *sp;

	sp = getsectbynamefromheader_64(mhp, segname, sectname);
	if(sp == NULL){
	    *size = 0;
	    return(NULL);
	}
	*size = sp->size;
	return((char *)((uintptr_t)(sp->addr)));
}

#ifdef __DYNAMIC__
/*
 * This routine returns the a pointer to the data for the named section in the
 * named segment if it exist in the named Framework.  Also it returns the size
 * of the section data indirectly through the pointer size.  Otherwise it
 * returns zero for the pointer and the size.  The last component of the path
 * of the Framework is passed as FrameworkName.
 */
void *
getsectdatafromFramework(
const char *FrameworkName,
const char *segname,
const char *sectname,
unsigned long *size)
{
    uint32_t i;
    unsigned long n;
    uintptr_t vmaddr_slide;
#ifndef __LP64__
    struct mach_header *mh;
    const struct section *s;
#else /* defined(__LP64__) */
    struct mach_header_64 *mh;
    const struct section_64 *s;
#endif /* defined(__LP64__) */
    char *name, *p;

        n = _dyld_image_count();
        for(i = 0; i < n ; i++){
            name = _dyld_get_image_name(i);
            p = strrchr(name, '/');
            if(p != NULL && p[1] != '\0')
                name = p + 1;
            if(strcmp(name, FrameworkName) != 0)
                continue;
            mh = _dyld_get_image_header(i);
            vmaddr_slide = _dyld_get_image_vmaddr_slide(i);
#ifndef __LP64__
            s = getsectbynamefromheader(mh, segname, sectname);
#else /* defined(__LP64__) */
            s = getsectbynamefromheader_64(mh, segname, sectname);
#endif /* defined(__LP64__) */
            if(s == NULL){
                *size = 0;
                return(NULL);
            }
            *size = s->size;
            return((void *)(s->addr + vmaddr_slide));
        }
        *size = 0;
        return(NULL);
}
#endif /* __DYNAMIC__ */
#endif /* !defined(CCTB_DRIVERKIT_ENABLED) */

#pragma clang diagnostic pop

#endif /* !defined(RLD) */
