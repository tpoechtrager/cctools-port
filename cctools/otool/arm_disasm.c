#include <stdlib.h>
#include <string.h>
#include "stuff/arch.h"
#include "stuff/bool.h"
#include <stdio.h>
#include <mach-o/loader.h>
#include <mach-o/nlist.h>
#include <mach-o/reloc.h>
#include <mach-o/arm/reloc.h>
#include "stuff/bytesex.h"
#include "stuff/symbol.h"
#include "stuff/llvm.h"
#include "otool.h"
#include "dyld_bind_info.h"
#include "ofile_print.h"
#include "arm_disasm.h"
#include "cxa_demangle.h"

/* Used by otool(1) to stay or switch out of thumb mode */
enum bool in_thumb = FALSE;

/* HACKS */
# define _(String) (String)
# define ATTRIBUTE_UNUSED

/* HACKS for bfd_stuff */
typedef enum bool bfd_boolean;
typedef unsigned int bfd_vma;
typedef char bfd_byte;
#define bfd_endian byte_sex
#define BFD_ENDIAN_LITTLE LITTLE_ENDIAN_BYTE_SEX
#define BFD_ENDIAN_BIG BIG_ENDIAN_BYTE_SEX

static struct disassemble_info {
  /* otool(1) specific stuff */
  enum bool verbose;
  /* Relocation information.  */
  struct relocation_info *relocs;
  uint32_t nrelocs;
  struct relocation_info *loc_relocs;
  uint32_t nloc_relocs;
  struct dyld_bind_info *dbi;
  uint64_t ndbi;
  /* Symbol table.  */
  struct nlist *symbols;
  struct nlist_64 *symbols64;
  uint32_t nsymbols;
  /* Symbols sorted by address.  */
  struct symbol *sorted_symbols;
  uint32_t nsorted_symbols;
  /* String table.  */
  char *strings;
  uint32_t strings_size;
  /* Other useful info.  */
  uint32_t ncmds;
  uint32_t sizeofcmds;
  struct load_command *load_commands;
  enum byte_sex object_byte_sex;
  uint32_t *indirect_symbols;
  uint32_t nindirect_symbols;
  cpu_type_t cputype;
  char *sect;
  uint32_t left;
  uint32_t addr;
  uint32_t sect_addr;
  char *object_addr;
  uint64_t object_size;
  char *demangled_name;
  struct inst *inst;
  struct inst *insts;
  uint32_t ninsts;
} dis_info;

/*
 * GetOpInfo() is the operand information call back function.  This is called to
 * get the symbolic information for an operand of an arm instruction.  This
 * is done from the relocation information, symbol table, etc.  That block of
 * information is a pointer to the struct disassemble_info that was passed when
 * the disassembler context was created and passed to back to GetOpInfo() when
 * called back by LLVMDisasmInstruction().  For arm and thumb the instruction
 * containing operand is at the PC parameter.  Since for arm and thumb they only
 * have one operand with symbolic information the Offset parameter is zero and
 * the Size parameter is 4 for arm and 4 or 2 for thumb, depending on the
 * instruction width.  The information is returned in TagBuf and for both
 * arm-apple-darwin10 and thumb-apple-dawrin10 Triples is the LLVMOpInfo1 struct
 * defined in "llvm-c/Disassembler.h".  The value of TagType for both Triples is
 * 1. If symbolic information is returned then this function returns 1 else it
 * returns 0.
 */
