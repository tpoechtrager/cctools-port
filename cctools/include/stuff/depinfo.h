/*
 * Copyright (c) 2019 Apple Computer, Inc. All rights reserved.
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
/*
 *  depinfo.h
 *  cctools libstuff
 *
 *  Created by Michael Trent on 9/9/19.
 */

#ifndef depinfo_h
#define depinfo_h

#include <stdint.h>

#if CLANG_VERSION_MAJOR >= 8 /* cctools-port */
enum : uint8_t {
#else
enum {
#endif
    DEPINFO_TOOL          = 0x00,
    DEPINFO_INPUT_FOUND   = 0x10,
    DEPINFO_INPUT_MISSING = 0x11,
    DEPINFO_OUTPUT        = 0x40,
};

#if CLANG_VERSION_MAJOR >= 8 /* cctools-port */
enum : uint32_t {
#else
enum {
#endif
    DI_READ_NONE          = 0,
    DI_READ_LOG           = (1 << 0),
    DI_READ_NORETVAL      = (1 << 1),
};

struct depinfo;

struct depinfo* depinfo_alloc(void);

void depinfo_free(struct depinfo* depinfo);
void depinfo_add(struct depinfo* depinfo, uint8_t opcode, const char* string);
int  depinfo_count(struct depinfo* depinfo);
int  depinfo_get(struct depinfo* depinfo, int index, uint8_t* opcode,
                const char** string);
void depinfo_sort(struct depinfo* depinfo);

struct depinfo* depinfo_read(const char* path, uint32_t flags);
int  depinfo_write(struct depinfo* depinfo, const char* path);

#endif /* depinfo_h */
