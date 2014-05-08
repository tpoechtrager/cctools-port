/*
 * Copyright (c) 2006 Apple Computer, Inc. All rights reserved.
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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include "stuff/errors.h"
#include "stuff/breakout.h"
#include "stuff/rnd.h"
#include "stuff/allocate.h"

/*
 * The structure that holds the -a <arch> <size> information from the command
 * line flags.
 */
struct arch_sign {
    struct arch_flag arch_flag;
    uint32_t datasize;
    enum bool found;
};
struct arch_sign *arch_signs;
uint32_t narch_signs = 0;

/* used by error routines as the name of the program */
char *progname = NULL;

static void usage(
    void);

static void process(
    struct arch *archs,
    uint32_t narchs);

static void setup_code_signature(
    struct arch *arch,
    struct member *member,
    struct object *object);

static struct linkedit_data_command *add_code_sig_load_command(
    struct arch *arch,
    char *arch_name);

/* apple_version is created by the libstuff/Makefile */
extern char apple_version[];
char *version = apple_version;

/*
 * The codesign_allocate(1) tool has the following usage:
 *
 *	codesign_allocate -i oldfile -a arch size ...  -o newfile
 * 
 * Where the oldfile is a Mach-O file that is input for the dynamic linker
 * and it creates or adds an 
 */
int
main(
int argc,
char **argv,
char **envp)
{
    uint32_t i;
    char *input, *output, *endp;
    struct arch *archs;
    uint32_t narchs;

	progname = argv[0];
	input = NULL;
	output = NULL;
	archs = NULL;
	narchs = 0;
	for(i = 1; i < argc; i++){
	    if(strcmp(argv[i], "-i") == 0){
		if(i + 1 == argc){
		    error("missing argument to: %s option", argv[i]);
		    usage();
		}
		if(input != NULL){
		    error("more than one: %s option specified", argv[i]);
		    usage();
		}
		input = argv[i+1];
		i++;
	    }
	    else if(strcmp(argv[i], "-o") == 0){
		if(i + 1 == argc){
		    error("missing argument to: %s option", argv[i]);
		    usage();
		}
		if(output != NULL){
		    error("more than one: %s option specified", argv[i]);
		    usage();
		}
		output = argv[i+1];
		i++;
	    }
	    else if(strcmp(argv[i], "-a") == 0){
		if(i + 2 == argc){
		    error("missing argument(s) to: %s option", argv[i]);
		    usage();
		}
		else{
		    arch_signs = reallocate(arch_signs,
			    (narch_signs + 1) * sizeof(struct arch_sign));
		    if(get_arch_from_flag(argv[i+1],
				  &(arch_signs[narch_signs].arch_flag)) == 0){
			error("unknown architecture specification flag: "
			      "%s %s %s", argv[i], argv[i+1], argv[i+2]);
			arch_usage();
			usage();
		    }
		    arch_signs[narch_signs].datasize =
			strtoul(argv[i+2], &endp, 0);
		    if(*endp != '\0')
			fatal("size for '-a %s %s' not a proper number",
			      argv[i+1], argv[i+2]);
		    if((arch_signs[narch_signs].datasize % 16) != 0)
			fatal("size for '-a %s %s' not a multiple of 16",
			      argv[i+1], argv[i+2]);
		    arch_signs[narch_signs].found = FALSE;
		    narch_signs++;
		    i += 2;
		}
	    }
	    else if(strcmp(argv[i], "-A") == 0){
		if(i + 3 == argc){
		    error("missing argument(s) to: %s option", argv[i]);
		    usage();
		}
		else{
		    arch_signs = reallocate(arch_signs,
			    (narch_signs + 1) * sizeof(struct arch_sign));

		    arch_signs[narch_signs].arch_flag.cputype = 
			strtoul(argv[i+1], &endp, 0);
		    if(*endp != '\0')
			fatal("cputype for '-A %s %s %s' not a proper number",
			      argv[i+1], argv[i+2], argv[i+3]);

		    arch_signs[narch_signs].arch_flag.cpusubtype = 
			strtoul(argv[i+2], &endp, 0);
		    if(*endp != '\0')
			fatal("cpusubtype for '-A %s %s %s' not a proper "
			      "number", argv[i+1], argv[i+2], argv[i+3]);

		    arch_signs[narch_signs].arch_flag.name = (char *)
			get_arch_name_from_types(
			    arch_signs[narch_signs].arch_flag.cputype,
			    arch_signs[narch_signs].arch_flag.cpusubtype);

		    arch_signs[narch_signs].datasize =
			strtoul(argv[i+3], &endp, 0);
		    if(*endp != '\0')
			fatal("size for '-A %s %s %s' not a proper number",
			      argv[i+1], argv[i+2], argv[i+3]);
		    if((arch_signs[narch_signs].datasize % 16) != 0)
			fatal("size for '-A %s %s %s' not a multiple of 16",
			      argv[i+1], argv[i+2], argv[i+3]);

		    arch_signs[narch_signs].found = FALSE;
		    narch_signs++;
		    i += 3;
		}
	    }
	    else{
		error("unknown flag: %s", argv[i]);
		usage();
	    }
	}
	if(input == NULL || output == NULL || narch_signs == 0)
	    usage();

	breakout(input, &archs, &narchs, FALSE);
	if(errors)
	    exit(EXIT_FAILURE);

	checkout(archs, narchs);

	process(archs, narchs);

	for(i = 0; i < narch_signs; i++){
	    if(arch_signs[i].found == FALSE)
		fatal("input file: %s does not contain a matching architecture "
		      "for specified '-a %s %u' option", input,
		      arch_signs[i].arch_flag.name, arch_signs[i].datasize);
	}

	writeout(archs, narchs, output, 0777, TRUE, FALSE, FALSE, NULL);

	if(errors)
	    return(EXIT_FAILURE);
	else
	    return(EXIT_SUCCESS);
}