static
int
GetOpInfo(
void *DisInfo,
uint64_t Pc,
uint64_t Offset,   /* should always be passed as 0 for arm or thumb */
uint64_t OpSize,   /* should always be passed as 4 for arm or 2 or 4 for thumb */
uint64_t InstSize, /* should always be passed as 4 for arm or 2 or 4 for thumb */
int TagType,       /* should always be passed as 1 for either Triple */
void *TagBuf)
{
    struct disassemble_info *info;
    struct LLVMOpInfo1 *op_info;
    uint64_t value, offset;
    int32_t low, high, mid, reloc_found;
    uint32_t sect_offset, i, r_address, r_symbolnum, r_type, r_extern, r_length,
	     r_value, r_scattered, pair_r_type, pair_r_value;
    uint32_t other_half;
    const char *strings, *name, *add, *sub;
    struct relocation_info *relocs, *rp, *pairp;
    struct scattered_relocation_info *srp, *spairp;
    uint32_t nrelocs, strings_size, n_strx;
    struct nlist *symbols;

	info = (struct disassemble_info *)DisInfo;

	op_info = (struct LLVMOpInfo1 *)TagBuf;
	value = op_info->Value;
	/* make sure all fields returned are zero if we don't set them */
	memset(op_info, '\0', sizeof(struct LLVMOpInfo1));
	op_info->Value = value;

	if(Offset != 0 || (OpSize != 4 && OpSize != 2) || TagType != 1 ||
	   info->verbose == FALSE)
	    return(0);

	sect_offset = (uint32_t)(Pc - info->sect_addr);
	relocs = info->relocs;
	nrelocs = info->nrelocs;
	symbols = info->symbols;
	strings = info->strings;
	strings_size = info->strings_size;

	r_symbolnum = 0;
	r_type = 0;
	r_extern = 0;
	r_value = 0;
	r_scattered = 0;
	other_half = 0;
	pair_r_value = 0;
	n_strx = 0;
	r_length = 0;

	reloc_found = 0;
	for(i = 0; i < nrelocs; i++){
	    rp = &relocs[i];
	    if(rp->r_address & R_SCATTERED){
		srp = (struct scattered_relocation_info *)rp;
		r_scattered = 1;
		r_address = srp->r_address;
		r_extern = 0;
		r_length = srp->r_length;
		r_type = srp->r_type;
		r_value = srp->r_value;
	    }
	    else{
		r_scattered = 0;
		r_address = rp->r_address;
		r_symbolnum = rp->r_symbolnum;
		r_extern = rp->r_extern;
		r_length = rp->r_length;
		r_type = rp->r_type;
	    }
	    if(r_type == ARM_RELOC_PAIR){
		fprintf(stderr, "Stray ARM_RELOC_PAIR relocation entry "
			"%u\n", i);
		continue;
	    }
	    if(r_address == sect_offset){
		if(r_type == ARM_RELOC_HALF ||
		   r_type == ARM_RELOC_SECTDIFF ||
		   r_type == ARM_RELOC_LOCAL_SECTDIFF ||
		   r_type == ARM_RELOC_HALF_SECTDIFF){
		    if(i+1 < nrelocs){
			pairp = &rp[1];
			if(pairp->r_address & R_SCATTERED){
			    spairp = (struct scattered_relocation_info *)
				     pairp;
			    other_half = spairp->r_address & 0xffff;
			    pair_r_type = spairp->r_type;
			    pair_r_value = spairp->r_value;
			}
			else{
			    other_half = pairp->r_address & 0xffff;
			    pair_r_type = pairp->r_type;
			}
			if(pair_r_type != ARM_RELOC_PAIR){
			    fprintf(stderr, "No ARM_RELOC_PAIR relocation "
				    "entry after entry %u\n", i);
			    continue;
			}
		    }
		}
		reloc_found = 1;
		break;
	    }
	    if(r_type == ARM_RELOC_HALF ||
	       r_type == ARM_RELOC_SECTDIFF ||
	       r_type == ARM_RELOC_LOCAL_SECTDIFF ||
	       r_type == ARM_RELOC_HALF_SECTDIFF){
		if(i+1 < nrelocs){
		    pairp = &rp[1];
		    if(pairp->r_address & R_SCATTERED){
			spairp = (struct scattered_relocation_info *)pairp;
			pair_r_type = spairp->r_type;
		    }
		    else{
			pair_r_type = pairp->r_type;
		    }
		    if(pair_r_type == ARM_RELOC_PAIR)
			i++;
		    else
			fprintf(stderr, "No ARM_RELOC_PAIR relocation "
				"entry after entry %u\n", i);
		}
	    }
	}

	if(reloc_found && r_extern == 1){
	    if(symbols != NULL)
		n_strx = symbols[r_symbolnum].n_un.n_strx;
	    if(n_strx >= strings_size)
		name = "bad string offset";
	    else
		name = strings + n_strx;
	    op_info->AddSymbol.Present = 1;
	    op_info->AddSymbol.Name = name;
	    if(value != 0){
		switch(r_type){
		case ARM_RELOC_HALF:
		    if((r_length & 0x1) == 1){
			op_info->Value = value << 16 | other_half;
			op_info->VariantKind =
			    LLVMDisassembler_VariantKind_ARM_HI16;
		    }
		    else{
			op_info->Value = other_half << 16 | value;
			op_info->VariantKind = 
			    LLVMDisassembler_VariantKind_ARM_LO16;
		    }
		    break;
		default:
		    break;
		}
	    }
	    else{
		switch(r_type){
		case ARM_RELOC_HALF:
		    if((r_length & 0x1) == 1){
			op_info->Value = value << 16 | other_half;
			op_info->VariantKind =
			    LLVMDisassembler_VariantKind_ARM_HI16;
		    }
		    else{
			op_info->Value = other_half << 16 | value;
			op_info->VariantKind =
			    LLVMDisassembler_VariantKind_ARM_LO16;
		    }
		    break;
		default:
		    break;
		}
	    }
	    return(1);
	}

	/*
	 * If we have a branch that is not an external relocation entry then
	 * return false so the code in tryAddingSymbolicOperand() can use
	 * SymbolLookUp() with the branch target address to look up the symbol
	 * and possiblity add an annotation for a symbol stub.
	 */
	if(reloc_found && r_extern == 0 &&
	   (r_type == ARM_RELOC_BR24 || r_type == ARM_THUMB_RELOC_BR22))
		return(0);

	offset = 0;
	if(reloc_found){
	    if(r_type == ARM_RELOC_HALF ||
	       r_type == ARM_RELOC_HALF_SECTDIFF){
		if((r_length & 0x1) == 1)
		    value = value << 16 | other_half;
		else
		    value = other_half << 16 | value;
	    }
	    if(r_scattered &&
               (r_type != ARM_RELOC_HALF &&
                r_type != ARM_RELOC_HALF_SECTDIFF)){
		offset = value - r_value;
		value = r_value;
	    }
	}

	if(reloc_found && r_type == ARM_RELOC_HALF_SECTDIFF){
	    if((r_length & 0x1) == 1)
		op_info->VariantKind =
		    LLVMDisassembler_VariantKind_ARM_HI16;
	    else
		op_info->VariantKind =
		    LLVMDisassembler_VariantKind_ARM_LO16;
	    add = guess_symbol(r_value, info->sorted_symbols,
			       info->nsorted_symbols, info->verbose);
	    sub = guess_symbol(pair_r_value, info->sorted_symbols,
			       info->nsorted_symbols, info->verbose);
	    offset = value - (r_value - pair_r_value);
	    op_info->AddSymbol.Present = 1;
	    if(add != NULL)
		op_info->AddSymbol.Name = add;
	    else
		op_info->AddSymbol.Value = r_value;
	    op_info->SubtractSymbol.Present = 1;
	    if(sub != NULL)
		op_info->SubtractSymbol.Name = sub;
	    else
		op_info->SubtractSymbol.Value = pair_r_value;
	    op_info->Value = offset;
	    return(1);
	}

	if(reloc_found == FALSE)
	    return(0);

	op_info->AddSymbol.Present = 1;
	op_info->Value = offset;
	if(reloc_found){
	    if(r_type == ARM_RELOC_HALF){
		if((r_length & 0x1) == 1)
		    op_info->VariantKind =
			LLVMDisassembler_VariantKind_ARM_HI16;
		else
		    op_info->VariantKind =
			LLVMDisassembler_VariantKind_ARM_LO16;
	    }
	}
	low = 0;
	high = info->nsorted_symbols - 1;
	mid = (high - low) / 2;
	while(high >= low){
	    if(info->sorted_symbols[mid].n_value == value){
	        op_info->AddSymbol.Present = 1;
		op_info->AddSymbol.Name = info->sorted_symbols[mid].name;
		return(1);
	    }
	    if(info->sorted_symbols[mid].n_value > value){
		high = mid - 1;
		mid = (high + low) / 2;
	    }
	    else{
		low = mid + 1;
		mid = (high + low) / 2;
	    }
	}
	op_info->AddSymbol.Value = value;
	return(1);
}

