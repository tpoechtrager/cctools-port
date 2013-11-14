/*
 * Copyright (c) 1999-2003 Apple Computer, Inc.  All Rights Reserved.
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
#if defined(__MWERKS__) && !defined(__private_extern__)
#define __private_extern__ __declspec(private_extern)
#endif

/*
 * Global types, variables and routines declared in the file ld.c.
 *
 * The following include file need to be included before this file:
 * #include <sys/loader.h>
 * #include <mach.h>
 * #include <stdarg.h>  (included in <stdio.h>)
 */

/* Type for the possible levels of stripping, in increasing order */
enum strip_levels {
    STRIP_NONE,
    STRIP_DUP_INCLS,
    STRIP_L_SYMBOLS,
    STRIP_MIN_DEBUG,
    STRIP_DEBUG,
    STRIP_NONGLOBALS,
    STRIP_DYNAMIC_EXECUTABLE,
    STRIP_ALL
};

/* The error level check for undefined symbols */
enum undefined_check_level {
    UNDEFINED_ERROR,
    UNDEFINED_WARNING,
    UNDEFINED_SUPPRESS,
    UNDEFINED_DYNAMIC_LOOKUP,
    UNDEFINED_DEFINE_A_WAY
};

/* The error level check for (twolevel namespace) multiply defined symbols */
enum multiply_defined_check_level {
    MULTIPLY_DEFINED_ERROR,
    MULTIPLY_DEFINED_WARNING,
    MULTIPLY_DEFINED_SUPPRESS
};

/* The error level check for read only relocs */
enum read_only_reloc_check_level {
    READ_ONLY_RELOC_ERROR,
    READ_ONLY_RELOC_WARNING,
    READ_ONLY_RELOC_SUPPRESS
};

/* The error level check for section difference relocs */
enum sect_diff_reloc_check_level {
    SECT_DIFF_RELOC_ERROR,
    SECT_DIFF_RELOC_WARNING,
    SECT_DIFF_RELOC_SUPPRESS
};

/* The error handling for weak reference mismatches */
enum weak_reference_mismatches_handling {
    WEAK_REFS_MISMATCH_ERROR,
    WEAK_REFS_MISMATCH_WEAK,
    WEAK_REFS_MISMATCH_NON_WEAK
};

extern struct macosx_deployment_target macosx_deployment_target;

/* name of this program as executed (argv[0]) */
extern char *progname;
/* indication of an error set in error(), for processing a number of errors
   and then exiting */
extern unsigned long errors;
/* the pagesize of the machine this program is running on, getpagesize() value*/
extern unsigned long host_pagesize;
/* the byte sex of the machine this program is running on */
extern enum byte_sex host_byte_sex;

/* name of output file */
extern char *outputfile;
/* type of output file */
extern unsigned long filetype;
/* multi or single module dylib output */
extern enum bool multi_module_dylib;

/*
 * The architecture of the output file as specified by -arch and the cputype
 * and cpusubtype of the object files being loaded which will be the output
 * cputype and cpusubtype.  specific_arch_flag is true if an -arch flag is
 * specified and the flag for a specific implementation of an architecture.
 */
extern struct arch_flag arch_flag;
extern enum bool specific_arch_flag;

/*
 * The -force_cpusubtype_ALL flag.
 */
extern enum bool force_cpusubtype_ALL;

/* the byte sex of the output file */
extern enum byte_sex target_byte_sex;

extern
enum bool trace;		/* print stages of link-editing */
extern
enum bool save_reloc;		/* save relocation information */
extern
enum bool output_for_dyld;	/* produce output for use with dyld */
extern
enum bool bind_at_load;		/* mark the output for dyld to be bound
				   when loaded */
extern
enum bool no_fix_prebinding;	/* mark the output for dyld to never
				   run fix_prebinding */
extern
enum bool load_map;		/* print a load map */
extern
enum bool define_comldsyms;	/* define common and link-editor defined
					   symbol reguardless of file type */
