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
 *  args.h
 *  cctools libstuff
 *
 *  Created by Michael Trent on 5/31/19.
 */

#ifndef args_h
#define args_h

/*
 * args_expand_at() recursively expands "@file" options as they appear in the
 * argc/argv options list.
 *
 * if "file" does not point to a valid file, the option "@file" will remain
 * in the options list.
 *
 * if "file" does point to a valid file, that file will be parsed as a series
 * of options separated by any amount of whitespace, and those options will
 * replace "@file" where it appears in the options list. options files may
 * themselves contain additional "@file" references, which will be recursively
 * expanded. to prevent infinite recursion, args_expand_at() will fail if it
 * attempts to load a "@file" path more than once.
 *
 * options are separated by any amount of whitespace. whitespace can be included
 * in an option if it is wrapped in single or double quotes. individual
 * characters can also be escaped with a single backslash character; including
 * whitespace, quotes, and other backslashes.
 *
 * NB: args_expand_at() allows options to include quotes in the middle of the
 * string; e.g., "one' 'option" will expand to "one option" rather than "one"
 * and "option". This is consistent with unix shell behavior, but not consistent
 * with some other implementations of the @file command-line option.

 * BUG: args_expand_at() will not return an error if an option contains an
 * unterminated quote character. The string "'one more time" will yield a single
 * option "'one more time".
 *
 * BUG: memory pointed to by argv_p is not freed if the pointer is replaced.
 * Also, it is not clear how the argv array and its contents should be freed
 * if the array is modified.
 */
int args_expand_at(int* argc_p, char** argv_p[]);

#endif /* args_h */