static
int
GetOpInfoOld(
void *DisInfo,
uint64_t Pc,
uint64_t Offset,
uint64_t OpSize,
int TagType,
void *TagBuf)
{
	return GetOpInfo(DisInfo, Pc, Offset, OpSize, OpSize, TagType, TagBuf);
}

/*
 * guess_cstring_pointer() is passed the address of what might be a pointer to a
 * literal string in a cstring section.  If that address is in a cstring section
 * it returns a pointer to that string.  Else it returns NULL.
 */
static
const char *
guess_cstring_pointer(
const uint32_t value,
const uint32_t ncmds,
const uint32_t sizeofcmds,
const struct load_command *load_commands,
const enum byte_sex load_commands_byte_sex,
const char *object_addr,
const uint64_t object_size)
{
    enum byte_sex host_byte_sex;
    enum bool swapped;
    uint32_t i, j, section_type, sect_offset, object_offset;
    const struct load_command *lc;
    struct load_command l;
    struct segment_command sg;
    struct section s;
    char *p;
    uint64_t big_load_end;
    const char *name;
    const char* addr_end;

	host_byte_sex = get_host_byte_sex();
	swapped = host_byte_sex != load_commands_byte_sex;

	lc = load_commands;
	big_load_end = 0;
    addr_end = object_addr + object_size;
	for(i = 0 ; i < ncmds; i++){
        if((char *)lc + sizeof(struct load_command) > addr_end){
          fprintf(stderr, "load command extends beyond the end of the file\n");
          return(NULL);
        }
	    memcpy((char *)&l, (char *)lc, sizeof(struct load_command));
	    if(swapped)
		swap_load_command(&l, host_byte_sex);
	    if(l.cmdsize % sizeof(int32_t) != 0)
		return(NULL);
	    big_load_end += l.cmdsize;
	    if(big_load_end > sizeofcmds)
		return(NULL);
	    switch(l.cmd){
	    case LC_SEGMENT:
        if((char *)lc+sizeof(struct segment_command) > addr_end){
          fprintf(stderr, "segment header extends beyond the end of the file\n");
          return(NULL);
        }
		memcpy((char *)&sg, (char *)lc, sizeof(struct segment_command));
		if(swapped)
		    swap_segment_command(&sg, host_byte_sex);
		p = (char *)lc + sizeof(struct segment_command);
		for(j = 0 ; j < sg.nsects ; j++){
            if(p + sizeof(struct section) > addr_end){
              fprintf(stderr, "section header in (%s) extends beyond"
                      "the end of the file\n", sg.segname);
              return(NULL);
            }
		    memcpy((char *)&s, p, sizeof(struct section));
		    p += sizeof(struct section);
		    if(swapped)
			swap_section(&s, 1, host_byte_sex);
		    section_type = s.flags & SECTION_TYPE;
		    if(section_type == S_CSTRING_LITERALS &&
		       value >= s.addr && value < s.addr + s.size){
			sect_offset = value - s.addr;
			object_offset = s.offset + sect_offset;
			if(object_offset < object_size){
			    name = object_addr + object_offset;
			    return(name);
			}
			else
			    return(NULL);
		    }
		}
		break;
	    }
	    if(l.cmdsize == 0){
		return(NULL);
	    }
	    lc = (struct load_command *)((char *)lc + l.cmdsize);
	    if((char *)lc > (char *)load_commands + sizeofcmds)
		return(NULL);
	}
	return(NULL);
}

/*
 * get_literal_pool_value() looks for a section difference relocation entry
 * at the pc.  And if so returns the add_symbol's value indirectly through
 * pointer_value and if that value has a symbol name then the name of a symbol
 * indirectly though name.
 */
static
enum bool
get_literal_pool_value(
bfd_vma pc,
char const **name,
uint32_t *pointer_value,
struct disassemble_info *info)
{
    int32_t reloc_found;
    uint32_t i, r_address, r_symbolnum, r_type, r_extern, r_length,
	     r_value, r_scattered, pair_r_type, pair_r_value;
    uint32_t other_half;
    struct relocation_info *rp, *pairp;
    struct scattered_relocation_info *srp, *spairp;
    uint32_t n_strx;

    struct relocation_info *relocs = info->relocs;
    uint32_t nrelocs = info->nrelocs;
    bfd_vma sect_offset = pc - info->sect_addr;

	r_symbolnum = 0;
	r_type = 0;
	r_extern = 0;
	r_value = 0;
	r_scattered = 0;
	other_half = 0;
	pair_r_value = 0;
	n_strx = 0;
	r_length = 0;