/*
 * usage() prints the current usage message and exits indicating failure.
 */
static
void
usage(
void)
{
	fprintf(stderr, "Usage: %s -i input [-a <arch> <size>]... "
		"[-A <cputype> <cpusubtype> <size>]... -o output\n",
		progname);
	exit(EXIT_FAILURE);
}

/*
 * process() walks the archs and calls setup_code_signature() to do the
 * work.
 */
static
void
process(
struct arch *archs,
uint32_t narchs)
{
    uint32_t i, j, offset, size;

	for(i = 0; i < narchs; i++){
	    /*
	     * Given that code signing is "meta" information about the file and 
	     * so does not really alter the "content" of the Mach-o file.
	     * codesign_allocate should never update the LC_ID_DYLIB timestamp.
	     */
	    archs[i].dont_update_LC_ID_DYLIB_timestamp = TRUE;

	    if(archs[i].type == OFILE_ARCHIVE){
		for(j = 0; j < archs[i].nmembers; j++){
		    if(archs[i].members[j].type == OFILE_Mach_O){
			setup_code_signature(archs + i, archs[i].members + j,
					     archs[i].members[j].object);
		    }
		}
		/*
		 * Reset the library offsets and size.
		 */
		offset = 0;
		for(j = 0; j < archs[i].nmembers; j++){
		    archs[i].members[j].offset = offset;
		    size = 0;
		    if(archs[i].members[j].member_long_name == TRUE){
			size = rnd(archs[i].members[j].member_name_size,
				     sizeof(long));
			archs[i].toc_long_name = TRUE;
		    }
		    if(archs[i].members[j].object != NULL){
			size += archs[i].members[j].object->object_size
			   - archs[i].members[j].object->input_sym_info_size
			   + archs[i].members[j].object->output_sym_info_size;
			sprintf(archs[i].members[j].ar_hdr->ar_size, "%-*ld",
			       (int)sizeof(archs[i].members[j].ar_hdr->ar_size),
			       (long)(size));
			/*
			 * This has to be done by hand because sprintf puts a
			 * null at the end of the buffer.
			 */
			memcpy(archs[i].members[j].ar_hdr->ar_fmag, ARFMAG,
			      (int)sizeof(archs[i].members[j].ar_hdr->ar_fmag));
		    }
		    else{
			size += archs[i].members[j].unknown_size;
		    }
		    offset += sizeof(struct ar_hdr) + size;
		}
		archs[i].library_size = offset;
	    }
	    else if(archs[i].type == OFILE_Mach_O){
		setup_code_signature(archs + i, NULL, archs[i].object);
	    }
	}
}

