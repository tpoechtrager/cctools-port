/* -*- mode: C++; c-basic-offset: 4; tab-width: 4 -*-
 *
 * Copyright (c) 2006-2010 Apple Inc. All rights reserved.
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

#ifdef LTO_SUPPORT

#ifndef __LTO_READER_H__
#define __LTO_READER_H__

#include <stdlib.h>
#include <pthread.h>
#include <sys/param.h>
#include <sys/fcntl.h>
#include <sys/stat.h>
#include <errno.h>
#include <mach-o/dyld.h>
#include <vector>
#include <ext/hash_set>
#include <ext/hash_map>

#include "MachOFileAbstraction.hpp"
#include "Architectures.hpp"
#include "ld.hpp"
#include "macho_relocatable_file.h"
#include "lto_file.h"

// #defines are a work around for <rdar://problem/8760268>
#define __STDC_LIMIT_MACROS 1
#define __STDC_CONSTANT_MACROS 1
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
	virtual const char*							name() const		{ return "import-atom"; }
	virtual uint64_t							size() const		{ return 0; }
	virtual uint64_t							objectAddress() const { return 0; }
	virtual void								copyRawContent(uint8_t buffer[]) const { }
	virtual void								setScope(Scope)		{ }
	virtual ld::Fixup::iterator					fixupsBegin() const	{ return &_undefs[0]; }
	virtual ld::Fixup::iterator					fixupsEnd()	const 	{ return &_undefs[_undefs.size()]; }

	// for adding references to symbols outside bitcode file
	void										addReference(const char* nm)
																	{ _undefs.push_back(ld::Fixup(0, ld::Fixup::k1of1, 
																				ld::Fixup::kindNone, false, nm)); }
private:

	ld::File&									_file;
	mutable std::vector<ld::Fixup>				_undefs;
};


//
// LLVM bitcode file 
//
class File : public ld::relocatable::File
{
public:
											File(const char* path, time_t mTime, ld::File::Ordinal ordinal, 
													 const uint8_t* content, uint32_t contentLength, cpu_type_t arch);
	virtual									~File();

	// overrides of ld::File
	virtual bool										forEachAtom(ld::File::AtomHandler&) const;
	virtual bool										justInTimeforEachAtom(const char* name, ld::File::AtomHandler&) const 
																					{ return false; }
	virtual uint32_t									cpuSubType() const			{ return _cpuSubType; }
	
	// overrides of ld::relocatable::File 
	virtual DebugInfoKind								debugInfo()	const			{ return _debugInfo; }
	virtual const char*									debugInfoPath() const		{ return _debugInfoPath; }
	virtual time_t										debugInfoModificationTime() const 
																					{ return _debugInfoModTime; }
	virtual const std::vector<ld::relocatable::File::Stab>*	stabs()	const			{ return NULL; }
	virtual bool										canScatterAtoms() const		{ return true; }

	lto_module_t										module()					{ return _module; }
	class InternalAtom&									internalAtom()				{ return _internalAtom; }
	void												setDebugInfo(ld::relocatable::File::DebugInfoKind k,
																	const char* pth, time_t modTime, uint32_t subtype)
																					{	_debugInfo = k; 
																						_debugInfoPath = pth; 
																						_debugInfoModTime = modTime; 
																						_cpuSubType = subtype;}

private:
	friend class Atom;
	friend class InternalAtom;
	friend class Parser;
	
	cpu_type_t								_architecture;
	class InternalAtom						_internalAtom;
	class Atom*								_atomArray;
	uint32_t								_atomArrayCount;
	lto_module_t							_module;
	const char*								_debugInfoPath;
	time_t									_debugInfoModTime;
	ld::Section								_section;
	ld::Fixup								_fixupToInternal;
	ld::relocatable::File::DebugInfoKind	_debugInfo; 
	uint32_t								_cpuSubType;
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
												ld::Atom::Definition d, ld::Atom::Combine c, ld::Atom::Alignment a, bool ah);

	// overrides of ld::Atom
	virtual ld::File*					file() const		{ return &_file; }
	virtual const char*					translationUnitSource() const
															{ return (_compiledAtom ? _compiledAtom->translationUnitSource() : NULL); }
	virtual const char*					name() const		{ return _name; }
	virtual uint64_t					size() const		{ return (_compiledAtom ? _compiledAtom->size() : 0); }
	virtual uint64_t					objectAddress() const { return (_compiledAtom ? _compiledAtom->objectAddress() : 0); }
	virtual void						copyRawContent(uint8_t buffer[]) const 
															{ if (_compiledAtom) _compiledAtom->copyRawContent(buffer); }
	virtual const uint8_t*				rawContentPointer() const 
															{ return (_compiledAtom ? _compiledAtom->rawContentPointer() : NULL);  }
	virtual unsigned long				contentHash(const class ld::IndirectBindingTable& ibt) const 
															{ return (_compiledAtom ? _compiledAtom->contentHash(ibt) : 0);  }
	virtual bool						canCoalesceWith(const ld::Atom& rhs, const class ld::IndirectBindingTable& ibt) const 
															{ return (_compiledAtom ? _compiledAtom->canCoalesceWith(rhs,ibt) : false); }
	virtual ld::Fixup::iterator				fixupsBegin() const	
															{ return (_compiledAtom ? _compiledAtom->fixupsBegin() : (ld::Fixup*)&_file._fixupToInternal); }
	virtual ld::Fixup::iterator				fixupsEnd() const	
															{ return (_compiledAtom ? _compiledAtom->fixupsEnd() : &((ld::Fixup*)&_file._fixupToInternal)[1]); }
	virtual ld::Atom::UnwindInfo::iterator	beginUnwind() const 
															{ return (_compiledAtom ? _compiledAtom->beginUnwind() : NULL); }
	virtual ld::Atom::UnwindInfo::iterator	endUnwind() const	
															{ return (_compiledAtom ? _compiledAtom->endUnwind() : NULL); }
	virtual ld::Atom::LineInfo::iterator	beginLineInfo() const 
															{ return (_compiledAtom ? _compiledAtom->beginLineInfo() : NULL); }
	virtual ld::Atom::LineInfo::iterator	endLineInfo() const 
															{ return (_compiledAtom ? _compiledAtom->endLineInfo() : NULL); }
															
	const ld::Atom*						compiledAtom()		{ return _compiledAtom; }
	void								setCompiledAtom(const ld::Atom& atom);

private:

	File&								_file;
	const char*							_name;
	const ld::Atom*						_compiledAtom;
};

											





class Parser 
{
public:
	static bool						validFile(const uint8_t* fileContent, uint64_t fileLength, cpu_type_t architecture, cpu_subtype_t subarch);
	static const char*				fileKind(const uint8_t* fileContent, uint64_t fileLength);
	static File*					parse(const uint8_t* fileContent, uint64_t fileLength, const char* path, 
											time_t modTime, ld::File::Ordinal ordinal, cpu_type_t architecture, cpu_subtype_t subarch, bool logAllFiles);
	static bool						libLTOisLoaded() { return (::lto_get_version() != NULL); }
	static bool						optimize(   const std::vector<const ld::Atom*>&	allAtoms,
												ld::Internal&						state,
												const OptimizeOptions&				options,
												ld::File::AtomHandler&				handler,
												std::vector<const ld::Atom*>&		newAtoms, 
												std::vector<const char*>&			additionalUndefines);

	static const char*			ltoVersion()	{ return ::lto_get_version(); }

private:
	static const char*				tripletPrefixForArch(cpu_type_t arch);
	static ld::relocatable::File*	parseMachOFile(const uint8_t* p, size_t len, const OptimizeOptions& options);

	class CStringEquals
	{
	public:
		bool operator()(const char* left, const char* right) const { return (strcmp(left, right) == 0); }
	};
	typedef	__gnu_cxx::hash_set<const char*, __gnu_cxx::hash<const char*>, CStringEquals>  CStringSet;
	typedef __gnu_cxx::hash_map<const char*, Atom*, __gnu_cxx::hash<const char*>, CStringEquals> CStringToAtom;
	
	class AtomSyncer : public ld::File::AtomHandler {
	public:
							AtomSyncer(std::vector<const char*>& a, std::vector<const ld::Atom*>&na,
										CStringToAtom la, CStringToAtom dla, const OptimizeOptions& options) :
										_options(options), _additionalUndefines(a), _newAtoms(na), _llvmAtoms(la), _deadllvmAtoms(dla) { }
		virtual void		doAtom(const class ld::Atom&);
		virtual void		doFile(const class ld::File&) { }
		
		const OptimizeOptions&			_options;
		std::vector<const char*>&		_additionalUndefines;
		std::vector<const ld::Atom*>&	_newAtoms;
		CStringToAtom					_llvmAtoms;
		CStringToAtom					_deadllvmAtoms;
	};

	static std::vector<File*>		_s_files;
};

std::vector<File*> Parser::_s_files;


bool Parser::validFile(const uint8_t* fileContent, uint64_t fileLength, cpu_type_t architecture, cpu_subtype_t subarch)
{
	for (const ArchInfo* t=archInfoArray; t->archName != NULL; ++t) {
		if ( (architecture == t->cpuType) && (!(t->isSubType) || (subarch == t->cpuSubType)) ) {
			bool result = ::lto_module_is_object_file_in_memory_for_target(fileContent, fileLength, t->llvmTriplePrefix);
			if ( !result ) {
				// <rdar://problem/8434487> LTO only supports thumbv7 not armv7
				if ( t->llvmTriplePrefixAlt[0] != '\0' ) {
					result = ::lto_module_is_object_file_in_memory_for_target(fileContent, fileLength, t->llvmTriplePrefixAlt);
				}
			}
			return result;
		}
	}
	return false;
}

const char* Parser::fileKind(const uint8_t* p, uint64_t fileLength)
{
	if ( (p[0] == 0xDE) && (p[1] == 0xC0) && (p[2] == 0x17) && (p[3] == 0x0B) ) {
		cpu_type_t arch = LittleEndian::get32(*((uint32_t*)(&p[16])));
		for (const ArchInfo* t=archInfoArray; t->archName != NULL; ++t) {
			if ( arch == t->cpuType ) {
				 if ( t->isSubType ) {
					if ( ::lto_module_is_object_file_in_memory_for_target(p, fileLength, t->llvmTriplePrefix) )
						return t->archName;
				}
				else {
					return t->archName;
				}
			}
		}
		return "unknown bitcode architecture";
	}
	return NULL;
}

File* Parser::parse(const uint8_t* fileContent, uint64_t fileLength, const char* path, time_t modTime, ld::File::Ordinal ordinal,
													cpu_type_t architecture, cpu_subtype_t subarch, bool logAllFiles) 
{
	File* f = new File(path, modTime, ordinal, fileContent, fileLength, architecture);
	_s_files.push_back(f);
	if ( logAllFiles ) 
		printf("%s\n", path);
	return f;
}


ld::relocatable::File* Parser::parseMachOFile(const uint8_t* p, size_t len, const OptimizeOptions& options) 
{
	mach_o::relocatable::ParserOptions objOpts;
	objOpts.architecture		= options.arch;
	objOpts.objSubtypeMustMatch = false; 
	objOpts.logAllFiles			= false;
	objOpts.convertUnwindInfo	= true;
	objOpts.subType				= 0;
	
	// mach-o parsing is done in-memory, but need path for debug notes
	const char* path = "/tmp/lto.o";
	time_t modTime = 0;
	if ( options.tmpObjectFilePath != NULL ) {
		path = options.tmpObjectFilePath;
		struct stat statBuffer;
		if ( stat(options.tmpObjectFilePath, &statBuffer) == 0 )
			modTime = statBuffer.st_mtime;
	}
	
	ld::relocatable::File* result = mach_o::relocatable::parse(p, len, path, modTime, ld::File::Ordinal::LTOOrdinal(), objOpts);
	if ( result != NULL )
		return result;
	throw "LLVM LTO, file is not of required architecture";
}



File::File(const char* pth, time_t mTime, ld::File::Ordinal ordinal, const uint8_t* content, uint32_t contentLength, cpu_type_t arch) 
	: ld::relocatable::File(pth,mTime,ordinal), _architecture(arch), _internalAtom(*this), 
	_atomArray(NULL), _atomArrayCount(0), _module(NULL), _debugInfoPath(pth), 
	_section("__TEXT_", "__tmp_lto", ld::Section::typeTempLTO),
	_fixupToInternal(0, ld::Fixup::k1of1, ld::Fixup::kindNone, &_internalAtom),
	_debugInfo(ld::relocatable::File::kDebugInfoNone), _cpuSubType(0)
{
	const bool log = false;
	
	// create llvm module
	_module = ::lto_module_create_from_memory(content, contentLength);
    if ( _module == NULL )
		throwf("could not parse object file %s: %s", pth, lto_get_error_message());

	if ( log ) fprintf(stderr, "bitcode file: %s\n", pth);
	
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
		ld::Atom::Combine combine = ld::Atom::combineNever;
		switch ( attr & LTO_SYMBOL_DEFINITION_MASK ) {
			case LTO_SYMBOL_DEFINITION_REGULAR:
				def = ld::Atom::definitionRegular;
				break;
			case LTO_SYMBOL_DEFINITION_TENTATIVE:
				def = ld::Atom::definitionTentative;
				break;
			case LTO_SYMBOL_DEFINITION_WEAK:
				def = ld::Atom::definitionRegular;
				combine = ld::Atom::combineByName;
				break;
			case LTO_SYMBOL_DEFINITION_UNDEFINED:
			case LTO_SYMBOL_DEFINITION_WEAKUNDEF:
				def = ld::Atom::definitionProxy;
				break;
			default:
				throwf("unknown definition kind for symbol %s in bitcode file %s", name, pth);
		}

		// make LLVM atoms for definitions and a reference for undefines
		if ( def != ld::Atom::definitionProxy ) {
			ld::Atom::Scope scope;
			bool autohide = false;
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
#if LTO_API_VERSION >= 4
				case LTO_SYMBOL_SCOPE_DEFAULT_CAN_BE_HIDDEN:
					scope = ld::Atom::scopeGlobal;
					autohide = true;
					break;
#endif
				default:
					throwf("unknown scope for symbol %s in bitcode file %s", name, pth);
			}
			// only make atoms for non-internal symbols 
			if ( scope == ld::Atom::scopeTranslationUnit )
				continue;
			uint8_t alignment = (attr & LTO_SYMBOL_ALIGNMENT_MASK);
			// make Atom using placement new operator
			new (&_atomArray[_atomArrayCount++]) Atom(*this, name, scope, def, combine, alignment, autohide);
			if ( scope != ld::Atom::scopeTranslationUnit )
				_internalAtom.addReference(name);
			if ( log ) fprintf(stderr, "\t0x%08X %s\n", attr, name);
		}
		else {
			// add to list of external references
			_internalAtom.addReference(name);
			if ( log ) fprintf(stderr, "\t%s (undefined)\n", name);
		}
	}
}

File::~File()
{
	if ( _module != NULL )
		::lto_module_dispose(_module);
}

bool File::forEachAtom(ld::File::AtomHandler& handler) const
{
	handler.doAtom(_internalAtom);
	for(uint32_t i=0; i < _atomArrayCount; ++i) {
		handler.doAtom(_atomArray[i]);
	}
	return true;
}

InternalAtom::InternalAtom(File& f)
	: ld::Atom(f._section, ld::Atom::definitionRegular, ld::Atom::combineNever, ld::Atom::scopeTranslationUnit, 
				ld::Atom::typeLTOtemporary, ld::Atom::symbolTableNotIn, true, false, false, ld::Atom::Alignment(0)),
		_file(f)
{
}

Atom::Atom(File& f, const char* nm, ld::Atom::Scope s, ld::Atom::Definition d, ld::Atom::Combine c, 
			ld::Atom::Alignment a, bool ah)
	: ld::Atom(f._section, d, c, s, ld::Atom::typeLTOtemporary, 
				ld::Atom::symbolTableIn, false, false, false, a),
		_file(f), _name(nm), _compiledAtom(NULL)
{
	if ( ah )
		this->setAutoHide();
}

void Atom::setCompiledAtom(const ld::Atom& atom)
{
	// set delegate so virtual methods go to it
	_compiledAtom = &atom;
	
	//fprintf(stderr, "setting lto atom %p to delegate to mach-o atom %p (%s)\n", this, &atom, atom.name());
	
	// update fields in ld::Atom to match newly constructed mach-o atom
	(const_cast<Atom*>(this))->setAttributesFromAtom(atom);
}



bool Parser::optimize(  const std::vector<const ld::Atom*>&	allAtoms,
						ld::Internal&						state,
						const OptimizeOptions&				options,
						ld::File::AtomHandler&				handler,
						std::vector<const ld::Atom*>&		newAtoms, 
						std::vector<const char*>&			additionalUndefines)
{
	const bool logMustPreserve = false;
	const bool logExtraOptions = false;
	const bool logBitcodeFiles = false;
	const bool logAtomsBeforeSync = false;

	// exit quickly if nothing to do
	if ( _s_files.size() == 0 ) 
		return false;
	
	// print out LTO version string if -v was used
	if ( options.verbose )
		fprintf(stderr, "%s\n", lto_get_version());
	
	// create optimizer and add each Reader
	lto_code_gen_t generator = ::lto_codegen_create();
	for (std::vector<File*>::iterator it=_s_files.begin(); it != _s_files.end(); ++it) {
		if ( logBitcodeFiles ) fprintf(stderr, "lto_codegen_add_module(%s)\n", (*it)->path());
		if ( ::lto_codegen_add_module(generator, (*it)->module()) )
			throwf("lto: could not merge in %s because %s", (*it)->path(), ::lto_get_error_message());
	}

	// add any -mllvm command line options
	for (std::vector<const char*>::const_iterator it=options.llvmOptions->begin(); it != options.llvmOptions->end(); ++it) {
		if ( logExtraOptions ) fprintf(stderr, "passing option to llvm: %s\n", *it);
		::lto_codegen_debug_options(generator, *it);
	}

	// The atom graph uses directed edges (references). Collect all references where 
	// originating atom is not part of any LTO Reader. This allows optimizer to optimize an 
	// external (i.e. not originated from same .o file) reference if all originating atoms are also 
	// defined in llvm bitcode file.
	CStringSet nonLLVMRefs;
	CStringToAtom llvmAtoms;
    bool hasNonllvmAtoms = false;
	for (std::vector<const ld::Atom*>::const_iterator it = allAtoms.begin(); it != allAtoms.end(); ++it) {
		const ld::Atom* atom = *it;
		// only look at references that come from an atom that is not an llvm atom
		if ( atom->contentType() != ld::Atom::typeLTOtemporary ) {
			if ( (atom->section().type() != ld::Section::typeMachHeader) && (atom->definition() != ld::Atom::definitionProxy) ) {
				hasNonllvmAtoms = true;
			}
			const ld::Atom* target;
			for (ld::Fixup::iterator fit=atom->fixupsBegin(); fit != atom->fixupsEnd(); ++fit) {
				switch ( fit->binding ) {
					case ld::Fixup::bindingDirectlyBound:
						// that reference an llvm atom
						if ( fit->u.target->contentType() == ld::Atom::typeLTOtemporary ) 
							nonLLVMRefs.insert(fit->u.target->name());
						break;
					case ld::Fixup::bindingsIndirectlyBound:
						target = state.indirectBindingTable[fit->u.bindingIndex];
						if ( target == NULL )
							throwf("'%s' in %s contains undefined reference", atom->name(), atom->file()->path());
						assert(target != NULL);
						if ( target->contentType() == ld::Atom::typeLTOtemporary )
							nonLLVMRefs.insert(target->name());
					default:
						break;
				}
			}
		}
		else {
			llvmAtoms[atom->name()] = (Atom*)atom;
		}
	}
	// if entry point is in a llvm bitcode file, it must be preserved by LTO
	if ( state.entryPoint!= NULL ) {
		if ( state.entryPoint->contentType() == ld::Atom::typeLTOtemporary ) 
			nonLLVMRefs.insert(state.entryPoint->name());
	}
	
	// deadAtoms are the atoms that the linker coalesced.  For instance weak or tentative definitions
	// overriden by another atom.  If any of these deadAtoms are llvm atoms and they were replaced
	// with a mach-o atom, we need to tell the lto engine to preserve (not optimize away) its dead 
	// atom so that the linker can replace it with the mach-o one later.
	CStringToAtom deadllvmAtoms;
	for (std::vector<const ld::Atom*>::const_iterator it = allAtoms.begin(); it != allAtoms.end(); ++it) {
		const ld::Atom* atom = *it;
		if ( atom->coalescedAway() && (atom->contentType() == ld::Atom::typeLTOtemporary) ) {
			const char* name = atom->name();
			if ( logMustPreserve ) fprintf(stderr, "lto_codegen_add_must_preserve_symbol(%s) because linker coalesce away and replace with a mach-o atom\n", name);
			::lto_codegen_add_must_preserve_symbol(generator, name);
			deadllvmAtoms[name] = (Atom*)atom;
		}
	}
	for (std::vector<File*>::iterator it=_s_files.begin(); it != _s_files.end(); ++it) {
		File* file = *it;
		for(uint32_t i=0; i < file->_atomArrayCount; ++i) {
			Atom* llvmAtom = &file->_atomArray[i];
			if ( llvmAtom->coalescedAway()  ) {
				const char* name = llvmAtom->name();
				if ( deadllvmAtoms.find(name) == deadllvmAtoms.end() ) {
					if ( logMustPreserve ) 
						fprintf(stderr, "lto_codegen_add_must_preserve_symbol(%s) because linker coalesce away and replace with a mach-o atom\n", name);
					::lto_codegen_add_must_preserve_symbol(generator, name);
					deadllvmAtoms[name] = (Atom*)llvmAtom;
				}
			}
			else if ( options.linkerDeadStripping && !llvmAtom->live() ) {
				const char* name = llvmAtom->name();
				deadllvmAtoms[name] = (Atom*)llvmAtom;
			}
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
		if ( (atom->scope() == ld::Atom::scopeGlobal) && options.preserveAllGlobals ) { 
			if ( logMustPreserve ) fprintf(stderr, "lto_codegen_add_must_preserve_symbol(%s) because global symbol\n", name);
			::lto_codegen_add_must_preserve_symbol(generator, name);
		}
		else if ( nonLLVMRefs.find(name) != nonLLVMRefs.end() ) {
			if ( logMustPreserve ) fprintf(stderr, "lto_codegen_add_must_preserve_symbol(%s) because referenced by a mach-o atom\n", name);
			::lto_codegen_add_must_preserve_symbol(generator, name);
		}
	}
	
    // special case running ld -r on all bitcode files to produce another bitcode file (instead of mach-o)
    if ( options.relocatable && !hasNonllvmAtoms ) {
		if ( ! ::lto_codegen_write_merged_modules(generator, options.outputFilePath) ) {
			// HACK, no good way to tell linker we are all done, so just quit
			exit(0);
		}
		warning("could not produce merged bitcode file");
    }
    
	// set code-gen model
	lto_codegen_model model = LTO_CODEGEN_PIC_MODEL_DYNAMIC;
	if ( options.mainExecutable ) {
		if ( options.staticExecutable ) {
			// darwin x86_64 "static" code model is really dynamic code model
			if ( options.arch == CPU_TYPE_X86_64 )
				model = LTO_CODEGEN_PIC_MODEL_DYNAMIC;
			else
				model = LTO_CODEGEN_PIC_MODEL_STATIC;
		}
		else {
			if ( options.pie )
				model = LTO_CODEGEN_PIC_MODEL_DYNAMIC;
			else
				model = LTO_CODEGEN_PIC_MODEL_DYNAMIC_NO_PIC;
		}
	}
	else {
		if ( options.allowTextRelocs )
			model = LTO_CODEGEN_PIC_MODEL_DYNAMIC_NO_PIC;
		else
			model = LTO_CODEGEN_PIC_MODEL_DYNAMIC;
	}
	if ( ::lto_codegen_set_pic_model(generator, model) )
		throwf("could not create set codegen model: %s", lto_get_error_message());

    // if requested, save off merged bitcode file
    if ( options.saveTemps ) {
        char tempBitcodePath[MAXPATHLEN];
        strcpy(tempBitcodePath, options.outputFilePath);
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
    if ( options.saveTemps ) {
        char tempMachoPath[MAXPATHLEN];
        strcpy(tempMachoPath, options.outputFilePath);
        strcat(tempMachoPath, ".lto.o");
        int fd = ::open(tempMachoPath, O_CREAT | O_WRONLY | O_TRUNC, 0666);
		if ( fd != -1) {
			::write(fd, machOFile, machOFileLen);
			::close(fd);
		}
		//	save off merged bitcode file
		char tempOptBitcodePath[MAXPATHLEN];
        strcpy(tempOptBitcodePath, options.outputFilePath);
        strcat(tempOptBitcodePath, ".lto.opt.bc");
        ::lto_codegen_write_merged_modules(generator, tempOptBitcodePath);
	}

	// if needed, save temp mach-o file to specific location
	if ( options.tmpObjectFilePath != NULL ) {
		int fd = ::open(options.tmpObjectFilePath, O_CREAT | O_WRONLY | O_TRUNC, 0666);
		if ( fd != -1) {
			::write(fd, machOFile, machOFileLen);
			::close(fd);
		}
		else {
			warning("could not write LTO temp file '%s', errno=%d", options.tmpObjectFilePath, errno);
		}
	}
	
	// parse generated mach-o file into a MachOReader
	ld::relocatable::File* machoFile = parseMachOFile(machOFile, machOFileLen, options);
	
	// sync generated mach-o atoms with existing atoms ld knows about
	if ( logAtomsBeforeSync ) {
		fprintf(stderr, "llvmAtoms:\n");
		for (CStringToAtom::iterator it = llvmAtoms.begin(); it != llvmAtoms.end(); ++it) {
			const char* name = it->first;
			//Atom* atom = it->second;
			fprintf(stderr, "\t%s\n", name);
		}
		fprintf(stderr, "deadllvmAtoms:\n");
		for (CStringToAtom::iterator it = deadllvmAtoms.begin(); it != deadllvmAtoms.end(); ++it) {
			const char* name = it->first;
			//Atom* atom = it->second;
			fprintf(stderr, "\t%s\n", name);
		}
	}
	AtomSyncer syncer(additionalUndefines, newAtoms, llvmAtoms, deadllvmAtoms, options);
	machoFile->forEachAtom(syncer);
			
	// Remove InternalAtoms from ld
	for (std::vector<File*>::iterator it=_s_files.begin(); it != _s_files.end(); ++it) {
		(*it)->internalAtom().setCoalescedAway();
	}
	// Remove Atoms from ld if code generator optimized them away
	for (CStringToAtom::iterator li = llvmAtoms.begin(), le = llvmAtoms.end(); li != le; ++li) {
		// check if setRealAtom() called on this Atom
		if ( li->second->compiledAtom() == NULL ) {
			//fprintf(stderr, "llvm optimized away %p %s\n", li->second, li->second->name());
			li->second->setCoalescedAway();
		}
	}
	
	// notify about file level attributes
	handler.doFile(*machoFile);
	
	// if final mach-o file has debug info, update original bitcode files to match
	for (std::vector<File*>::iterator it=_s_files.begin(); it != _s_files.end(); ++it) {
		(*it)->setDebugInfo(machoFile->debugInfo(), machoFile->path(),
							machoFile->modificationTime(), machoFile->cpuSubType());
	}
	
	return true;
}


void Parser::AtomSyncer::doAtom(const ld::Atom& machoAtom)
{
	// update proxy atoms to point to real atoms and find new atoms
	const char* name = machoAtom.name();
	if ( machoAtom.scope() >= ld::Atom::scopeLinkageUnit ) {
		CStringToAtom::iterator pos = _llvmAtoms.find(name);
		if ( pos != _llvmAtoms.end() ) {
			// turn Atom into a proxy for this mach-o atom
			pos->second->setCompiledAtom(machoAtom);
		}
		else {
			// an atom of this name was not in the allAtoms list the linker gave us
			if ( _deadllvmAtoms.find(name) != _deadllvmAtoms.end() ) {
				// this corresponding to an atom that the linker coalesced away or marked not-live
				if ( _options.linkerDeadStripping ) {
					// llvm seems to want this atom and -dead_strip is enabled, so it will be deleted if not needed, so add back
					Atom* llvmAtom = _deadllvmAtoms[name];
					llvmAtom->setCompiledAtom(machoAtom);
					_newAtoms.push_back(&machoAtom);
				}
				else {
					// Don't pass it back as a new atom
				} 
			}
			else
			{
				// this is something new that lto conjured up, tell ld its new
				_newAtoms.push_back(&machoAtom);
			}
		}
	}
	else {
		// ld only knew about non-static atoms, so this one must be new
		_newAtoms.push_back(&machoAtom);
	}
	
	// adjust fixups to go through proxy atoms
	//fprintf(stderr, "adjusting fixups in atom: %s\n", machoAtom.name());
	for (ld::Fixup::iterator fit=machoAtom.fixupsBegin(); fit != machoAtom.fixupsEnd(); ++fit) {
		switch ( fit->binding ) {
			case ld::Fixup::bindingNone:
				break;
			case ld::Fixup::bindingByNameUnbound:
				// don't know if this target has been seen by linker before or if it is new
				// be conservative and tell linker it is new
				_additionalUndefines.push_back(fit->u.name);
				//fprintf(stderr, "    by name ref to: %s\n", fit->u.name);
				break;
			case ld::Fixup::bindingDirectlyBound:
				// If mach-o atom is referencing another mach-o atom then 
				// reference is not going through Atom proxy. Fix it here to ensure that all
				// llvm symbol references always go through Atom proxy.
				if (  fit->u.target->scope() != ld::Atom::scopeTranslationUnit ) {
					const char* targetName = fit->u.target->name();
					CStringToAtom::iterator pos = _llvmAtoms.find(targetName);
					if ( pos != _llvmAtoms.end() ) {
						fit->u.target = pos->second;
					}
					else {
						if ( _deadllvmAtoms.find(targetName) != _deadllvmAtoms.end() ) {
							// target was coalesed away and replace by mach-o atom from a non llvm .o file
							fit->binding = ld::Fixup::bindingByNameUnbound;
							fit->u.name = targetName;
						}
					}
				}
				//fprintf(stderr, "    direct ref to: %s (scope=%d)\n", fit->u.target->name(), fit->u.target->scope());
				break;
			case ld::Fixup::bindingByContentBound:
				//fprintf(stderr, "    direct by content to: %s\n", fit->u.target->name());
				break;
			case ld::Fixup::bindingsIndirectlyBound:
				assert(0 && "indirect binding found in initial mach-o file?");
				//fprintf(stderr, "    indirect by content to: %u\n", fit->u.bindingIndex);
				break;
		}
	}

}

class Mutex {
	static pthread_mutex_t lto_lock;
public:
	Mutex() { pthread_mutex_lock(&lto_lock); }
	~Mutex() { pthread_mutex_unlock(&lto_lock); }
};
pthread_mutex_t Mutex::lto_lock = PTHREAD_MUTEX_INITIALIZER;

//
// Used by archive reader to see if member is an llvm bitcode file
//
bool isObjectFile(const uint8_t* fileContent, uint64_t fileLength, cpu_type_t architecture, cpu_subtype_t subarch)
{
	Mutex lock;
	return Parser::validFile(fileContent, fileLength, architecture, subarch);
}


//
// main function used by linker to instantiate ld::Files
//
ld::relocatable::File* parse(const uint8_t* fileContent, uint64_t fileLength, 
								const char* path, time_t modTime, ld::File::Ordinal ordinal,
								cpu_type_t architecture, cpu_subtype_t subarch, bool logAllFiles)
{
	Mutex lock;
	if ( Parser::validFile(fileContent, fileLength, architecture, subarch) )
		return Parser::parse(fileContent, fileLength, path, modTime, ordinal, architecture, subarch, logAllFiles);
	else
		return NULL;
}

//
// used by "ld -v" to report version of libLTO.dylib being used
//
const char* version()
{
	Mutex lock;
	return ::lto_get_version();
}


//
// used by ld for error reporting
//
bool libLTOisLoaded()
{
	Mutex lock;
	return (::lto_get_version() != NULL);
}

//
// used by ld for error reporting
//
const char* archName(const uint8_t* fileContent, uint64_t fileLength)
{
	Mutex lock;
	return Parser::fileKind(fileContent, fileLength);
}

//
// used by ld for doing link time optimization
//
bool optimize(  const std::vector<const ld::Atom*>&	allAtoms,
				ld::Internal&						state,
				const OptimizeOptions&				options,
				ld::File::AtomHandler&				handler,
				std::vector<const ld::Atom*>&		newAtoms, 
				std::vector<const char*>&			additionalUndefines)
{ 
	Mutex lock;
	return Parser::optimize(allAtoms, state, options, handler, newAtoms, additionalUndefines);
}



}; // namespace lto


#endif

#endif /* LTO SUPPORT */