extern
enum bool seglinkedit;		/* create the link edit segment */
extern
enum bool whyload;		/* print why archive members are
					   loaded */
extern
enum bool flush;		/* Use the output_flush routine to flush
				   output file by pages */
extern
enum bool sectorder_detail;	/* print sectorder warnings in detail */
extern
enum bool nowarnings;		/* suppress warnings */
extern
enum bool no_arch_warnings;	/* suppress wrong arch warnings */
extern
enum bool arch_errors_fatal;	/* cause wrong arch errors to be fatal */
extern
enum bool archive_ObjC;		/* objective-C archive semantics */
extern
enum bool archive_all;		/* always load everything in archives */
extern
enum bool keep_private_externs;	/* don't turn private externs into
					   non-external symbols */
/* TRUE if -dynamic is specified, FALSE if -static is specified */
extern enum bool dynamic;

/* The level of symbol table stripping */
extern enum strip_levels strip_level;
/* Strip the base file symbols (the -A argument's symbols) */
extern enum bool strip_base_symbols;

/* strip dead blocks */
extern enum bool dead_strip;
/* don't strip module init and term sections */
extern enum bool no_dead_strip_inits_and_terms;
/* print timings for dead striping code */
extern enum bool dead_strip_times;

#ifndef RLD
/*
 * Data structures to perform selective exporting of global symbols.
 * save_symbols is the names of the symbols from -exported_symbols_list
 * remove_symbols is the names of the symbols from -unexported_symbols_list
 */
extern struct symbol_list *save_symbols;
extern uint32_t nsave_symbols;
extern struct symbol_list *remove_symbols;
extern uint32_t nremove_symbols;

/*
 * -executable_path option's argument, executable_path is used to replace
 * "@executable_path for dependent libraries.
 */
extern char *executable_path;
#endif /* RLD */

/* The list of symbols to be traced */
extern char **trace_syms;
extern unsigned long ntrace_syms;

/* The number of references of undefined symbols to print */
extern unsigned long Yflag;

/* The list of allowed undefined symbols */
extern char **undef_syms;
extern unsigned long nundef_syms;

/* The list of -dylib_file arguments */
extern char **dylib_files;
extern unsigned long ndylib_files;

/* The checking for undefined symbols */
extern enum undefined_check_level undefined_flag;

/* The checking for (twolevel namespace) multiply defined symbols */
extern enum multiply_defined_check_level
    multiply_defined_flag;
extern enum multiply_defined_check_level
    multiply_defined_unused_flag;
/* the -nomultidefs option */
extern enum bool nomultidefs;

/* The checking for read only relocs */
extern enum read_only_reloc_check_level read_only_reloc_flag;

/* The checking for pic relocs */
extern enum sect_diff_reloc_check_level sect_diff_reloc_flag;

/* The handling for weak reference mismatches */
extern enum weak_reference_mismatches_handling
    weak_reference_mismatches;

/* The prebinding optimization */
extern enum bool prebinding;
extern enum bool prebind_allow_overlap;
extern enum bool prebind_all_twolevel_modules;

/* True if -m is specified to allow multiply symbols, as a warning */
extern enum bool allow_multiply_defined_symbols;

/* The segment alignment */
extern unsigned long segalign;
#ifndef RLD
extern enum bool segalign_specified;
#endif /* !defined(RLD) */
/* The size of pagezero from the -pagezero_size flag */
extern unsigned long pagezero_size;
/* The maximum segment alignment allowed to be specified, in hex */
#define MAXSEGALIGN		0x8000
/* The default section alignment */
extern unsigned long defaultsectalign;
/* The maximum section alignment allowed to be specified, as a power of two */
#define MAXSECTALIGN		15 /* 2**15 or 0x8000 */
/* The default section alignment if not specified, as a power of two */
#define DEFAULTSECTALIGN	4  /* 2**4 or 16 */

/* The first segment address */
extern unsigned long seg1addr;
extern enum bool seg1addr_specified;

