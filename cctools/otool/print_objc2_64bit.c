/*
 * Copyright © 2009 Apple Inc. All rights reserved.
 *
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
#include "stdio.h"
#include "stdlib.h"
#include "stddef.h"
#include "string.h"
#include "mach-o/loader.h"
#include "mach-o/arm64/reloc.h"
#include "stuff/allocate.h"
#include "stuff/bytesex.h"
#include "stuff/symbol.h"
#include "stuff/reloc.h"
#include "dyld_bind_info.h"
#include "ofile_print.h"

extern char *oname;

/*
 * Here we need structures that have the same memory layout and size as the
 * 64-bit Objective-C meta data structures that can be used in this 32-bit
 * program.
 *
 * The real structure definitions come from the objc4 project in the private
 * header file runtime/objc-runtime-new.h in that project.
 */

struct class_t {
    uint64_t isa;		/* class_t * (64-bit pointer) */
    uint64_t superclass;	/* class_t * (64-bit pointer) */
    uint64_t cache;		/* Cache (64-bit pointer) */
    uint64_t vtable;		/* IMP * (64-bit pointer) */
    uint64_t data;		/* class_ro_t * (64-bit pointer) */
};

static
void
swap_class_t(
struct class_t *c,
enum byte_sex target_byte_sex)
{
	c->isa = SWAP_LONG_LONG(c->isa);
	c->superclass = SWAP_LONG_LONG(c->superclass);
	c->cache = SWAP_LONG_LONG(c->cache);
	c->vtable = SWAP_LONG_LONG(c->vtable);
	c->data = SWAP_LONG_LONG(c->data);
}

struct class_ro_t {
    uint32_t flags;
    uint32_t instanceStart;
    uint32_t instanceSize;
    uint32_t reserved;
    uint64_t ivarLayout;	/* const uint8_t * (64-bit pointer) */
    uint64_t name; 		/* const char * (64-bit pointer) */
    uint64_t baseMethods; 	/* const method_list_t * (64-bit pointer) */
    uint64_t baseProtocols; 	/* const protocol_list_t * (64-bit pointer) */
    uint64_t ivars; 		/* const ivar_list_t * (64-bit pointer) */
    uint64_t weakIvarLayout; 	/* const uint8_t * (64-bit pointer) */
    uint64_t baseProperties; 	/* const struct objc_property_list *
							(64-bit pointer) */
};

/* Values for class_ro_t->flags */
#define RO_META               (1<<0)
#define RO_ROOT               (1<<1)
#define RO_HAS_CXX_STRUCTORS  (1<<2)


static
void
swap_class_ro_t(
struct class_ro_t *cro,
enum byte_sex target_byte_sex)
{
	cro->flags = SWAP_INT(cro->flags);
	cro->instanceStart = SWAP_INT(cro->instanceStart);
	cro->instanceSize = SWAP_INT(cro->instanceSize);
	cro->reserved = SWAP_INT(cro->reserved);
	cro->ivarLayout = SWAP_LONG_LONG(cro->ivarLayout);
	cro->name = SWAP_LONG_LONG(cro->name);
	cro->baseMethods = SWAP_LONG_LONG(cro->baseMethods);
	cro->baseProtocols = SWAP_LONG_LONG(cro->baseProtocols);
	cro->ivars = SWAP_LONG_LONG(cro->ivars);
	cro->weakIvarLayout = SWAP_LONG_LONG(cro->weakIvarLayout);
	cro->baseProperties = SWAP_LONG_LONG(cro->baseProperties);
}

struct method_list_t {
    uint32_t entsize;
    uint32_t count;
    /* struct method_t first;  These structures follow inline */
};

static
void
swap_method_list_t(
struct method_list_t *ml,
enum byte_sex target_byte_sex)
{
	ml->entsize = SWAP_INT(ml->entsize);
	ml->count = SWAP_INT(ml->count);
}

struct method_t {
    uint64_t name;	/* SEL (64-bit pointer) */
    uint64_t types;	/* const char * (64-bit pointer) */
    uint64_t imp;	/* IMP (64-bit pointer) */
};

static
void
swap_method_t(
struct method_t *m,
enum byte_sex target_byte_sex)
{
	m->name = SWAP_LONG_LONG(m->name);
	m->types = SWAP_LONG_LONG(m->types);
	m->imp = SWAP_LONG_LONG(m->imp);
}

struct ivar_list_t {
    uint32_t entsize;
    uint32_t count;
    /* struct ivar_t first;  These structures follow inline */
};

static
void
swap_ivar_list_t(
struct ivar_list_t *il,
enum byte_sex target_byte_sex)
{
	il->entsize = SWAP_INT(il->entsize);
	il->count = SWAP_INT(il->count);
}

struct ivar_t {
    uint64_t offset;	/* uintptr_t * (64-bit pointer) */
    uint64_t name;	/* const char * (64-bit pointer) */
    uint64_t type;	/* const char * (64-bit pointer) */
    uint32_t alignment;
    uint32_t size;
};

static
void
swap_ivar_t(
struct ivar_t *i,
enum byte_sex target_byte_sex)
{
	i->offset = SWAP_LONG_LONG(i->offset);
	i->name = SWAP_LONG_LONG(i->name);
	i->type = SWAP_LONG_LONG(i->type);
	i->alignment = SWAP_INT(i->alignment);
	i->size = SWAP_INT(i->size);
}

struct protocol_list_t {
    uint64_t count;	/* uintptr_t (a 64-bit value) */
    /* struct protocol_t * list[0];  These pointers follow inline */
};

static
void
swap_protocol_list_t(
struct protocol_list_t *pl,
enum byte_sex target_byte_sex)
{
	pl->count = SWAP_LONG_LONG(pl->count);
}

struct protocol_t {
    uint64_t isa;			/* id * (64-bit pointer) */
    uint64_t name;			/* const char * (64-bit pointer) */
    uint64_t protocols;			/* struct protocol_list_t *
							(64-bit pointer) */
    uint64_t instanceMethods;		/* method_list_t * (64-bit pointer) */
    uint64_t classMethods;		/* method_list_t * (64-bit pointer) */
    uint64_t optionalInstanceMethods;	/* method_list_t * (64-bit pointer) */
    uint64_t optionalClassMethods;	/* method_list_t * (64-bit pointer) */
    uint64_t instanceProperties;	/* struct objc_property_list *
							   (64-bit pointer) */
};

static
void
swap_protocol_t(
struct protocol_t *p,
enum byte_sex target_byte_sex)
{
	p->isa = SWAP_LONG_LONG(p->isa);
	p->name = SWAP_LONG_LONG(p->name);
	p->protocols = SWAP_LONG_LONG(p->protocols);
	p->instanceMethods = SWAP_LONG_LONG(p->instanceMethods);
	p->classMethods = SWAP_LONG_LONG(p->classMethods);
	p->optionalInstanceMethods = SWAP_LONG_LONG(p->optionalInstanceMethods);
	p->optionalClassMethods = SWAP_LONG_LONG(p->optionalClassMethods);
	p->instanceProperties = SWAP_LONG_LONG(p->instanceProperties);
}

struct objc_property_list {
    uint32_t entsize;
    uint32_t count;
    /* struct objc_property first;  These structures follow inline */
};

static
void
swap_objc_property_list(
struct objc_property_list *pl,
enum byte_sex target_byte_sex)
{
	pl->entsize = SWAP_INT(pl->entsize);
	pl->count = SWAP_INT(pl->count);
}

struct objc_property {
    uint64_t name;		/* const char * (64-bit pointer) */
    uint64_t attributes;	/* const char * (64-bit pointer) */
};

static
void
swap_objc_property(
struct objc_property *op,
enum byte_sex target_byte_sex)
{
	op->name = SWAP_LONG_LONG(op->name);
	op->attributes = SWAP_LONG_LONG(op->attributes);
}

struct category_t {
    uint64_t name; 		/* const char * (64-bit pointer) */
    uint64_t cls;		/* struct class_t * (64-bit pointer) */
    uint64_t instanceMethods;	/* struct method_list_t * (64-bit pointer) */
    uint64_t classMethods;	/* struct method_list_t * (64-bit pointer) */
    uint64_t protocols;		/* struct protocol_list_t * (64-bit pointer) */
    uint64_t instanceProperties; /* struct objc_property_list *
				    (64-bit pointer) */
};

static
void
swap_category_t(
struct category_t *c,
enum byte_sex target_byte_sex)
{
	c->name = SWAP_LONG_LONG(c->name);
	c->cls = SWAP_LONG_LONG(c->cls);
	c->instanceMethods = SWAP_LONG_LONG(c->instanceMethods);
	c->classMethods = SWAP_LONG_LONG(c->classMethods);
	c->protocols = SWAP_LONG_LONG(c->protocols);
	c->instanceProperties = SWAP_LONG_LONG(c->instanceProperties);
}

struct message_ref {
    uint64_t imp;	/* IMP (64-bit pointer) */
    uint64_t sel;	/* SEL (64-bit pointer) */
};

static
void
swap_message_ref(
struct message_ref *mr,
enum byte_sex target_byte_sex)
{
	mr->imp = SWAP_LONG_LONG(mr->imp);
	mr->sel = SWAP_LONG_LONG(mr->sel);
}

struct objc_image_info {
    uint32_t version;
    uint32_t flags;
};
/* masks for objc_image_info.flags */
#define OBJC_IMAGE_IS_REPLACEMENT (1<<0)
#define OBJC_IMAGE_SUPPORTS_GC (1<<1)


static
void
swap_objc_image_info(
struct objc_image_info *o,
enum byte_sex target_byte_sex)
{
	o->version = SWAP_INT(o->version);
	o->flags = SWAP_INT(o->flags);
}

struct objc_string_object_64 {
    uint64_t isa;		/* class_t * (64-bit pointer) */
    uint64_t characters;	/* char * (64-bit pointer) */
    uint32_t _length;		/* number of non-NULL characters in above */
    uint32_t _pad;		/* unused padding, compiler uses .space 4 */
};

static
void
swap_string_object_64(
struct objc_string_object_64 *string_object,
enum byte_sex target_byte_sex)
{
	string_object->isa = SWAP_LONG_LONG(string_object->isa);
	string_object->characters = SWAP_LONG_LONG(string_object->characters);
	string_object->_length = SWAP_INT(string_object->_length);
	string_object->_pad = SWAP_INT(string_object->_pad);
}

