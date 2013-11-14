/* -*- mode: C++; c-basic-offset: 4; tab-width: 4 -*-
 *
 * Copyright (c) 2006-2009 Apple Inc. All rights reserved.
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

#ifndef __LTO_READER_H__
#define __LTO_READER_H__

#include <stdlib.h>
#include <mach-o/dyld.h>
#include <vector>
#include <ext/hash_set>
#include <ext/hash_map>

#include "MachOFileAbstraction.hpp"
#include "Architectures.hpp"
#include "ld.hpp"

#include "llvm-c/lto.h"


namespace lto {
	  

//
// ld64 only tracks non-internal symbols from an llvm bitcode file.  
// We model this by having an InternalAtom which represent all internal functions and data.
// All non-interal symbols from a bitcode file are represented by an Atom
// and each Atom has a reference to the InternalAtom.  The InternalAtom
// also has references to each symbol external to the bitcode file. 
//
class InternalAtom : public ld::Atom
{
public:
												InternalAtom(class File& f);
	// overrides of ld::Atom
	virtual ld::File*							file() const		{ return &_file; }
	virtual bool								translationUnitSource(const char** dir, const char** nm) const
																	{ return false; }
	virtual const char*							name() const		{ return "import-atom"; }
	virtual uint64_t							size() const		{ return 0; }
	virtual uint64_t							objectAddress() const { return 0; }
	virtual void								copyRawContent(uint8_t buffer[]) const { }
	virtual void								setScope(Scope)		{ }
	virtual ld::Fixup::iterator					fixupsBegin()		{ return &_undefs[0]; }
	virtual ld::Fixup::iterator					fixupsEnd()			{ return &_undefs[_undefs.size()]; }

	// for adding references to symbols outside bitcode file
	void										addReference(const char* name)
																	{ _undefs.push_back(ld::Fixup(0, ld::Fixup::k1of1, 
																				ld::Fixup::fixupNone, false, name)); }
private:

	ld::File&									_file;
	std::vector<ld::Fixup>						_undefs;
};


//
// LLVM bitcode file 
//
class File : public ld::relocatable::File
{
public:
											File(const char* path, time_t mTime, const uint8_t* content, 
													uint32_t contentLength, uint32_t ordinal, cpu_type_t arch);
	virtual									~File();

	// overrides of ld::File
	virtual bool										forEachAtom(ld::File::AtomHandler&);
	virtual bool										justInTimeforEachAtom(const char* name, ld::File::AtomHandler&) 
																					{ return false; }
	
	// overrides of ld::relocatable::File 
	virtual bool										objcReplacementClasses()	{ return false; }
	virtual DebugInfoKind								debugInfo()					{ return ld::relocatable::File::kDebugInfoNone; }
	virtual std::vector<ld::relocatable::File::Stab>*	stabs()						{ return NULL; }
	virtual bool										canScatterAtoms()			{ return true; }

	lto_module_t										module()					{ return _module; }
	class InternalAtom&									internalAtom()				{ return _internalAtom; }
private:
	friend class Atom;
	friend class InternalAtom;
	
	cpu_type_t								_architecture;
	class InternalAtom						_internalAtom;
	class Atom*								_atomArray;
	uint32_t								_atomArrayCount;
	lto_module_t							_module;
	ld::Section								_section;
};

//
// Atom acts as a proxy Atom for the symbols that are exported by LLVM bitcode file. Initially,
// Reader creates Atoms to allow linker proceed with usual symbol resolution phase. After
// optimization is performed, real Atoms are created for these symobls. However these real Atoms
// are not inserted into global symbol table. Atom holds real Atom and forwards appropriate
// methods to real atom.
//
class Atom : public ld::Atom
{
public:
										Atom(File& f, const char* name, ld::Atom::Scope s, 
													ld::Atom::Definition d, ld::Atom::Alignment a);

	// overrides of ld::Atom
	virtual ld::File*					file() const		{ return &_file; }
	virtual bool						translationUnitSource(const char** dir, const char** nm) const
															{ return (_compiledAtom ? _compiledAtom->translationUnitSource(dir, nm) : false); }
	virtual const char*					name() const		{ return _name; }
	virtual uint64_t					size() const		{ return (_compiledAtom ? _compiledAtom->size() : 0); }
	virtual uint64_t					objectAddress() const { return (_compiledAtom ? _compiledAtom->objectAddress() : 0); }
	virtual void						copyRawContent(uint8_t buffer[]) const 
															{ if (_compiledAtom) _compiledAtom->copyRawContent(buffer); }
															
	ld::Atom*							compiledAtom()		{ return _compiledAtom; }
	void								setCompiledAtom(ld::Atom& atom) 
															{ _compiledAtom = &atom; }
private:

	File&								_file;
	const char*							_name;
	ld::Atom*							_compiledAtom;
};

											





class Parser 
{
public:
	static bool						validFile(const uint8_t* fileContent, uint64_t fileLength, cpu_type_t architecture);
	static const char*				fileKind(const uint8_t* fileContent);
	static File*					parse(const uint8_t* fileContent, uint64_t fileLength, const char* path, 
											time_t modTime, uint32_t ordinal, cpu_type_t architecture);
	static bool						libLTOisLoaded() { return (::lto_get_version() != NULL); }
	static bool						optimize(const std::vector<ld::Atom*>& allAtoms, std::vector<ld::Atom*>& newAtoms, 
												std::vector<const char*>& additionalUndefines, 
												const std::set<ld::Atom*>&,
												std::vector<ld::Atom*>& newDeadAtoms,
												uint32_t nextInputOrdinal, 
												ld::OutFile* writer, ld::Atom* entryPointAtom,
												const std::vector<const char*>& llvmOptions,
												bool allGlobalsAReDeadStripRoots,
												bool verbose, bool saveTemps, 
												const char* outputFilePath,
												bool pie,  bool mainExecutable, bool staticExecutable, bool relocatable,
												bool allowTextRelocs, cpu_type_t arch);

	static const char*			ltoVersion()	{ return ::lto_get_version(); }

private:
	static const char*				tripletPrefixForArch(cpu_type_t arch);
	static ld::relocatable::File*	parseMachOFile(const uint8_t* p, size_t len, uint32_t nextInputOrdinal, cpu_type_t arch);

	class CStringEquals
	{
	public:
		bool operator()(const char* left, const char* right) const { return (strcmp(left, right) == 0); }
	};
	typedef	__gnu_cxx::hash_set<const char*, __gnu_cxx::hash<const char*>, CStringEquals>  CStringSet;
	typedef __gnu_cxx::hash_map<const char*, Atom*, __gnu_cxx::hash<const char*>, CStringEquals> CStringToAtom;
	
	class AtomSyncer : public ld::File::AtomHandler {
	public:
							AtomSyncer(std::vector<const char*>& a, std::vector<ld::Atom*>&na,
										CStringToAtom la, CStringToAtom dla) :
										additionalUndefines(a), newAtoms(na), llvmAtoms(la), deadllvmAtoms(dla) { }
		virtual void		doAtom(class ld::Atom&);
		
		std::vector<const char*>&		additionalUndefines;
		std::vector<ld::Atom*>&			newAtoms;
		CStringToAtom					llvmAtoms;
		CStringToAtom					deadllvmAtoms;
	};

	static std::vector<File*>		_s_files;
};

std::vector<File*> Parser::_s_files;


const char* Parser::tripletPrefixForArch(cpu_type_t arch)
{
	switch (arch) {
		case CPU_TYPE_POWERPC:
			return "powerpc-";
		case CPU_TYPE_POWERPC64:
			return "powerpc64-";
		case CPU_TYPE_I386:
			return "i386-";
		case CPU_TYPE_X86_64:
			return "x86_64-";
		case CPU_TYPE_ARM:
			return "arm";
	}
	return "";
}

bool Parser::validFile(const uint8_t* fileContent, uint64_t fileLength, cpu_type_t architecture)
{
	return ::lto_module_is_object_file_in_memory_for_target(fileContent, fileLength, tripletPrefixForArch(architecture));
}

const char* Parser::fileKind(const uint8_t* p)
{
	if ( (p[0] == 0xDE) && (p[1] == 0xC0) && (p[2] == 0x17) && (p[3] == 0x0B) ) {
		uint32_t arch = LittleEndian::get32(*((uint32_t*)(&p[16])));
		switch (arch) {
			case CPU_TYPE_POWERPC:
				return "ppc";
			case CPU_TYPE_I386:
				return "i386";
			case CPU_TYPE_X86_64:
				return "x86_64";
			case CPU_TYPE_ARM:
				return "arm";
		}
		return "unknown bitcode architecture";
	}
	return NULL;
}

File* Parser::parse(const uint8_t* fileContent, uint64_t fileLength, const char* path, time_t modTime, 
																uint32_t ordinal, cpu_type_t architecture) 
{
	File* f = new File(path, modTime, fileContent, fileLength, ordinal, architecture);
	_s_files.push_back(f);
	return f;
}


ld::relocatable::File* Parser::parseMachOFile(const uint8_t* p, size_t len, uint32_t nextInputOrdinal, cpu_type_t arch) 
{
	switch ( arch ) {
		case CPU_TYPE_POWERPC:
			if ( mach_o::relocatable::Parser<ppc>::validFile(p) )
				return mach_o::relocatable::Parser<ppc>::parse(p, len, "/tmp/lto.o", 0, nextInputOrdinal);
			break;
		case CPU_TYPE_POWERPC64:
			if ( mach_o::relocatable::Parser<ppc64>::validFile(p) )
				return mach_o::relocatable::Parser<ppc64>::parse(p, len, "/tmp/lto.o", 0, nextInputOrdinal);
			break;
		case CPU_TYPE_I386:
			if ( mach_o::relocatable::Parser<x86>::validFile(p) )
				return mach_o::relocatable::Parser<x86>::parse(p, len, "/tmp/lto.o", 0, nextInputOrdinal);
			break;
		case CPU_TYPE_X86_64:
			if ( mach_o::relocatable::Parser<x86_64>::validFile(p) )
				return mach_o::relocatable::Parser<x86_64>::parse(p, len, "/tmp/lto.o", 0, nextInputOrdinal);
			break;
		case CPU_TYPE_ARM:
			if ( mach_o::relocatable::Parser<arm>::validFile(p) )
				return mach_o::relocatable::Parser<arm>::parse(p, len, "/tmp/lto.o", 0, nextInputOrdinal);
			break;
	}
	throw "LLVM LTO, file is not of required architecture";
}



File::File(const char* path, time_t mTime, const uint8_t* content, uint32_t contentLength, uint32_t ordinal, cpu_type_t arch) 
	: ld::relocatable::File(path,mTime,ordinal), _architecture(arch), _internalAtom(*this), 
	_atomArray(NULL), _atomArrayCount(0), _module(NULL),
	_section("__TEXT_", "__tmp_lto", ld::Section::typeUnclassified)
{
	// create llvm module
	_module = ::lto_module_create_from_memory(content, contentLength);
    if ( _module == NULL )
		throwf("could not parse object file %s: %s", path, lto_get_error_message());
	
	// create atom for each global symbol in module
	uint32_t count = ::lto_module_get_num_symbols(_module);
	_atomArray = (Atom*)malloc(sizeof(Atom)*count);
	for (uint32_t i=0; i < count; ++i) {
		const char* name = ::lto_module_get_symbol_name(_module, i);
		lto_symbol_attributes attr = lto_module_get_symbol_attribute(_module, i);

		// <rdar://problem/6378110> LTO doesn't like dtrace symbols
		// ignore dtrace static probes for now
		// later when codegen is done and a mach-o file is produces the probes will be processed
		if ( (strncmp(name, "___dtrace_probe$", 16) == 0) || (strncmp(name, "___dtrace_isenabled$", 20) == 0) )
			continue;
				
		ld::Atom::Definition def;
		switch ( attr & LTO_SYMBOL_DEFINITION_MASK ) {
			case LTO_SYMBOL_DEFINITION_REGULAR:
				def = ld::Atom::definitionRegular;
				break;
			case LTO_SYMBOL_DEFINITION_TENTATIVE:
				def = ld::Atom::definitionTentative;
				break;
			case LTO_SYMBOL_DEFINITION_WEAK:
				def = ld::Atom::definitionRegular;
				break;
			case LTO_SYMBOL_DEFINITION_UNDEFINED:
			case LTO_SYMBOL_DEFINITION_WEAKUNDEF:
				def = ld::Atom::definitionProxy;
				break;
			default:
				throwf("unknown definition kind for symbol %s in bitcode file %s", name, path);
		}

		// make LLVM atoms for definitions and a reference for undefines
		if ( def != ld::Atom::definitionProxy ) {
			ld::Atom::Scope scope;
			switch ( attr & LTO_SYMBOL_SCOPE_MASK) {
				case LTO_SYMBOL_SCOPE_INTERNAL:
					scope = ld::Atom::scopeTranslationUnit;
					break;
				case LTO_SYMBOL_SCOPE_HIDDEN:
					scope = ld::Atom::scopeLinkageUnit;
					break;
				case LTO_SYMBOL_SCOPE_DEFAULT:
					scope = ld::Atom::scopeGlobal;
					break;
				default:
					throwf("unknown scope for symbol %s in bitcode file %s", name, path);
			}
			// only make atoms for non-internal symbols 
			if ( scope == ld::Atom::scopeTranslationUnit )
				continue;
			uint8_t alignment = (attr & LTO_SYMBOL_ALIGNMENT_MASK);
			// make Atom using placement new operator
			new (&_atomArray[_atomArrayCount++]) Atom(*this, name, scope, def, alignment);
		}
		else {
			// add to list of external references
			_internalAtom.addReference(name);
		}
	}
}

File::~File()
{
	if ( _module != NULL )
		::lto_module_dispose(_module);
}

bool File::forEachAtom(ld::File::AtomHandler& handler)
{
	handler.doAtom(_internalAtom);
	for(uint32_t i=0; i < _atomArrayCount; ++i) {
		handler.doAtom(_atomArray[i]);
	}
	return true;
}

InternalAtom::InternalAtom(File& f)
	: ld::Atom(f._section, ld::Atom::definitionRegular, ld::Atom::combineNever, ld::Atom::scopeTranslationUnit, 
				ld::Atom::typeLTOtemporary, ld::Atom::symbolTableNotIn, false, false, ld::Atom::Alignment(0)),
		_file(f)
{
}

Atom::Atom(File& f, const char* name, ld::Atom::Scope s, ld::Atom::Definition d, ld::Atom::Alignment a)
	: ld::Atom(f._section, d, ld::Atom::combineNever, s, ld::Atom::typeLTOtemporary, ld::Atom::symbolTableIn, false, false, a),
		_file(f), _name(name), _compiledAtom(NULL)
{
}




bool Parser::optimize(const std::vector<ld::Atom*>& allAtoms, std::vector<ld::Atom*>& newAtoms, 
												std::vector<const char*>& additionalUndefines, 
												const std::set<ld::Atom*>& deadAtoms,
												std::vector<ld::Atom*>& newlyDeadAtoms,
												uint32_t nextInputOrdinal, 
												ld::OutFile* writer, ld::Atom* entryPointAtom,
												const std::vector<const char*>& llvmOptions,
												bool allGlobalsAReDeadStripRoots,
												bool verbose, bool saveTemps, 
												const char* outputFilePath,
												bool pie, bool mainExecutable, bool staticExecutable, bool relocatable,
												bool allowTextRelocs, cpu_type_t arch)
{ 
	// exit quickly if nothing to do
	if ( _s_files.size() == 0 ) 
		return false;
	
	// print out LTO version string if -v was used
	if ( verbose )
		fprintf(stderr, "%s\n", lto_get_version());
	
	// create optimizer and add each Reader
	lto_code_gen_t generator = ::lto_codegen_create();
	for (std::vector<File*>::iterator it=_s_files.begin(); it != _s_files.end(); ++it) {
		if ( ::lto_codegen_add_module(generator, (*it)->module()) )
			throwf("lto: could not merge in %s because %s", (*it)->path(), ::lto_get_error_message());
	}

	// add any -mllvm command line options
	for (std::vector<const char*>::const_iterator it=llvmOptions.begin(); it != llvmOptions.end(); ++it) {
		::lto_codegen_debug_options(generator, *it);
	}

	// The atom graph uses directed edges (references). Collect all references where 
	// originating atom is not part of any LTO Reader. This allows optimizer to optimize an 
	// external (i.e. not originated from same .o file) reference if all originating atoms are also 
	// defined in llvm bitcode file.
	CStringSet nonLLVMRefs;
	CStringToAtom llvmAtoms;
    bool hasNonllvmAtoms = false;
	for (std::vector<ld::Atom*>::const_iterator it = allAtoms.begin(); it != allAtoms.end(); ++it) {
		ld::Atom* atom = *it;
		// only look at references that come from an atom that is not an llvm atom
		if ( atom->contentType() != ld::Atom::typeLTOtemporary ) {
			// remember if we've seen any atoms not from an llvm reader and not from the writer
//			if ( atom->getFile() != writer )
//				hasNonllvmAtoms = true;
			for (ld::Fixup::iterator fit=atom->fixupsBegin(); fit != atom->fixupsEnd(); ++fit) {
				if ( fit->binding != ld::Fixup::bindingByNameBound )
					continue;
				// and reference an llvm atom
				if ( fit->u.target->contentType() == ld::Atom::typeLTOtemporary ) 
					nonLLVMRefs.insert(fit->u.target->name());
			}
		}
		else {
			llvmAtoms[atom->name()] = (Atom*)atom;
		}
	}
	// if entry point is in a llvm bitcode file, it must be preserved by LTO
	if ( entryPointAtom != NULL ) {
		if ( entryPointAtom->contentType() == ld::Atom::typeLTOtemporary ) 
			nonLLVMRefs.insert(entryPointAtom->name());
	}
	
	// deadAtoms are the atoms that the linker coalesced.  For instance weak or tentative definitions
	// overriden by another atom.  If any of these deadAtoms are llvm atoms and they were replaced
	// with a mach-o atom, we need to tell the lto engine to preserve (not optimize away) its dead 
	// atom so that the linker can replace it with the mach-o one later.
	CStringToAtom deadllvmAtoms;
	for (std::set<ld::Atom*>::iterator it = deadAtoms.begin(); it != deadAtoms.end(); ++it) {
		ld::Atom* atom = *it;
		if ( atom->contentType() == ld::Atom::typeLTOtemporary ) {
			const char* name = atom->name();
			::lto_codegen_add_must_preserve_symbol(generator, name);
			deadllvmAtoms[name] = (Atom*)atom;
		}
	}

	
	// tell code generator about symbols that must be preserved
	for (CStringToAtom::iterator it = llvmAtoms.begin(); it != llvmAtoms.end(); ++it) {
		const char* name = it->first;
		Atom* atom = it->second;
		// Include llvm Symbol in export list if it meets one of following two conditions
		// 1 - atom scope is global (and not linkage unit).
		// 2 - included in nonLLVMRefs set.
		// If a symbol is not listed in exportList then LTO is free to optimize it away.
		if ( (atom->scope() == ld::Atom::scopeGlobal) ) 
			::lto_codegen_add_must_preserve_symbol(generator, name);
		else if ( nonLLVMRefs.find(name) != nonLLVMRefs.end() ) 
			::lto_codegen_add_must_preserve_symbol(generator, name);
	}
	
    // special case running ld -r on all bitcode files to produce another bitcode file (instead of mach-o)
    if ( relocatable && !hasNonllvmAtoms ) {
		if ( ! ::lto_codegen_write_merged_modules(generator, outputFilePath) ) {
			// HACK, no good way to tell linker we are all done, so just quit
			exit(0);
		}
		warning("could not produce merged bitcode file");
    }
    
	// set code-gen model
	lto_codegen_model model = LTO_CODEGEN_PIC_MODEL_DYNAMIC;
	if ( mainExecutable ) {
		if ( staticExecutable ) {
			// darwin x86_64 "static" code model is really dynamic code model
			if ( arch == CPU_TYPE_X86_64 )
				model = LTO_CODEGEN_PIC_MODEL_DYNAMIC;
			else
				model = LTO_CODEGEN_PIC_MODEL_STATIC;
		}
		else {
			if ( pie )
				model = LTO_CODEGEN_PIC_MODEL_DYNAMIC;
			else
				model = LTO_CODEGEN_PIC_MODEL_DYNAMIC_NO_PIC;
		}
	}
	else {
		if ( allowTextRelocs )
			model = LTO_CODEGEN_PIC_MODEL_DYNAMIC_NO_PIC;
		else
			model = LTO_CODEGEN_PIC_MODEL_DYNAMIC;
	}
	if ( ::lto_codegen_set_pic_model(generator, model) )
		throwf("could not create set codegen model: %s", lto_get_error_message());

    // if requested, save off merged bitcode file
    if ( saveTemps ) {
        char tempBitcodePath[MAXPATHLEN];
        strcpy(tempBitcodePath, outputFilePath);
        strcat(tempBitcodePath, ".lto.bc");
        ::lto_codegen_write_merged_modules(generator, tempBitcodePath);
    }

#if LTO_API_VERSION >= 3
	// find assembler next to linker
	char path[PATH_MAX];
	uint32_t bufSize = PATH_MAX;
	if ( _NSGetExecutablePath(path, &bufSize) != -1 ) {
		char* lastSlash = strrchr(path, '/');
		if ( lastSlash != NULL ) {
			strcpy(lastSlash+1, "as");
			struct stat statInfo;
			if ( stat(path, &statInfo) == 0 )
				::lto_codegen_set_assembler_path(generator, path);
		}
	}
#endif
	// run code generator
	size_t machOFileLen;
	const uint8_t* machOFile = (uint8_t*)::lto_codegen_compile(generator, &machOFileLen);
	if ( machOFile == NULL ) 
		throwf("could not do LTO codegen: %s", ::lto_get_error_message());
	
    // if requested, save off temp mach-o file
    if ( saveTemps ) {
        char tempMachoPath[MAXPATHLEN];
        strcpy(tempMachoPath, outputFilePath);
        strcat(tempMachoPath, ".lto.o");
        int fd = ::open(tempMachoPath, O_CREAT | O_WRONLY | O_TRUNC, 0666);
		if ( fd != -1) {
			::write(fd, machOFile, machOFileLen);
			::close(fd);
		}
		//	save off merged bitcode file
		char tempOptBitcodePath[MAXPATHLEN];
        strcpy(tempOptBitcodePath, outputFilePath);
        strcat(tempOptBitcodePath, ".lto.opt.bc");
        ::lto_codegen_write_merged_modules(generator, tempOptBitcodePath);
	}

	// parse generated mach-o file into a MachOReader
	ld::File* machoFile = parseMachOFile(machOFile, machOFileLen, nextInputOrdinal, arch);
	
	// sync generated mach-o atoms with existing atoms ld knows about
	AtomSyncer syncer(additionalUndefines,newAtoms,llvmAtoms,deadllvmAtoms);
	machoFile->forEachAtom(syncer);
			
	// Remove InternalAtoms from ld
	for (std::vector<File*>::iterator it=_s_files.begin(); it != _s_files.end(); ++it) {
		newlyDeadAtoms.push_back(&((*it)->internalAtom()));
	}
	// Remove Atoms from ld if code generator optimized them away
	for (CStringToAtom::iterator li = llvmAtoms.begin(), le = llvmAtoms.end(); li != le; ++li) {
		// check if setRealAtom() called on this Atom
		if ( li->second->compiledAtom() == NULL )
			newlyDeadAtoms.push_back(li->second);
	}
	
	return true;
}


void Parser::AtomSyncer::doAtom(ld::Atom& machoAtom)
{
	// update proxy atoms to point to real atoms and find new atoms
	const char* name = machoAtom.name();
	if ( machoAtom.scope() >= ld::Atom::scopeLinkageUnit ) {
		CStringToAtom::iterator pos = llvmAtoms.find(name);
		if ( pos != llvmAtoms.end() ) {
			// turn Atom into a proxy for this mach-o atom
			pos->second->setCompiledAtom(machoAtom);
		}
		else {
			// an atom of this name was not in the allAtoms list the linker gave us
			if ( deadllvmAtoms.find(name) != deadllvmAtoms.end() ) {
				// this corresponding to an atom that the linker coalesced away.  
				// Don't pass it back as a new atom
			}
			else
			{
				// this is something new that lto conjured up, tell ld its new
				newAtoms.push_back(&machoAtom);
			}
		}
	}
	else {
		// ld only knew about non-satic atoms, so this one must be new
		newAtoms.push_back(&machoAtom);
	}
	
	// adjust fixups to go through proxy atoms
	for (ld::Fixup::iterator fit=machoAtom.fixupsBegin(); fit != machoAtom.fixupsEnd(); ++fit) {
		switch ( fit->binding ) {
			case ld::Fixup::bindingNone:
				break;
			case ld::Fixup::bindingByNameUnbound:
				// don't know if this target has been seen by linker before or if it is new
				// be conservitive and tell linker it is new
				additionalUndefines.push_back(fit->u.name);
				break;
			case ld::Fixup::bindingByNameBound:
				break;
			case ld::Fixup::bindingDirectlyBound:
				// If mach-o atom is referencing another mach-o atom then 
				// reference is not going through Atom proxy. Fix it here to ensure that all
				// llvm symbol references always go through Atom proxy.
				break;
			case ld::Fixup::bindingByContentBound:
				break;
		}
	}

}
	


}; // namespace lto


#endif