/*
 * setup_code_signature() does the work to add or update the needed
 * LC_CODE_SIGNATURE load command for the specified broken out ofile if it
 * is of one of the architecures specifed with a -a command line options.
 */
static
void
setup_code_signature(
struct arch *arch,
struct member *member,
struct object *object)
{
    uint32_t i;
    cpu_type_t cputype;
    cpu_subtype_t cpusubtype;
    uint32_t flags, linkedit_end;
    uint32_t dyld_info_start;
    uint32_t dyld_info_end;
    uint32_t align_delta;

	linkedit_end = 0;
	/*
	 * First set up all the pointers and sizes of the symbolic info.
	 */
	if(object->st != NULL && object->st->nsyms != 0){
	    if(object->mh != NULL){
		object->output_symbols = (struct nlist *)
		    (object->object_addr + object->st->symoff);
		if(object->object_byte_sex != get_host_byte_sex())
		    swap_nlist(object->output_symbols,
			       object->st->nsyms,
			       get_host_byte_sex());
		object->output_symbols64 = NULL;
	    }
	    else{
		object->output_symbols64 = (struct nlist_64 *)
		    (object->object_addr + object->st->symoff);
		if(object->object_byte_sex != get_host_byte_sex())
		    swap_nlist_64(object->output_symbols64,
				  object->st->nsyms,
				  get_host_byte_sex());
		object->output_symbols = NULL;
	    }
	    object->output_nsymbols = object->st->nsyms;
	    object->output_strings =
		object->object_addr + object->st->stroff;
	    object->output_strings_size = object->st->strsize;
	    if(object->mh != NULL){
		object->input_sym_info_size =
		    object->st->nsyms * sizeof(struct nlist) +
		    object->st->strsize;
	    }
	    else{
		object->input_sym_info_size =
		    object->st->nsyms * sizeof(struct nlist_64) +
		    object->st->strsize;
	    }
	}
	if(object->dyld_info != NULL){
	    /* there are five parts to the dyld info, but
	     codesign_allocate does not alter them, so copy as a block */
	    dyld_info_start = 0;
	    if (object->dyld_info->rebase_off != 0)
		dyld_info_start = object->dyld_info->rebase_off;
	    else if (object->dyld_info->bind_off != 0)
		dyld_info_start = object->dyld_info->bind_off;
	    else if (object->dyld_info->weak_bind_off != 0)
		dyld_info_start = object->dyld_info->weak_bind_off;
	    else if (object->dyld_info->lazy_bind_off != 0)
		dyld_info_start = object->dyld_info->lazy_bind_off;
	    else if (object->dyld_info->export_off != 0)
		dyld_info_start = object->dyld_info->export_off;
	    dyld_info_end = 0;
	    if (object->dyld_info->export_size != 0)
		dyld_info_end = object->dyld_info->export_off
		    + object->dyld_info->export_size;
	    else if (object->dyld_info->lazy_bind_size != 0)
		dyld_info_end = object->dyld_info->lazy_bind_off
		    + object->dyld_info->lazy_bind_size;
	    else if (object->dyld_info->weak_bind_size != 0)
		dyld_info_end = object->dyld_info->weak_bind_off
		    + object->dyld_info->weak_bind_size;
	    else if (object->dyld_info->bind_size != 0)
		dyld_info_end = object->dyld_info->bind_off
		    + object->dyld_info->bind_size;
	    else if (object->dyld_info->rebase_size != 0)
		dyld_info_end = object->dyld_info->rebase_off
		    + object->dyld_info->rebase_size;
	    object->output_dyld_info = object->object_addr + dyld_info_start; 
	    object->output_dyld_info_size = dyld_info_end - dyld_info_start;
	    object->output_sym_info_size += object->output_dyld_info_size;
	}
	if(object->dyst != NULL){
	    object->output_ilocalsym = object->dyst->ilocalsym;
	    object->output_nlocalsym = object->dyst->nlocalsym;
	    object->output_iextdefsym = object->dyst->iextdefsym;
	    object->output_nextdefsym = object->dyst->nextdefsym;
	    object->output_iundefsym = object->dyst->iundefsym;
	    object->output_nundefsym = object->dyst->nundefsym;
	    object->output_indirect_symtab = (uint32_t *)
		(object->object_addr + object->dyst->indirectsymoff);
	    object->output_loc_relocs = (struct relocation_info *)
		(object->object_addr + object->dyst->locreloff);
	    if(object->split_info_cmd != NULL){
		object->output_split_info_data = 
		(object->object_addr + object->split_info_cmd->dataoff);
		object->output_split_info_data_size = 
		    object->split_info_cmd->datasize;
	    }
	    if(object->func_starts_info_cmd != NULL){
		object->output_func_start_info_data = 
		(object->object_addr + object->func_starts_info_cmd->dataoff);
		object->output_func_start_info_data_size = 
		    object->func_starts_info_cmd->datasize;
	    }
	    if(object->data_in_code_cmd != NULL){
		object->output_data_in_code_info_data = 
		(object->object_addr + object->data_in_code_cmd->dataoff);
		object->output_data_in_code_info_data_size = 
		    object->data_in_code_cmd->datasize;
	    }
	    if(object->code_sign_drs_cmd != NULL){
		object->output_code_sign_drs_info_data = 
		(object->object_addr + object->code_sign_drs_cmd->dataoff);
		object->output_code_sign_drs_info_data_size = 
		    object->code_sign_drs_cmd->datasize;
	    }
	    if(object->link_opt_hint_cmd != NULL){
		object->output_link_opt_hint_info_data = 
		(object->object_addr + object->link_opt_hint_cmd->dataoff);
		object->output_link_opt_hint_info_data_size = 
		    object->link_opt_hint_cmd->datasize;
	    }
	    object->output_ext_relocs = (struct relocation_info *)
		(object->object_addr + object->dyst->extreloff);
	    object->output_tocs =
		(struct dylib_table_of_contents *)
		(object->object_addr + object->dyst->tocoff);
	    object->output_ntoc = object->dyst->ntoc;
	    if(object->mh != NULL){
		object->output_mods = (struct dylib_module *)
		    (object->object_addr + object->dyst->modtaboff);
		object->output_mods64 = NULL;
	    }
	    else{
		object->output_mods64 = (struct dylib_module_64 *)
		    (object->object_addr + object->dyst->modtaboff);
		object->output_mods = NULL;
	    }
	    object->output_nmodtab = object->dyst->nmodtab;
	    object->output_refs = (struct dylib_reference *)
		(object->object_addr + object->dyst->extrefsymoff);
	    object->output_nextrefsyms = object->dyst->nextrefsyms;
	    if(object->hints_cmd != NULL){
		object->output_hints = (struct twolevel_hint *)
		    (object->object_addr +
		     object->hints_cmd->offset);
	    }
	    if(object->dyld_info != NULL){
		object->input_sym_info_size += object->dyld_info->rebase_size
					    + object->dyld_info->bind_size
					    + object->dyld_info->weak_bind_size
					    + object->dyld_info->lazy_bind_size
					    + object->dyld_info->export_size;
	    }
	    object->input_sym_info_size +=
		object->dyst->nlocrel *
		    sizeof(struct relocation_info) +
		object->dyst->nextrel *
		    sizeof(struct relocation_info) +
		object->dyst->ntoc *
		    sizeof(struct dylib_table_of_contents)+
		object->dyst->nextrefsyms *
		    sizeof(struct dylib_reference);
	    if(object->split_info_cmd != NULL)
		object->input_sym_info_size += object->split_info_cmd->datasize;
	    if(object->func_starts_info_cmd != NULL)
		object->input_sym_info_size +=
		    object->func_starts_info_cmd->datasize;
	    if(object->data_in_code_cmd != NULL)
		object->input_sym_info_size +=
		    object->data_in_code_cmd->datasize;
	    if(object->code_sign_drs_cmd != NULL)
		object->input_sym_info_size +=
		    object->code_sign_drs_cmd->datasize;
	    if(object->link_opt_hint_cmd != NULL)
		object->input_sym_info_size +=
		    object->link_opt_hint_cmd->datasize;
	    if(object->mh != NULL){
		object->input_sym_info_size +=
		    object->dyst->nmodtab *
			sizeof(struct dylib_module) +
		    object->dyst->nindirectsyms *
			sizeof(uint32_t);
	    }
	    else{
		object->input_sym_info_size +=
		    object->dyst->nmodtab *
			sizeof(struct dylib_module_64) +
		    object->dyst->nindirectsyms *
			sizeof(uint32_t) +
		    object->input_indirectsym_pad;
	    }
	    if(object->hints_cmd != NULL){
		object->input_sym_info_size +=
		    object->hints_cmd->nhints *
		    sizeof(struct twolevel_hint);
	    }
	}
	object->output_sym_info_size = object->input_sym_info_size;
	if(object->code_sig_cmd != NULL){
	    object->input_sym_info_size = rnd(object->input_sym_info_size,
						16);
	    object->input_sym_info_size += object->code_sig_cmd->datasize;
	}