struct cfstring_t {
    uint64_t isa;		/* class_t * (64-bit pointer) */
    uint64_t flags;		/* flag bits */
    uint64_t characters;	/* char * (64-bit pointer) */
    uint64_t length;		/* number of non-NULL characters in above */
};

static
void
swap_cfstring_t(
struct cfstring_t *cfstring,
enum byte_sex target_byte_sex)
{
        cfstring->isa = SWAP_LONG_LONG(cfstring->isa);
        cfstring->flags = SWAP_LONG_LONG(cfstring->flags);
        cfstring->characters = SWAP_LONG_LONG(cfstring->characters);
        cfstring->length = SWAP_LONG_LONG(cfstring->length);
}

struct info {
    char *object_addr;
    uint32_t object_size;
    enum bool swapped;
    enum byte_sex host_byte_sex;
    struct section_info_64 *sections;
    uint32_t nsections;
    cpu_type_t cputype;
    cpu_subtype_t cpusubtype;
    struct nlist_64 *symbols64;
    uint32_t nsymbols;
    char *strings;
    uint32_t strings_size;
    struct symbol *sorted_symbols;
    uint32_t nsorted_symbols;
    uint64_t database;
    struct relocation_info *ext_relocs;
    uint32_t next_relocs;
    struct relocation_info *loc_relocs;
    uint32_t nloc_relocs;
    struct dyld_bind_info *dbi;
    uint64_t ndbi;
    enum bool ThreadedRebaseBind;
    enum bool verbose;
    enum bool Vflag;
    uint32_t depth;
};

struct section_info_64 {
    char segname[16];
    char sectname[16];
    char *contents;
    uint64_t addr;
    uint64_t size;
    uint32_t offset;
    struct relocation_info *relocs;
    uint32_t nrelocs;
    enum bool cstring;
    enum bool protected;
    enum bool zerofill;
};

static void walk_pointer_list(
    char *listname,
    struct section_info_64 *s,
    struct info *info,
    void (*func)(uint64_t, struct info *));

static void print_class_t(
    uint64_t p,
    struct info *info);

static void print_class_ro_t(
    uint64_t p,
    struct info *info,
    enum bool *is_meta_class);

static void print_layout_map(
    uint64_t p,
    struct info *info);

static void print_method_list_t(
    uint64_t p,
    struct info *info,
    char *indent);

static void print_ivar_list_t(
    uint64_t p,
    struct info *info);

static void print_protocol_list_t(
    uint64_t p,
    struct info *info);

static void print_objc_property_list(
    uint64_t p,
    struct info *info);

static void print_category_t(
    uint64_t p,
    struct info *info);

static void print_message_refs(
    struct section_info_64 *s,
    struct info *info);

static void print_image_info(
    struct section_info_64 *s,
    struct info *info);

static void get_sections_64(
    struct load_command *load_commands,
    uint32_t ncmds,
    uint32_t sizeofcmds,
    enum byte_sex object_byte_sex,
    char *object_addr,
    uint32_t object_size,
    struct section_info_64 **sections,
    uint32_t *nsections,
    uint64_t *database);

static struct section_info_64 *get_section_64(
    struct section_info_64 *sections,
    uint32_t nsections,
    char *segname,
    char *sectname);

static void get_cstring_section_64(
    struct load_command *load_commands,
    uint32_t ncmds,
    uint32_t sizeofcmds,
    enum byte_sex object_byte_sex,
    char *object_addr,
    uint32_t object_size,
    struct section_info_64 *cstring_section_ptr);

static void *get_pointer_64(
    uint64_t p,
    uint32_t *offset,
    uint32_t *left,
    struct section_info_64 **s,
    struct section_info_64 *sections,
    uint32_t nsections);

static const char *get_symbol_64(
    uint32_t sect_offset,
    uint64_t sect_addr,
    uint64_t database,
    uint64_t value,
    struct relocation_info *relocs,
    uint32_t nrelocs,
    struct info *info,
    uint64_t *n_value,
    int64_t *addend);

static void print_pointer(
    uint64_t pointer,
    const char *sym_name,
    uint64_t n_value,
    int64_t addend,
    const char *name,
    char *newline,
    struct info *info);

/*
 * Print the objc2 meta data in 64-bit Mach-O files.
 */
void
print_objc2_64bit(
cpu_type_t cputype,
cpu_subtype_t cpusubtype,
struct load_command *load_commands,
uint32_t ncmds,
uint32_t sizeofcmds,
enum byte_sex object_byte_sex,
char *object_addr,
uint32_t object_size,
struct nlist_64 *symbols64,
uint32_t nsymbols,
char *strings,
uint32_t strings_size,
struct symbol *sorted_symbols,
uint32_t nsorted_symbols,
struct relocation_info *ext_relocs,
uint32_t next_relocs,
struct relocation_info *loc_relocs,
uint32_t nloc_relocs,
struct dyld_bind_info *dbi,
uint64_t ndbi,
enum bool ThreadedRebaseBind,
enum bool verbose,
enum bool Vflag)
{
    struct section_info_64 *s;
    struct info info;

	info.object_addr = object_addr;
	info.object_size = object_size;
	info.host_byte_sex = get_host_byte_sex();
	info.swapped = info.host_byte_sex != object_byte_sex;
	info.cputype = cputype;
	info.cpusubtype = cpusubtype;
	info.symbols64 = symbols64;
	info.nsymbols = nsymbols;
	info.strings = strings;
	info.strings_size = strings_size;
	info.sorted_symbols = sorted_symbols;
	info.nsorted_symbols = nsorted_symbols;
	info.ext_relocs = ext_relocs;
	info.next_relocs = next_relocs;
	info.loc_relocs = loc_relocs;
	info.nloc_relocs = nloc_relocs;
	info.dbi = dbi;
	info.ndbi = ndbi;
	info.ThreadedRebaseBind = ThreadedRebaseBind;
	info.verbose = verbose;
	info.Vflag = Vflag;
	get_sections_64(load_commands, ncmds, sizeofcmds, object_byte_sex,
			object_addr, object_size, &info.sections,
			&info.nsections, &info.database);

	s = get_section_64(info.sections, info.nsections,
				"__OBJC2", "__class_list");
	if(s == NULL)
	    s = get_section_64(info.sections, info.nsections,
				"__DATA", "__objc_classlist");
	if(s == NULL)
	    s = get_section_64(info.sections, info.nsections,
				"__DATA_CONST", "__objc_classlist");
	if(s == NULL)
	    s = get_section_64(info.sections, info.nsections,
				"__DATA_DIRTY", "__objc_classlist");
	info.depth = 0;
	walk_pointer_list("class", s, &info, print_class_t);

	s = get_section_64(info.sections, info.nsections,
				"__OBJC2", "__class_refs");
	if(s == NULL)
	    s = get_section_64(info.sections, info.nsections,
				"__DATA", "__objc_classrefs");
	if(s == NULL)
	    s = get_section_64(info.sections, info.nsections,
				"__DATA_CONST", "__objc_classrefs");
	if(s == NULL)
	    s = get_section_64(info.sections, info.nsections,
				"__DATA_DIRTY", "__objc_classrefs");
	walk_pointer_list("class refs", s, &info, NULL);

	s = get_section_64(info.sections, info.nsections,
				"__OBJC2", "__super_refs");
	if(s == NULL)
	    s = get_section_64(info.sections, info.nsections,
				"__DATA", "__objc_superrefs");
	if(s == NULL)
	    s = get_section_64(info.sections, info.nsections,
				"__DATA_CONST", "__objc_superrefs");
	if(s == NULL)
	    s = get_section_64(info.sections, info.nsections,
				"__DATA_DIRTY", "__objc_superrefs");
	walk_pointer_list("super refs", s, &info, NULL);

	s = get_section_64(info.sections, info.nsections,
				"__OBJC2", "__category_list");
	if(s == NULL)
	    s = get_section_64(info.sections, info.nsections,
				"__DATA", "__objc_catlist");
	if(s == NULL)
	    s = get_section_64(info.sections, info.nsections,
				"__DATA_CONST", "__objc_catlist");
	if(s == NULL)
	    s = get_section_64(info.sections, info.nsections,
				"__DATA_DIRTY", "__objc_catlist");
	walk_pointer_list("category", s, &info, print_category_t);

	s = get_section_64(info.sections, info.nsections,
				"__OBJC2", "__protocol_list");
	if(s == NULL)
	    s = get_section_64(info.sections, info.nsections,
				"__DATA", "__objc_protolist");
	if(s == NULL)
	    s = get_section_64(info.sections, info.nsections,
				"__DATA_CONST", "__objc_protolist");
	if(s == NULL)
	    s = get_section_64(info.sections, info.nsections,
				"__DATA_DIRTY", "__objc_protolist");
	walk_pointer_list("protocol", s, &info, NULL);

	s = get_section_64(info.sections, info.nsections,
				"__OBJC2", "__message_refs");
	if(s == NULL)
	    s = get_section_64(info.sections, info.nsections,
				"__DATA", "__objc_msgrefs");
	if(s == NULL)
	    s = get_section_64(info.sections, info.nsections,
				"__DATA_CONST", "__objc_msgrefs");
	if(s == NULL)
	    s = get_section_64(info.sections, info.nsections,
				"__DATA_DIRTY", "__objc_msgrefs");
	print_message_refs(s, &info);

	s = get_section_64(info.sections, info.nsections,
				"__OBJC", "__image_info");
	if(s == NULL)
	    s = get_section_64(info.sections, info.nsections,
				"__DATA", "__objc_imageinfo");
	if(s == NULL)
	    s = get_section_64(info.sections, info.nsections,
				"__DATA_CONST", "__objc_imageinfo");
	if(s == NULL)
	    s = get_section_64(info.sections, info.nsections,
				"__DATA_DIRTY", "__objc_imageinfo");
	print_image_info(s, &info);
}