	reloc_found = 0;
	for(i = 0; i < nrelocs; i++){
	    rp = &relocs[i];
	    if(rp->r_address & R_SCATTERED){
		srp = (struct scattered_relocation_info *)rp;
		r_scattered = 1;
		r_address = srp->r_address;
		r_extern = 0;
		r_length = srp->r_length;
		r_type = srp->r_type;
		r_value = srp->r_value;
	    }
	    else{
		r_scattered = 0;
		r_address = rp->r_address;
		r_symbolnum = rp->r_symbolnum;
		r_extern = rp->r_extern;
		r_length = rp->r_length;
		r_type = rp->r_type;
	    }
	    if(r_type == ARM_RELOC_PAIR){
		fprintf(stderr, "Stray ARM_RELOC_PAIR relocation entry "
			"%u\n", i);
		continue;
	    }
	    if(r_address == sect_offset){
		if(r_type == ARM_RELOC_HALF ||
		   r_type == ARM_RELOC_SECTDIFF ||
		   r_type == ARM_RELOC_LOCAL_SECTDIFF ||
		   r_type == ARM_RELOC_HALF_SECTDIFF){
		    if(i+1 < nrelocs){
			pairp = &rp[1];
			if(pairp->r_address & R_SCATTERED){
			    spairp = (struct scattered_relocation_info *)
				     pairp;
			    other_half = spairp->r_address & 0xffff;
			    pair_r_type = spairp->r_type;
			    pair_r_value = spairp->r_value;
			}
			else{
			    other_half = pairp->r_address & 0xffff;
			    pair_r_type = pairp->r_type;
			}
			if(pair_r_type != ARM_RELOC_PAIR){
			    fprintf(stderr, "No ARM_RELOC_PAIR relocation "
				    "entry after entry %u\n", i);
			    continue;
			}
		    }
		}
		reloc_found = 1;
		break;
	    }
	    if(r_type == ARM_RELOC_HALF ||
	       r_type == ARM_RELOC_SECTDIFF ||
	       r_type == ARM_RELOC_LOCAL_SECTDIFF ||
	       r_type == ARM_RELOC_HALF_SECTDIFF){
		if(i+1 < nrelocs){
		    pairp = &rp[1];
		    if(pairp->r_address & R_SCATTERED){
			spairp = (struct scattered_relocation_info *)pairp;
			pair_r_type = spairp->r_type;
		    }
		    else{
			pair_r_type = pairp->r_type;
		    }
		    if(pair_r_type == ARM_RELOC_PAIR)
			i++;
		    else
			fprintf(stderr, "No ARM_RELOC_PAIR relocation "
				"entry after entry %u\n", i);
		}
	    }
	}

	if(reloc_found && r_length == 2 &&
	   (r_type == ARM_RELOC_SECTDIFF ||
	    r_type == ARM_RELOC_LOCAL_SECTDIFF)){
	    *name = guess_symbol(r_value, info->sorted_symbols,
			         info->nsorted_symbols, info->verbose);
	    *pointer_value = r_value;
	    return(TRUE);
	}
	*name = NULL;
	*pointer_value = 0;
	return(FALSE);
}

/*
 * guess_literal_pointer() returns a name of a symbol or string if the value
 * passed in is the address of a literal pointer and the literal pointer's value
 * is and address of a symbol or cstring.  
 */
static
const char *
guess_literal_pointer(
const uint32_t value,	  /* the value of the reference */
const uint32_t pc,	  /* pc of the referencing instruction */
uint64_t *reference_type, /* type returned, symbol name or string literal*/
struct disassemble_info *info)
{
    uint32_t i, j, ncmds, sizeofcmds, sect_addr, object_size, pointer_value;
    enum byte_sex object_byte_sex, host_byte_sex;
    enum bool swapped;
    struct load_command *load_commands, *lc, l;
    struct segment_command sg;
    struct section s;
    char *object_addr, *p;
    uint64_t big_load_end;
    const char *name;

	ncmds = info->ncmds;
	sizeofcmds = info->sizeofcmds;
	load_commands = info->load_commands;
	sect_addr = info->sect_addr;
	object_byte_sex = info->object_byte_sex;
	object_addr = info->object_addr;
	object_size = (uint32_t)info->object_size;

	host_byte_sex = get_host_byte_sex();
	swapped = host_byte_sex != object_byte_sex;

