#include <stdio.h>
#include <mach-o/loader.h>
#include <stuff/bool.h>
#include <stuff/allocate.h>
#include "dyld_bind_info.h"

const char *
bindTypeName(
uint8_t type)
{
        switch(type){
	    case BIND_TYPE_POINTER:
		return("pointer");
	    case BIND_TYPE_TEXT_ABSOLUTE32:
		return("text abs32");
	    case BIND_TYPE_TEXT_PCREL32:
		return("text rel32");
        }
        return("!!unknown!!");
}

const char *
ordinalName(
int libraryOrdinal,
const char **dylibs,
uint32_t ndylibs)
{
        switch(libraryOrdinal){
	    case BIND_SPECIAL_DYLIB_SELF:
		return("this-image");
	    case BIND_SPECIAL_DYLIB_MAIN_EXECUTABLE:
		return("main-executable");
	    case BIND_SPECIAL_DYLIB_FLAT_LOOKUP:
		return("flat-namespace");
        }
        if(libraryOrdinal < BIND_SPECIAL_DYLIB_FLAT_LOOKUP)
	    return("unknown special ordinal");
        if(libraryOrdinal > ndylibs)
	    return("libraryOrdinal out of range");
        return(dylibs[libraryOrdinal-1]);
}

uint64_t
segStartAddress(
uint8_t segIndex,
struct segment_command **segs,
uint32_t nsegs,
struct segment_command_64 **segs64,
uint32_t nsegs64)
{
	if(segs != NULL){
	    if(segIndex > nsegs)
		return(0); /* throw "segment index out of range"; */
	    return(segs[segIndex]->vmaddr);
	}
	else {
	    if(segIndex > nsegs64)
		return(0); /* throw "segment index out of range"; */
	    return(segs64[segIndex]->vmaddr);
	}
}

const char *
segmentName(
uint8_t segIndex,
struct segment_command **segs,
uint32_t nsegs,
struct segment_command_64 **segs64,
uint32_t nsegs64)
{
	if(segs != NULL){
	    if(segIndex > nsegs)
		return("??"); /* throw "segment index out of range"; */
	    return(segs[segIndex]->segname);
	}
	else{
	    if(segIndex > nsegs64)
		return("??"); /* throw "segment index out of range"; */
	    return(segs64[segIndex]->segname);
	}
}

const char *
sectionName(
uint8_t segIndex,
uint64_t address,
struct segment_command **segs,
uint32_t nsegs,
struct segment_command_64 **segs64,
uint32_t nsegs64)
{
    struct section *s;
    struct section_64 *s64;
    uint32_t i;

	if(segs != NULL){
	    if(segIndex > nsegs)
		return("??"); /* throw "segment index out of range"; */
	    
	    s = (struct section *)((char *)segs[segIndex] + 
				   sizeof(struct segment_command));
	    for(i = 0; i < segs[segIndex]->nsects; i++){
		if(s->addr <= address && address < s->addr + s->size)
		    return(s->sectname);
		s++;
	    }
	}
	else{
	    if(segIndex > nsegs64)
		return("??"); /* throw "segment index out of range"; */
	    
	    s64 = (struct section_64 *)((char *)segs64[segIndex] + 
					sizeof(struct segment_command_64));
	    for(i = 0; i < segs64[segIndex]->nsects; i++){
		if(s64->addr <= address && address < s64->addr + s64->size)
		    return(s64->sectname);
		s64++;
	    }
	}
	return("??");
}

static
uint64_t
read_uleb128(
const uint8_t **pp,
const uint8_t* end)
{
    const uint8_t *p = *pp;
    uint64_t result = 0;
    int bit = 0;

        do{
	    if(p == end){
		*pp = p;
		return(0); /* throwf("malformed uleb128"); */
	    }

	    uint64_t slice = *p & 0x7f;

	    if(bit >= 64 || slice << bit >> bit != slice){
		*pp = p;
		return(0); /* throwf("uleb128 too big"); */
	    }
	    else {
		result |= (slice << bit);
		bit += 7;
	    }
        }while(*p++ & 0x80);
	*pp = p;
        return(result);
}

static
int64_t
read_sleb128(
const uint8_t **pp,
const uint8_t* end)
{
    const uint8_t *p = *pp;
    int64_t result = 0;
    int bit = 0;
    uint8_t byte;

        do{
	    if(p == end){
		*pp = p;
		return(0); /* throwf("malformed sleb128"); */
	    }
	    byte = *p++;
	    result |= (((int64_t)(byte & 0x7f)) << bit);
	    bit += 7;
        }while (byte & 0x80);
        // sign extend negative numbers
        if((byte & 0x40) != 0)
	    result |= (-1LL) << bit;
	*pp = p;
        return(result);
}