static
void
walk_pointer_list(
char *listname,
struct section_info_64 *s,
struct info *info,
void (*func)(uint64_t, struct info *))
{
    uint32_t i, size, left;
    uint64_t p, n_value;
    int64_t addend;
    const char *name;

	if(s == NULL)
	    return;

	printf("Contents of (%.16s,%.16s) section\n", s->segname, s->sectname);
	for(i = 0; i < s->size; i += sizeof(uint64_t)){

	    memset(&p, '\0', sizeof(uint64_t));
	    left = s->size - i; 
	    size = left < sizeof(uint64_t) ?
		   left : sizeof(uint64_t);
	    if(s->contents + i + size > info->object_addr + info->object_size)
		return;
	    memcpy(&p, s->contents + i, size);

	    if(i + sizeof(uint64_t) > s->size)
		printf("%s list pointer extends past end of (%s,%s) "
		       "section\n", listname, s->segname, s->sectname);
	    printf("%016llx ", s->addr + i);

	    if(info->swapped)
		p = SWAP_LONG_LONG(p);

	    name = get_symbol_64(i, s->addr, info->database, p, s->relocs,
				 s->nrelocs, info, &n_value, &addend);
	    print_pointer(p, NULL, n_value, addend, name, "\n", info);

	    if(func != NULL)
		func(n_value + addend, info);
	}
}

/*
 * get_objc2_64bit_cfstring_name() is used for disassembly and is passed a
 * pointer to a cfstring and returns its name.
 */
char *
get_objc2_64bit_cfstring_name(
uint64_t p,
struct load_command *load_commands,
uint32_t ncmds,
uint32_t sizeofcmds,
enum byte_sex object_byte_sex,
char *object_addr,
uint32_t object_size,
struct nlist_64 *symbols64,
uint32_t nsymbols,
char *strings,
uint32_t strings_size,
cpu_type_t cputype)
{
    struct section_info_64 *sections, *s;
    uint32_t nsections, left, offset;
    uint64_t database, n_value, cfs_characters;
    int64_t addend;
    struct cfstring_t cfs;
    char *name;
    const char *symbol_name;
    void *r;
    struct info info;

	memset(&info, '\0', sizeof(struct info));
	info.symbols64 = symbols64;
	info.nsymbols = nsymbols;
	info.strings = strings;
	info.strings_size = strings_size;
	info.cputype = cputype;
	info.verbose = TRUE;

	get_sections_64(load_commands, ncmds, sizeofcmds, object_byte_sex,
			object_addr, object_size, &sections, &nsections,
			&database);

	r = get_pointer_64(p, &offset, &left, &s, sections, nsections);
	if(r == NULL || left < sizeof(struct cfstring_t))
	    return(NULL);
	memcpy(&cfs, r, sizeof(struct cfstring_t));
	if(get_host_byte_sex() != object_byte_sex)
	    swap_cfstring_t(&cfs, get_host_byte_sex());
	symbol_name = get_symbol_64(offset +
				    offsetof(struct cfstring_t, characters),
				    s->addr, database, p, s->relocs,
				    s->nrelocs, &info, &n_value, &addend);
	if(symbol_name == NULL){
	    if(sections != NULL)
		free(sections);
	    return(NULL);
	}
	cfs_characters = n_value + addend;

	name = get_pointer_64(cfs_characters, NULL, &left, NULL,
			      sections, nsections);

	if(sections != NULL)
	    free(sections);

	return(name);
}

/*
 * get_objc2_64bit_class_name() is used for disassembly and is passed a pointer
 * to an Objective-C class and returns the class name.  It is also passed the
 * address of the pointer, so when the pointer is zero as it can be in an .o
 * file, that is use to look for an external relocation entry with a symbol
 * name.
 */
char *
get_objc2_64bit_class_name(
uint64_t p,
uint64_t address_of_p,
struct load_command *load_commands,
uint32_t ncmds,
uint32_t sizeofcmds,
enum byte_sex object_byte_sex,
char *object_addr,
uint32_t object_size,
struct nlist_64 *symbols64,
uint32_t nsymbols,
char *strings,
uint32_t strings_size,
cpu_type_t cputype)
{
    struct section_info_64 *sections, *s;
    uint32_t nsections, left, offset;
    uint64_t database, n_value;
    int64_t addend;
    struct class_t c;
    struct class_ro_t cro;
    char *name, *class_name;
    const char *symbol_name;
    void *r;
    struct info info;

	memset(&info, '\0', sizeof(struct info));
	info.symbols64 = symbols64;
	info.nsymbols = nsymbols;
	info.strings = strings;
	info.strings_size = strings_size;
	info.cputype = cputype;
	info.verbose = TRUE;

	get_sections_64(load_commands, ncmds, sizeofcmds, object_byte_sex,
			object_addr, object_size, &sections, &nsections,
			&database);
	if(p == 0){
	    r = get_pointer_64(address_of_p, &offset, &left, &s, sections,
			       nsections);
	    if(r == NULL || left < sizeof(uint64_t)){
		if(sections != NULL)
		    free(sections);
		return(NULL);
	    }
	    symbol_name = get_symbol_64(offset, s->addr, database,
				        address_of_p, s->relocs, s->nrelocs,
					&info, &n_value, &addend);
	    if(symbol_name == NULL){
		if(sections != NULL)
		    free(sections);
		return(NULL);
	    }
	    class_name = rindex(symbol_name, '$');
	    if(class_name != NULL &&
	       class_name[1] == '_' && class_name[2] != '\0'){
		if(sections != NULL)
		    free(sections);
		return(class_name + 2);
	    }
	    else{
		if(sections != NULL)
		    free(sections);
		return(NULL);
	    }
	}

	r = get_pointer_64(p, NULL, &left, NULL, sections, nsections);
	if(r == NULL || left < sizeof(struct class_t)){
	    if(sections != NULL)
		free(sections);
	    return(NULL);
	}
	memcpy(&c, r, sizeof(struct class_t));
	if(get_host_byte_sex() != object_byte_sex)
	    swap_class_t(&c, get_host_byte_sex());
	if(c.data == 0){
	    if(sections != NULL)
		free(sections);
	    return(NULL);
        }

	r = get_pointer_64(c.data, NULL, &left, NULL, sections, nsections);
	if(r == NULL || left < sizeof(struct class_ro_t)){
	    if(sections != NULL)
		free(sections);
	    return(NULL);
	}
	memcpy(&cro, r, sizeof(struct class_ro_t));
	if(get_host_byte_sex() != object_byte_sex)
	    swap_class_ro_t(&cro, get_host_byte_sex());
	
	if(cro.name == 0){
	    if(sections != NULL)
		free(sections);
	    return(NULL);
	}

	name = get_pointer_64(cro.name, NULL, &left, NULL, sections, nsections);

	if(sections != NULL)
	    free(sections);

	return(name);
}

/*
 * get_objc2_64bit_selref() is used for disassembly and is passed a the address
 * of a pointer to an Objective-C selector reference when the pointer value is
 * zero as in a .o file and is likely to have a external relocation entry with
 * who's symbol's n_value is the real pointer to the selector name.  If that is
 * the case the real pointer to the selector name is returned else 0 is
 * returned
 */
uint64_t
get_objc2_64bit_selref(
uint64_t address_of_p,
struct load_command *load_commands,
uint32_t ncmds,
uint32_t sizeofcmds,
enum byte_sex object_byte_sex,
char *object_addr,
uint32_t object_size,
struct nlist_64 *symbols64,
uint32_t nsymbols,
char *strings,
uint32_t strings_size,
cpu_type_t cputype)
{
    struct section_info_64 *sections, *s;
    uint32_t nsections, left, offset;
    uint64_t database, n_value;
    int64_t addend;
    void *r;
    const char *symbol_name;
    struct info info;

	memset(&info, '\0', sizeof(struct info));
	info.symbols64 = symbols64;
	info.nsymbols = nsymbols;
	info.strings = strings;
	info.strings_size = strings_size;
	info.cputype = cputype;
	info.verbose = TRUE;

	get_sections_64(load_commands, ncmds, sizeofcmds, object_byte_sex,
			object_addr, object_size, &sections, &nsections,
			&database);
	r = get_pointer_64(address_of_p, &offset, &left, &s, sections,
			   nsections);
	if(r == NULL || left < sizeof(uint64_t)){
	    if(sections != NULL)
		free(sections);
	    return(0);
	}
	symbol_name = get_symbol_64(offset, s->addr, database,
				    address_of_p, s->relocs, s->nrelocs,
				    &info, &n_value, &addend);
	if(symbol_name == NULL){
	    if(sections != NULL)
		free(sections);
	    return(0);
	}
	return(n_value);
}

static
void
print_class_t(
uint64_t p,
struct info *info)
{
    struct class_t c;
    void *r;
    uint32_t offset, left;
    struct section_info_64 *s;
    const char *name, *sym_name;
    enum bool is_meta_class;
    uint64_t n_value, isa_n_value;
    int64_t addend, isa_addend;

	is_meta_class = FALSE;
	r = get_pointer_64(p, &offset, &left, &s,
			   info->sections, info->nsections);
	if(r == NULL)
	    return;
	memset(&c, '\0', sizeof(struct class_t));
	if(left < sizeof(struct class_t)){
	    memcpy(&c, r, left);
	    printf("   (class_t entends past the end of the section)\n");
	}
	else
	    memcpy(&c, r, sizeof(struct class_t));
	if(info->swapped)
	    swap_class_t(&c, info->host_byte_sex);
	name = get_symbol_64(offset + offsetof(struct class_t, isa),
			     s->addr, info->database, c.isa, s->relocs,
			     s->nrelocs, info, &isa_n_value, &isa_addend);
	printf("           isa ");
	print_pointer(c.isa, NULL, isa_n_value, isa_addend, name, "\n", info);
	name = get_symbol_64(offset + offsetof(struct class_t, superclass),
			     s->addr, info->database, c.superclass, s->relocs,
			     s->nrelocs, info, &n_value, &addend);
	printf("    superclass ");
	print_pointer(c.superclass, NULL, n_value, addend, name, "\n", info);
	name = get_symbol_64(offset + offsetof(struct class_t, cache),
			     s->addr, info->database, c.cache, s->relocs,
			     s->nrelocs, info, &n_value, &addend);
	printf("         cache ");
	print_pointer(c.cache, NULL, n_value, addend, name, "\n", info);
	name = get_symbol_64(offset + offsetof(struct class_t, vtable),
			     s->addr, info->database, c.vtable, s->relocs,
			     s->nrelocs, info, &n_value, &addend);
	printf("        vtable ");
	print_pointer(c.vtable, NULL, n_value, addend, name, "\n", info);