	lc = load_commands;
	big_load_end = 0;
	for(i = 0 ; i < ncmds; i++){
	    memcpy((char *)&l, (char *)lc, sizeof(struct load_command));
	    if(swapped)
		swap_load_command(&l, host_byte_sex);
	    if(l.cmdsize % sizeof(int32_t) != 0)
		return(NULL);
	    big_load_end += l.cmdsize;
	    if(big_load_end > sizeofcmds)
		return(NULL);
	    switch(l.cmd){
	    case LC_SEGMENT:
		memcpy((char *)&sg, (char *)lc, sizeof(struct segment_command));
		if(swapped)
		    swap_segment_command(&sg, host_byte_sex);
		p = (char *)lc + sizeof(struct segment_command);
		for(j = 0 ; j < sg.nsects ; j++){
		    memcpy((char *)&s, p, sizeof(struct section));
		    p += sizeof(struct section);
		    if(swapped)
			swap_section(&s, 1, host_byte_sex);
		    if(sect_addr == s.addr &&
		       pc >= s.addr && pc < s.addr + s.size &&
		       value >= s.addr && value + 4 <= s.addr + s.size){
			/*
			 * The problem here is while we can get the value in the
			 * pool with code like this:
			 * section_start = object_addr + s.offset;
			 * section_offset = value - s.addr;
			 * pool_value =
			 *    section_start[section_offset + 3] << 24 |
			 *    section_start[section_offset + 2] << 16 |
			 *    section_start[section_offset + 1] << 8 |
			 *    section_start[section_offset];
			 * we don't know the pc value that will be added
			 * to it. As that is done as a separate instruction like
			 * "L1: add rx, pc, rt" where the ldr loaded up the
			 * pool_value in rt.  The pool_value in a relocatable
			 * object will be something like this:
			 *    .long LC1-(L1+8)
			 * where LC1 is the pointer_value we are interested in.
			 * So we call get_literal_pool_value() to see if the
			 * literal pool has a relocation entry.  If so it
			 * returns the pointer_value and the name of a symbol
			 * if any for that pointer_value.
			 */
			if(get_literal_pool_value(value, &name, &pointer_value,
						  info) == FALSE){
			    return(NULL);
			}
			/*
			 * If the value in the literal pool is the address of a
			 * symbol then get_literal_pool_value() will set name
			 * to the name of the symbol.
			 */
			if(name != NULL){
			    *reference_type =
			     LLVMDisassembler_ReferenceType_Out_LitPool_SymAddr;
			    return(name);
			}
			/*
			 * If not, next see if the pointer value is pointing to
			 * a cstring.
			 */
			name = guess_cstring_pointer(pointer_value, ncmds,
					sizeofcmds, load_commands,
					object_byte_sex, object_addr,
					object_size);
			if(name != NULL){
			    *reference_type =
			    LLVMDisassembler_ReferenceType_Out_LitPool_CstrAddr;
			    return(name);
			}
		    }
		}
		break;
	    }
	    if(l.cmdsize == 0){
		return(NULL);
	    }
	    lc = (struct load_command *)((char *)lc + l.cmdsize);
	    if((char *)lc > (char *)load_commands + sizeofcmds)
		return(NULL);
	}
	return(NULL);
}

/*
 * The symbol lookup function passed to LLVMCreateDisasm().  It looks up the
 * SymbolValue using the info passed vis the pointer to the struct
 * disassemble_info that was passed when disassembler context is created and
 * returns the symbol name that matches or NULL if none.
 *
 * When this is called to get a symbol name for a branch target then the
 * ReferenceType can be LLVMDisassembler_ReferenceType_In_Branch and then
 * SymbolValue will be looked for in the indirect symbol table to determine if
 * it is an address for a symbol stub.  If so then the symbol name for that
 * stub is returned indirectly through ReferenceName and then ReferenceType is
 * set to LLVMDisassembler_ReferenceType_Out_SymbolStub.
 * 
 * This may be called may also be called by the disassembler for such things
 * like adding a comment for a PC plus a constant offset load instruction to use
 * a symbol name instead of a load address value.  In this case ReferenceType
 * can be LLVMDisassembler_ReferenceType_In_PCrel_Load and the ReferencePC is
 * also passed.  In this case if the SymbolValue is an address in a literal
 * pool then the referenced pointer in the literal pool is used for the returned
 * ReferenceName and then ReferenceType.  Which may be a symbol name and
 * LLVMDisassembler_ReferenceType_Out_LitPool_SymAddr when the literal pool
 * entry is the address of a symbol.  Or a pointer to a literal cstring and
 * LLVMDisassembler_ReferenceType_Out_LitPool_CstrAddr when the literal pool
 * entry is the address of a literal cstring.
 */
static
const char *
SymbolLookUp(
void *DisInfo,
uint64_t SymbolValue,
uint64_t *ReferenceType,
uint64_t ReferencePC,
const char **ReferenceName)
{
    struct disassemble_info *info;
    const char *SymbolName;
    uint32_t i;

	info = (struct disassemble_info *)DisInfo;
	if(info->verbose == FALSE){
	    *ReferenceName = NULL;
	    *ReferenceType = LLVMDisassembler_ReferenceType_InOut_None;
	    return(NULL);
	}
	SymbolName = guess_symbol(SymbolValue, info->sorted_symbols,
				  info->nsorted_symbols, TRUE);
	if(SymbolName == NULL && info->insts != NULL && info->ninsts != 0){
	    for(i = 0; i < info->ninsts; i++){
		if(info->insts[i].address == SymbolValue){
		    SymbolName = info->insts[i].tmp_label;
		    break;
		}
	    }
	}

	if(*ReferenceType == LLVMDisassembler_ReferenceType_In_Branch){
	    *ReferenceName = guess_indirect_symbol(SymbolValue,
		    info->ncmds, info->sizeofcmds, info->load_commands,
		    info->object_byte_sex, info->indirect_symbols,
		    info->nindirect_symbols, info->symbols, NULL,
		    info->nsymbols, info->strings, info->strings_size);
	    if(*ReferenceName != NULL)
		*ReferenceType = LLVMDisassembler_ReferenceType_Out_SymbolStub;
	    else if(SymbolName != NULL && strncmp(SymbolName, "__Z", 3) == 0){
		if(info->demangled_name != NULL)
		    free(info->demangled_name);
		info->demangled_name = __cxa_demangle(SymbolName + 1, 0, 0, 0);
		if(info->demangled_name != NULL){
		    *ReferenceName = info->demangled_name;
		    *ReferenceType =
			LLVMDisassembler_ReferenceType_DeMangled_Name;
		}
		else
		    *ReferenceType = LLVMDisassembler_ReferenceType_InOut_None;
	    }
	    else
		*ReferenceType = LLVMDisassembler_ReferenceType_InOut_None;
	    if(info->inst != NULL && SymbolName == NULL){
		info->inst->has_raw_target_address = TRUE;
		info->inst->raw_target_address = SymbolValue;
	    }
	}
	else if(*ReferenceType == LLVMDisassembler_ReferenceType_In_PCrel_Load){
	    *ReferenceName = guess_literal_pointer((uint32_t)SymbolValue,
						   (uint32_t)ReferencePC,
						   ReferenceType, info);
	    if(*ReferenceName == NULL)
		*ReferenceType = LLVMDisassembler_ReferenceType_InOut_None;
	}
	else if(SymbolName != NULL && strncmp(SymbolName, "__Z", 3) == 0){
	    if(info->demangled_name != NULL)
		free(info->demangled_name);
	    info->demangled_name = __cxa_demangle(SymbolName + 1, 0, 0, 0);
	    if(info->demangled_name != NULL){
		*ReferenceName = info->demangled_name;
		*ReferenceType =
		    LLVMDisassembler_ReferenceType_DeMangled_Name;
	    }
	}
	else{
	    *ReferenceName = NULL;
	    *ReferenceType = LLVMDisassembler_ReferenceType_InOut_None;
	}
	return(SymbolName);
}