	/*
	 * Now see if one of the -a flags matches this object.
	 */
	if(object->mh != NULL){
	    cputype = object->mh->cputype;
	    cpusubtype = object->mh->cpusubtype & ~CPU_SUBTYPE_MASK;
	    flags = object->mh->flags;
	}
	else{
	    cputype = object->mh64->cputype;
	    cpusubtype = object->mh64->cpusubtype & ~CPU_SUBTYPE_MASK;
	    flags = object->mh64->flags;
	}
	for(i = 0; i < narch_signs; i++){
	    if(arch_signs[i].arch_flag.cputype == cputype &&
	       arch_signs[i].arch_flag.cpusubtype == cpusubtype)
		break;
	}
	/*
	 * If we didn't find a matching -a flag then just use the existing
	 * code signature if any.
	 */
	if(i >= narch_signs){
	    if(object->code_sig_cmd != NULL){
		object->output_code_sig_data_size =
		    object->code_sig_cmd->datasize;
	    }
	    object->output_sym_info_size = object->input_sym_info_size;
	    return;
	}

	/*
	 * We did find a matching -a flag for this object
	 */
	arch_signs[i].found = TRUE;

	/*
	 * We now allow statically linked objects as well as objects that are
	 * input for the dynamic linker or an MH_OBJECT filetypes to have
	 * code signatures.  So no checks are done here anymore based on the
	 * flags or filetype in the mach_header.
	 */
	