	sym_name = get_symbol_64(offset + offsetof(struct class_t, data),
			         s->addr, info->database, c.data, s->relocs,
			         s->nrelocs, info, &n_value, &addend);
	printf("          data ");
	print_pointer(c.data, sym_name, n_value, addend,
		      "(struct class_ro_t *)", NULL, info);
	/*
	 * This is a Swift class if some of the low bits of the pointer
	 * are set.
	 */
	if((c.data + n_value) & 0x7)
	    printf(" Swift class");
	printf("\n");
	print_class_ro_t((n_value + addend) & ~0x7, info, &is_meta_class);

	if(is_meta_class == FALSE &&
           c.isa + isa_n_value != p &&
	   c.isa + isa_n_value != 0 &&
	   info->depth < 100){
	    info->depth++;
	    printf("Meta Class\n");
	    print_class_t(isa_n_value + isa_addend, info);
	}
}

static
void
print_class_ro_t(
uint64_t p,
struct info *info,
enum bool *is_meta_class)
{
    struct class_ro_t cro;
    void *r;
    uint32_t offset, left;
    struct section_info_64 *s;
    const char *name, *sym_name;
    uint64_t n_value;
    int64_t addend;

	r = get_pointer_64(p, &offset, &left, &s, info->sections,
			   info->nsections);
	if(r == NULL)
	    return;
	memset(&cro, '\0', sizeof(struct class_ro_t));
	if(left < sizeof(struct class_ro_t)){
	    memcpy(&cro, r, left);
	    printf("   (class_ro_t entends past the end of the section)\n");
	}
	else
	    memcpy(&cro, r, sizeof(struct class_ro_t));
	if(info->swapped)
	    swap_class_ro_t(&cro, info->host_byte_sex);
	printf("                    flags 0x%x", cro.flags);
	if(info->verbose){
	    if(cro.flags & RO_META)
		printf(" RO_META");
	    if(cro.flags & RO_ROOT)
		printf(" RO_ROOT");
	    if(cro.flags & RO_HAS_CXX_STRUCTORS)
		printf(" RO_HAS_CXX_STRUCTORS");
	}
	printf("\n");
	printf("            instanceStart %u\n", cro.instanceStart);
	printf("             instanceSize %u\n", cro.instanceSize);
	printf("                 reserved 0x%x\n", cro.reserved);
	sym_name = get_symbol_64(offset + offsetof(struct class_ro_t,
						   ivarLayout),
			         s->addr, info->database, cro.ivarLayout,
				 s->relocs, s->nrelocs, info, &n_value,
				 &addend);
	printf("               ivarLayout ");
	print_pointer(cro.ivarLayout, sym_name, n_value, addend, NULL, "\n",
		      info);
	print_layout_map(n_value + addend, info);

	printf("                     name ");
	sym_name = get_symbol_64(offset + offsetof(struct class_ro_t, name),
			         s->addr, info->database, cro.name, s->relocs,
			         s->nrelocs, info, &n_value, &addend);
	print_pointer(cro.name, sym_name, n_value, addend, NULL, NULL, info);

	name = get_pointer_64(n_value + addend, NULL, &left, NULL,
			      info->sections, info->nsections);
	if(info->verbose && name != NULL)
	    printf(" %.*s", (int)left, name);
	printf("\n");
	printf("              baseMethods ");
	sym_name = get_symbol_64(offset +
				    offsetof(struct class_ro_t, baseMethods),
			         s->addr, info->database, cro.baseMethods,
				 s->relocs, s->nrelocs, info, &n_value,
			         &addend);
	print_pointer(cro.baseMethods, sym_name, n_value, addend,
		      "(struct method_list_t *)", "\n", info);

	if(n_value + addend != 0)
	    print_method_list_t(n_value + addend, info, "");

	printf("            baseProtocols ");
	sym_name = get_symbol_64(offset +
				    offsetof(struct class_ro_t, baseProtocols),
			         s->addr, info->database, cro.baseProtocols,
				 s->relocs, s->nrelocs, info, &n_value,
				 &addend);
	print_pointer(cro.baseProtocols, sym_name, n_value, addend, NULL, "\n",
		      info);
	if(n_value + addend != 0)
	    print_protocol_list_t(n_value + addend, info);

	printf("                    ivars ");
	sym_name = get_symbol_64(offset +
				    offsetof(struct class_ro_t, ivars),
			         s->addr, info->database, cro.ivars, s->relocs,
				 s->nrelocs, info, &n_value, &addend);
	print_pointer(cro.ivars, sym_name, n_value, addend, NULL, "\n", info);
	if(n_value + addend != 0)
	    print_ivar_list_t(n_value + addend, info);

	printf("           weakIvarLayout ");
	sym_name = get_symbol_64(offset +
				    offsetof(struct class_ro_t, weakIvarLayout),
			         s->addr, info->database, cro.weakIvarLayout,
				 s->relocs, s->nrelocs, info, &n_value,
				 &addend);
	print_pointer(cro.weakIvarLayout, sym_name, n_value, addend, NULL,
		      "\n", info);
	print_layout_map(n_value + addend, info);

	printf("           baseProperties ");
	sym_name = get_symbol_64(offset +
				    offsetof(struct class_ro_t, baseProperties),
			         s->addr, info->database, cro.baseProperties,
				 s->relocs, s->nrelocs, info, &n_value,
			         &addend);
	print_pointer(cro.baseProperties, sym_name, n_value, addend, NULL, "\n",
		       info);
	if(n_value + addend != 0)
	    print_objc_property_list(n_value + addend, info);

	if(is_meta_class)
	    *is_meta_class = (cro.flags & RO_META) ? TRUE : FALSE;
}

static
void
print_layout_map(
uint64_t p,
struct info *info)
{
    uint32_t offset, left;
    struct section_info_64 *s;
    char *layout_map;

	if(p == 0)
	    return;
	layout_map = get_pointer_64(p, &offset, &left, &s, 
				    info->sections, info->nsections);
	if(layout_map != NULL){
	    printf("                layout map: ");
	    do{
		printf("0x%02x ", (*layout_map) & 0xff);
		left--;
		layout_map++;
	    }while(*layout_map != '\0' && left != 0);
	    printf("\n");
	}
}

static
void
print_method_list_t(
uint64_t p,
struct info *info,
char *indent)
{
    struct method_list_t ml;
    struct method_t m;
    void *r;
    uint32_t offset, left, i;
    struct section_info_64 *s;
    const char *name, *sym_name;
    uint64_t n_value;
    int64_t addend;

	r = get_pointer_64(p, &offset, &left, &s, info->sections,
			   info->nsections);
	if(r == NULL)
	    return;
	memset(&ml, '\0', sizeof(struct method_list_t));
	if(left < sizeof(struct method_list_t)){
	    memcpy(&ml, r, left);
	    printf("%s   (method_list_t entends past the end of the "
		   "section)\n", indent);
	}
	else
	    memcpy(&ml, r, sizeof(struct method_list_t));
	if(info->swapped)
	    swap_method_list_t(&ml, info->host_byte_sex);
	printf("%s\t\t   entsize %u\n", indent, ml.entsize);
	printf("%s\t\t     count %u\n", indent, ml.count);

	p += sizeof(struct method_list_t);
	offset += sizeof(struct method_list_t);
	for(i = 0; i < ml.count; i++){
	    r = get_pointer_64(p, &offset, &left, &s, info->sections,
			       info->nsections);
	    if(r == NULL)
		return;
	    memset(&m, '\0', sizeof(struct method_t));
	    if(left < sizeof(struct method_t)){
		memcpy(&m, r, left);
		printf("%s   (method_t entends past the end of the "
		       "section)\n", indent);
	    }
	    else
		memcpy(&m, r, sizeof(struct method_t));
	    if(info->swapped)
		swap_method_t(&m, info->host_byte_sex);

	    printf("%s\t\t      name ", indent);
	    sym_name = get_symbol_64(offset + offsetof(struct method_t, name),
				     s->addr, info->database, m.name,
				     s->relocs, s->nrelocs, info, &n_value,
				     &addend);
	    print_pointer(m.name, sym_name, n_value, addend, NULL, NULL, info);

	    if(info->verbose){
		name = get_pointer_64(n_value + addend, NULL, &left, NULL,
				      info->sections, info->nsections);
		if(name != NULL)
		    printf(" %.*s", (int)left, name);
	    }
	    printf("\n");

	    printf("%s\t\t     types ", indent);
	    sym_name = get_symbol_64(offset + offsetof(struct method_t, types),
				     s->addr, info->database, m.types,
				     s->relocs, s->nrelocs, info, &n_value,
				     &addend);
	    print_pointer(m.types, sym_name, n_value, addend, NULL, NULL, info);
	    if(info->verbose){
		name = get_pointer_64(n_value + addend, NULL, &left, NULL,
				      info->sections, info->nsections);
		if(name != NULL)
		    printf(" %.*s", (int)left, name);
	    }
	    printf("\n");

	    printf("%s\t\t       imp ", indent);
	    name = get_symbol_64(offset + offsetof(struct method_t, imp),
				 s->addr, info->database, m.imp, s->relocs,
				 s->nrelocs, info, &n_value, &addend);
	    print_pointer(m.imp, NULL, n_value, addend, name, "\n", info);

	    p += sizeof(struct method_t);
	    offset += sizeof(struct method_t);
	}
}

static
void
print_ivar_list_t(
uint64_t p,
struct info *info)
{
    struct ivar_list_t il;
    struct ivar_t i;
    void *r;
    uint32_t offset, left, j;
    struct section_info_64 *s;
    const char *name, *sym_name;
    uint64_t *ivar_offset_p, n_value;
    uint32_t ivar_offset;
    int64_t addend;


	r = get_pointer_64(p, &offset, &left, &s, info->sections,
			   info->nsections);
	if(r == NULL)
	    return;
	memset(&il, '\0', sizeof(struct ivar_list_t));
	if(left < sizeof(struct ivar_list_t)){
	    memcpy(&il, r, left);
	    printf("   (ivar_list_t entends past the end of the section)\n");
	}
	else
	    memcpy(&il, r, sizeof(struct ivar_list_t));
	if(info->swapped)
	    swap_ivar_list_t(&il, info->host_byte_sex);
	printf("                    entsize %u\n", il.entsize);
	printf("                      count %u\n", il.count);

