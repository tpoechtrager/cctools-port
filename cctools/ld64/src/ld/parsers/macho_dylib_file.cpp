
/* -*- mode: C++; c-basic-offset: 4; tab-width: 4 -*-
 *
 * Copyright (c) 2005-2011 Apple Inc. All rights reserved.
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


#include <stdint.h>
#include <math.h>
#include <unistd.h>
#include <sys/param.h>
#include <sys/mman.h>


#include <vector>
#include <set>
#include <algorithm>
#include <unordered_map>
#include <unordered_set>

#include "Architectures.hpp"
#include "MachOFileAbstraction.hpp"
#include "MachOTrie.hpp"
#include "macho_dylib_file.h"
#include "../code-sign-blobs/superblob.h"

namespace mach_o {
namespace dylib {


// forward reference
template <typename A> class File;


//
// An ExportAtom has no content.  It exists so that the linker can track which imported
// symbols came from which dynamic libraries.
//
template <typename A>
class ExportAtom : public ld::Atom
{
public:
											ExportAtom(const File<A>& f, const char* nm, bool weakDef, 
														bool tlv, typename A::P::uint_t address)
												: ld::Atom(f._importProxySection, ld::Atom::definitionProxy, 
													(weakDef? ld::Atom::combineByName : ld::Atom::combineNever),
													ld::Atom::scopeLinkageUnit, 
													(tlv ? ld::Atom::typeTLV : ld::Atom::typeUnclassified), 
													symbolTableNotIn, false, false, false, ld::Atom::Alignment(0)), 
													_file(f), _name(nm), _address(address) {}
	// overrides of ld::Atom
	virtual const ld::File*						file() const		{ return &_file; }
	virtual const char*							name() const		{ return _name; }
	virtual uint64_t							size() const		{ return 0; }
	virtual uint64_t							objectAddress() const { return _address; }
	virtual void								copyRawContent(uint8_t buffer[]) const { }
	virtual void								setScope(Scope)		{ }

protected:
	typedef typename A::P					P;
	typedef typename A::P::uint_t			pint_t;

	virtual									~ExportAtom() {}

	const File<A>&							_file;
	const char*								_name;
	pint_t									_address;
};



//
// An ImportAtom has no content.  It exists so that when linking a main executable flat-namespace
// the imports of all flat dylibs are checked
//
template <typename A>
class ImportAtom : public ld::Atom
{
public:
												ImportAtom(File<A>& f, std::vector<const char*>& imports);

	// overrides of ld::Atom
	virtual ld::File*							file() const		{ return &_file; }
	virtual const char*							name() const		{ return "import-atom"; }
	virtual uint64_t							size() const		{ return 0; }
	virtual uint64_t							objectAddress() const { return 0; }
	virtual void								copyRawContent(uint8_t buffer[]) const { }
	virtual void								setScope(Scope)		{ }
	virtual ld::Fixup::iterator					fixupsBegin() const	{ return &_undefs[0]; }
	virtual ld::Fixup::iterator					fixupsEnd()	const	{ return &_undefs[_undefs.size()]; }

protected:
	typedef typename A::P					P;

	virtual									~ImportAtom() {}


	File<A>&								_file;
	mutable std::vector<ld::Fixup>			_undefs;
};

template <typename A>
ImportAtom<A>::ImportAtom(File<A>& f, std::vector<const char*>& imports)
: ld::Atom(f._flatDummySection, ld::Atom::definitionRegular, ld::Atom::combineNever, ld::Atom::scopeTranslationUnit, 
	ld::Atom::typeUnclassified, symbolTableNotIn, false, false, false, ld::Atom::Alignment(0)), _file(f) 
{ 
	for(std::vector<const char*>::iterator it=imports.begin(); it != imports.end(); ++it) {
		_undefs.push_back(ld::Fixup(0, ld::Fixup::k1of1, ld::Fixup::kindNone, false, strdup(*it)));
	}
}



//
// The reader for a dylib extracts all exported symbols names from the memory-mapped
// dylib, builds a hash table, then unmaps the file.  This is an important memory
// savings for large dylibs.
//
template <typename A>
class File : public ld::dylib::File
{
public:
	static bool								validFile(const uint8_t* fileContent, bool executableOrDylib);
											File(const uint8_t* fileContent, uint64_t fileLength, const char* path,   
													time_t mTime, ld::File::Ordinal ordinal, bool linkingFlatNamespace, 
													bool linkingMainExecutable, bool hoistImplicitPublicDylibs, 
													ld::MacVersionMin macMin, ld::IOSVersionMin iPhoneMin, bool allowSimToMacOSX, bool addVers, 
													bool logAllFiles, const char* installPath, bool indirectDylib);
	virtual									~File() {}

	// overrides of ld::File
	virtual bool							forEachAtom(ld::File::AtomHandler&) const;
	virtual bool							justInTimeforEachAtom(const char* name, ld::File::AtomHandler&) const;
	virtual ld::File::ObjcConstraint		objCConstraint() const		{ return _objcContraint; }
	
	// overrides of ld::dylib::File
	virtual void							processIndirectLibraries(ld::dylib::File::DylibHandler*, bool);
	virtual bool							providedExportAtom() const	{ return _providedAtom; }
	virtual const char*						parentUmbrella() const		{ return _parentUmbrella; }
	virtual const std::vector<const char*>*	allowableClients() const	{ return _allowableClients.size() != 0 ? &_allowableClients : NULL; }
	virtual bool							hasWeakExternals() const	{ return _hasWeakExports; }
	virtual bool							deadStrippable() const		{ return _deadStrippable; }
	virtual bool							hasPublicInstallName() const{ return _hasPublicInstallName; }
	virtual bool							hasWeakDefinition(const char* name) const;
	virtual bool							allSymbolsAreWeakImported() const;
	virtual const void*						codeSignatureDR() const		{ return _codeSignatureDR; }
	virtual bool							installPathVersionSpecific() const { return _installPathOverride; }


protected:

	struct ReExportChain { ReExportChain* prev; File<A>* file; };

	void											assertNoReExportCycles(ReExportChain*);

private:
	typedef typename A::P					P;
	typedef typename A::P::E				E;
	typedef typename A::P::uint_t			pint_t;

	friend class ExportAtom<A>;
	friend class ImportAtom<A>;

	struct CStringHash {
		std::size_t operator()(const char* __s) const {
			unsigned long __h = 0;
			for ( ; *__s; ++__s)
				__h = 5 * __h + *__s;
			return size_t(__h);
		};
	};
	struct AtomAndWeak { ld::Atom* atom; bool weakDef; bool tlv; pint_t address; };
	typedef std::unordered_map<const char*, AtomAndWeak, ld::CStringHash, ld::CStringEquals> NameToAtomMap;
	typedef std::unordered_set<const char*, CStringHash, ld::CStringEquals>  NameSet;

	struct Dependent { const char* path; File<A>* dylib; bool reExport; };

	bool										containsOrReExports(const char* name, bool* weakDef, bool* tlv, pint_t* defAddress) const;
	bool										isPublicLocation(const char* pth);
	bool										wrongOS() { return _wrongOS; }
	void										addSymbol(const char* name, bool weak, bool tlv, pint_t address);
	void										addDyldFastStub();
	void										buildExportHashTableFromExportInfo(const macho_dyld_info_command<P>* dyldInfo,
																				const uint8_t* fileContent);
	void										buildExportHashTableFromSymbolTable(const macho_dysymtab_command<P>* dynamicInfo, 
														const macho_nlist<P>* symbolTable, const char* strings,
														const uint8_t* fileContent);
	static uint32_t								parseVersionNumber32(const char* versionString);
	static const char*							objCInfoSegmentName();
	static const char*							objCInfoSectionName();
	
	const ld::MacVersionMin						_macVersionMin;
	const ld::IOSVersionMin						_iOSVersionMin;
	const bool									_allowSimToMacOSXLinking;
	const bool									_addVersionLoadCommand;
	bool										_linkingFlat;
	bool										_implicitlyLinkPublicDylibs;
	ld::File::ObjcConstraint					_objcContraint;
	ld::Section									_importProxySection;
	ld::Section									_flatDummySection;
	std::vector<Dependent>						_dependentDylibs;
	std::vector<const char*>   					_allowableClients;
	mutable NameToAtomMap						_atoms;
	NameSet										_ignoreExports;
	const char*									_parentUmbrella;
	ImportAtom<A>*								_importAtom;
	const void*									_codeSignatureDR;
	bool										_noRexports;
	bool										_hasWeakExports;
	bool										_deadStrippable;
	bool										_hasPublicInstallName;
	mutable bool								_providedAtom;
	bool										_explictReExportFound;
	bool										_wrongOS;
	bool										_installPathOverride;
	bool										_indirectDylibsProcessed;
	
	static bool									_s_logHashtable;
};

template <typename A>
bool File<A>::_s_logHashtable = false;

template <> const char* File<x86_64>::objCInfoSegmentName() { return "__DATA"; }
template <> const char* File<arm>::objCInfoSegmentName() { return "__DATA"; }
template <typename A> const char* File<A>::objCInfoSegmentName() { return "__OBJC"; }

template <> const char* File<x86_64>::objCInfoSectionName() { return "__objc_imageinfo"; }
template <> const char* File<arm>::objCInfoSectionName() { return "__objc_imageinfo"; }
template <typename A> const char* File<A>::objCInfoSectionName() { return "__image_info"; }

template <typename A>
File<A>::File(const uint8_t* fileContent, uint64_t fileLength, const char* pth, time_t mTime, ld::File::Ordinal ord,
				bool linkingFlatNamespace, bool linkingMainExecutable, bool hoistImplicitPublicDylibs,
				ld::MacVersionMin macMin, ld::IOSVersionMin iOSMin, bool allowSimToMacOSX, bool addVers,
				bool logAllFiles, const char* targetInstallPath, bool indirectDylib)
	: ld::dylib::File(strdup(pth), mTime, ord), 
	_macVersionMin(macMin), _iOSVersionMin(iOSMin), _allowSimToMacOSXLinking(allowSimToMacOSX), _addVersionLoadCommand(addVers), 
	_linkingFlat(linkingFlatNamespace), _implicitlyLinkPublicDylibs(hoistImplicitPublicDylibs),
	_objcContraint(ld::File::objcConstraintNone),
	_importProxySection("__TEXT", "__import", ld::Section::typeImportProxies, true),
	_flatDummySection("__LINKEDIT", "__flat_dummy", ld::Section::typeLinkEdit, true),
	_parentUmbrella(NULL), _importAtom(NULL), _codeSignatureDR(NULL), 
	_noRexports(false), _hasWeakExports(false), 
	_deadStrippable(false), _hasPublicInstallName(false), 
	 _providedAtom(false), _explictReExportFound(false), _wrongOS(false), _installPathOverride(false), _indirectDylibsProcessed(false)
{
	const macho_header<P>* header = (const macho_header<P>*)fileContent;
	const uint32_t cmd_count = header->ncmds();
	const macho_load_command<P>* const cmds = (macho_load_command<P>*)((char*)header + sizeof(macho_header<P>));
	const macho_load_command<P>* const cmdsEnd = (macho_load_command<P>*)((char*)header + sizeof(macho_header<P>) + header->sizeofcmds());

	// write out path for -t option
	if ( logAllFiles )
		printf("%s\n", pth);

	// a "blank" stub has zero load commands
	if ( (header->filetype() == MH_DYLIB_STUB) && (cmd_count == 0) ) {	
		// no further processing needed
		munmap((caddr_t)fileContent, fileLength);
		return;
	}


	// optimize the case where we know there is no reason to look at indirect dylibs
	_noRexports = (header->flags() & MH_NO_REEXPORTED_DYLIBS) 
					|| (header->filetype() == MH_BUNDLE) 
					|| (header->filetype() == MH_EXECUTE);  // bundles and exectuables can be used via -bundle_loader
	_hasWeakExports = (header->flags() & MH_WEAK_DEFINES);
	_deadStrippable = (header->flags() & MH_DEAD_STRIPPABLE_DYLIB);
	
	// pass 1: get pointers, and see if this dylib uses compressed LINKEDIT format
	const macho_dysymtab_command<P>* dynamicInfo = NULL;
	const macho_dyld_info_command<P>* dyldInfo = NULL;
	const macho_linkedit_data_command<P>* codeSignature = NULL;
	const macho_nlist<P>* symbolTable = NULL;
	const char*	strings = NULL;
	bool compressedLinkEdit = false;
	uint32_t dependentLibCount = 0;
	const macho_load_command<P>* cmd = cmds;
	for (uint32_t i = 0; i < cmd_count; ++i) {
		macho_dylib_command<P>* dylibID;
		const macho_symtab_command<P>* symtab;
		switch (cmd->cmd()) {
			case LC_SYMTAB:
				symtab = (macho_symtab_command<P>*)cmd;
				symbolTable = (const macho_nlist<P>*)((char*)header + symtab->symoff());
				strings = (char*)header + symtab->stroff();
				if ( (symtab->stroff() + symtab->strsize()) > fileLength )
					throwf("mach-o string pool extends beyond end of file in %s", pth);
				break;
			case LC_DYSYMTAB:
				dynamicInfo = (macho_dysymtab_command<P>*)cmd;
				break;
			case LC_DYLD_INFO:
			case LC_DYLD_INFO_ONLY:
				dyldInfo = (macho_dyld_info_command<P>*)cmd;
				compressedLinkEdit = true;
				break;
			case LC_ID_DYLIB:
				dylibID = (macho_dylib_command<P>*)cmd;
				_dylibInstallPath			= strdup(dylibID->name());
				_dylibTimeStamp				= dylibID->timestamp();
				_dylibCurrentVersion		= dylibID->current_version();
				_dylibCompatibilityVersion	= dylibID->compatibility_version();
				_hasPublicInstallName		= isPublicLocation(_dylibInstallPath);
				break;
			case LC_LOAD_DYLIB:
			case LC_LOAD_WEAK_DYLIB:
				++dependentLibCount;
				break;
			case LC_REEXPORT_DYLIB:
				_explictReExportFound = true;
				++dependentLibCount;
				break;
			case LC_SUB_FRAMEWORK:
				_parentUmbrella = strdup(((macho_sub_framework_command<P>*)cmd)->umbrella());
				break;
			case LC_SUB_CLIENT:
				_allowableClients.push_back(strdup(((macho_sub_client_command<P>*)cmd)->client()));
				break;
			case LC_VERSION_MIN_MACOSX:
				if ( (_iOSVersionMin != ld::iOSVersionUnset) && !_allowSimToMacOSXLinking ) { 
					_wrongOS = true;
					if ( _addVersionLoadCommand && !indirectDylib )
						throw "building for iOS Simulator, but linking against dylib built for MacOSX";
				}
				break;
			case LC_VERSION_MIN_IPHONEOS:
				if ( _macVersionMin != ld::macVersionUnset ) {
					_wrongOS = true;
					if ( _addVersionLoadCommand && !indirectDylib )
						throw "building for MacOSX, but linking against dylib built for iOS Simulator";
				}
				break;
			case LC_CODE_SIGNATURE:
				codeSignature = (macho_linkedit_data_command<P>* )cmd;
				break;
			case macho_segment_command<P>::CMD:
				// check for Objective-C info
				if ( strcmp(((macho_segment_command<P>*)cmd)->segname(), objCInfoSegmentName()) == 0 ) {
					const macho_segment_command<P>* segment = (macho_segment_command<P>*)cmd;
					const macho_section<P>* const sectionsStart = (macho_section<P>*)((char*)segment + sizeof(macho_segment_command<P>));
					const macho_section<P>* const sectionsEnd = &sectionsStart[segment->nsects()];
					for (const macho_section<P>* sect=sectionsStart; sect < sectionsEnd; ++sect) {
						if ( strncmp(sect->sectname(), objCInfoSectionName(), strlen(objCInfoSectionName())) == 0 ) {
							//	struct objc_image_info  {
							//		uint32_t	version;	// initially 0
							//		uint32_t	flags;
							//	};
							// #define OBJC_IMAGE_SUPPORTS_GC   2
							// #define OBJC_IMAGE_GC_ONLY       4
						        // #define OBJC_IMAGE_IS_SIMULATED  32
							//
							const uint32_t* contents = (uint32_t*)(&fileContent[sect->offset()]);
							if ( (sect->size() >= 8) && (contents[0] == 0) ) {
								uint32_t flags = E::get32(contents[1]);
								if ( (flags & 4) == 4 )
									_objcContraint = ld::File::objcConstraintGC;
								else if ( (flags & 2) == 2 )
									_objcContraint = ld::File::objcConstraintRetainReleaseOrGC;
								else if ( (flags & 32) == 32 )
									_objcContraint = ld::File::objcConstraintRetainReleaseForSimulator;
								else
									_objcContraint = ld::File::objcConstraintRetainRelease;
							}
							else if ( sect->size() > 0 ) {
								warning("can't parse %s/%s section in %s", objCInfoSegmentName(), objCInfoSectionName(), this->path());
							}
						}
					}
				}
		}
		cmd = (const macho_load_command<P>*)(((char*)cmd)+cmd->cmdsize());
		if ( cmd > cmdsEnd )
			throwf("malformed dylb, load command #%d is outside size of load commands in %s", i, pth);
	}

	// figure out if we need to examine dependent dylibs
	// with compressed LINKEDIT format, MH_NO_REEXPORTED_DYLIBS can be trusted
	bool processDependentLibraries = true;
	if  ( compressedLinkEdit && _noRexports && !linkingFlatNamespace) 
		processDependentLibraries = false;
	
	if ( processDependentLibraries ) {
		// pass 2 builds list of all dependent libraries
		_dependentDylibs.reserve(dependentLibCount);
		cmd = cmds;
		unsigned int reExportDylibCount = 0;  
		for (uint32_t i = 0; i < cmd_count; ++i) {
			switch (cmd->cmd()) {
				case LC_LOAD_DYLIB:
				case LC_LOAD_WEAK_DYLIB:
					// with new linkedit format only care about LC_REEXPORT_DYLIB
					if ( compressedLinkEdit && !linkingFlatNamespace ) 
						break;
				case LC_REEXPORT_DYLIB:
					++reExportDylibCount;
					Dependent entry;
					entry.path = strdup(((macho_dylib_command<P>*)cmd)->name());
					entry.dylib = NULL;
					entry.reExport = (cmd->cmd() == LC_REEXPORT_DYLIB);
					if ( (targetInstallPath == NULL) || (strcmp(targetInstallPath, entry.path) != 0) )
						_dependentDylibs.push_back(entry);
					break;
			}
			cmd = (const macho_load_command<P>*)(((char*)cmd)+cmd->cmdsize());
		}
		// verify MH_NO_REEXPORTED_DYLIBS bit was correct
		if ( compressedLinkEdit && !linkingFlatNamespace ) {
			assert(reExportDylibCount != 0);
		}
		// pass 3 add re-export info
		cmd = cmds;
		for (uint32_t i = 0; i < cmd_count; ++i) {
			const char* frameworkLeafName;
			const char* dylibBaseName;
			switch (cmd->cmd()) {
				case LC_SUB_UMBRELLA:
					frameworkLeafName = ((macho_sub_umbrella_command<P>*)cmd)->sub_umbrella();
					for (typename std::vector<Dependent>::iterator it = _dependentDylibs.begin(); it != _dependentDylibs.end(); ++it) {
						const char* dylibName = it->path;
						const char* lastSlash = strrchr(dylibName, '/');
						if ( (lastSlash != NULL) && (strcmp(&lastSlash[1], frameworkLeafName) == 0) )
							it->reExport = true;
					}
					break;
				case LC_SUB_LIBRARY:
					dylibBaseName = ((macho_sub_library_command<P>*)cmd)->sub_library();
					for (typename std::vector<Dependent>::iterator it = _dependentDylibs.begin(); it != _dependentDylibs.end(); ++it) {
						const char* dylibName = it->path;
						const char* lastSlash = strrchr(dylibName, '/');
						const char* leafStart = &lastSlash[1];
						if ( lastSlash == NULL )
							leafStart = dylibName;
						const char* firstDot = strchr(leafStart, '.');
						int len = strlen(leafStart);
						if ( firstDot != NULL )
							len = firstDot - leafStart;
						if ( strncmp(leafStart, dylibBaseName, len) == 0 )
							it->reExport = true;
					}
					break;
			}
			cmd = (const macho_load_command<P>*)(((char*)cmd)+cmd->cmdsize());
		}
	}
	
	// validate minimal load commands
	if ( (_dylibInstallPath == NULL) && ((header->filetype() == MH_DYLIB) || (header->filetype() == MH_DYLIB_STUB)) ) 
		throwf("dylib %s missing LC_ID_DYLIB load command", pth);
	if ( dyldInfo == NULL ) {
		if ( symbolTable == NULL )
			throw "binary missing LC_SYMTAB load command";
		if ( dynamicInfo == NULL )
			throw "binary missing LC_DYSYMTAB load command";
	}
	
	// if linking flat and this is a flat dylib, create one atom that references all imported symbols
	if ( linkingFlatNamespace && linkingMainExecutable && ((header->flags() & MH_TWOLEVEL) == 0) ) {
		std::vector<const char*> importNames;
		importNames.reserve(dynamicInfo->nundefsym());
		const macho_nlist<P>* start = &symbolTable[dynamicInfo->iundefsym()];
		const macho_nlist<P>* end = &start[dynamicInfo->nundefsym()];
		for (const macho_nlist<P>* sym=start; sym < end; ++sym) {
			importNames.push_back(&strings[sym->n_strx()]);
		}
		_importAtom = new ImportAtom<A>(*this, importNames);
	}
	
	// if the dylib is code signed, look for its Designated Requirement
	if ( codeSignature != NULL ) {
		const Security::BlobCore* overallSignature = (Security::BlobCore*)((char*)header + codeSignature->dataoff());
		typedef Security::SuperBlob<Security::kSecCodeMagicEmbeddedSignature> EmbeddedSignatureBlob;
		typedef Security::SuperBlob<Security::kSecCodeMagicRequirementSet> InternalRequirementsBlob;
		const EmbeddedSignatureBlob* signature = EmbeddedSignatureBlob::specific(overallSignature);
		if ( signature->validateBlob(codeSignature->datasize()) ) {
			const InternalRequirementsBlob* ireq = signature->find<InternalRequirementsBlob>(Security::cdRequirementsSlot);
			if ( (ireq != NULL) && ireq->validateBlob() ) {
				const Security::BlobCore* dr = ireq->find(Security::kSecDesignatedRequirementType);
				if ( (dr != NULL) && dr->validateBlob(Security::kSecCodeMagicRequirement) ) {
					// <rdar://problem/10968461> make copy because mapped file is about to be unmapped
					_codeSignatureDR = ::malloc(dr->length());
					::memcpy((void*)_codeSignatureDR, dr, dr->length());
				}
			}
		}
	}
	
	// build hash table
	if ( dyldInfo != NULL ) 
		buildExportHashTableFromExportInfo(dyldInfo, fileContent);
	else
		buildExportHashTableFromSymbolTable(dynamicInfo, symbolTable, strings, fileContent);
	
	// unmap file
	munmap((caddr_t)fileContent, fileLength);
}

//
// Parses number of form X[.Y[.Z]] into a uint32_t where the nibbles are xxxx.yy.zz
//
template <typename A>
uint32_t File<A>::parseVersionNumber32(const char* versionString)
{
	uint32_t x = 0;
	uint32_t y = 0;
	uint32_t z = 0;
	char* end;
	x = strtoul(versionString, &end, 10);
	if ( *end == '.' ) {
		y = strtoul(&end[1], &end, 10);
		if ( *end == '.' ) {
			z = strtoul(&end[1], &end, 10);
		}
	}
	if ( (*end != '\0') || (x > 0xffff) || (y > 0xff) || (z > 0xff) )
		throwf("malformed 32-bit x.y.z version number: %s", versionString);

	return (x << 16) | ( y << 8 ) | z;
}

template <typename A>
void File<A>::buildExportHashTableFromSymbolTable(const macho_dysymtab_command<P>* dynamicInfo, 
												const macho_nlist<P>* symbolTable, const char* strings, 
												const uint8_t* fileContent)
{
	if ( dynamicInfo->tocoff() == 0 ) {
		if ( _s_logHashtable ) fprintf(stderr, "ld: building hashtable of %u toc entries for %s\n", dynamicInfo->nextdefsym(), this->path());
		const macho_nlist<P>* start = &symbolTable[dynamicInfo->iextdefsym()];
		const macho_nlist<P>* end = &start[dynamicInfo->nextdefsym()];
		_atoms.reserve(dynamicInfo->nextdefsym()); // set initial bucket count
		for (const macho_nlist<P>* sym=start; sym < end; ++sym) {
			this->addSymbol(&strings[sym->n_strx()], (sym->n_desc() & N_WEAK_DEF) != 0, false, sym->n_value());
		}
	}
	else {
		int32_t count = dynamicInfo->ntoc();
		_atoms.reserve(count); // set initial bucket count
		if ( _s_logHashtable ) fprintf(stderr, "ld: building hashtable of %u entries for %s\n", count, this->path());
		const struct dylib_table_of_contents* toc = (dylib_table_of_contents*)(fileContent + dynamicInfo->tocoff());
		for (int32_t i = 0; i < count; ++i) {
			const uint32_t index = E::get32(toc[i].symbol_index);
			const macho_nlist<P>* sym = &symbolTable[index];
			this->addSymbol(&strings[sym->n_strx()], (sym->n_desc() & N_WEAK_DEF) != 0, false, sym->n_value());
		}
	}
	
	// special case old libSystem
	if ( (_dylibInstallPath != NULL) && (strcmp(_dylibInstallPath, "/usr/lib/libSystem.B.dylib") == 0) )
		addDyldFastStub();
}


template <typename A>
void File<A>::buildExportHashTableFromExportInfo(const macho_dyld_info_command<P>* dyldInfo, 
																const uint8_t* fileContent)
{
	if ( _s_logHashtable ) fprintf(stderr, "ld: building hashtable from export info in %s\n", this->path());
	if ( dyldInfo->export_size() > 0 ) {
		const uint8_t* start = fileContent + dyldInfo->export_off();
		const uint8_t* end = &start[dyldInfo->export_size()];
		std::vector<mach_o::trie::Entry> list;
		parseTrie(start, end, list);
		for (std::vector<mach_o::trie::Entry>::iterator it=list.begin(); it != list.end(); ++it) 
			this->addSymbol(it->name, 
							it->flags & EXPORT_SYMBOL_FLAGS_WEAK_DEFINITION, 
							(it->flags & EXPORT_SYMBOL_FLAGS_KIND_MASK) == EXPORT_SYMBOL_FLAGS_KIND_THREAD_LOCAL,
							 it->address);
	}
}


template <>
void File<x86_64>::addDyldFastStub()
{
	addSymbol("dyld_stub_binder", false, false, 0);
}

template <>
void File<x86>::addDyldFastStub()
{
	addSymbol("dyld_stub_binder", false, false, 0);
}

template <typename A>
void File<A>::addDyldFastStub()
{
	// do nothing
}

template <typename A>
void File<A>::addSymbol(const char* name, bool weakDef, bool tlv, pint_t address)
{
	//fprintf(stderr, "addSymbol() %s\n", name);
	// symbols that start with $ld$ are meta-data to the static linker
	// <rdar://problem/5182537> need way for ld and dyld to see different exported symbols in a dylib
	if ( strncmp(name, "$ld$", 4) == 0 ) {	
		//    $ld$ <action> $ <condition> $ <symbol-name>
		const char* symAction = &name[4];
		const char* symCond = strchr(symAction, '$');
		if ( symCond != NULL ) {
			char curOSVers[16];
			if ( _macVersionMin != ld::macVersionUnset ) {
				sprintf(curOSVers, "$os%d.%d$", (_macVersionMin >> 16), ((_macVersionMin >> 8) & 0xFF));
			}
			else if ( _iOSVersionMin != ld::iOSVersionUnset ) {
				sprintf(curOSVers, "$os%d.%d$", (_iOSVersionMin >> 16), ((_iOSVersionMin >> 8) & 0xFF));
			}
			else {
				assert(0 && "targeting neither macosx nor iphoneos");
			}
			if ( strncmp(symCond, curOSVers, strlen(curOSVers)) == 0 ) {
				const char* symName = strchr(&symCond[1], '$');
				if ( symName != NULL ) {
					++symName;
					if ( strncmp(symAction, "hide$", 5) == 0 ) {
						if ( _s_logHashtable ) fprintf(stderr, "  adding %s to ignore set for %s\n", symName, this->path());
						_ignoreExports.insert(strdup(symName));
						return;
					}
					else if ( strncmp(symAction, "add$", 4) == 0 ) {
						this->addSymbol(symName, weakDef, false, 0);
						return;
					}
					else if ( strncmp(symAction, "install_name$", 13) == 0 ) {
						_dylibInstallPath = symName;
						_installPathOverride = true;
						// <rdar://problem/14448206> CoreGraphics redirects to ApplicationServices, but with wrong compat version
						if ( strcmp(_dylibInstallPath, "/System/Library/Frameworks/ApplicationServices.framework/Versions/A/ApplicationServices") == 0 )
							_dylibCompatibilityVersion = parseVersionNumber32("1.0");
						return;
					}
					else if ( strncmp(symAction, "compatibility_version$", 22) == 0 ) {
						_dylibCompatibilityVersion = parseVersionNumber32(symName);
						return;
					}
					else {
						warning("bad symbol action: %s in dylib %s", name, this->path());
					}
				}
			}
		}	
		else {
			warning("bad symbol condition: %s in dylib %s", name, this->path());
		}
	}
	
	// add symbol as possible export if we are not supposed to ignore it
	if ( _ignoreExports.count(name) == 0 ) {
		AtomAndWeak bucket;
		bucket.atom = NULL;
		bucket.weakDef = weakDef;
		bucket.tlv = tlv;
		bucket.address = address;
		if ( _s_logHashtable ) fprintf(stderr, "  adding %s to hash table for %s\n", name, this->path());
		_atoms[strdup(name)] = bucket;
	}
}


template <typename A>
bool File<A>::forEachAtom(ld::File::AtomHandler& handler) const
{
	handler.doFile(*this);
	// if doing flatnamespace and need all this dylib's imports resolve
	// add atom which references alls undefines in this dylib
	if ( _importAtom != NULL ) {
		handler.doAtom(*_importAtom);
		return true;
	}
	return false;
}

template <typename A>
bool File<A>::hasWeakDefinition(const char* name) const
{
	// if supposed to ignore this export, then pretend I don't have it
	if ( _ignoreExports.count(name) != 0 )
		return false;
		
	typename NameToAtomMap::const_iterator pos = _atoms.find(name);
	if ( pos != _atoms.end() ) {
		return pos->second.weakDef;
	}
	else {
		// look in children that I re-export
		for (typename std::vector<Dependent>::const_iterator it = _dependentDylibs.begin(); it != _dependentDylibs.end(); ++it) {
			if ( it->reExport ) {
				//fprintf(stderr, "getJustInTimeAtomsFor: %s NOT found in %s, looking in child %s\n", name, this->path(), (*it)->getInstallPath());
				typename NameToAtomMap::iterator cpos = it->dylib->_atoms.find(name);
				if ( cpos != it->dylib->_atoms.end() ) 
					return cpos->second.weakDef;
			}
		}
	}
	return false;
}


// <rdar://problem/5529626> If only weak_import symbols are used, linker should use LD_LOAD_WEAK_DYLIB
template <typename A>
bool File<A>::allSymbolsAreWeakImported() const
{
	bool foundNonWeakImport = false;
	bool foundWeakImport = false;
	//fprintf(stderr, "%s:\n", this->path());
	for (typename NameToAtomMap::const_iterator it = _atoms.begin(); it != _atoms.end(); ++it) {
		const ld::Atom* atom = it->second.atom;
		if ( atom != NULL ) {
			if ( atom->weakImported() )
				foundWeakImport = true;
			else
				foundNonWeakImport = true;
			//fprintf(stderr, "  weak_import=%d, name=%s\n", atom->weakImported(), it->first);
		}
	}
	
	// don't automatically weak link dylib with no imports
	// so at least one weak import symbol and no non-weak-imported symbols must be found
	return foundWeakImport && !foundNonWeakImport;
}


template <typename A>
bool File<A>::containsOrReExports(const char* name, bool* weakDef, bool* tlv, pint_t* defAddress) const
{
	if ( _ignoreExports.count(name) != 0 )
		return false;

	// check myself
	typename NameToAtomMap::iterator pos = _atoms.find(name);
	if ( pos != _atoms.end() ) {
		*weakDef = pos->second.weakDef;
		*tlv = pos->second.tlv;
		*defAddress = pos->second.address;
		return true;
	}
	
	// check dylibs I re-export
	for (typename std::vector<Dependent>::const_iterator it = _dependentDylibs.begin(); it != _dependentDylibs.end(); ++it) {
		if ( it->reExport && !it->dylib->implicitlyLinked() ) {
			if ( it->dylib->containsOrReExports(name, weakDef, tlv, defAddress) )
				return true;
		}
	}
		
	return false;
}


template <typename A>
bool File<A>::justInTimeforEachAtom(const char* name, ld::File::AtomHandler& handler) const
{
	// if supposed to ignore this export, then pretend I don't have it
	if ( _ignoreExports.count(name) != 0 )
		return false;
	
	
	AtomAndWeak bucket;
	if ( this->containsOrReExports(name, &bucket.weakDef, &bucket.tlv, &bucket.address) ) {
		bucket.atom = new ExportAtom<A>(*this, name, bucket.weakDef, bucket.tlv, bucket.address);
		_atoms[name] = bucket;
		_providedAtom = true;
		if ( _s_logHashtable ) fprintf(stderr, "getJustInTimeAtomsFor: %s found in %s\n", name, this->path());
		// call handler with new export atom
		handler.doAtom(*bucket.atom);
		return true;
	}
	 
	return false;
}



template <typename A>
bool File<A>::isPublicLocation(const char* pth)
{
	// -no_implicit_dylibs disables this optimization
	if ( ! _implicitlyLinkPublicDylibs )
		return false;
	
	// /usr/lib is a public location
	if ( (strncmp(pth, "/usr/lib/", 9) == 0) && (strchr(&pth[9], '/') == NULL) )
		return true;

	// /System/Library/Frameworks/ is a public location
	if ( strncmp(pth, "/System/Library/Frameworks/", 27) == 0 ) {
		const char* frameworkDot = strchr(&pth[27], '.');
		// but only top level framework
		// /System/Library/Frameworks/Foo.framework/Versions/A/Foo                 ==> true
		// /System/Library/Frameworks/Foo.framework/Resources/libBar.dylib         ==> false
		// /System/Library/Frameworks/Foo.framework/Frameworks/Bar.framework/Bar   ==> false
		// /System/Library/Frameworks/Foo.framework/Frameworks/Xfoo.framework/XFoo ==> false
		if ( frameworkDot != NULL ) {
			int frameworkNameLen = frameworkDot - &pth[27];
			if ( strncmp(&pth[strlen(pth)-frameworkNameLen-1], &pth[26], frameworkNameLen+1) == 0 )
				return true;
		}
	}
		
	return false;
}

template <typename A>
void File<A>::processIndirectLibraries(ld::dylib::File::DylibHandler* handler, bool addImplicitDylibs)
{
	// only do this once
	if ( _indirectDylibsProcessed )
		return;
	const static bool log = false;
	if ( log ) fprintf(stderr, "processIndirectLibraries(%s)\n", this->installPath());
	if ( _linkingFlat ) {
		for (typename std::vector<Dependent>::iterator it = _dependentDylibs.begin(); it != _dependentDylibs.end(); it++) {
			it->dylib = (File<A>*)handler->findDylib(it->path, this->path());
		}
	}
	else if ( _noRexports ) {
		// MH_NO_REEXPORTED_DYLIBS bit set, then nothing to do
	}
	else {
		// two-level, might have re-exports
		for (typename std::vector<Dependent>::iterator it = _dependentDylibs.begin(); it != _dependentDylibs.end(); it++) {
			if ( it->reExport ) {
				if ( log ) fprintf(stderr, "processIndirectLibraries() parent=%s, child=%s\n", this->installPath(), it->path);
				// a LC_REEXPORT_DYLIB, LC_SUB_UMBRELLA or LC_SUB_LIBRARY says we re-export this child
				it->dylib = (File<A>*)handler->findDylib(it->path, this->path());
				if ( it->dylib->hasPublicInstallName() && !it->dylib->wrongOS() ) {
					// promote this child to be automatically added as a direct dependent if this already is
					if ( (this->explicitlyLinked() || this->implicitlyLinked()) && (strcmp(it->path,it->dylib->installPath()) == 0) ) {
						if ( log ) fprintf(stderr, "processIndirectLibraries() implicitly linking %s\n", it->dylib->installPath());
						it->dylib->setImplicitlyLinked();
					}
					else if ( it->dylib->explicitlyLinked() || it->dylib->implicitlyLinked() ) {
						if ( log ) fprintf(stderr, "processIndirectLibraries() parent is not directly linked, but child is, so no need to re-export child\n");
					}
					else {
						if ( log ) fprintf(stderr, "processIndirectLibraries() parent is not directly linked, so parent=%s will re-export child=%s\n", this->installPath(), it->path);
					}
				}
				else {
					// add all child's symbols to me
					if ( log ) fprintf(stderr, "processIndirectLibraries() child is not public, so parent=%s will re-export child=%s\n", this->installPath(), it->path);
				}
			}
			else if ( !_explictReExportFound ) {
				// see if child contains LC_SUB_FRAMEWORK with my name
				it->dylib = (File<A>*)handler->findDylib(it->path, this->path());
				const char* parentUmbrellaName = it->dylib->parentUmbrella();
				if ( parentUmbrellaName != NULL ) {
					const char* parentName = this->path();
					const char* lastSlash = strrchr(parentName, '/');
					if ( (lastSlash != NULL) && (strcmp(&lastSlash[1], parentUmbrellaName) == 0) ) {
						// add all child's symbols to me
						it->reExport = true;
						if ( log ) fprintf(stderr, "processIndirectLibraries() umbrella=%s will re-export child=%s\n", this->installPath(), it->path);
					}
				}
			}
		}
	}
	
	// check for re-export cycles
	ReExportChain chain;
	chain.prev = NULL;
	chain.file = this;
	this->assertNoReExportCycles(&chain);
	
	_indirectDylibsProcessed = true;
}

template <typename A>
void File<A>::assertNoReExportCycles(ReExportChain* prev)
{
	// recursively check my re-exported dylibs
	ReExportChain chain;
	chain.prev = prev;
	chain.file = this;
	for (typename std::vector<Dependent>::iterator it = _dependentDylibs.begin(); it != _dependentDylibs.end(); it++) {
		if ( it->reExport ) {
			ld::File* child = it->dylib;
			// check child is not already in chain 
			for (ReExportChain* p = prev; p != NULL; p = p->prev) {
				if ( p->file == child ) {
					throwf("cycle in dylib re-exports with %s and %s", child->path(), this->path());
				}
			}
			if ( it->dylib != NULL )
				it->dylib->assertNoReExportCycles(&chain);
		}
	}
}


template <typename A>
class Parser 
{
public:
	typedef typename A::P					P;

	static bool										validFile(const uint8_t* fileContent, bool executableOrDyliborBundle);
	static const char*								fileKind(const uint8_t* fileContent);
	static ld::dylib::File*							parse(const uint8_t* fileContent, uint64_t fileLength, 
															const char* path, time_t mTime, 
															ld::File::Ordinal ordinal, const Options& opts, bool indirectDylib) {
															return new File<A>(fileContent, fileLength, path, mTime,
																			ordinal, opts.flatNamespace(), 
																			opts.linkingMainExecutable(),
																			opts.implicitlyLinkIndirectPublicDylibs(), 
																			opts.macosxVersionMin(), 
																			opts.iOSVersionMin(),
																			opts.allowSimulatorToLinkWithMacOSX(),
																			opts.addVersionLoadCommand(),
																			opts.logAllFiles(), 
																			opts.installPath(),
																			indirectDylib);
														}

};



template <>
bool Parser<x86>::validFile(const uint8_t* fileContent, bool executableOrDyliborBundle)
{
	const macho_header<P>* header = (const macho_header<P>*)fileContent;
	if ( header->magic() != MH_MAGIC )
		return false;
	if ( header->cputype() != CPU_TYPE_I386 )
		return false;
	switch ( header->filetype() ) {
		case MH_DYLIB:
		case MH_DYLIB_STUB:
			return true;
		case MH_BUNDLE:
			if ( executableOrDyliborBundle )
				return true;
			else
				throw "can't link with bundle (MH_BUNDLE) only dylibs (MH_DYLIB)";
		case MH_EXECUTE:
			if ( executableOrDyliborBundle )
				return true;
			else
				throw "can't link with a main executable";
		default:
			return false;
	}
}

template <>
bool Parser<x86_64>::validFile(const uint8_t* fileContent, bool executableOrDyliborBundle)
{
	const macho_header<P>* header = (const macho_header<P>*)fileContent;
	if ( header->magic() != MH_MAGIC_64 )
		return false;
	if ( header->cputype() != CPU_TYPE_X86_64 )
		return false;
	switch ( header->filetype() ) {
		case MH_DYLIB:
		case MH_DYLIB_STUB:
			return true;
		case MH_BUNDLE:
			if ( executableOrDyliborBundle )
				return true;
			else
				throw "can't link with bundle (MH_BUNDLE) only dylibs (MH_DYLIB)";
		case MH_EXECUTE:
			if ( executableOrDyliborBundle )
				return true;
			else
				throw "can't link with a main executable";
		default:
			return false;
	}
}

template <>
bool Parser<arm>::validFile(const uint8_t* fileContent, bool executableOrDyliborBundle)
{
	const macho_header<P>* header = (const macho_header<P>*)fileContent;
	if ( header->magic() != MH_MAGIC )
		return false;
	if ( header->cputype() != CPU_TYPE_ARM )
		return false;
	switch ( header->filetype() ) {
		case MH_DYLIB:
		case MH_DYLIB_STUB:
			return true;
		case MH_BUNDLE:
			if ( executableOrDyliborBundle )
				return true;
			else
				throw "can't link with bundle (MH_BUNDLE) only dylibs (MH_DYLIB)";
		case MH_EXECUTE:
			if ( executableOrDyliborBundle )
				return true;
			else
				throw "can't link with a main executable";
		default:
			return false;
	}
}



template <>
bool Parser<arm64>::validFile(const uint8_t* fileContent, bool executableOrDyliborBundle)
{
	const macho_header<P>* header = (const macho_header<P>*)fileContent;
	if ( header->magic() != MH_MAGIC_64 )
		return false;
	if ( header->cputype() != CPU_TYPE_ARM64 )
		return false;
	switch ( header->filetype() ) {
		case MH_DYLIB:
		case MH_DYLIB_STUB:
			return true;
		case MH_BUNDLE:
			if ( executableOrDyliborBundle )
				return true;
			else
				throw "can't link with bundle (MH_BUNDLE) only dylibs (MH_DYLIB)";
		case MH_EXECUTE:
			if ( executableOrDyliborBundle )
				return true;
			else
				throw "can't link with a main executable";
		default:
			return false;
	}
}


bool isDylibFile(const uint8_t* fileContent, cpu_type_t* result, cpu_subtype_t* subResult)
{
	if ( Parser<x86_64>::validFile(fileContent, false) ) {
		*result = CPU_TYPE_X86_64;
		const macho_header<Pointer64<LittleEndian> >* header = (const macho_header<Pointer64<LittleEndian> >*)fileContent;
		*subResult = header->cpusubtype();
		return true;
	}
	if ( Parser<x86>::validFile(fileContent, false) ) {
		*result = CPU_TYPE_I386;
		*subResult = CPU_SUBTYPE_X86_ALL;
		return true;
	}
	if ( Parser<arm>::validFile(fileContent, false) ) {
		*result = CPU_TYPE_ARM;
		const macho_header<Pointer32<LittleEndian> >* header = (const macho_header<Pointer32<LittleEndian> >*)fileContent;
		*subResult = header->cpusubtype();
		return true;
	}
	if ( Parser<arm64>::validFile(fileContent, false) ) {
		*result = CPU_TYPE_ARM64;
		*subResult = CPU_SUBTYPE_ARM64_ALL;
		return true;
	}
#if 0
/* PORT FIXME (may not even be porting bug) */
	if ( Parser<ppc>::validFile(fileContent, false) ) {
		*result = CPU_TYPE_POWERPC;
		const macho_header<Pointer32<BigEndian> >* header = (const macho_header<Pointer32<BigEndian> >*)fileContent;
		*subResult = header->cpusubtype();
		return true;
	}
	if ( Parser<ppc64>::validFile(fileContent, false) ) {
		*result = CPU_TYPE_POWERPC64;
		*subResult = CPU_SUBTYPE_POWERPC_ALL;
		return true;
	}