	/*
	 * If this has a code signature load command reuse it and just change
	 * the size of that data.  But do not use the old data.
	 */
	if(object->code_sig_cmd != NULL){
	    if(object->seg_linkedit != NULL){
		object->seg_linkedit->filesize +=
		    arch_signs[i].datasize - object->code_sig_cmd->datasize;
		if(object->seg_linkedit->filesize >
		   object->seg_linkedit->vmsize)
		    object->seg_linkedit->vmsize =
			rnd(object->seg_linkedit->filesize,
			      get_segalign_from_flag(&arch_signs[i].arch_flag));
	    }
	    else if(object->seg_linkedit64 != NULL){
		object->seg_linkedit64->filesize +=
		    arch_signs[i].datasize;
		object->seg_linkedit64->filesize -=
		    object->code_sig_cmd->datasize;
		if(object->seg_linkedit64->filesize >
		   object->seg_linkedit64->vmsize)
		    object->seg_linkedit64->vmsize =
			rnd(object->seg_linkedit64->filesize,
			      get_segalign_from_flag(&arch_signs[i].arch_flag));
	    }

	    object->code_sig_cmd->datasize = arch_signs[i].datasize;
	    object->output_code_sig_data_size = arch_signs[i].datasize;
	    object->output_code_sig_data = NULL;

	    object->output_sym_info_size = rnd(object->output_sym_info_size,
						 16);
	    object->output_sym_info_size += object->code_sig_cmd->datasize;
	}
	/*
	 * The object does not have a code signature load command we add one.
	 * And if that does not fail we then set the new load command's size and
	 * offset of the code signature data to allocate in the object.  We also
	 * adjust the linkedit's segment size.
	 */
	else{
	    object->code_sig_cmd = add_code_sig_load_command(arch,
						arch_signs[i].arch_flag.name);
	    object->code_sig_cmd->datasize = arch_signs[i].datasize;
	    if(object->seg_linkedit != NULL)
		linkedit_end = object->seg_linkedit->fileoff +
			       object->seg_linkedit->filesize;
	    else if(object->seg_linkedit64 != NULL)
		linkedit_end = object->seg_linkedit64->fileoff +
			       object->seg_linkedit64->filesize;
	    else if(object->mh_filetype == MH_OBJECT)
		linkedit_end = object->object_size;
	    else
		fatal("can't allocate code signature data for: %s (for "
		      "architecture %s) because file does not have a "
		      SEG_LINKEDIT " segment", arch->file_name,
		      arch_signs[i].arch_flag.name);

	    object->code_sig_cmd->dataoff = rnd(linkedit_end, 16);
	    object->output_code_sig_data_size = arch_signs[i].datasize;
	    object->output_code_sig_data = NULL;
	    align_delta = object->code_sig_cmd->dataoff - linkedit_end;

	    if(object->output_sym_info_size != 0)
	        object->output_sym_info_size = rnd(object->output_sym_info_size,
						   16);
	    else
		object->output_sym_info_size = align_delta;
	    object->output_sym_info_size += object->code_sig_cmd->datasize;

	    if(object->seg_linkedit != NULL){
		object->seg_linkedit->filesize =
		    rnd(object->seg_linkedit->filesize, 16) +
		    object->code_sig_cmd->datasize;
		if(object->seg_linkedit->filesize >
		   object->seg_linkedit->vmsize)
		    object->seg_linkedit->vmsize =
			rnd(object->seg_linkedit->filesize,
			      get_segalign_from_flag(&arch_signs[i].arch_flag));
	    }
	    else if(object->seg_linkedit64 != NULL){
		object->seg_linkedit64->filesize =
		    rnd(object->seg_linkedit64->filesize, 16) +
		    object->code_sig_cmd->datasize;
		if(object->seg_linkedit64->filesize >
		   object->seg_linkedit64->vmsize)
		    object->seg_linkedit64->vmsize =
			rnd(object->seg_linkedit64->filesize,
			      get_segalign_from_flag(&arch_signs[i].arch_flag));
	    }
	}
}