	p += sizeof(struct ivar_list_t);
	offset += sizeof(struct ivar_list_t);
	for(j = 0; j < il.count; j++){
	    r = get_pointer_64(p, &offset, &left, &s, info->sections,
			       info->nsections);
	    if(r == NULL)
		return;
	    memset(&i, '\0', sizeof(struct ivar_t));
	    if(left < sizeof(struct ivar_t)){
		memcpy(&i, r, left);
		printf("   (ivar_t entends past the end of the section)\n");
	    }
	    else
		memcpy(&i, r, sizeof(struct ivar_t));
	    if(info->swapped)
		swap_ivar_t(&i, info->host_byte_sex);

	    printf("\t\t\t   offset ");
	    sym_name = get_symbol_64(offset + offsetof(struct ivar_t, offset),
				     s->addr, info->database, i.offset,
				     s->relocs, s->nrelocs, info, &n_value,
				     &addend);
	    print_pointer(i.offset, sym_name, n_value, addend, NULL, NULL,
			  info);
	    ivar_offset_p = get_pointer_64(n_value + addend, NULL, &left, NULL,
					   info->sections, info->nsections);
	    if(ivar_offset_p != NULL && left >= sizeof(ivar_offset)){
		memcpy(&ivar_offset, ivar_offset_p, sizeof(ivar_offset));
		if(info->swapped) 
		    ivar_offset = SWAP_INT(ivar_offset);
		if(info->verbose)
		    printf(" %u", ivar_offset);
            }
	    printf("\n");

	    printf("\t\t\t     name ");
	    sym_name = get_symbol_64(offset + offsetof(struct ivar_t, name),
				     s->addr, info->database, i.name, s->relocs,
				     s->nrelocs, info, &n_value, &addend);
	    print_pointer(i.name, sym_name, n_value, addend, NULL, NULL,
			  info);
	    name = get_pointer_64(n_value + addend, NULL, &left, NULL,
				  info->sections, info->nsections);
	    if(info->verbose && name != NULL)
		printf(" %.*s", (int)left, name);
	    printf("\n");

	    printf("\t\t\t     type ");
	    sym_name = get_symbol_64(offset + offsetof(struct ivar_t, type),
				     s->addr, info->database, i.type, s->relocs,
				     s->nrelocs, info, &n_value, &addend);
	    name = get_pointer_64(n_value + addend, NULL, &left, NULL,
				  info->sections, info->nsections);
	    print_pointer(i.type, sym_name, n_value, addend, NULL, NULL, info);
	    if(info->verbose && name != NULL)
		printf(" %.*s", (int)left, name);
	    printf("\n");

	    printf("\t\t\talignment %u\n", i.alignment);
	    printf("\t\t\t     size %u\n", i.size);

	    p += sizeof(struct ivar_t);
	    offset += sizeof(struct ivar_t);
	}
}

static
void
print_protocol_list_t(
uint64_t p,
struct info *info)
{
    struct protocol_list_t pl;
    uint64_t q, n_value;
    int64_t addend;
    struct protocol_t pc;
    void *r;
    uint32_t offset, left, i;
    struct section_info_64 *s;
    const char *name, *sym_name;

	r = get_pointer_64(p, &offset, &left, &s, info->sections,
			   info->nsections);
	if(r == NULL)
	    return;
	memset(&pl, '\0', sizeof(struct protocol_list_t));
	if(left < sizeof(struct protocol_list_t)){
	    memcpy(&pl, r, left);
	    printf("   (protocol_list_t entends past the end of the "
		   "section)\n");
	}
	else
	    memcpy(&pl, r, sizeof(struct protocol_list_t));
	if(info->swapped)
	    swap_protocol_list_t(&pl, info->host_byte_sex);
	printf("                      count %llu\n", pl.count);

	p += sizeof(struct protocol_list_t);
	offset += sizeof(struct protocol_list_t);
	for(i = 0; i < pl.count; i++){
	    r = get_pointer_64(p, &offset, &left, &s, info->sections,
			       info->nsections);
	    if(r == NULL)
		return;
	    q = 0;
	    if(left < sizeof(uint64_t)){
		memcpy(&q, r, left);
		printf("   (protocol_t * entends past the end of the "
		       "section)\n");
	    }
	    else
		memcpy(&q, r, sizeof(uint64_t));
	    if(info->swapped)
		q = SWAP_LONG_LONG(q);

	    printf("\t\t      list[%u] ", i);
	    sym_name = get_symbol_64(offset, s->addr, info->database, q,
				     s->relocs, s->nrelocs, info, &n_value,
				     &addend);
	    print_pointer(q, sym_name, n_value, addend, "(struct protocol_t *)",
			  "\n", info);

	    r = get_pointer_64(n_value + addend, &offset, &left, &s,
			       info->sections, info->nsections);
	    if(r == NULL)
		return;
	    memset(&pc, '\0', sizeof(struct protocol_t));
	    if(left < sizeof(struct protocol_t)){
		memcpy(&pc, r, left);
		printf("   (protocol_t entends past the end of the section)\n");
	    }
	    else
		memcpy(&pc, r, sizeof(struct protocol_t));
	    if(info->swapped)
		swap_protocol_t(&pc, info->host_byte_sex);

	    printf("\t\t\t      isa 0x%llx\n", pc.isa);

	    printf("\t\t\t     name ");
	    sym_name = get_symbol_64(offset + offsetof(struct protocol_t, name),
				     s->addr, info->database, pc.name,
				     s->relocs, s->nrelocs, info, &n_value,
				     &addend);
	    print_pointer(pc.name, sym_name, n_value, addend, NULL, NULL, info);

	    name = get_pointer_64(n_value + addend , NULL, &left, NULL,
				  info->sections, info->nsections);
	    if(info->verbose && name != NULL)
		printf(" %.*s", (int)left, name);
	    printf("\n");

	    printf("\t\t\tprotocols ");
	    sym_name = get_symbol_64(offset + offsetof(struct protocol_t,
						       protocols),
				     s->addr, info->database, pc.protocols,
				     s->relocs, s->nrelocs, info, &n_value,
				     &addend);
	    print_pointer(pc.protocols, sym_name, n_value, addend, NULL, "\n",
			  info);

	    printf("\t\t  instanceMethods ");
	    sym_name = get_symbol_64(offset + offsetof(struct protocol_t,
						       instanceMethods),
				     s->addr, info->database,
				     pc.instanceMethods, s->relocs, s->nrelocs,
				     info, &n_value, &addend);
	    print_pointer(pc.instanceMethods, sym_name, n_value, addend,
		          "(struct method_list_t *)", "\n", info);
	    if(n_value + addend != 0)
		print_method_list_t(n_value + addend, info, "\t");

	    printf("\t\t     classMethods ");
	    sym_name = get_symbol_64(offset + offsetof(struct protocol_t,
						       classMethods),
				     s->addr, info->database,
				     pc.classMethods, s->relocs, s->nrelocs,
				     info, &n_value, &addend);
	    print_pointer(pc.classMethods, sym_name, n_value, addend,
			  "(struct method_list_t *)", "\n", info);

	    if(n_value + addend != 0)
		print_method_list_t(n_value + addend, info, "\t");

	    printf("\t  optionalInstanceMethods ");
	    sym_name = get_symbol_64(offset + offsetof(struct protocol_t,
						       optionalInstanceMethods),
				     s->addr, info->database,
				     pc.optionalInstanceMethods, s->relocs,
				     s->nrelocs, info, &n_value, &addend);
	    print_pointer(pc.optionalInstanceMethods, sym_name, n_value, addend,
			  NULL, "\n", info);

	    printf("\t     optionalClassMethods 0x%llx\n",
		   pc.optionalClassMethods);
	    printf("\t       instanceProperties ");
	    sym_name = get_symbol_64(offset + offsetof(struct protocol_t,
						       instanceProperties),
				     s->addr, info->database,
				     pc.instanceProperties, s->relocs,
				     s->nrelocs, info, &n_value, &addend);
	    print_pointer(pc.instanceProperties, sym_name, n_value, addend,
			  NULL, "\n", info);

	    p += sizeof(uint64_t);
	    offset += sizeof(uint64_t);
	}
}

static
void
print_objc_property_list(
uint64_t p,
struct info *info)
{
    struct objc_property_list opl;
    struct objc_property op;
    void *r;
    uint32_t offset, left, j;
    struct section_info_64 *s;
    const char *name, *sym_name;
    uint64_t n_value;
    int64_t addend;

	r = get_pointer_64(p, &offset, &left, &s, info->sections,
			   info->nsections);
	if(r == NULL)
	    return;
	memset(&opl, '\0', sizeof(struct objc_property_list));
	if(left < sizeof(struct objc_property_list)){
	    memcpy(&opl, r, left);
	    printf("   (objc_property_list entends past the end of the "
		   "section)\n");
	}
	else
	    memcpy(&opl, r, sizeof(struct objc_property_list));
	if(info->swapped)
	    swap_objc_property_list(&opl, info->host_byte_sex);
	printf("                    entsize %u\n", opl.entsize);
	printf("                      count %u\n", opl.count);

	p += sizeof(struct objc_property_list);
	offset += sizeof(struct objc_property_list);
	for(j = 0; j < opl.count; j++){
	    r = get_pointer_64(p, &offset, &left, &s, info->sections,
			       info->nsections);
	    if(r == NULL)
		return;
	    memset(&op, '\0', sizeof(struct objc_property));
	    if(left < sizeof(struct objc_property)){
		memcpy(&op, r, left);
		printf("   (objc_property entends past the end of the "
		       "section)\n");
	    }
	    else
		memcpy(&op, r, sizeof(struct objc_property));
	    if(info->swapped)
		swap_objc_property(&op, info->host_byte_sex);

	    printf("\t\t\t     name ");
	    sym_name = get_symbol_64(offset + offsetof(struct objc_property,
						       name),
				     s->addr, info->database, op.name,
				     s->relocs, s->nrelocs, info, &n_value,
				     &addend);
	    print_pointer(op.name, sym_name, n_value, addend, NULL, NULL, info);
	    name = get_pointer_64(n_value + addend, NULL, &left, NULL,
				  info->sections, info->nsections);
	    if(info->verbose && name != NULL)
		printf(" %.*s", (int)left, name);
	    printf("\n");

	    printf("\t\t\tattributes ");
	    sym_name = get_symbol_64(offset + offsetof(struct objc_property,
						       attributes),
				     s->addr, info->database, op.attributes,
				     s->relocs, s->nrelocs, info, &n_value,
				     &addend);
	    print_pointer(op.attributes, sym_name, n_value, addend, NULL, NULL,
			  info);
	    name = get_pointer_64(n_value + addend, NULL, &left, NULL,
				  info->sections, info->nsections);
	    if(info->verbose && name != NULL)
		printf(" %.*s", (int)left, name);
	    printf("\n");

	    p += sizeof(struct objc_property);
	    offset += sizeof(struct objc_property);
	}
}