#endif
	return false;
}

template <>
const char* Parser<x86>::fileKind(const uint8_t* fileContent)
{
	const macho_header<P>* header = (const macho_header<P>*)fileContent;
	if ( header->magic() != MH_MAGIC )
		return NULL;
	if ( header->cputype() != CPU_TYPE_I386 )
		return NULL;
	return "i386";
}

template <>
const char* Parser<x86_64>::fileKind(const uint8_t* fileContent)
{
	const macho_header<P>* header = (const macho_header<P>*)fileContent;
	if ( header->magic() != MH_MAGIC_64 )
		return NULL;
	if ( header->cputype() != CPU_TYPE_X86_64 )
		return NULL;
	return "x86_64";
}

template <>
const char* Parser<arm>::fileKind(const uint8_t* fileContent)
{
	const macho_header<P>* header = (const macho_header<P>*)fileContent;
	if ( header->magic() != MH_MAGIC )
		return NULL;
	if ( header->cputype() != CPU_TYPE_ARM )
		return NULL;
	for (const ArchInfo* t=archInfoArray; t->archName != NULL; ++t) {
		if ( (t->cpuType == CPU_TYPE_ARM) && ((cpu_subtype_t)header->cpusubtype() == t->cpuSubType) ) {
			return t->archName;
		}
	}
	return "arm???";
}