LLVMDisasmContextRef
create_arm_llvm_disassembler(
cpu_subtype_t cpusubtype)
{
    LLVMDisasmContextRef dc;
    char *TripleName;
    char *mcpu_default;

	mcpu_default = mcpu;
	switch(cpusubtype){
	case CPU_SUBTYPE_ARM_V4T:
	    TripleName = "armv4t-apple-darwin10";
	    break;
	case CPU_SUBTYPE_ARM_V5TEJ:
	    TripleName = "armv5-apple-darwin10";
	    break;
	case CPU_SUBTYPE_ARM_XSCALE:
	    TripleName = "xscale-apple-darwin10";
	    break;
	case CPU_SUBTYPE_ARM_V6:
	    TripleName = "armv6-apple-darwin10";
	    break;
	case CPU_SUBTYPE_ARM_V6M:
	    TripleName = "armv6m-apple-darwin10";
	    if(*mcpu_default == '\0')
		mcpu_default = "cortex-m0";
	    break;
	default:
	case CPU_SUBTYPE_ARM_V7:
	    TripleName = "armv7-apple-darwin10";
	    break;
	case CPU_SUBTYPE_ARM_V7F:
	    TripleName = "armv7f-apple-darwin10";
	    break;
	case CPU_SUBTYPE_ARM_V7S:
	    TripleName = "armv7s-apple-darwin10";
	    break;
	case CPU_SUBTYPE_ARM_V7K:
	    TripleName = "armv7k-apple-darwin10";
	    break;
	case CPU_SUBTYPE_ARM_V7M:
	    TripleName = "armv7m-apple-darwin10";
	    if(*mcpu_default == '\0')
		mcpu_default = "cortex-m3";
	    break;
	case CPU_SUBTYPE_ARM_V7EM:
	    TripleName = "armv7em-apple-darwin10";
	    if(*mcpu_default == '\0')
		mcpu_default = "cortex-m4";
	    break;
	case CPU_SUBTYPE_ARM_V8M_MAIN:
	    TripleName = "armv8m.main-apple-darwin10";
	    if(*mcpu_default == '\0')
		mcpu_default = "cortex-m33";
	    break;
	case CPU_SUBTYPE_ARM_V8M_BASE:
	    TripleName = "armv8m.base-apple-darwin10";
	    if(*mcpu_default == '\0')
		mcpu_default = "cortex-m23";
	    break;
	case CPU_SUBTYPE_ARM_V8_1M_MAIN:
	    TripleName = "armv8.1m.main-apple-darwin10";
	    if(*mcpu_default == '\0')
		mcpu_default = "cortex-m55";
	    break;
	}

	LLVMOpInfoCallback OpInfo = llvm_disasm_new_getopinfo_abi()
	    ? GetOpInfo : (LLVMOpInfoCallback)GetOpInfoOld;
	dc =
#ifdef STATIC_LLVM
	    LLVMCreateDisasm
#else
	    llvm_create_disasm
#endif
		(TripleName, mcpu_default, &dis_info, 1, OpInfo, SymbolLookUp);
	return(dc);
}

void
delete_arm_llvm_disassembler(
LLVMDisasmContextRef dc)
{
#ifdef STATIC_LLVM
	LLVMDisasmDispose
#else
	llvm_disasm_dispose
#endif
	    (dc);
}

LLVMDisasmContextRef
create_thumb_llvm_disassembler(
cpu_subtype_t cpusubtype)
{
    LLVMDisasmContextRef dc;
    char *TripleName;
    char *mcpu_default;

	mcpu_default = mcpu;
	switch(cpusubtype){
	case CPU_SUBTYPE_ARM_V4T:
	    TripleName = "thumbv4t-apple-darwin10";
	    break;
	case CPU_SUBTYPE_ARM_V5TEJ:
	    TripleName = "thumbv5-apple-darwin10";
	    break;
	case CPU_SUBTYPE_ARM_XSCALE:
	    TripleName = "xscale-apple-darwin10";
	    break;
	case CPU_SUBTYPE_ARM_V6:
	    TripleName = "thumbv6-apple-darwin10";
	    break;
	case CPU_SUBTYPE_ARM_V6M:
	    TripleName = "thumbv6m-apple-darwin10";
	    if(*mcpu_default == '\0')
		mcpu_default = "cortex-m0";
	    break;
	default:
	case CPU_SUBTYPE_ARM_V7:
	    TripleName = "thumbv7-apple-darwin10";
	    break;
	case CPU_SUBTYPE_ARM_V7F:
	    TripleName = "thumbv7f-apple-darwin10";
	    break;
	case CPU_SUBTYPE_ARM_V7S:
	    TripleName = "thumbv7s-apple-darwin10";
	    break;
	case CPU_SUBTYPE_ARM_V7K:
	    TripleName = "thumbv7k-apple-darwin10";
	    break;
	case CPU_SUBTYPE_ARM_V7M:
	    TripleName = "thumbv7m-apple-darwin10";
	    if(*mcpu_default == '\0')
		mcpu_default = "cortex-m3";
	    break;
	case CPU_SUBTYPE_ARM_V7EM:
	    TripleName = "thumbv7em-apple-darwin10";
	    if(*mcpu_default == '\0')
		mcpu_default = "cortex-m4";
	    break;
	case CPU_SUBTYPE_ARM_V8M_MAIN:
	    TripleName = "thumbv8m.main-apple-darwin10";
	    if(*mcpu_default == '\0')
		mcpu_default = "cortex-m33";
	    break;
	case CPU_SUBTYPE_ARM_V8M_BASE:
	    TripleName = "thumbv8m.base-apple-darwin10";
	    if(*mcpu_default == '\0')
		mcpu_default = "cortex-m23";
	    break;
	case CPU_SUBTYPE_ARM_V8_1M_MAIN:
	    TripleName = "thumbv8.1m.main-apple-darwin10";
	    if(*mcpu_default == '\0')
		mcpu_default = "cortex-m55";
	    break;
	}

	LLVMOpInfoCallback OpInfo = llvm_disasm_new_getopinfo_abi()
	    ? GetOpInfo : (LLVMOpInfoCallback)GetOpInfoOld;
	dc =
#ifdef STATIC_LLVM
	    LLVMCreateDisasm
#else
	    llvm_create_disasm
#endif
		(TripleName, mcpu_default, &dis_info, 1, OpInfo,
		 SymbolLookUp);
	return(dc);
}