static
void
print_category_t(
uint64_t p,
struct info *info)
{
    struct category_t c;
    void *r;
    uint32_t offset, left;
    struct section_info_64 *s;
    const char *name, *sym_name;
    uint64_t n_value;
    int64_t addend;

	r = get_pointer_64(p, &offset, &left, &s,
			   info->sections, info->nsections);
	if(r == NULL)
	    return;
	memset(&c, '\0', sizeof(struct category_t));
	if(left < sizeof(struct category_t)){
	    memcpy(&c, r, left);
	    printf("   (category_t entends past the end of the section)\n");
	}
	else
	    memcpy(&c, r, sizeof(struct category_t));
	if(info->swapped)
	    swap_category_t(&c, info->host_byte_sex);

	printf("              name ");
	sym_name = get_symbol_64(offset + offsetof(struct category_t, name),
			         s->addr, info->database, c.name, s->relocs,
			         s->nrelocs, info, &n_value, &addend);
	print_pointer(c.name, sym_name, n_value, addend, NULL, NULL, info);
	name = get_pointer_64(n_value + addend, NULL, &left, NULL,
			      info->sections, info->nsections);
	if(name != NULL)
	    printf(" %.*s", (int)left, name);
	printf("\n");

	printf("               cls ");
	sym_name = get_symbol_64(offset + offsetof(struct category_t, cls),
			         s->addr, info->database, c.cls, s->relocs,
			         s->nrelocs, info, &n_value, &addend);
	print_pointer(c.cls, sym_name, n_value, addend, NULL, "\n", info);
	if(n_value + addend != 0)
	    print_class_t(n_value + addend, info);

	printf("   instanceMethods ");
	sym_name = get_symbol_64(offset + offsetof(struct category_t,
						   instanceMethods),
			     s->addr, info->database, c.instanceMethods,
			     s->relocs, s->nrelocs, info, &n_value, &addend);
	print_pointer(c.instanceMethods, sym_name, n_value, addend, NULL, "\n",
		      info);
	if(n_value + addend != 0)
	    print_method_list_t(n_value + addend, info, "");

	printf("      classMethods ");
	sym_name = get_symbol_64(offset + offsetof(struct category_t,
						   classMethods),
			     s->addr, info->database, c.classMethods,
			     s->relocs, s->nrelocs, info, &n_value, &addend);
	print_pointer(c.classMethods, sym_name, n_value, addend, NULL, "\n",
		      info);
	if(n_value + addend != 0)
	    print_method_list_t(n_value + addend, info, "");

	printf("         protocols ");
	sym_name = get_symbol_64(offset + offsetof(struct category_t,
						   protocols),
			     s->addr, info->database, c.protocols,
			     s->relocs, s->nrelocs, info, &n_value, &addend);
	print_pointer(c.protocols, sym_name, n_value, addend, NULL, "\n", info);
	if(n_value + addend != 0)
	    print_protocol_list_t(n_value + addend, info);

	printf("instanceProperties ");
	sym_name = get_symbol_64(offset + offsetof(struct category_t,
						   instanceProperties),
			     s->addr, info->database, c.instanceProperties,
			     s->relocs, s->nrelocs, info, &n_value, &addend);
	print_pointer(c.instanceProperties, sym_name, n_value, addend, NULL,
		      "\n", info);
	if(n_value + addend)
	    print_objc_property_list(n_value + addend, info);
}

static
void
print_message_refs(
struct section_info_64 *s,
struct info *info)
{
    uint32_t i, left, offset;
    uint64_t p, n_value;
    int64_t addend;
    struct message_ref mr;
    const char *name, *sym_name;
    void *r;

	if(s == NULL)
	    return;

	printf("Contents of (%.16s,%.16s) section\n", s->segname, s->sectname);
	offset = 0;
	for(i = 0; i < s->size; i += sizeof(struct message_ref)){
	    p = s->addr + i;
	    r = get_pointer_64(p, &offset, &left, &s,
			       info->sections, info->nsections);
	    if(r == NULL)
		return;
	    memset(&mr, '\0', sizeof(struct message_ref));
	    if(left < sizeof(struct message_ref)){
		memcpy(&mr, r, left);
		printf(" (message_ref entends past the end of the section)\n");
	    }
	    else
		memcpy(&mr, r, sizeof(struct message_ref));
	    if(info->swapped)
		swap_message_ref(&mr, info->host_byte_sex);

	    printf("  imp ");
	    name = get_symbol_64(offset + offsetof(struct message_ref, imp),
				 s->addr, info->database, mr.imp, s->relocs,
				 s->nrelocs, info, &n_value, &addend);
	    print_pointer(mr.imp, sym_name, n_value, addend, NULL, "\n", info);

	    printf("  sel ");
	    sym_name = get_symbol_64(offset + offsetof(struct message_ref, sel),
				     s->addr, info->database, mr.sel, s->relocs,
				     s->nrelocs, info, &n_value, &addend);
	    print_pointer(mr.sel, sym_name, n_value, addend, NULL, NULL, info);
	    name = get_pointer_64(n_value + addend, NULL, &left, NULL,
				  info->sections, info->nsections);
	    if(name != NULL)
		printf(" %.*s", (int)left, name);
	    printf("\n");

	    offset += sizeof(struct message_ref);
	}
}

static
void
print_image_info(
struct section_info_64 *s,
struct info *info)
{
    uint32_t left, offset, swift_version;
    uint64_t p;
    struct objc_image_info o;
    void *r;

	if(s == NULL)
	    return;

	printf("Contents of (%.16s,%.16s) section\n", s->segname, s->sectname);
	p = s->addr;
	r = get_pointer_64(p, &offset, &left, &s,
			   info->sections, info->nsections);
	if(r == NULL)
	    return;
	memset(&o, '\0', sizeof(struct objc_image_info));
	if(left < sizeof(struct objc_image_info)){
	    memcpy(&o, r, left);
	    printf(" (objc_image_info entends past the end of the section)\n");
	}
	else
	    memcpy(&o, r, sizeof(struct objc_image_info));
	if(info->swapped)
	    swap_objc_image_info(&o, info->host_byte_sex);
	printf("  version %u\n", o.version);
	printf("    flags 0x%x", o.flags);
	if(o.flags & OBJC_IMAGE_IS_REPLACEMENT)
	    printf(" OBJC_IMAGE_IS_REPLACEMENT");
	if(o.flags & OBJC_IMAGE_SUPPORTS_GC)
	    printf(" OBJC_IMAGE_SUPPORTS_GC");
	swift_version = (o.flags >> 8) & 0xff;
	if(swift_version != 0){
	    if(swift_version == 1)
		printf(" Swift 1.0");
	    else if(swift_version == 2)
		printf(" Swift 1.1");
	    else if(swift_version == 3)
		printf(" Swift 2.0");
	    else if(swift_version == 4)
		printf(" Swift 3.0");
	    else if(swift_version == 5)
		printf(" Swift 4.0");
	    else if(swift_version == 6)
	      printf(" Swift 4.1/4.2");
	    else if(swift_version == 7)
	      printf(" Swift 5 or later");
	    else
		printf(" unknown future Swift version (%d)", swift_version);
	}
	printf("\n");
}

void
print_objc_string_object_section_64(
char *sectname,
struct load_command *load_commands,
uint32_t ncmds,
uint32_t sizeofcmds,
enum byte_sex object_byte_sex,
char *object_addr,
uint32_t object_size,
cpu_type_t cputype,
struct nlist_64 *symbols64,
uint32_t nsymbols,
char *strings,
const uint32_t strings_size,
struct symbol *sorted_symbols,
uint32_t nsorted_symbols,
enum bool verbose)
{
    struct info info;
    struct section_info_64 *o, cstring_section;
    struct objc_string_object_64 *string_objects, *s, string_object;
    uint64_t string_objects_addr, string_objects_size;
    uint32_t size, left;
    char *p;
    const char *name;

	printf("Contents of (" SEG_OBJC ",%s) section\n", sectname);
	info.object_addr = object_addr;
	info.object_size = object_size;
	info.host_byte_sex = get_host_byte_sex();
	info.swapped = info.host_byte_sex != object_byte_sex;
	info.cputype = cputype;
	info.symbols64 = symbols64;
	info.nsymbols = nsymbols;
	info.strings = strings;
	info.strings_size = strings_size;
	info.sorted_symbols = sorted_symbols;
	info.nsorted_symbols = nsorted_symbols;
	info.verbose = verbose;
	get_sections_64(load_commands, ncmds, sizeofcmds, object_byte_sex,
			object_addr, object_size, &info.sections,
			&info.nsections, &info.database);
	o = get_section_64(info.sections, info.nsections, SEG_OBJC, sectname);
	if(o == NULL)
	    return;
	get_cstring_section_64(load_commands, ncmds, sizeofcmds,object_byte_sex,
			       object_addr, object_size, &cstring_section);

	string_objects = (struct objc_string_object_64 *)o->contents;
	string_objects_addr = o->addr;
	string_objects_size = o->size;
	for(s = string_objects;
	    (char *)s < (char *)string_objects + string_objects_size;
	    s++){

	    memset(&string_object, '\0', sizeof(struct objc_string_object_64));
	    left = string_objects_size - (s - string_objects); 
	    size = left < sizeof(struct objc_string_object_64) ?
		   left : sizeof(struct objc_string_object_64);
	    memcpy(&string_object, s, size);

	    if((char *)s + sizeof(struct objc_string_object_64) >
	       (char *)s + string_objects_size)
		printf("String Object extends past end of %s section\n",
		       sectname);
	    printf("String Object 0x%llx\n",
		   string_objects_addr + ((char *)s - (char *)string_objects));

	    if(info.swapped)
		swap_string_object_64(&string_object, info.host_byte_sex);
	    printf("           isa 0x%llx", string_object.isa);
	    name = get_symbol_64((uintptr_t)s - (uintptr_t)string_objects,
				 o->addr, info.database, string_object.isa,
				 o->relocs, o->nrelocs, &info, NULL, NULL);
	    if(name != NULL)
		printf(" %s\n", name);
	    else
		printf("\n");
	    printf("    characters 0x%llx", string_object.characters);
	    if(verbose){
		p = get_pointer_64(string_object.characters, NULL, &left,
				   NULL, info.sections, info.nsections);
		if(p != NULL)
		    printf(" %.*s\n", (int)left, p);
	    }
	    else
		printf("\n");
	    printf("       _length %u\n", string_object._length);
	    printf("          _pad %u\n", string_object._pad);
	}
}