/* read-only and read-write segment addresses */
extern unsigned long segs_read_only_addr;
extern enum bool segs_read_only_addr_specified;
extern unsigned long segs_read_write_addr;
extern enum bool segs_read_write_addr_specified;

/* The stack address and size */
extern unsigned long stack_addr;
extern enum bool stack_addr_specified;
extern unsigned long stack_size;
extern enum bool stack_size_specified;

/* TRUE if -allow_stack_execute is specified */
extern enum bool allow_stack_execute;

/* The header pad */
extern unsigned long headerpad;
/*
 * If specified makes sure the header pad is big enough to change all the
 * install name of the dylibs in the output to MAXPATHLEN.
 */
extern enum bool headerpad_max_install_names;

/* The name of the specified entry point */
extern char *entry_point_name;

/* The name of the specified library initialization routine */
extern char *init_name;

/* The dylib information */
extern char *dylib_install_name;
extern uint32_t dylib_current_version;
extern uint32_t dylib_compatibility_version;

/* the umbrella/sub framework information */
extern enum bool sub_framework;
extern enum bool umbrella_framework;
extern char *sub_framework_name;
extern char *umbrella_framework_name;
extern char *client_name;
extern char **allowable_clients;
extern unsigned long nallowable_clients;

/* The list of sub_umbrella frameworks */
extern char **sub_umbrellas;
extern unsigned long nsub_umbrellas;

/* The list of sub_library dynamic libraries */
extern char **sub_librarys;
extern unsigned long nsub_librarys;

/* The dylinker information */
extern char *dylinker_install_name;

/* set to TRUE if -private_bundle is specified */
extern enum bool private_bundle;

/* The value of the environment variable NEXT_ROOT */
extern char *next_root;

/* TRUE if the environment variable LD_TRACE_ARCHIVES
   (or temporarily RC_TRACE_ARCHIVES) is set */
extern enum bool ld_trace_archives;

/* TRUE if the environment variable LD_TRACE_DYLIBS
   (or temporarily RC_TRACE_DYLIBS) is set */
extern enum bool ld_trace_dylibs;

/* TRUE if the environment variable LD_TRACE_PREBINDING_DISABLED
   (or temporarily RC_TRACE_PREBINDING_DISABLED) is set */
extern enum bool ld_trace_prebinding_disabled;

/* the argument to -final_output if any */
extern char *final_output;

/* The variables to support namespace options */
extern enum bool namespace_specified;
extern enum bool twolevel_namespace;
extern enum bool force_flat_namespace;

extern enum bool twolevel_namespace_hints;

extern void *allocate(
    unsigned long size);
extern void *reallocate(
    void *,
    unsigned long size);
extern unsigned long rnd(
    unsigned long v,
    unsigned long r);
extern void tell_ProjectBuilder(
    char *message);
extern void print(
    const char *format, ...) __attribute__ ((format (printf, 1, 2)));
extern void ld_trace(
    const char *format, ...) __attribute__ ((format (printf, 1, 2)));
extern void vprint(
    const char *format, va_list ap);
extern void warning(
    const char *format, ...) __attribute__ ((format (printf, 1, 2)));
extern void error(
    const char *format, ...) __attribute__ ((format (printf, 1, 2)));
extern void fatal(
    const char *format, ...) __attribute__ ((format (printf, 1, 2)));
extern void warning_with_cur_obj(
    const char *format, ...) __attribute__ ((format (printf, 1, 2)));
extern void error_with_cur_obj(
    const char *format, ...) __attribute__ ((format (printf, 1, 2)));
extern void system_warning(
    const char *format, ...) __attribute__ ((format (printf, 1, 2)));
extern void system_error(
    const char *format, ...) __attribute__ ((format (printf, 1, 2)));
extern void system_fatal(
    const char *format, ...) __attribute__ ((format (printf, 1, 2)));
extern void mach_fatal(
    kern_return_t r,
    char *format, ...) __attribute__ ((format (printf, 2, 3)));

#ifdef DEBUG
extern unsigned long debug;		/* link-editor debugging */
#endif /* DEBUG */