#if SUPPORT_ARCH_arm64
template <>
const char* Parser<arm64>::fileKind(const uint8_t* fileContent)
{
  const macho_header<P>* header = (const macho_header<P>*)fileContent;
  if ( header->magic() != MH_MAGIC_64 )
    return NULL;
  if ( header->cputype() != CPU_TYPE_ARM64 )
    return NULL;
  return "arm64";
}
#endif

//
// used by linker is error messages to describe mismatched files
//
const char* archName(const uint8_t* fileContent)
{
	if ( Parser<x86_64>::validFile(fileContent, true) ) {
		return Parser<x86_64>::fileKind(fileContent);
	}
	if ( Parser<x86>::validFile(fileContent, true) ) {
		return Parser<x86>::fileKind(fileContent);
	}
	if ( Parser<arm>::validFile(fileContent, true) ) {
		return Parser<arm>::fileKind(fileContent);
	}
#if SUPPORT_ARCH_arm64
	if ( Parser<arm64>::validFile(fileContent, false) ) {
		return Parser<arm64>::fileKind(fileContent);
	}
#endif
	return NULL;
}


//
// main function used by linker to instantiate ld::Files
//
ld::dylib::File* parse(const uint8_t* fileContent, uint64_t fileLength, 
							const char* path, time_t modTime, const Options& opts, ld::File::Ordinal ordinal, 
							bool bundleLoader, bool indirectDylib)
{
	switch ( opts.architecture() ) {
#if SUPPORT_ARCH_x86_64
		case CPU_TYPE_X86_64:
			if ( Parser<x86_64>::validFile(fileContent, bundleLoader) )
				return Parser<x86_64>::parse(fileContent, fileLength, path, modTime, ordinal, opts, indirectDylib);
			break;
#endif
#if SUPPORT_ARCH_i386
		case CPU_TYPE_I386:
			if ( Parser<x86>::validFile(fileContent, bundleLoader) )
				return Parser<x86>::parse(fileContent, fileLength, path, modTime, ordinal, opts, indirectDylib);
			break;
#endif
#if SUPPORT_ARCH_arm_any
		case CPU_TYPE_ARM:
			if ( Parser<arm>::validFile(fileContent, bundleLoader) )
				return Parser<arm>::parse(fileContent, fileLength, path, modTime, ordinal, opts, indirectDylib);
			break;
#endif
#if SUPPORT_ARCH_arm64
		case CPU_TYPE_ARM64:
			if ( Parser<arm64>::validFile(fileContent, bundleLoader) )
				return Parser<arm64>::parse(fileContent, fileLength, path, modTime, ordinal, opts, indirectDylib);
			break;
#endif
	}
	return NULL;
}


}; // namespace dylib
}; // namespace mach_o