static
void
get_sections_64(
struct load_command *load_commands,
uint32_t ncmds,
uint32_t sizeofcmds,
enum byte_sex object_byte_sex,
char *object_addr,
uint32_t object_size,
struct section_info_64 **sections,
uint32_t *nsections,
uint64_t *database) 
{
    enum byte_sex host_byte_sex;
    enum bool swapped, database_set, zerobased, encrypt_found, encrypt64_found;

    uint32_t i, j, left, size;
    struct load_command lcmd, *lc;
    char *p;
    struct segment_command_64 sg64;
    struct section_64 s64;
    struct encryption_info_command encrypt;
    struct encryption_info_command_64 encrypt64;

	host_byte_sex = get_host_byte_sex();
	swapped = host_byte_sex != object_byte_sex;

	*sections = NULL;
	*nsections = 0;
	database_set = FALSE;
	*database = 0;
	zerobased = FALSE;
	encrypt_found = FALSE;
	encrypt64_found = FALSE;

	lc = load_commands;
	for(i = 0 ; i < ncmds; i++){
	    memcpy((char *)&lcmd, (char *)lc, sizeof(struct load_command));
	    if(swapped)
		swap_load_command(&lcmd, host_byte_sex);
	    if(lcmd.cmdsize % sizeof(int32_t) != 0)
		printf("load command %u size not a multiple of "
		       "sizeof(int32_t)\n", i);
	    if((char *)lc + lcmd.cmdsize >
	       (char *)load_commands + sizeofcmds)
		printf("load command %u extends past end of load "
		       "commands\n", i);
	    left = sizeofcmds - ((char *)lc - (char *)load_commands);

	    switch(lcmd.cmd){
	    case LC_SEGMENT_64:
		memset((char *)&sg64, '\0', sizeof(struct segment_command_64));
		size = left < sizeof(struct segment_command_64) ?
		       left : sizeof(struct segment_command_64);
		memcpy((char *)&sg64, (char *)lc, size);
		if(swapped)
		    swap_segment_command_64(&sg64, host_byte_sex);
		if((sg64.initprot & VM_PROT_WRITE) == VM_PROT_WRITE &&
		   database_set == FALSE){
		    *database = sg64.vmaddr;
		    database_set = TRUE;
		}
		if((sg64.initprot & VM_PROT_READ) == VM_PROT_READ &&
		   sg64.vmaddr == 0)
		    zerobased = TRUE;
		p = (char *)lc + sizeof(struct segment_command_64);
		for(j = 0 ; j < sg64.nsects ; j++){
		    if(p + sizeof(struct section_64) >
		       (char *)load_commands + sizeofcmds){
			printf("section structure command extends past "
			       "end of load commands\n");
		    }
		    left = sizeofcmds - (p - (char *)load_commands);
		    memset((char *)&s64, '\0', sizeof(struct section_64));
		    size = left < sizeof(struct section_64) ?
			   left : sizeof(struct section_64);
		    memcpy((char *)&s64, p, size);
		    if(swapped)
			swap_section_64(&s64, 1, host_byte_sex);

		    *sections = reallocate(*sections,
		       sizeof(struct section_info_64) * (*nsections + 1));
		    memcpy((*sections)[*nsections].segname,
			   s64.segname, 16);
		    memcpy((*sections)[*nsections].sectname,
			   s64.sectname, 16);
		    (*sections)[*nsections].addr = s64.addr;
		    (*sections)[*nsections].contents = object_addr + s64.offset;
		    (*sections)[*nsections].offset = s64.offset;
		    (*sections)[*nsections].zerofill =
			(s64.flags & SECTION_TYPE) == S_ZEROFILL ? TRUE : FALSE;
		    if(s64.offset > object_size){
			printf("section contents of: (%.16s,%.16s) is past "
			       "end of file\n", s64.segname, s64.sectname);
			(*sections)[*nsections].size =  0;
		    }
		    else if(s64.offset + s64.size > object_size){
			printf("part of section contents of: (%.16s,%.16s) "
			       "is past end of file\n",
			       s64.segname, s64.sectname);
			(*sections)[*nsections].size = object_size - s64.offset;
		    }
		    else
			(*sections)[*nsections].size = s64.size;
		    if(s64.reloff >= object_size){
			printf("relocation entries offset for (%.16s,%.16s)"
			       ": is past end of file\n", s64.segname,
			       s64.sectname);
			(*sections)[*nsections].nrelocs = 0;
		    }
		    else{
			(*sections)[*nsections].relocs =
			    (struct relocation_info *)(object_addr +
						       s64.reloff);
			if(s64.reloff +
			   s64.nreloc * sizeof(struct relocation_info) >
							    object_size){
			    printf("relocation entries for section (%.16s,"
				   "%.16s) extends past end of file\n",
				   s64.segname, s64.sectname);
			    (*sections)[*nsections].nrelocs =
				(object_size - s64.reloff) /
					    sizeof(struct relocation_info);
			}
			else
			    (*sections)[*nsections].nrelocs = s64.nreloc;
			if(swapped)
			    swap_relocation_info(
				(*sections)[*nsections].relocs,
				(*sections)[*nsections].nrelocs,
				host_byte_sex);
		    }
		    if(sg64.flags & SG_PROTECTED_VERSION_1)
			(*sections)[*nsections].protected = TRUE;
		    else
			(*sections)[*nsections].protected = FALSE;
		    if((s64.flags & SECTION_TYPE) == S_CSTRING_LITERALS)
			(*sections)[*nsections].cstring = TRUE;
		    else
			(*sections)[*nsections].cstring = FALSE;
		    (*nsections)++;

		    if(p + sizeof(struct section_64) >
		       (char *)load_commands + sizeofcmds)
			break;
		    p += size;
		}
		break;
	    case LC_ENCRYPTION_INFO:
		memset((char *)&encrypt, '\0',
		       sizeof(struct encryption_info_command));
		size = left < sizeof(struct encryption_info_command) ?
		       left : sizeof(struct encryption_info_command);
		memcpy((char *)&encrypt, (char *)lc, size);
		if(swapped)
		    swap_encryption_command(&encrypt, host_byte_sex);
		encrypt_found = TRUE;
		break;
	    case LC_ENCRYPTION_INFO_64:
		memset((char *)&encrypt64, '\0',
		       sizeof(struct encryption_info_command_64));
		size = left < sizeof(struct encryption_info_command_64) ?
		       left : sizeof(struct encryption_info_command_64);
		memcpy((char *)&encrypt64, (char *)lc, size);
		if(swapped)
		    swap_encryption_command_64(&encrypt64, host_byte_sex);
		encrypt64_found = TRUE;
		break;
	    }
	    if(lcmd.cmdsize == 0){
		printf("load command %u size zero (can't advance to other "
		       "load commands)\n", i);
		break;
	    }
	    lc = (struct load_command *)((char *)lc + lcmd.cmdsize);
	    if((char *)lc > (char *)load_commands + sizeofcmds)
		break;
	}

	if(encrypt_found == TRUE && encrypt.cryptid != 0){
	    for(i = 0; i < *nsections; i++){
		if((*sections)[i].size > 0 && (*sections)[i].zerofill == FALSE){
		    if((*sections)[i].offset >
		       encrypt.cryptoff + encrypt.cryptsize){
			/* section starts past encryption area */ ;
		    }
		    else if((*sections)[i].offset + (*sections)[i].size <
			encrypt.cryptoff){
			/* section ends before encryption area */ ;
		    }
		    else{
			/* section has part in the encrypted area */
			(*sections)[i].protected = TRUE;
		    }
		}
	    }
	}
	if(encrypt64_found == TRUE && encrypt64.cryptid != 0){
	    for(i = 0; i < *nsections; i++){
		if((*sections)[i].size > 0 && (*sections)[i].zerofill == FALSE){
		    if((*sections)[i].offset >
		       encrypt64.cryptoff + encrypt64.cryptsize){
			/* section starts past encryption area */ ;
		    }
		    else if((*sections)[i].offset + (*sections)[i].size <
			encrypt64.cryptoff){
			/* section ends before encryption area */ ;
		    }
		    else{
			/* section has part in the encrypted area */
			(*sections)[i].protected = TRUE;
		    }
		}
	    }
	}
}

static
struct section_info_64 *
get_section_64(
struct section_info_64 *sections,
uint32_t nsections,
char *segname,
char *sectname)
{
    uint32_t i;

	for(i = 0; i < nsections; i++){
	    if(strncmp(sections[i].segname, segname, 16) == 0 &&
	       strncmp(sections[i].sectname, sectname, 16) == 0){
		return(sections + i);
	    }
	}
	return(NULL);
}

static
void
get_cstring_section_64(
struct load_command *load_commands,
uint32_t ncmds,
uint32_t sizeofcmds,
enum byte_sex object_byte_sex,
char *object_addr,
uint32_t object_size,
struct section_info_64 *cstring_section)
{
    enum byte_sex host_byte_sex;
    enum bool swapped;

    uint32_t i, j, left, size;
    struct load_command lcmd, *lc;
    char *p;
    struct segment_command_64 sg64;
    struct section_64 s64;

	host_byte_sex = get_host_byte_sex();
	swapped = host_byte_sex != object_byte_sex;

	memset(cstring_section, '\0', sizeof(struct section_info_64));