/*
 * add_code_sig_load_command() sees if there is space to add a code signature
 * load command for the specified arch and arch_name.  If so it returns a
 * pointer to the load command which the caller will fill in the dataoff and
 * datasize fields.  If it can't be added a fatal error message is printed
 * saying to relink the file.
 */
static
struct linkedit_data_command *
add_code_sig_load_command(
struct arch *arch,
char *arch_name)
{
    uint32_t i, j, low_fileoff;
    uint32_t ncmds, sizeofcmds, sizeof_mach_header;
    struct load_command *lc;
    struct segment_command *sg;
    struct segment_command_64 *sg64;
    struct section *s;
    struct section_64 *s64;
    struct linkedit_data_command *code_sig;

        if(arch->object->mh != NULL){
            ncmds = arch->object->mh->ncmds;
	    sizeofcmds = arch->object->mh->sizeofcmds;
	    sizeof_mach_header = sizeof(struct mach_header);
	}
	else{
            ncmds = arch->object->mh64->ncmds;
	    sizeofcmds = arch->object->mh64->sizeofcmds;
	    sizeof_mach_header = sizeof(struct mach_header_64);
	}

	/*
	 * The size of the new load commands that includes the added code
	 * signature load command is larger than the existing load commands, so
	 * see if they can be fitted in before the contents of the first section
	 * (or segment in the case of a LINKEDIT segment only file).
	 */
	low_fileoff = UINT_MAX;
	lc = arch->object->load_commands;
	for(i = 0; i < ncmds; i++){
	    if(lc->cmd == LC_SEGMENT){
		sg = (struct segment_command *)lc;
		s = (struct section *)
		    ((char *)sg + sizeof(struct segment_command));
		if(sg->nsects != 0){
		    for(j = 0; j < sg->nsects; j++){
			if(s->size != 0 &&
			(s->flags & S_ZEROFILL) != S_ZEROFILL &&
			(s->flags & S_THREAD_LOCAL_ZEROFILL) !=
				    S_THREAD_LOCAL_ZEROFILL &&
			s->offset < low_fileoff)
			    low_fileoff = s->offset;
			s++;
		    }
		}
		else{
		    if(sg->filesize != 0 && sg->fileoff < low_fileoff)
			low_fileoff = sg->fileoff;
		}
	    }
	    else if(lc->cmd == LC_SEGMENT_64){
		sg64 = (struct segment_command_64 *)lc;
		s64 = (struct section_64 *)
		    ((char *)sg64 + sizeof(struct segment_command_64));
		if(sg64->nsects != 0){
		    for(j = 0; j < sg64->nsects; j++){
			if(s64->size != 0 &&
			(s64->flags & S_ZEROFILL) != S_ZEROFILL &&
			(s64->flags & S_THREAD_LOCAL_ZEROFILL) !=
				      S_THREAD_LOCAL_ZEROFILL &&
			s64->offset < low_fileoff)
			    low_fileoff = s64->offset;
			s64++;
		    }
		}
		else{
		    if(sg64->filesize != 0 && sg64->fileoff < low_fileoff)
			low_fileoff = sg64->fileoff;
		}
	    }
	    lc = (struct load_command *)((char *)lc + lc->cmdsize);
	}
	if(sizeofcmds + sizeof(struct linkedit_data_command) +
	   sizeof_mach_header > low_fileoff)
	    fatal("can't allocate code signature data for: %s (for architecture"
		  " %s) because larger updated load commands do not fit (the "
		  "program must be relinked using a larger -headerpad value)", 
		  arch->file_name, arch_name);
	/*
	 * There is space for the new load commands. So just use that space for
	 * the new code signature load command and set the fields.
	 */
	code_sig = (struct linkedit_data_command *)
		   ((char *)arch->object->load_commands + sizeofcmds);
	code_sig->cmd = LC_CODE_SIGNATURE;
	code_sig->cmdsize = sizeof(struct linkedit_data_command);
	/* these two feilds will be set by the caller */
	code_sig->dataoff = 0;
	code_sig->datasize = 0;
	
        if(arch->object->mh != NULL){
            arch->object->mh->sizeofcmds = sizeofcmds +
	       sizeof(struct linkedit_data_command);
            arch->object->mh->ncmds = ncmds + 1;
        }
	else{
            arch->object->mh64->sizeofcmds = sizeofcmds +
	       sizeof(struct linkedit_data_command);
            arch->object->mh64->ncmds = ncmds + 1;
        }
	return(code_sig);
}