/*
 * get_dyld_bind_info() unpacks the dyld bind info from the data from an
 * LC_BIND_INFO load command pointed to by start through end into the internal
 * dyld_bind_info structs returned through dbi, and its count ndbi.  The array
 * of dylib names and their count are passed in in dylibs and ndylibs.  The
 * array of segments (either 32-bit with segs & nsegs or 64-bit segs64 & nsegs64)
 * are used to determine which sections the pointers are in.
 */
void
get_dyld_bind_info(
const uint8_t *start, /* inputs */
const uint8_t* end,
const char **dylibs,
uint32_t ndylibs,
struct segment_command **segs,
uint32_t nsegs,
struct segment_command_64 **segs64,
uint32_t nsegs64,
struct dyld_bind_info **dbi, /* output */
uint64_t *ndbi)
{
    const uint8_t *p;
    uint8_t type;
    uint8_t segIndex;
    uint64_t segOffset;
    const char* symbolName;
    const char* fromDylib;
    int libraryOrdinal;
    int64_t addend;
    uint32_t count;
    uint32_t skip;
    uint64_t segStartAddr;
    const char* segName;
    const char* typeName;
    const char* weak_import;
    enum bool done = FALSE;

    const char *sectName;
    uint32_t pass;
    uint64_t n;

    uint32_t sizeof_pointer;
    if(segs)
	sizeof_pointer = 4;
    else
	sizeof_pointer = 8;

    n = 0;
    for(pass = 1; pass <= 2; pass++){
	p = start;
	type = 0;
	segIndex = 0;
	segOffset = 0;
	symbolName = NULL;
	fromDylib = "??";
	libraryOrdinal = 0;
	addend = 0;
	segStartAddr = 0;
	segName = "??";
	typeName = "??";
	weak_import = "";
	done = FALSE;
	if(pass == 2){
	    *dbi = (struct dyld_bind_info *)
		  allocate(n * sizeof(struct dyld_bind_info));
	    *ndbi = n;
	    n = 0;
	}
	while(!done && (p < end)){
	    uint8_t immediate = *p & BIND_IMMEDIATE_MASK;
	    uint8_t opcode = *p & BIND_OPCODE_MASK;
	    ++p;
	    switch(opcode){
		case BIND_OPCODE_DONE:
		    done = TRUE;
		    break;
		case BIND_OPCODE_SET_DYLIB_ORDINAL_IMM:
		    libraryOrdinal = immediate;
		    fromDylib = ordinalName(libraryOrdinal, dylibs, ndylibs);
		    break;
		case BIND_OPCODE_SET_DYLIB_ORDINAL_ULEB:
		    libraryOrdinal = read_uleb128(&p, end);
		    fromDylib = ordinalName(libraryOrdinal, dylibs, ndylibs);
		    break;
		case BIND_OPCODE_SET_DYLIB_SPECIAL_IMM:
		    // the special ordinals are negative numbers
		    if(immediate == 0)
			libraryOrdinal = 0;
		    else {
			int8_t signExtended = BIND_OPCODE_MASK | immediate;
			libraryOrdinal = signExtended;
		    }
		    fromDylib = ordinalName(libraryOrdinal, dylibs, ndylibs);
		    break;
		case BIND_OPCODE_SET_SYMBOL_TRAILING_FLAGS_IMM:
		    symbolName = (char*)p;
		    while(*p != '\0')
			++p;
		    ++p;
		    if((immediate & BIND_SYMBOL_FLAGS_WEAK_IMPORT) != 0)
			weak_import = " (weak import)";
		    else
			weak_import = "";
		    break;
		case BIND_OPCODE_SET_TYPE_IMM:
		    type = immediate;
		    typeName = bindTypeName(type);
		    break;
		case BIND_OPCODE_SET_ADDEND_SLEB:
		    addend = read_sleb128(&p, end);
		    break;
		case BIND_OPCODE_SET_SEGMENT_AND_OFFSET_ULEB:
		    segIndex = immediate;
		    segStartAddr = segStartAddress(segIndex, segs, nsegs,
						   segs64, nsegs64);
		    segName = segmentName(segIndex, segs, nsegs,
					  segs64, nsegs64);
		    segOffset = read_uleb128(&p, end);
		    break;
		case BIND_OPCODE_ADD_ADDR_ULEB:
		    segOffset += read_uleb128(&p, end);
		    break;
		case BIND_OPCODE_DO_BIND:
		    sectName = sectionName(segIndex, segStartAddr + segOffset,
					   segs, nsegs, segs64, nsegs64);
		    if(pass == 2){
			(*dbi)[n].segname = segName;
			(*dbi)[n].sectname = sectName;
			(*dbi)[n].address = segStartAddr+segOffset;
			(*dbi)[n].bind_type = type;
			(*dbi)[n].addend = addend;
			(*dbi)[n].dylibname = fromDylib;
			(*dbi)[n].symbolname = symbolName;
			(*dbi)[n].weak_import = *weak_import != '\0';
		    }
		    n++;
		    segOffset += sizeof_pointer;
		    break;
		case BIND_OPCODE_DO_BIND_ADD_ADDR_ULEB:
		    sectName = sectionName(segIndex, segStartAddr + segOffset,
					   segs, nsegs, segs64, nsegs64);
		    if(pass == 2){
			(*dbi)[n].segname = segName;
			(*dbi)[n].sectname = sectName;
			(*dbi)[n].address = segStartAddr+segOffset;
			(*dbi)[n].bind_type = type;
			(*dbi)[n].addend = addend;
			(*dbi)[n].dylibname = fromDylib;
			(*dbi)[n].symbolname = symbolName;
			(*dbi)[n].weak_import = *weak_import != '\0';
		    }
		    n++;
		    segOffset += read_uleb128(&p, end) + sizeof_pointer;
		    break;
		case BIND_OPCODE_DO_BIND_ADD_ADDR_IMM_SCALED:
		    sectName = sectionName(segIndex, segStartAddr + segOffset,
					   segs, nsegs, segs64, nsegs64);
		    if(pass == 2){
			(*dbi)[n].segname = segName;
			(*dbi)[n].sectname = sectName;
			(*dbi)[n].address = segStartAddr+segOffset;
			(*dbi)[n].bind_type = type;
			(*dbi)[n].addend = addend;
			(*dbi)[n].dylibname = fromDylib;
			(*dbi)[n].symbolname = symbolName;
			(*dbi)[n].weak_import = *weak_import != '\0';
		    }
		    n++;
		    segOffset += immediate * sizeof_pointer + sizeof_pointer;
		    break;
		case BIND_OPCODE_DO_BIND_ULEB_TIMES_SKIPPING_ULEB:
		    count = read_uleb128(&p, end);
		    skip = read_uleb128(&p, end);
		    sectName = sectionName(segIndex, segStartAddr + segOffset,
					   segs, nsegs, segs64, nsegs64);
		    for (uint32_t i=0; i < count; ++i) {
			if(pass == 2){
			    (*dbi)[n].segname = segName;
			    (*dbi)[n].sectname = sectName;
			    (*dbi)[n].address = segStartAddr+segOffset;
			    (*dbi)[n].bind_type = type;
			    (*dbi)[n].addend = addend;
			    (*dbi)[n].dylibname = fromDylib;
			    (*dbi)[n].symbolname = symbolName;
			    (*dbi)[n].weak_import = *weak_import != '\0';
			}
			n++;
			segOffset += skip + sizeof_pointer;
		    }
		    break;
		default:
		    return; /* throwf("bad bind opcode %d", *p); */
	    }
	}	
    }

}