	lc = load_commands;
	for(i = 0 ; i < ncmds; i++){
	    memcpy((char *)&lcmd, (char *)lc, sizeof(struct load_command));
	    if(swapped)
		swap_load_command(&lcmd, host_byte_sex);
	    if(lcmd.cmdsize % sizeof(int32_t) != 0)
		printf("load command %u size not a multiple of "
		       "sizeof(int32_t)\n", i);
	    if((char *)lc + lcmd.cmdsize >
	       (char *)load_commands + sizeofcmds)
		printf("load command %u extends past end of load "
		       "commands\n", i);
	    left = sizeofcmds - ((char *)lc - (char *)load_commands);

	    switch(lcmd.cmd){
	    case LC_SEGMENT_64:
		memset((char *)&sg64, '\0', sizeof(struct segment_command_64));
		size = left < sizeof(struct segment_command_64) ?
		       left : sizeof(struct segment_command_64);
		memcpy((char *)&sg64, (char *)lc, size);
		if(swapped)
		    swap_segment_command_64(&sg64, host_byte_sex);

		p = (char *)lc + sizeof(struct segment_command_64);
		for(j = 0 ; j < sg64.nsects ; j++){
		    if(p + sizeof(struct section_64) >
		       (char *)load_commands + sizeofcmds){
			printf("section structure command extends past "
			       "end of load commands\n");
		    }
		    left = sizeofcmds - (p - (char *)load_commands);
		    memset((char *)&s64, '\0', sizeof(struct section_64));
		    size = left < sizeof(struct section_64) ?
			   left : sizeof(struct section_64);
		    memcpy((char *)&s64, p, size);
		    if(swapped)
			swap_section_64(&s64, 1, host_byte_sex);

		    if(strcmp(s64.segname, SEG_TEXT) == 0 &&
		       strcmp(s64.sectname, "__cstring") == 0){
			cstring_section->addr = s64.addr;
			cstring_section->contents = object_addr + s64.offset;
			if(s64.offset > object_size){
			    printf("section contents of: (%.16s,%.16s) is past "
				   "end of file\n", s64.segname, s64.sectname);
			    cstring_section->size = 0;
			}
			else if(s64.offset + s64.size > object_size){
			    printf("part of section contents of: (%.16s,%.16s) "
				   "is past end of file\n",
				   s64.segname, s64.sectname);
			    cstring_section->size = object_size - s64.offset;
			}
			else
			    cstring_section->size = s64.size;
			if(sg64.flags & SG_PROTECTED_VERSION_1)
			    cstring_section->protected = TRUE;
			else
			    cstring_section->protected = FALSE;
			cstring_section->cstring = TRUE;
			return;
		    }

		    if(p + sizeof(struct section) >
		       (char *)load_commands + sizeofcmds)
			break;
		    p += size;
		}
		break;
	    }
	    if(lcmd.cmdsize == 0){
		printf("load command %u size zero (can't advance to other "
		       "load commands)\n", i);
		break;
	    }
	    lc = (struct load_command *)((char *)lc + lcmd.cmdsize);
	    if((char *)lc > (char *)load_commands + sizeofcmds)
		break;
	}
}

static
void *
get_pointer_64(
uint64_t p,
uint32_t *offset,
uint32_t *left,
struct section_info_64 **s,
struct section_info_64 *sections,
uint32_t nsections)
{
    void *r;
    uint64_t addr;
    uint32_t i;

	addr = p;
	for(i = 0; i < nsections; i++){
	    if(addr >= sections[i].addr &&
	       addr < sections[i].addr + sections[i].size){
		if(s != NULL)
		    *s = sections + i;
		if(offset != NULL)
		    *offset = addr - sections[i].addr;
		if(left != NULL)
		    *left = sections[i].size - (addr - sections[i].addr);
		if(sections[i].protected == TRUE && sections[i].cstring == TRUE)
		    r = "some string from a protected section";
		else
		    r = sections[i].contents + (addr - sections[i].addr);
		return(r);
	    }
	}
	if(s != NULL)
	    *s = NULL;
	if(offset != NULL)
	    *offset = 0;
	if(left != NULL)
	    *left = 0;
	return(NULL);
}

/*
 * get_symbol_64() returns the name of a symbol (or NULL). Based on the
 * relocation information at the specified section offset, address and database
 * or the (pointer) value.  It indirectly returns the symbol's value through
 * *n_value and the relocation's addend through *addend.  Since the later values
 * are needed to walk the pointers it is up to the caller to check the
 * info->verbose flag to print the name or the *n_value + *addend or raw
 * pointer value.
 */
static
const char *
get_symbol_64(
uint32_t sect_offset,
uint64_t sect_addr,
uint64_t database,
uint64_t value,
struct relocation_info *relocs,
uint32_t nrelocs,
struct info *info,
uint64_t *n_value,
int64_t *addend)
{
    uint32_t i;
    unsigned int r_symbolnum;
    uint32_t n_strx;
    const char *name;

	if(n_value != NULL)
	    *n_value = 0;
	if(addend != NULL)
	    *addend = value;

	/*
	 * In the info->verbose == FALSE case we can't simply return now as for
	 * the ThreadedRebaseBind case we need to return the real pointer value
	 * in "n_value + addend" without the bits from the ThreadedRebaseBind.
	 * To do this we need look through the bind entries or in the rebase
	 * case move the original pointer value masked with the right bits off
	 * into n_value and zero out the addend so the caller can get the real
	 * pointer value from n_value + addend and indirect through that.
	 * The caller now has to check info->verbose == FALSE to print the
	 * original pointer but use the n_value + addend to follow the pointer.
	 */

	/*
	 * First look in section's relocation entries if it has them which is
	 * the .o file case to find the name, n_value and added.
	 */
	for(i = 0; i < nrelocs; i++){
	    if((uint32_t)relocs[i].r_address == sect_offset){
		r_symbolnum = relocs[i].r_symbolnum;
		if(relocs[i].r_extern){
		    if(r_symbolnum >= info->nsymbols)
			break;
		    n_strx = info->symbols64[r_symbolnum].n_un.n_strx;
		    if(n_strx <= 0 || n_strx >= info->strings_size)
			break;
		    /*
		     * If this is arm64e and if r_type is a
		     * ARM64_RELOC_AUTHENTICATED_POINTER we need to adjust
		     * addend to just the low 32-bits (signed) of the pointer
		     * value.
		     */
		    if(info->cputype == CPU_TYPE_ARM64 &&
		       info->cpusubtype == CPU_SUBTYPE_ARM64E &&
		       relocs[i].r_type == ARM64_RELOC_AUTHENTICATED_POINTER){
			if(addend != NULL){
			    *addend = 0xffffffffULL & value;
			    if((*addend & 0x80000000ULL) != 0)
				*addend |= 0xffffffff00000000ULL;
			}
		    }
		    if(n_value != NULL)
			*n_value = info->symbols64[r_symbolnum].n_value;
		    return(info->strings + n_strx);
		}
		break;
	    }
	    if(reloc_has_pair(info->cputype, relocs[i].r_type) == TRUE)
		i++;
	}

	/*
	 * Next look in external relocation entries of if it has them which is
	 * the original dyld image case to find the name, n_value and added.
	 */
	for(i = 0; i < info->next_relocs; i++){
	    if((uint32_t)info->ext_relocs[i].r_address ==
		database + sect_offset){
		r_symbolnum = info->ext_relocs[i].r_symbolnum;
		if(info->ext_relocs[i].r_extern){
		    if(r_symbolnum >= info->nsymbols)
			break;
		    n_strx = info->symbols64[r_symbolnum].n_un.n_strx;
		    if(n_strx <= 0 || n_strx >= info->strings_size)
			break;
		    if(n_value != NULL)
			*n_value = info->symbols64[r_symbolnum].n_value;
		    return(info->strings + n_strx);
		}
		break;
	    }
	    if(reloc_has_pair(info->cputype, info->ext_relocs[i].r_type) ==TRUE)
		i++;
	}

	/*
	 * Lastly look in the dyld bind entries if it has them which is
	 * the modern fully linked dyld image case to find the name and added.
	 */
	name = get_dyld_bind_info_symbolname(sect_addr + sect_offset,
					     info->dbi, info->ndbi,
					     info->ThreadedRebaseBind, addend);
        /*
	 * If we find a bind entry we return the name which may not be printed
	 * if not in verbose mode.  But we needed to make the call above to
         * get the correct addend if info->ThreadedRebaseBind was true.
	 */
	if(name != NULL)
	    return(name);

	/*
	 * Fully linked modern images for dyld get will get here if it is has
	 * a rebase entry, and the pointer value in "value" would be what this
	 * pointer is pointing to in this image normally.
	 *
	 * But if info->ThreadedRebaseBind is true, to get the correct pointer
	 * value we need to know to mask off the upper bits and only keep the
	 * low 51-bits.
	 */
	/*
	 * Unless this is arm64e we have to look for the high authenticated bit
	 * to know to use only the low 32-bits as the pointer value.
	 */
	/* So at this point, we set n_value as the masked pointer value
         * and zero as the addend for return or the value to call guess_symbol()
	 * with for a guess at which symbol has this address.
	 */
	if(info->ThreadedRebaseBind){
	    if(info->cputype == CPU_TYPE_ARM64 &&
	       info->cpusubtype == CPU_SUBTYPE_ARM64E &&
	       (value & 0x8000000000000000ULL) != 0)
		value = value & 0xffffffffULL;
	    else
		value = value & 0x7ffffffffffffULL;
	    if(n_value != NULL)
		*n_value = value;
	    if(addend != NULL)
		*addend = 0;
	}

	/*
	 * We don't guess for symbol values of zero as it is wrong most of the
	 * time.
	 */
	if(value == 0)
	    return(NULL);

	return(guess_symbol(value, info->sorted_symbols, info->nsorted_symbols,
			    info->verbose));
}

/*
 * This prints only the raw pointer value if info->verbose is FALSE.  Else it
 * will print some combination of the sym_name or "n_value + addend" and the
 * name if not NULL if it has those or the raw pointer value and the name.
 */
static
void
print_pointer(
uint64_t pointer,
const char *sym_name,
uint64_t n_value,
int64_t addend,
const char *name,
char *newline,
struct info *info)
{
    enum bool auth_zero;

	auth_zero = FALSE;
	/*
	 * This is the case when the pointer is really zero but has the
         * authenticated bit set.
	 */ 
	if(info->cputype == CPU_TYPE_ARM64 &&
	   info->cpusubtype == CPU_SUBTYPE_ARM64E &&
           (pointer & 0x8000000000000000ULL) != 0 &&
           (pointer & 0xffffffffULL) == 0)
	    auth_zero = TRUE;

	if(info->verbose && (n_value != 0 ||
           auth_zero ||
           info->ThreadedRebaseBind)){
	    if(info->Vflag && sym_name != NULL)
		printf("%s", sym_name);
	    else{
		printf("0x%llx", n_value);
		if(addend != 0)
		    printf(" + 0x%llx", addend);
	    }
	}
	else
	    printf("0x%llx", pointer);
	if(info->verbose && name != NULL)
	    printf(" %s", name);
	if(newline != NULL)
	    printf("%s", newline);
}