void
delete_thumb_llvm_disassembler(
LLVMDisasmContextRef dc)
{
#ifdef STATIC_LLVM
	LLVMDisasmDispose
#else
	llvm_disasm_dispose
#endif
	    (dc);
}

/*
 * Print the section contents pointed to by sect as data .long, .short or .byte
 * depending on its kind.
 */
static
uint32_t
print_data_in_code(
unsigned char *sect,
uint32_t sect_left,
uint32_t dice_left,
uint16_t kind)
{
    uint32_t value, left, size;

       left = dice_left;
       if(left > sect_left)
           left = sect_left;
       switch(kind){
       default:
       case DICE_KIND_DATA:
           if(left >= 4){
               value = sect[3] << 24 |
                       sect[2] << 16 |
                       sect[1] << 8 |
                       sect[0];
               if(!Xflag && !gflag && !no_show_raw_insn)
                   printf("\t%08x", value);
               printf("\t.long %u\t@ ", value);
               size = 4;
           }
           else if(left >= 2){
               value = sect[1] << 8 |
                       sect[0];
               if(!Xflag && !gflag && !no_show_raw_insn)
                   printf("\t    %04x", value);
               printf("\t.short %u\t@ ", value);
               size = 2;
           }
           else {
               value = sect[0];
               if(!Xflag && !gflag && !no_show_raw_insn)
                   printf("\t      %02x", value & 0xff);
               printf("\t.byte %u\t@ ", value & 0xff);
               size = 1;
           }
           if(kind == DICE_KIND_DATA)
               printf("KIND_DATA\n");
           else
               printf("kind = %u\n", kind);
           return(size);
       case DICE_KIND_JUMP_TABLE8:
           value = sect[0];
            if(!Xflag && !gflag && !no_show_raw_insn)
               printf("\t      %02x", value);
           printf("\t.byte %3u\t@ KIND_JUMP_TABLE8\n", value);
           return(1);
       case DICE_KIND_JUMP_TABLE16:
           value = sect[1] << 8 |
                   sect[0];
            if(!Xflag && !gflag && !no_show_raw_insn)
               printf("\t    %04x", value & 0xffff);
           printf("\t.short %5u\t@ KIND_JUMP_TABLE16\n", value & 0xffff);
           return(2);
       case DICE_KIND_JUMP_TABLE32:
       case DICE_KIND_ABS_JUMP_TABLE32:
           value = sect[3] << 24 |
                   sect[2] << 16 |
                   sect[1] << 8 |
                   sect[0];
            if(!Xflag && !gflag && !no_show_raw_insn)
               printf("\t%08x", value);
           printf("\t.long %u\t@ ", value);
           if(kind == DICE_KIND_JUMP_TABLE32)
               printf("KIND_JUMP_TABLE32\n");
           else
               printf("KIND_ABS_JUMP_TABLE32\n");
           return(4);
       }
}

static
uint32_t read_up_to_word(char* sect, enum byte_sex object_byte_sex, uint32_t len)
{
    enum byte_sex host_byte_sex;
    enum bool swapped;

    host_byte_sex = get_host_byte_sex();
    swapped = host_byte_sex != object_byte_sex;
    
    uint32_t bytes;
    memcpy(&bytes, sect, len);
    if ( swapped )
        bytes = SWAP_INT(bytes);
    return bytes;
}

static void
set_thumb_mode(
uint32_t addr,
uint32_t nsorted_symbols,
struct symbol *sorted_symbols,
enum bool *in_thumb);

/*
 * This is the routine called by Apple's otool(1)'s main() routine in otool.c
 * and is written as the glue between the otool(1) code and the GNU code.
 */