/*
 * print_dyld_bind_info() prints the internal expanded dyld bind information in
 * the same format as dyldinfo(1)'s -bind option.
 */
void
print_dyld_bind_info(
struct dyld_bind_info *dbi,
uint64_t ndbi)
{
    uint64_t n;

	printf("bind information:\n");
	printf("segment section          address        type    addend dylib"
	       "            symbol\n");
	for(n = 0; n < ndbi; n++){
	    printf("%-7s %-16.16s 0x%08llX %10s  %5lld %-16s %s%s\n",
		dbi[n].segname,
		dbi[n].sectname,
		dbi[n].address,
		bindTypeName(dbi[n].bind_type),
		dbi[n].addend,
		dbi[n].dylibname,
		dbi[n].symbolname,
		dbi[n].weak_import ? " (weak import)" : "");
	}
}

/*
 * get_dyld_bind_info_symbolname() is passed an address and the internal expanded
 * dyld bind information.  If the address is found its binding symbol name is
 * returned.  If not NULL.
 */
const char *
get_dyld_bind_info_symbolname(
uint64_t address,
struct dyld_bind_info *dbi,
uint64_t ndbi)
{
    uint64_t n;

	for(n = 0; n < ndbi; n++){
	    if(dbi[n].address == address)
		return(dbi[n].symbolname);
	}
	return(NULL);
}