uint32_t
arm_disassemble(
char *sect,
uint32_t left,
uint32_t addr,
uint32_t sect_addr,
enum byte_sex object_byte_sex,
struct relocation_info *relocs,
uint32_t nrelocs,
struct nlist *symbols,
uint32_t nsymbols,
struct symbol *sorted_symbols,
uint32_t nsorted_symbols,
char *strings,
uint32_t strings_size,
uint32_t *indirect_symbols,
uint32_t nindirect_symbols,
struct load_command *load_commands,
uint32_t ncmds,
uint32_t sizeofcmds,
cpu_subtype_t cpusubtype,
enum bool verbose,
LLVMDisasmContextRef arm_dc,
LLVMDisasmContextRef thumb_dc,
char *object_addr,
uint64_t object_size,
struct data_in_code_entry *dices,
uint32_t ndices,
uint64_t seg_addr,
struct inst *inst,
struct inst *insts,
uint32_t ninsts)
{
    uint32_t bytes_consumed;
    char dst[4096];

    if(left == 0) {
       printf("(end of section)\n");
       return(left);
    }

    dis_info.verbose = verbose;
    dis_info.relocs = relocs;
    dis_info.nrelocs = nrelocs;
    dis_info.symbols = symbols;
    dis_info.nsymbols = nsymbols;
    dis_info.sorted_symbols = sorted_symbols;
    dis_info.nsorted_symbols = nsorted_symbols;
    dis_info.strings = strings;
    dis_info.strings_size = strings_size;
    dis_info.load_commands = load_commands;
    dis_info.object_byte_sex = object_byte_sex;
    dis_info.indirect_symbols = indirect_symbols;
    dis_info.nindirect_symbols = nindirect_symbols;
    dis_info.ncmds = ncmds;
    dis_info.sizeofcmds = sizeofcmds;

    dis_info.sect = sect;
    dis_info.left = left;
    dis_info.addr = addr;
    dis_info.sect_addr = sect_addr;

    dis_info.object_addr = object_addr;
    dis_info.object_size = object_size;

    dis_info.inst = inst;
    dis_info.insts = insts;
    dis_info.ninsts = ninsts;

    dis_info.demangled_name = NULL;
    
    /* These are the ways otool(1) determines if we are in thumb mode */
    if (Bflag)
        in_thumb = TRUE;
    else
        set_thumb_mode(addr, nsorted_symbols, sorted_symbols, &in_thumb);

    if (ndices) {
         /* Note: in final linked images, offset is from the base address */
         /* Note: in object files, offset is from first section address */
         uint32_t offset;
         if(nrelocs == 0) /* TODO better test for final linked image */
             offset = (uint32_t)(addr - seg_addr);
         else
             offset = addr - sect_addr;
         for(size_t i = 0; i < ndices; i++){
             if(offset >= dices[i].offset &&
               offset < dices[i].offset + dices[i].length){
               bytes_consumed = print_data_in_code((unsigned char *)sect,
                                 left,
                                 dices[i].offset + dices[i].length - offset,
                                  dices[i].kind);
               if ((dices[i].kind == DICE_KIND_JUMP_TABLE8) &&
                   (offset == (dices[i].offset + dices[i].length - 1)) &&
                   (dices[i].length & 1)) {
                 ++bytes_consumed;
               }
               return(bytes_consumed);
             }
         }
    }
    
    dst[4095] = '\0';
    bytes_consumed = (uint32_t)llvm_disasm_instruction(in_thumb ? thumb_dc : arm_dc,
                                                      (uint8_t *)sect, left, addr, dst, 4095);

    if( bytes_consumed != 0 ) {
        if(!Xflag && !gflag && !no_show_raw_insn) {
            uint32_t opcodeLen = bytes_consumed > 4 ? 4 : bytes_consumed;
            uint32_t bytes = read_up_to_word(sect, object_byte_sex, opcodeLen);

            if ( opcodeLen == 4 && in_thumb ) {
                printf("\t%04x%04x", bytes & 0xFFFF, (bytes >> 16) & 0xFFFFF);
            } else if ( opcodeLen == 4 ) {
                printf("\t%08x", bytes);
            } else if ( opcodeLen == 2 ) {
                printf("\t    %04x", bytes);
            }
        }

        printf("%s\n", dst);
    } else {
        bytes_consumed = in_thumb ? 2 : 4;
        if ( left < bytes_consumed )
            bytes_consumed = left;

        uint32_t bytes = read_up_to_word(sect, object_byte_sex, bytes_consumed);
        if ( bytes_consumed == 4 ) {
            if(!Xflag && !gflag && !no_show_raw_insn)
                printf("\t%08x", bytes);
            printf("\t.long\t0x%08x\n", bytes);
        } else if ( bytes_consumed == 2 ) {
            if(!Xflag && !gflag && !no_show_raw_insn)
                printf("\t    %04x", bytes);
            printf("\t.short\t0x%04x\n", bytes);
        } else {
            printf("\tinvalid instruction encoding");
        }
    }

    return(bytes_consumed);
}

/* Set in_thumb accordingly:
 * If no symbols are at addr, don't change it.
 * If there are symbols at addr, and any of them are THUMB_DEFs, set it.
 * If there are symbols at addr, but none of them are THUMB_DEFs, clear it.
 */
static void
set_thumb_mode(
uint32_t addr,
uint32_t nsorted_symbols,
struct symbol *sorted_symbols,
enum bool *in_thumb)
{
    int32_t high, low, mid;
 
        low = 0;
        high = nsorted_symbols - 1; 
        mid = (high - low) / 2;
        while(high >= low){
            if(sorted_symbols[mid].n_value == addr) {
                /* Find the first symbol at this address */
                while(mid && sorted_symbols[mid-1].n_value == addr){
                    mid--;
                }
                do{
                    if(sorted_symbols[mid].is_thumb){
                        *in_thumb = TRUE;
                        return;
                    }
                    mid++;
                    if(mid > nsorted_symbols ||
                       sorted_symbols[mid].n_value != addr){
                        *in_thumb = FALSE;
                        return;
                    }
                } while(1);
            }
            if(sorted_symbols[mid].n_value > addr){
                high = mid - 1;
                mid = (high + low) / 2;
            }
            else{
                low = mid + 1;
                mid = (high + low) / 2;
            }
        }
}
