/* -*- mode: C++; c-basic-offset: 4; tab-width: 4 -*-*
 *
 * Copyright (c) 2009-2010 Apple Inc. All rights reserved.
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
 

#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <errno.h>
#include <limits.h>
#include <unistd.h>
#include <mach/mach_time.h>
#include <mach/vm_statistics.h>
#include <mach/mach_init.h>
#include <mach/mach_host.h>
#include <dlfcn.h>
#include <mach-o/dyld.h>
#include <mach-o/fat.h>
#include <dispatch/dispatch.h>

#include <string>
#include <sstream>
#include <map>
#include <set>
#include <string>
#include <vector>
#include <list>
#include <algorithm>
#include <dlfcn.h>
#include <AvailabilityMacros.h>

#include "Options.h"
#include "ld.hpp"
#include "Bitcode.hpp"
#include "InputFiles.h"
#include "Mangling.h"
#include "SymbolTable.h"
#include "Resolver.h"
#include "parsers/lto_file.h"

#include "configure.h"

#define VAL(x) #x
#define STRINGIFY(x) VAL(x)

namespace ld {
namespace tool {


//
// An ExportAtom has no content.  It exists so that the linker can track which imported
// symbols came from which dynamic libraries.
//
class UndefinedProxyAtom : public ld::Atom
{
public:
											UndefinedProxyAtom(const char* nm)
												: ld::Atom(_s_section, ld::Atom::definitionProxy, 
													ld::Atom::combineNever, ld::Atom::scopeLinkageUnit, 
													ld::Atom::typeUnclassified, 
													ld::Atom::symbolTableIn, false, false, false, ld::Atom::Alignment(0)), 
													_name(nm) {}
	// overrides of ld::Atom
	virtual const ld::File*						file() const		{ return NULL; }
	virtual const char*							name() const		{ return _name; }
	virtual uint64_t							size() const		{ return 0; }
	virtual uint64_t							objectAddress() const { return 0; }
	virtual void								copyRawContent(uint8_t buffer[]) const { }
	virtual void								setScope(Scope)		{ }

protected:

	virtual									~UndefinedProxyAtom() {}

	const char*								_name;
	
	static ld::Section						_s_section;
};

ld::Section UndefinedProxyAtom::_s_section("__TEXT", "__import", ld::Section::typeImportProxies, true);




class AliasAtom : public ld::Atom
{
public:
										AliasAtom(const ld::Atom& target, const char* nm) : 
											ld::Atom(target.section(), target.definition(), ld::Atom::combineNever,
													ld::Atom::scopeGlobal, target.contentType(), 
													target.symbolTableInclusion(), target.dontDeadStrip(), 
													target.isThumb(), true, target.alignment()),
											_name(nm), 
											_aliasOf(target),
											_fixup(0, ld::Fixup::k1of1, ld::Fixup::kindNoneFollowOn, &target) { }

	// overrides of ld::Atom
	virtual const ld::File*				file() const		{ return _aliasOf.file(); }
	virtual const char*						translationUnitSource() const
															{ return _aliasOf.translationUnitSource(); }
	virtual const char*					name() const		{ return _name; }
	virtual uint64_t					size() const		{ return 0; }
	virtual uint64_t					objectAddress() const { return _aliasOf.objectAddress(); }
	virtual void						copyRawContent(uint8_t buffer[]) const { }
	virtual const uint8_t*				rawContentPointer() const { return NULL; }
	virtual unsigned long				contentHash(const class ld::IndirectBindingTable& ibt) const 
															{ return _aliasOf.contentHash(ibt);  }
	virtual bool						canCoalesceWith(const ld::Atom& rhs, const class ld::IndirectBindingTable& ibt) const 
															{ return _aliasOf.canCoalesceWith(rhs,ibt); }
	virtual ld::Fixup::iterator				fixupsBegin() const	{ return (ld::Fixup*)&_fixup; }
	virtual ld::Fixup::iterator				fixupsEnd()	const	{ return &((ld::Fixup*)&_fixup)[1]; }
	virtual ld::Atom::UnwindInfo::iterator	beginUnwind() const { return  NULL; }
	virtual ld::Atom::UnwindInfo::iterator	endUnwind() const	{ return NULL; }
	virtual ld::Atom::LineInfo::iterator	beginLineInfo() const { return  NULL; }
	virtual ld::Atom::LineInfo::iterator	endLineInfo() const { return NULL; }

	void									setFinalAliasOf() const {
												(const_cast<AliasAtom*>(this))->setAttributesFromAtom(_aliasOf);
												(const_cast<AliasAtom*>(this))->setScope(ld::Atom::scopeGlobal);
											}
															
private:
	const char*							_name;
	const ld::Atom&						_aliasOf;
	ld::Fixup							_fixup;
};



class SectionBoundaryAtom : public ld::Atom
{
public:
	static SectionBoundaryAtom*			makeSectionBoundaryAtom(const char* name, bool start, const char* segSectName, const Options&	opts);
	static SectionBoundaryAtom*			makeOldSectionBoundaryAtom(const char* name, bool start);
	
	// overrides of ld::Atom
	virtual const ld::File*				file() const		{ return NULL; }
	virtual const char*					name() const		{ return _name; }
	virtual uint64_t					size() const		{ return 0; }
	virtual void						copyRawContent(uint8_t buffer[]) const { }
	virtual const uint8_t*				rawContentPointer() const { return NULL; }
	virtual uint64_t					objectAddress() const { return 0; }
															
private:

										SectionBoundaryAtom(const char* nm, const ld::Section& sect,
															ld::Atom::ContentType cont) : 
											ld::Atom(sect, 
													ld::Atom::definitionRegular, 
													ld::Atom::combineNever,
													ld::Atom::scopeLinkageUnit, 
													cont, 
													ld::Atom::symbolTableNotIn,  
													false, false, true, ld::Atom::Alignment(0)),
											_name(nm) { }

	const char*							_name;
};

SectionBoundaryAtom* SectionBoundaryAtom::makeSectionBoundaryAtom(const char* name, bool start, const char* segSectName, const Options&	opts)
{

	const char* segSectDividor = strrchr(segSectName, '$');
	if ( segSectDividor == NULL )
		throwf("malformed section$ symbol name: %s", name);
	const char* sectionName = segSectDividor + 1;
	int segNameLen = segSectDividor - segSectName;
	if ( segNameLen > 16 )
		throwf("malformed section$ symbol name: %s", name);
	char segName[18];
	strlcpy(segName, segSectName, segNameLen+1);

	ld::Section::Type sectType = ld::Section::typeUnclassified;
	if (!strcmp(segName, "__TEXT") && !strcmp(sectionName, "__thread_starts"))
		sectType = ld::Section::typeThreadStarts;
	else if (!strcmp(segName, "__TEXT") && !strcmp(sectionName, "__chain_starts"))
		sectType = ld::Section::typeChainStarts;
	else if (!strcmp(segName, "__TEXT") && !strcmp(sectionName, "__rebase_info"))
		sectType = ld::Section::typeRebaseRLE;
	else if (!strcmp(segName, "__DATA") && !strcmp(sectionName, "__thread_bss"))
		sectType = ld::Section::typeTLVZeroFill;
	else if (!strcmp(segName, "__DATA") && !strcmp(sectionName, "__thread_data"))
		sectType = ld::Section::typeTLVInitialValues;
	else if (!strcmp(segName, "__DATA") && !strcmp(sectionName, "__thread_vars"))
		sectType = ld::Section::typeTLVDefs;
	else if (!strcmp(segName, "__DATA") && !strcmp(sectionName, "__zerofill")) {
		if ( opts.mergeZeroFill() )
			sectType = ld::Section::typeZeroFill;
		else
			warning("reference to non-existent __zerofill section because -merge_zero_fill_sections option not used");
	}

	const ld::Section* section = new ld::Section(strdup(segName), sectionName, sectType);
	return new SectionBoundaryAtom(name, *section, (start ? ld::Atom::typeSectionStart : typeSectionEnd));
}

SectionBoundaryAtom* SectionBoundaryAtom::makeOldSectionBoundaryAtom(const char* name, bool start)
{
	// e.g. __DATA__bss__begin
	char segName[18];
	strlcpy(segName, name, 7);
	
	char sectName[18];
	int nameLen = strlen(name);
	strlcpy(sectName, &name[6], (start ? nameLen-12 : nameLen-10));
	warning("grandfathering in old symbol '%s' as alias for 'section$%s$%s$%s'", name, start ? "start" : "end", segName, sectName);
	const ld::Section* section = new ld::Section(strdup(segName), strdup(sectName), ld::Section::typeUnclassified);
	return new SectionBoundaryAtom(name, *section, (start ? ld::Atom::typeSectionStart : typeSectionEnd));
}




class SegmentBoundaryAtom : public ld::Atom
{
public:
	static SegmentBoundaryAtom*			makeSegmentBoundaryAtom(const char* name, bool start, const char* segName); 
	static SegmentBoundaryAtom*			makeOldSegmentBoundaryAtom(const char* name, bool start); 
	
	// overrides of ld::Atom
	virtual const ld::File*				file() const		{ return NULL; }
	virtual const char*					name() const		{ return _name; }
	virtual uint64_t					size() const		{ return 0; }
	virtual void						copyRawContent(uint8_t buffer[]) const { }
	virtual const uint8_t*				rawContentPointer() const { return NULL; }
	virtual uint64_t					objectAddress() const { return 0; }
															
private:

										SegmentBoundaryAtom(const char* nm, const ld::Section& sect,
															ld::Atom::ContentType cont) : 
											ld::Atom(sect, 
													ld::Atom::definitionRegular, 
													ld::Atom::combineNever,
													ld::Atom::scopeLinkageUnit, 
													cont, 
													ld::Atom::symbolTableNotIn,  
													false, false, true, ld::Atom::Alignment(0)),
											_name(nm) { }

	const char*							_name;
};

SegmentBoundaryAtom* SegmentBoundaryAtom::makeSegmentBoundaryAtom(const char* name, bool start, const char* segName)
{
	if ( *segName == '\0' )
		throwf("malformed segment$ symbol name: %s", name);
	if ( strlen(segName) > 16 )
		throwf("malformed segment$ symbol name: %s", name);
	
	if ( start ) {
		const ld::Section* section = new ld::Section(segName, "__start", ld::Section::typeFirstSection, true);
		return new SegmentBoundaryAtom(name, *section, ld::Atom::typeSectionStart);
	}
	else {
		const ld::Section* section = new ld::Section(segName, "__end", ld::Section::typeLastSection, true);
		return new SegmentBoundaryAtom(name, *section, ld::Atom::typeSectionEnd);
	}
}

SegmentBoundaryAtom* SegmentBoundaryAtom::makeOldSegmentBoundaryAtom(const char* name, bool start)
{
	// e.g. __DATA__begin
	char temp[18];
	strlcpy(temp, name, 7);
	char* segName = strdup(temp);
	
	warning("grandfathering in old symbol '%s' as alias for 'segment$%s$%s'", name, start ? "start" : "end", segName);

	if ( start ) {
		const ld::Section* section = new ld::Section(segName, "__start", ld::Section::typeFirstSection, true);
		return new SegmentBoundaryAtom(name, *section, ld::Atom::typeSectionStart);
	}
	else {
		const ld::Section* section = new ld::Section(segName, "__end", ld::Section::typeLastSection, true);
		return new SegmentBoundaryAtom(name, *section, ld::Atom::typeSectionEnd);
	}
}

void Resolver::initializeState()
{
	_internal.cpuSubType = _options.subArchitecture();
	
	// In -r mode, look for -linker_option additions
	if ( _options.outputKind() == Options::kObjectFile ) {
		ld::relocatable::File::LinkerOptionsList lo = _options.linkerOptions();
		for (relocatable::File::LinkerOptionsList::const_iterator it=lo.begin(); it != lo.end(); ++it) {
			doLinkerOption(*it, "command line");
		}
	}
#ifdef LD64_VERSION_NUM
	uint32_t packedNum = Options::parseVersionNumber32(STRINGIFY(LD64_VERSION_NUM));
	uint64_t combined = (uint64_t)TOOL_LD << 32 | packedNum;
	_internal.toolsVersions.insert(combined);
#endif
}

void Resolver::buildAtomList()
{
	// each input files contributes initial atoms
	_atoms.reserve(1024);
	_inputFiles.forEachInitialAtom(*this, _internal);
    
	_completedInitialObjectFiles = true;
	
	//_symbolTable.printStatistics();
}


void Resolver::doLinkerOption(const std::vector<const char*>& linkerOption, const char* fileName)
{
	if ( linkerOption.size() == 1 ) {
		const char* lo1 = linkerOption.front();
		if ( strncmp(lo1, "-l", 2) == 0) {
			if (_internal.linkerOptionLibraries.count(&lo1[2]) == 0) {
				_internal.unprocessedLinkerOptionLibraries.insert(&lo1[2]);
			}
		}
		else if ( strncmp(lo1, "-needed-l", 9) == 0) {
			const char* libName = &lo1[9];
			if (_internal.linkerOptionLibraries.count(libName) == 0) {
				_internal.unprocessedLinkerOptionLibraries.insert(libName);
			}
			_internal.linkerOptionNeededLibraries.insert(libName);
		}
		else {
			warning("unknown linker option from object file ignored: '%s' in %s", lo1, fileName);
		}
	}
	else if ( linkerOption.size() == 2 ) {
		const char* lo2a = linkerOption[0];
		const char* lo2b = linkerOption[1];
		if ( strcmp(lo2a, "-framework") == 0) {
			if (_internal.linkerOptionFrameworks.count(lo2b) == 0) {
				_internal.unprocessedLinkerOptionFrameworks.insert(lo2b);
			}
		}
		else if ( strcmp(lo2a, "-needed_framework") == 0 ) {
			if (_internal.linkerOptionFrameworks.count(lo2b) == 0) {
				_internal.unprocessedLinkerOptionFrameworks.insert(lo2b);
			}
			_internal.linkerOptionNeededFrameworks.insert(lo2b);
		}
		else {
			warning("unknown linker option from object file ignored: '%s' '%s' from %s", lo2a, lo2b, fileName);
		}
	}
	else {
		warning("unknown linker option from object file ignored, starting with: '%s' from %s", linkerOption.front(), fileName);
	}
}


void Resolver::doFile(const ld::File& file)
{
	const ld::relocatable::File* objFile = dynamic_cast<const ld::relocatable::File*>(&file);
	const ld::dylib::File* dylibFile = dynamic_cast<const ld::dylib::File*>(&file);

	if ( objFile != NULL ) {
		// if file has linker options, process them
		ld::relocatable::File::LinkerOptionsList* lo = objFile->linkerOptions();
		if ( lo != NULL && !_options.ignoreAutoLink() ) {
			for (relocatable::File::LinkerOptionsList::const_iterator it=lo->begin(); it != lo->end(); ++it) {
				this->doLinkerOption(*it, file.path());
			}
			// <rdar://problem/23053404> process any additional linker-options introduced by this new archive member being loaded
			if ( _completedInitialObjectFiles ) {
				_inputFiles.addLinkerOptionLibraries(_internal, *this);
				_inputFiles.createIndirectDylibs();
			}
		}
		// update which form of ObjC is being used
		if ( objFile->hasObjC() )
			_internal.hasObjC = true;

		// Resolve bitcode section in the object file
		if ( _options.bundleBitcode() ) {
			if ( objFile->getBitcode() == NULL ) {
				// Handle the special case for compiler_rt objects. Add the file to the list to be process.
				if ( objFile->sourceKind() == ld::relocatable::File::kSourceCompilerArchive ) {
					_internal.filesFromCompilerRT.push_back(objFile);
				}
				else if (objFile->sourceKind() != ld::relocatable::File::kSourceLTO  ) {
					// No bitcode section, figure out if the object file comes from LTO/compiler static library
					_options.platforms().forEach(^(ld::Platform platform, uint32_t minVersion, uint32_t sdkVersion, bool &stop) {
						if ( platformInfo(platform).supportsEmbeddedBitcode ) {
							throwf("'%s' does not contain bitcode. "
								   "You must rebuild it with bitcode enabled (Xcode setting ENABLE_BITCODE), obtain an updated library from the vendor, or disable bitcode for this target.", file.path());
						}
						else {
							warning("all bitcode will be dropped because '%s' was built without bitcode. "
									"You must rebuild it with bitcode enabled (Xcode setting ENABLE_BITCODE), obtain an updated library from the vendor, or disable bitcode for this target. ", file.path());
							_internal.filesWithBitcode.clear();
							_internal.dropAllBitcode = true;
						}
					});
				}
			} else {
				// contains bitcode, check if it is just a marker
				if ( objFile->getBitcode()->isMarker() ) {
					// if -bitcode_verify_bundle is used, check if all the object files participate in the linking have full bitcode embedded.
					// error on any marker encountered.
					if ( _options.verifyBitcode() )
						throwf("bitcode bundle could not be generated because '%s' was built without full bitcode. "
							   "All object files and libraries for bitcode must be generated from Xcode Archive or Install build",
							   objFile->path());
					// update the internal state that marker is encountered.
					_internal.embedMarkerOnly = true;
					_internal.filesWithBitcode.clear();
					_internal.dropAllBitcode = true;
				} else if ( !_internal.dropAllBitcode )
					_internal.filesWithBitcode.push_back(objFile);
			}
		}

		// verify that all files have the same class_ro_t pointer signing
		// setting
		if ( objFile->hasObjC() ) {
			if ( _internal.objcClassROPointerSigning == ClassROSigningUnknown ) {
				if (objFile->objcHasSignedClassROs()) {
					_internal.objcClassROPointerSigning = ClassROSigningEnabled;
				} else {
					_internal.objcClassROPointerSigning = ClassROSigningDisabled;
				}
			} else if ((_internal.objcClassROPointerSigning == ClassROSigningDisabled && objFile->objcHasSignedClassROs())) {
				if ( _options.warnOnClassROSigningMismatches() ) {
					warning("'%s' was built with class_ro_t pointer signing enabled, but previous .o files were not", objFile->path());
					_internal.objcClassROPointerSigning = ClassROSigningMismatch;
				} else {
					throwf("'%s' was built with class_ro_t pointer signing enabled, but previous .o files were not", objFile->path());
				}
			} else if ((_internal.objcClassROPointerSigning == ClassROSigningEnabled && !objFile->objcHasSignedClassROs())) {
				if ( _options.warnOnClassROSigningMismatches() ) {
					warning("'%s' was not built with class_ro_t pointer signing enabled, but previous .o files were", objFile->path());
					_internal.objcClassROPointerSigning = ClassROSigningMismatch;
				} else {
					throwf("'%s' was not built with class_ro_t pointer signing enabled, but previous .o files were", objFile->path());
				}
			}
		}

		// verify all files use same version of Swift language
		if ( file.swiftVersion() != 0 ) {
			_internal.someObjectFileHasSwift = true;
			if ( _internal.swiftVersion == 0 ) {
				_internal.swiftVersion = file.swiftVersion();
			}
			else if ( file.swiftVersion() != _internal.swiftVersion ) {
				char fileVersion[64];
				char otherVersion[64];
				Options::userReadableSwiftVersion(file.swiftVersion(), fileVersion);
				Options::userReadableSwiftVersion(_internal.swiftVersion, otherVersion);
				if ( file.swiftVersion() > _internal.swiftVersion ) {
					if ( _options.warnOnSwiftABIVersionMismatches() ) {
						warning("%s compiled with newer version of Swift language (%s) than previous files (%s)",
						        file.path(), fileVersion, otherVersion);
					} else {
						throwf("not all .o files built with same Swift language version. Started with (%s), now found (%s) in",
						       otherVersion, fileVersion);
					}
				}
				else {
					if ( _options.warnOnSwiftABIVersionMismatches() ) {
						warning("%s compiled with older version of Swift language (%s) than previous files (%s)",
						        file.path(), fileVersion, otherVersion);
					} else {
						throwf("not all .o files built with same Swift language version. Started with (%s), now found (%s) in",
						       otherVersion, fileVersion);
					}
				}
			}
		}

		// record minimums swift language version used
		if ( file.swiftLanguageVersion() != 0 ) {
			if ( (_internal.swiftLanguageVersion == 0) || (_internal.swiftLanguageVersion > file.swiftLanguageVersion()) )
				_internal.swiftLanguageVersion = file.swiftLanguageVersion();
		}
		
		// in -r mode, if any .o files have dwarf then add UUID to output .o file
		if ( objFile->debugInfo() == ld::relocatable::File::kDebugInfoDwarf )
			_internal.someObjectFileHasDwarf = true;
			
		// remember if any .o file did not have MH_SUBSECTIONS_VIA_SYMBOLS bit set
		if ( ! objFile->canScatterAtoms() )
			_internal.allObjectFilesScatterable = false;

		// remember if building for profiling (so we don't warn about initializers)
		if ( objFile->hasllvmProfiling() )
			_havellvmProfiling = true;

		// remember if we found .o without platform info
		if ( objFile->platforms().empty() )
			_internal.objectFileFoundWithNoVersion = true;

		// update set of known tools used
		for (const std::pair<uint32_t,uint32_t>& entry : objFile->toolVersions()) {
			uint64_t combined = (uint64_t)entry.first << 32 | entry.second;
			_internal.toolsVersions.insert(combined);
		}

		// update cpu-sub-type
		cpu_subtype_t nextObjectSubType = file.cpuSubType();
		switch ( _options.architecture() ) {
			case CPU_TYPE_ARM:
				if ( _options.subArchitecture() != nextObjectSubType ) {
					if ( (_options.subArchitecture() == CPU_SUBTYPE_ARM_ALL) && _options.forceCpuSubtypeAll() ) {
						// hack to support gcc multillib build that tries to make sub-type-all slice
					}
					else if ( nextObjectSubType == CPU_SUBTYPE_ARM_ALL ) {
						warning("CPU_SUBTYPE_ARM_ALL subtype is deprecated: %s", file.path());
					}
					else if ( _options.allowSubArchitectureMismatches() ) {
						//warning("object file %s was built for different arm sub-type (%d) than link command line (%d)", 
						//	file.path(), nextObjectSubType, _options.subArchitecture());
					}
					else {
						throwf("object file %s was built for different arm sub-type (%d) than link command line (%d)", 
							file.path(), nextObjectSubType, _options.subArchitecture());
					}
				}
				break;

			case CPU_TYPE_I386:
				_internal.cpuSubType = CPU_SUBTYPE_I386_ALL;
				break;
				
			case CPU_TYPE_X86_64:
				if ( _options.subArchitecture() != nextObjectSubType ) {
					// <rdar://problem/47240066> allow x86_64h to link with x86_64 .o files
					if ( (_options.subArchitecture() == CPU_SUBTYPE_X86_64_H) && (nextObjectSubType == CPU_SUBTYPE_X86_64_ALL) )
						break;
					if ( _options.allowSubArchitectureMismatches() ) {
						warning("object file %s was built for different x86_64 sub-type (%d) than link command line (%d)", 
							file.path(), nextObjectSubType, _options.subArchitecture());
					}
					else {
						throwf("object file %s was built for different x86_64 sub-type (%d) than link command line (%d)", 
							file.path(), nextObjectSubType, _options.subArchitecture());
					}
				}
				break;
			case CPU_TYPE_ARM64:
				if (_options.subArchitecture() == CPU_SUBTYPE_ARM64E) {
					if ((file.cpuSubTypeFlags() & 0x80) == 0) {
						warning("object file built with an incompatible arm64e ABI: '%s'", file.path());
						break;
					}
					if (!_internal.hasArm64eABIVersion) {
						_internal.arm64eABIVersion = file.cpuSubTypeFlags();
						_internal.hasArm64eABIVersion = true;
					} else {
						// The compilers that generate ABI versions have not been submitted yet, so only warn about old .o files
						// when we have already seen a new one.
						if ( _internal.arm64eABIVersion != file.cpuSubTypeFlags()) {
							const char* originalVersion = (_internal.arm64eABIVersion & 0x40) ? "kernel" : "user";
							const char* fileVersion     = (file.cpuSubTypeFlags() & 0x40)     ? "kernel" : "user";
							warning("object file was built for different arm64e ABI (%s version %u) than earlier object files (%s version %u): %s",
									fileVersion, (file.cpuSubTypeFlags() & 0x3f), originalVersion, (_internal.arm64eABIVersion & 0x3f), file.path());
						}
					}
				}
				break;
		}
	}
	if ( dylibFile != NULL ) {
		// Check dylib for bitcode, if the library install path is relative path or @rpath, it has to contain bitcode
		if ( _options.bundleBitcode() ) {
			bool isSystemFramework = ( dylibFile->installPath() != NULL ) && ( dylibFile->installPath()[0] == '/' );
			if (!isSystemFramework) {
				// rdar://52804818 The swift dylibs in the SDK do not have absolute installnames in order to support
				// back deployment
				for (const auto& sdkPath : _options.sdkPaths()) {
					char swiftPath[MAXPATHLEN];
					strlcpy(swiftPath, sdkPath, MAXPATHLEN);
					strlcat(swiftPath, "/usr/lib/swift/", MAXPATHLEN);
					if (strncmp(swiftPath, dylibFile->path(), strlen(swiftPath)) == 0) {
						isSystemFramework = true;
						break;
					}
				}
			}
			if ( dylibFile->getBitcode() == NULL && !isSystemFramework ) {
				// Check if the dylib is from toolchain by checking the path
				char tcLibPath[PATH_MAX];
				char ldPath[PATH_MAX];
				char tempPath[PATH_MAX];
				uint32_t bufSize = PATH_MAX;
				// toolchain library path should pointed to *.xctoolchain/usr/lib
				if ( _NSGetExecutablePath(ldPath, &bufSize) != -1 ) {
					if ( realpath(ldPath, tempPath) != NULL ) {
						char* lastSlash = strrchr(tempPath, '/');
						if ( lastSlash != NULL )
							strcpy(lastSlash, "/../lib");
					}
				}
				// Compare toolchain library path to the dylib path
				if ( realpath(tempPath, tcLibPath) == NULL ||
					 realpath(dylibFile->path(), tempPath) == NULL ||
					 strncmp(tcLibPath, tempPath, strlen(tcLibPath)) != 0 ) {
					_options.platforms().forEach(^(ld::Platform platform, uint32_t minVersion, uint32_t sdkVersion, bool &stop) {
						if ( platformInfo(platform).supportsEmbeddedBitcode ) {
							throwf("'%s' does not contain bitcode. "
								   "You must rebuild it with bitcode enabled (Xcode setting ENABLE_BITCODE), obtain an updated library from the vendor, or disable bitcode for this target.", file.path());
						}
						else {
							warning("all bitcode will be dropped because '%s' was built without bitcode. "
									"You must rebuild it with bitcode enabled (Xcode setting ENABLE_BITCODE), obtain an updated library from the vendor, or disable bitcode for this target.", file.path());
							_internal.filesWithBitcode.clear();
							_internal.dropAllBitcode = true;
						}
					});
				}
			}
			// Error on bitcode marker in non-system frameworks if -bitcode_verify is used
			if ( _options.verifyBitcode() && !isSystemFramework &&
				 dylibFile->getBitcode() != NULL && dylibFile->getBitcode()->isMarker() )
				throwf("bitcode bundle could not be generated because '%s' was built without full bitcode. "
					   "All frameworks and dylibs for bitcode must be generated from Xcode Archive or Install build",
					   dylibFile->path());
		}

		// Don't allow swift frameworks to link other swift frameworks.
		if ( !_internal.firstSwiftDylibFile && _options.outputKind() == Options::kDynamicLibrary
			&& file.swiftVersion() != 0 && getenv("LD_DISALLOW_SWIFT_LINKING_SWIFT")) {
			// Check that we aren't a whitelisted path.
			bool inWhiteList = false;
			const char *whitelistedPaths[] = { "/System/Library/PrivateFrameworks/Swift" };
			for (auto whitelistedPath : whitelistedPaths) {
				if (!strncmp(whitelistedPath, dylibFile->installPath(), strlen(whitelistedPath))) {
					inWhiteList = true;
					break;
				}
			}
			if (!inWhiteList) {
				_internal.firstSwiftDylibFile = dylibFile;
			}
		}

		// <rdar://problem/25680358> verify dylibs use same version of Swift language
		if ( file.swiftVersion() != 0 ) {
			if ( (_internal.swiftVersion != 0) && (file.swiftVersion() != _internal.swiftVersion) ) {
				char fileVersion[64];
				char otherVersion[64];
				Options::userReadableSwiftVersion(file.swiftVersion(), fileVersion);
				Options::userReadableSwiftVersion(_internal.swiftVersion, otherVersion);
				if ( file.swiftVersion() > _internal.swiftVersion ) {
					if ( _options.warnOnSwiftABIVersionMismatches() ) {
						warning("%s compiled with newer version of Swift language (%s) than previous files (%s)",
						        file.path(), fileVersion, otherVersion);
					} else {
						throwf("%s compiled with newer version of Swift language (%s) than previous files (%s)",
						       file.path(), fileVersion, otherVersion);
					}
				}
				else {
					if ( _options.warnOnSwiftABIVersionMismatches() ) {
						warning("%s compiled with older version of Swift language (%s) than previous files (%s)",
						        file.path(), fileVersion, otherVersion);
					} else {
						throwf("%s compiled with older version of Swift language (%s) than previous files (%s)",
						       file.path(), fileVersion, otherVersion);
					}
				}
			}
		}

		const char* depInstallName = dylibFile->installPath();
		// <rdar://problem/17229513> embedded frameworks are only supported on iOS 8 and later
		if ( (depInstallName != NULL) && (depInstallName[0] != '/') ) {
			if ( _options.platforms().contains(ld::Platform::iOS) && !_options.platforms().minOS(iOS_8_0) ) {
				// <rdar://problem/17598404> only warn about linking against embedded dylib if it is built for iOS 8 or later
				if ( dylibFile->platforms().minOS(ld::iOS_8_0) )
					throwf("embedded dylibs/frameworks are only supported on iOS 8.0 and later (%s)", depInstallName);
			}
		}
		if ( _options.sharedRegionEligible() ) {
			assert(depInstallName != NULL);
			if ( depInstallName[0] == '@' ) {
				warning("invalid -install_name (%s) in dependent dylib (%s). Dylibs/frameworks which might go in dyld shared cache "
						"cannot link with dylib that uses @rpath, @loader_path, etc.", depInstallName, dylibFile->path());
			}
			else if ( !_options.sharedCacheEligiblePath(depInstallName) ) {
				warning("invalid -install_name (%s) in dependent dylib (%s). Dylibs/frameworks which might go in dyld shared cache "
						"cannot link with dylibs that won't be in the shared cache", depInstallName, dylibFile->path());
			}
		}
	}
}


void Resolver::doAtom(const ld::Atom& atom)
{
	//fprintf(stderr, "Resolver::doAtom(%p), name=%s, sect=%s, scope=%d\n", &atom, atom.name(), atom.section().sectionName(), atom.scope());
	if ( _ltoCodeGenFinished && (atom.contentType() == ld::Atom::typeLTOtemporary) && (atom.scope() != ld::Atom::scopeTranslationUnit) )
		warning("'%s' is implemented in bitcode, but it was loaded too late", atom.name());

	// If asked to do so, drop any atoms from three sections that store reflection metadata from the Swift compiler.
	if ( _options.removeSwiftReflectionMetadataSections() ) {
		if ( strcmp(atom.section().sectionName(), "__swift5_reflstr") == 0 )
			return;
		if ( strcmp(atom.section().sectionName(), "__swift5_fieldmd") == 0 )
			return;
		if ( strcmp(atom.section().sectionName(), "__swift5_assocty") == 0 )
			return;
	}

	// add to list of known atoms
	_atoms.push_back(&atom);
	
	// adjust scope
	if ( _options.hasExportRestrictList() || _options.hasReExportList() ) {
		const char* name = atom.name();
		switch ( atom.scope() ) {
			case ld::Atom::scopeTranslationUnit:
				break;
			case ld::Atom::scopeLinkageUnit:
				if ( _options.hasExportMaskList() && _options.shouldExport(name) ) {
					// <rdar://problem/5062685> ld does not report error when -r is used and exported symbols are not defined.
					if ( _options.outputKind() == Options::kObjectFile ) 
						throwf("cannot export hidden symbol %s", name);
					// .objc_class_name_* symbols are special 
					if ( atom.section().type() != ld::Section::typeObjC1Classes ) {
						if ( atom.definition() == ld::Atom::definitionProxy ) {
							// .exp file says to export a symbol, but that symbol is in some dylib being linked
							if ( _options.canReExportSymbols() ) {
								// marking proxy atom as global triggers the re-export
								(const_cast<ld::Atom*>(&atom))->setScope(ld::Atom::scopeGlobal);
							}
							else if ( _options.outputKind() == Options::kDynamicLibrary ) {
								if ( atom.file() != NULL )
									warning("target OS does not support re-exporting symbol %s from %s\n", _options.demangleSymbol(name), atom.safeFilePath());
								else
									warning("target OS does not support re-exporting symbol %s\n", _options.demangleSymbol(name));
							}
						}
						else {
							if ( atom.file() != NULL )
								warning("cannot export hidden symbol %s from %s", _options.demangleSymbol(name), atom.safeFilePath());
							else
								warning("cannot export hidden symbol %s", _options.demangleSymbol(name));
						}
					}
				}
				else if ( _options.shouldReExport(name) && _options.canReExportSymbols() ) {
					if ( atom.definition() == ld::Atom::definitionProxy ) {
						// marking proxy atom as global triggers the re-export
						(const_cast<ld::Atom*>(&atom))->setScope(ld::Atom::scopeGlobal);
					}
					else {
						throwf("requested re-export symbol %s is not from a dylib, but from %s\n", _options.demangleSymbol(name), atom.safeFilePath());
					}
				}
				break;
			case ld::Atom::scopeGlobal:
				// check for globals that are downgraded to hidden
				if ( ! _options.shouldExport(name) ) {
					(const_cast<ld::Atom*>(&atom))->setScope(ld::Atom::scopeLinkageUnit);
					//fprintf(stderr, "demote %s to hidden\n", name);
				}
				if ( _options.canReExportSymbols() && _options.shouldReExport(name) && (atom.definition() != ld::Atom::definitionProxy) ) {
					throwf("requested re-export symbol %s is not from a dylib, but from %s\n", _options.demangleSymbol(name), atom.safeFilePath());
				}
				break;
		}
	}

	// work around for kernel that uses 'l' labels in assembly code
	if ( (atom.symbolTableInclusion() == ld::Atom::symbolTableNotInFinalLinkedImages) 
			&& (atom.name()[0] == 'l') && (_options.outputKind() == Options::kStaticExecutable) 
			&& (strncmp(atom.name(), "ltmp", 4) != 0) )
		(const_cast<ld::Atom*>(&atom))->setSymbolTableInclusion(ld::Atom::symbolTableIn);


	// tell symbol table about non-static atoms
	if ( atom.scope() != ld::Atom::scopeTranslationUnit ) {
		Options::Treatment duplicates = Options::Treatment::kError;
		if (_options.deadCodeStrip() ) {
			if ( _options.allowDeadDuplicates() )
				duplicates = Options::Treatment::kSuppress;
			else if ( _completedInitialObjectFiles )
				duplicates = Options::Treatment::kWarning;
		}
		_symbolTable.add(atom, duplicates);
		
		// add symbol aliases defined on the command line
		if ( _options.haveCmdLineAliases() ) {
			const std::vector<Options::AliasPair>& aliases = _options.cmdLineAliases();
			for (std::vector<Options::AliasPair>::const_iterator it=aliases.begin(); it != aliases.end(); ++it) {
				if ( strcmp(it->realName, atom.name()) == 0 ) {
					if ( strcmp(it->realName, it->alias) == 0 ) {
						warning("ignoring alias of itself '%s'", it->realName);
					}
					else {
						const AliasAtom* alias = new AliasAtom(atom, it->alias);
						_aliasesFromCmdLine.push_back(alias);
						this->doAtom(*alias);
					}
				}
			}
		}
	}

	// convert references by-name or by-content to by-slot
	this->convertReferencesToIndirect(atom);
	
	// remember if any atoms are proxies that require LTO
	if ( atom.contentType() == ld::Atom::typeLTOtemporary )
		_haveLLVMObjs = true;
	
	// remember if any atoms are aliases
	if ( atom.section().type() == ld::Section::typeTempAlias )
		_haveAliases = true;
	
	// error or warn about initializers
	if ( (atom.section().type() == ld::Section::typeInitializerPointers) && !_havellvmProfiling ) {
		switch ( _options.initializersTreatment() ) {
			case Options::kError:
				throwf("static initializer found in '%s'",atom.safeFilePath());
			case Options::kWarning:
				warning("static initializer found in '%s'. Use -no_inits to make this an error.  Use -no_warn_inits to suppress warning",atom.safeFilePath());
				break;
			default:
				break;
		}
	}
}

bool Resolver::isDtraceProbe(ld::Fixup::Kind kind)
{
	switch (kind) {
		case ld::Fixup::kindStoreX86DtraceCallSiteNop:
		case ld::Fixup::kindStoreX86DtraceIsEnableSiteClear:
		case ld::Fixup::kindStoreARMDtraceCallSiteNop:
		case ld::Fixup::kindStoreARMDtraceIsEnableSiteClear:
		case ld::Fixup::kindStoreARM64DtraceCallSiteNop:
		case ld::Fixup::kindStoreARM64DtraceIsEnableSiteClear:
		case ld::Fixup::kindStoreThumbDtraceCallSiteNop:
		case ld::Fixup::kindStoreThumbDtraceIsEnableSiteClear:
		case ld::Fixup::kindDtraceExtra:
			return true;
		default: 
			break;
	}
	return false;
}

void Resolver::convertReferencesToIndirect(const ld::Atom& atom)
{
	// convert references by-name or by-content to by-slot
	SymbolTable::IndirectBindingSlot slot;
	const ld::Atom* dummy;
	ld::Fixup::iterator end = atom.fixupsEnd();
	for (ld::Fixup::iterator fit=atom.fixupsBegin(); fit != end; ++fit) {
		if ( fit->kind == ld::Fixup::kindLinkerOptimizationHint )
			_internal.someObjectHasOptimizationHints = true;
		switch ( fit->binding ) { 
			case ld::Fixup::bindingByNameUnbound:
				if ( isDtraceProbe(fit->kind) && (_options.outputKind() != Options::kObjectFile ) ) {
					// in final linked images, remove reference
					fit->binding = ld::Fixup::bindingNone;
				}
				else {
					slot = _symbolTable.findSlotForName(fit->u.name);
					fit->binding = ld::Fixup::bindingsIndirectlyBound;
					fit->u.bindingIndex = slot;
				}
				break;
			case ld::Fixup::bindingByContentBound:
				switch ( fit->u.target->combine() ) {
					case ld::Atom::combineNever:
					case ld::Atom::combineByName:
						assert(0 && "wrong combine type for bind by content");
						break;
					case ld::Atom::combineByNameAndContent:
						slot = _symbolTable.findSlotForContent(fit->u.target, &dummy);
						fit->binding = ld::Fixup::bindingsIndirectlyBound;
						fit->u.bindingIndex = slot;
						break;
					case ld::Atom::combineByNameAndReferences:
						slot = _symbolTable.findSlotForReferences(fit->u.target, &dummy);
						fit->binding = ld::Fixup::bindingsIndirectlyBound;
						fit->u.bindingIndex = slot;
						break;
				}
				break;
			case ld::Fixup::bindingNone:
			case ld::Fixup::bindingDirectlyBound:
			case ld::Fixup::bindingsIndirectlyBound:
				break;
		}
	}
}


void Resolver::addInitialUndefines()
{
	// add initial undefines from -u option
	for (Options::UndefinesIterator it=_options.initialUndefinesBegin(); it != _options.initialUndefinesEnd(); ++it) {
		_symbolTable.findSlotForName(*it);
	}
}

void Resolver::resolveCurrentUndefines() {
	std::vector<std::string_view> undefineNames;
	_symbolTable.undefines(undefineNames);
	for (const std::string_view& undefsv : undefineNames) {
		// <rdar://95875374> Don't search libraries for objc_msgSend stubs, they're synthesized.
		if ( undefsv.starts_with("_objc_msgSend$") ) {
			// Synthesize the stubs already if needed, so that they don't appear
			// repeatedly on undefines list.
			if ( _synthesizeObjcMsgSendStubs ) {
				this->doAtom(*new UndefinedProxyAtom(undefsv.data()));
				_needsObjcMsgSendProxy = true;
			}
			continue;
		}

		// load for previous undefine may also have loaded this undefine, so check again
		if ( ! _symbolTable.hasName(undefsv) ) {
			const char* undef = undefsv.data();
			_inputFiles.searchLibraries(undef, true, true, false, *this);
			if ( !_symbolTable.hasName(undefsv) && (_options.outputKind() != Options::kObjectFile) ) {
				if ( undefsv.starts_with("section$") ) {
					if ( undefsv.starts_with("section$start$") ) {
						this->doAtom(*SectionBoundaryAtom::makeSectionBoundaryAtom(undef, true, &undef[14], _options));
					}
					else if ( undefsv.starts_with("section$end$") ) {
						this->doAtom(*SectionBoundaryAtom::makeSectionBoundaryAtom(undef, false, &undef[12], _options));
					}
				}
				else if ( undefsv.starts_with("segment$") ) {
					if ( undefsv.starts_with("segment$start$") ) {
						this->doAtom(*SegmentBoundaryAtom::makeSegmentBoundaryAtom(undef, true, &undef[14]));
					}
					else if ( undefsv.starts_with("segment$end$") ) {
						this->doAtom(*SegmentBoundaryAtom::makeSegmentBoundaryAtom(undef, false, &undef[12]));
					}
				}
				else if ( _options.outputKind() == Options::kPreload ) {
					// for iBoot grandfather in old style section labels
					size_t undefLen = undefsv.size();
					if ( undefsv.ends_with("__begin") ) {
						if ( undefLen > 13 )
							this->doAtom(*SectionBoundaryAtom::makeOldSectionBoundaryAtom(undef, true));
						else
							this->doAtom(*SegmentBoundaryAtom::makeOldSegmentBoundaryAtom(undef, true));
					}
					else if ( undefsv.ends_with("__end") ) {
						if ( undefLen > 11 )
							this->doAtom(*SectionBoundaryAtom::makeOldSectionBoundaryAtom(undef, false));
						else
							this->doAtom(*SegmentBoundaryAtom::makeOldSegmentBoundaryAtom(undef, false));
					}
				}
			}
		}
	}
}

void Resolver::resolveAllUndefines()
{
	// keep resolving undefines and tentative overrides until no more undefines
	// were added in last loop
	unsigned int undefineGenCount = 0xFFFFFFFF;
	while ( undefineGenCount != _symbolTable.updateCount() ) {
		// first resolve all undefines, there can be several iterations needed as
		// more object files are added
		// only then we'll look for overrides of common symbols, this is because
		// commons may not have concrete overrides and in that case we'd be
		// looking for the same overrides many more times
		unsigned int innerUndefineGenCount = 0xFFFFFFFF;
		while ( innerUndefineGenCount != _symbolTable.updateCount() ) {
			innerUndefineGenCount = _symbolTable.updateCount();
			resolveCurrentUndefines();
		}

		undefineGenCount = innerUndefineGenCount;

		// <rdar://problem/5894163> need to search archives for overrides of common symbols
		if ( _symbolTable.hasTentativeDefinitions() ) {
			bool searchDylibs = (_options.commonsMode() == Options::kCommonsOverriddenByDylibs);
			std::vector<std::string_view> tents;
			_symbolTable.tentativeDefs(tents);
			for (const std::string_view& tent : tents) {
				// load for previous tentative may also have loaded this tentative, so check again
				const ld::Atom* curAtom = _symbolTable.atomForName(tent);
				assert(curAtom != NULL);
				if ( curAtom->definition() == ld::Atom::definitionTentative ) {
					_inputFiles.searchLibraries(tent.data(), searchDylibs, true, true, *this);
				}
			}
		}
	}
	
	// Use linker options to resolve any remaining undefined symbols
	if ( !_internal.linkerOptionLibraries.empty() || !_internal.linkerOptionFrameworks.empty() ) {
		std::vector<std::string_view> undefineNames;
		_symbolTable.undefines(undefineNames);
		if ( undefineNames.size() != 0 ) {
			for (const std::string_view& undef : undefineNames) {
				if ( ! _symbolTable.hasName(undef) ) {
					_inputFiles.searchLibraries(undef.data(), true, true, false, *this);
				}
			}
		}
	}

	// rdar://84220322 (linker should synthesize stubs for calls to _objc_msgSend$<blah>)
	if ( _synthesizeObjcMsgSendStubs ) {
		std::vector<std::string_view> undefineNames;
		_symbolTable.undefines(undefineNames);

		for (const std::string_view& undef : undefineNames) {
			if ( undef.starts_with("_objc_msgSend$") ) {
				// make temp proxy so that Resolve phase completes
				this->doAtom(*new UndefinedProxyAtom(undef.data()));
				_needsObjcMsgSendProxy = true;
			}
		}

		// make sure obj_msgSend is available for use by objc_stubs pass
		if ( _needsObjcMsgSendProxy ) {
			if ( !_symbolTable.hasName("_objc_msgSend") ) {
				_inputFiles.searchLibraries("_objc_msgSend", true, false, false, *this);
				if ( _options.undefinedTreatment() == Options::kUndefinedDynamicLookup && !_symbolTable.hasName("_objc_msgSend") ) {
					this->doAtom(*new UndefinedProxyAtom("_objc_msgSend"));
				}
			}
			if ( _symbolTable.hasName("_objc_msgSend") ) {
				SymbolTable::IndirectBindingSlot slot = _symbolTable.findSlotForName("_objc_msgSend");
				_internal.objcMsgSendProxy = _internal.indirectBindingTable[slot];
				_internal.objcMsgSendSlot = slot;
			}
		}
	}

	// create proxies as needed for undefined symbols
	if ( (_options.undefinedTreatment() != Options::kUndefinedError) || (_options.outputKind() == Options::kObjectFile) ) {
		std::vector<std::string_view> undefineNames;
		_symbolTable.undefines(undefineNames);
		for (const std::string_view& undef: undefineNames) {
			// <rdar://problem/14547001> "ld -r -exported_symbol _foo" has wrong error message if _foo is undefined
			bool makeProxy = true;
			if ( (_options.outputKind() == Options::kObjectFile) && _options.hasExportMaskList() && _options.shouldExport(undef.data()) )
				makeProxy = false;
			if ( makeProxy )
				this->doAtom(*new UndefinedProxyAtom(undef.data()));
		}
	}

	// support -U option
	if ( _options.someAllowedUndefines() ) {
		std::vector<std::string_view> undefineNames;
		_symbolTable.undefines(undefineNames);
		for (const std::string_view& undef: undefineNames) {
			if ( _options.allowedUndefined(undef.data()) ) {
				// make proxy
				this->doAtom(*new UndefinedProxyAtom(undef.data()));
			}
		}
	}
	
	// After resolving all the undefs within the linkageUnit, record all the remaining undefs and all the proxies.
	if (_options.bundleBitcode() && _options.hideSymbols())
		_symbolTable.mustPreserveForBitcode(_internal.allUndefProxies);

}


void Resolver::markLive(const ld::Atom& atom, WhyLiveBackChain* previous)
{
	//fprintf(stderr, "markLive(%p) %s\n", &atom, atom.name());
	// if -why_live cares about this symbol, then dump chain
	if ( _printWhyLive ) {
		[[unlikely]]
		if ( (previous->referer != NULL) && _options.printWhyLive(atom.name()) ) {
			fprintf(stderr, "%s from %s\n", atom.name(), atom.safeFilePath());
			int depth = 1;
			for(WhyLiveBackChain* p = previous; p != NULL; p = p->previous, ++depth) {
				for(int i=depth; i > 0; --i)
					fprintf(stderr, "  ");
				fprintf(stderr, "%s from %s\n", p->referer->name(), p->referer->safeFilePath());
			}
		}
	}

	// if already marked live, then done (stop recursion)
	if ( atom.live() )
		return;
		
	// mark this atom is live
	(const_cast<ld::Atom*>(&atom))->setLive();
	
	// mark all atoms it references as live
	WhyLiveBackChain thisChain;
	thisChain.previous = previous;
	thisChain.referer = &atom;
	for (ld::Fixup::iterator fit = atom.fixupsBegin(), end=atom.fixupsEnd(); fit != end; ++fit) {
		const ld::Atom* target;
		switch ( fit->kind ) {
			case ld::Fixup::kindNone:
			case ld::Fixup::kindNoneFollowOn:
			case ld::Fixup::kindNoneGroupSubordinate:
			case ld::Fixup::kindNoneGroupSubordinateFDE:
			case ld::Fixup::kindNoneGroupSubordinateLSDA:
			case ld::Fixup::kindNoneGroupSubordinatePersonality:
			case ld::Fixup::kindSetTargetAddress:
			case ld::Fixup::kindSubtractTargetAddress:
			case ld::Fixup::kindStoreTargetAddressLittleEndian32:
			case ld::Fixup::kindStoreTargetAddressLittleEndian64:
#if SUPPORT_ARCH_arm64e
			case ld::Fixup::kindStoreTargetAddressLittleEndianAuth64:
#endif
			case ld::Fixup::kindStoreTargetAddressBigEndian32:
			case ld::Fixup::kindStoreTargetAddressBigEndian64:
			case ld::Fixup::kindStoreTargetAddressX86PCRel32:
			case ld::Fixup::kindStoreTargetAddressX86BranchPCRel32:
			case ld::Fixup::kindStoreTargetAddressX86PCRel32GOTLoad:
			case ld::Fixup::kindStoreTargetAddressX86PCRel32GOTLoadNowLEA:
			case ld::Fixup::kindStoreTargetAddressX86PCRel32TLVLoad:
			case ld::Fixup::kindStoreTargetAddressX86PCRel32TLVLoadNowLEA:
			case ld::Fixup::kindStoreTargetAddressX86Abs32TLVLoad:
			case ld::Fixup::kindStoreTargetAddressX86Abs32TLVLoadNowLEA:
			case ld::Fixup::kindStoreTargetAddressARMBranch24:
			case ld::Fixup::kindStoreTargetAddressThumbBranch22:
#if SUPPORT_ARCH_arm64
			case ld::Fixup::kindStoreTargetAddressARM64Branch26:
			case ld::Fixup::kindStoreTargetAddressARM64Page21:
			case ld::Fixup::kindStoreTargetAddressARM64GOTLoadPage21:
			case ld::Fixup::kindStoreTargetAddressARM64GOTLeaPage21:
			case ld::Fixup::kindStoreTargetAddressARM64TLVPLoadPage21:
			case ld::Fixup::kindStoreTargetAddressARM64TLVPLoadNowLeaPage21:
#endif
				if ( fit->binding == ld::Fixup::bindingByContentBound ) {
					// normally this was done in convertReferencesToIndirect()
					// but a archive loaded .o file may have a forward reference
					SymbolTable::IndirectBindingSlot slot;
					const ld::Atom* dummy;
					switch ( fit->u.target->combine() ) {
						case ld::Atom::combineNever:
						case ld::Atom::combineByName:
							assert(0 && "wrong combine type for bind by content");
							break;
						case ld::Atom::combineByNameAndContent:
							slot = _symbolTable.findSlotForContent(fit->u.target, &dummy);
							fit->binding = ld::Fixup::bindingsIndirectlyBound;
							fit->u.bindingIndex = slot;
							break;
						case ld::Atom::combineByNameAndReferences:
							slot = _symbolTable.findSlotForReferences(fit->u.target, &dummy);
							fit->binding = ld::Fixup::bindingsIndirectlyBound;
							fit->u.bindingIndex = slot;
							break;
					}
				}
				switch ( fit->binding ) {
					case ld::Fixup::bindingDirectlyBound:
						markLive(*(fit->u.target), &thisChain);
						break;
					case ld::Fixup::bindingByNameUnbound:
						// doAtom() did not convert to indirect in dead-strip mode, so that now
						fit->u.bindingIndex = _symbolTable.findSlotForName(fit->u.name);
						fit->binding = ld::Fixup::bindingsIndirectlyBound;
						// fall into next case
						[[clang::fallthrough]];
					case ld::Fixup::bindingsIndirectlyBound:
						target = _internal.indirectBindingTable[fit->u.bindingIndex];
						if ( target != NULL ) {
							this->markLive(*target, &thisChain);
						}
						break;
					default:
						assert(0 && "bad binding during dead stripping");
				}
				break;
            default:
                break;    
		}
	}

}

class NotLiveLTO {
public:
	bool operator()(const ld::Atom* atom) const {
		if (atom->live() || atom->dontDeadStrip() )
			return false;
		// don't kill combinable atoms in first pass
		switch ( atom->combine() ) {
			case ld::Atom::combineByNameAndContent:
			case ld::Atom::combineByNameAndReferences:
				return false;
			default:
				return true;
		}
	}
};

bool Resolver::atomIsDeadStripRoot(const ld::Atom* atom, bool forceDeadStrip) const {
	// <rdar://problem/57667716> LTO code-gen is done, doing second dead strip pass.
	// Don't use import-atom any more
	if ( forceDeadStrip && (atom->contentType() == ld::Atom::typeLTOtemporary)
			&& (strcmp((atom)->name(), "import-atom") == 0) ) {
		return false;
	}

	// add to set of dead-strip-roots, all symbols that the compiler marks as don't strip
	if ( atom->dontDeadStrip() ) {
		return true;
	}

	if ( atom->scope() == ld::Atom::scopeGlobal ) {
		// <rdar://problem/5524973> -exported_symbols_list that has wildcards and -dead_strip
		// in dylibs, every global atom in initial .o files is a root
		if ( _options.hasWildCardExportRestrictList() || _options.allGlobalsAreDeadStripRoots() ) {
			if ( _options.shouldExport(atom->name()) )
				return true;
		}
	}

	// <rdar://problem/49468634> if doing LTO, mark all libclang_rt* mach-o atoms
	// as live since the backend may suddenly codegen uses of them
	if ( _haveLLVMObjs && !forceDeadStrip && (atom->contentType() !=  ld::Atom::typeLTOtemporary) ) {
		if ( isCompilerSupportLib(atom->safeFilePath()) ) {
			return true;
		}
	}

	return false;
}

// Callback should be a void(*)(const ld::Atom*) function. Using a template here
// allows for the entire `forEachDeadStripRoot` to be inlined along with the callback.
// The root atoms aren't uniqued, the callback might be invoked multiple times
// for the same atom.
template<typename T>
void Resolver::forEachDeadStripRoot(std::vector<const ld::Atom*>& dontDeadStripIfReferencesLive,
									bool force,
									T callback) {
	// add entry point (main) to live roots
	const ld::Atom* entry = this->entryPoint(true);
	if ( entry != NULL )
		callback(entry);
		
	// add -exported_symbols_list, -init, and -u entries to live roots
	for (const char* undefined : _options.initialUndefines()) {
		SymbolTable::IndirectBindingSlot slot = _symbolTable.findSlotForName(undefined);
		if ( _internal.indirectBindingTable[slot] == NULL ) {
			_inputFiles.searchLibraries(undefined, false, true, false, *this);
		}
		if ( _internal.indirectBindingTable[slot] != NULL )
			callback(_internal.indirectBindingTable[slot]);
	}
	
	// this helper is only referenced by synthesize stubs, assume it will be used
	if ( _internal.classicBindingHelper != NULL ) 
		callback(_internal.classicBindingHelper);

	// this helper is only referenced by synthesize stubs, assume it will be used
	if ( _internal.compressedFastBinderProxy != NULL ) 
		callback(_internal.compressedFastBinderProxy);

	// this helper is only referenced by synthesized lazy stubs, assume it will be used
	if ( _internal.lazyBindingHelper != NULL )
		callback(_internal.lazyBindingHelper);

	// this helper is only referenced by synthesized objc stubs
	if ( _internal.objcMsgSendProxy != NULL )
		callback(_internal.objcMsgSendProxy);

	// add all dont-dead-strip atoms as roots
	for (const ld::Atom* atom : _atoms) {
		// skip atoms that were coalesced away, they're definitely no longer live
		if ( atom->coalescedAway() )
			continue;

		if ( atomIsDeadStripRoot(atom, force) ) {
			callback(atom);
		} else if ( atom->dontDeadStripIfReferencesLive() )
			dontDeadStripIfReferencesLive.push_back(atom);
	}
}

static const bool atomHasLiveRef(const ld::Internal& state, const ld::Atom* atom) {
	const ld::Fixup::iterator fixupsEnd = atom->fixupsEnd();
	for (ld::Fixup::iterator fit=atom->fixupsBegin(); fit != fixupsEnd; ++fit) {
		const Atom* target = NULL;
		switch ( fit->binding ) {
			case ld::Fixup::bindingDirectlyBound:
				target = fit->u.target;
				break;
			case ld::Fixup::bindingsIndirectlyBound:
				target = state.indirectBindingTable[fit->u.bindingIndex];
				break;
			default:
				break;
		}
		if ( (target != NULL) && target->live() )
			return true;
	}

	return false;
}

void Resolver::deadStripOptimize(bool force)
{
	// only do this optimization with -dead_strip
	if ( ! _options.deadCodeStrip() )
		return;

	std::vector<const ld::Atom*> dontDeadStripIfReferencesLive;

	if ( force ) {
		// We're in a second run of dead stripping, unset liveness so markLive() will recurse
		for (const ld::Atom* atom : _atoms) {
			(const_cast<ld::Atom*>(atom))->setLive(false);
		}
	}

	// mark all roots as live, and all atoms they reference
	forEachDeadStripRoot(dontDeadStripIfReferencesLive, force, [this](const ld::Atom * atom) {
		WhyLiveBackChain rootChain;
		rootChain.previous = NULL;
		rootChain.referer = atom;
		this->markLive(*atom, &rootChain);
	});
	
	// special case atoms that need to be live if they reference something live
	for (const Atom* liveIfRefLiveAtom : dontDeadStripIfReferencesLive) {
		//fprintf(stderr, "live-if-live atom: %s\n", liveIfRefLiveAtom->name());
		if ( liveIfRefLiveAtom->live() )
			continue;

		if ( atomHasLiveRef(_internal, liveIfRefLiveAtom) ) {
			WhyLiveBackChain rootChain;
			rootChain.previous = NULL;
			rootChain.referer = liveIfRefLiveAtom;
			this->markLive(*liveIfRefLiveAtom, &rootChain);
		}
	}

	// now remove all non-live atoms from _atoms
	const bool log = false;
	if ( log ) {
		fprintf(stderr, "deadStripOptimize() all %ld atoms with liveness:\n", _atoms.size());
		for (const ld::Atom* atom : _atoms) {
			const ld::File* file = atom->file();
			fprintf(stderr, "  live=%d  atom=%p  name=%s from=%s\n", atom->live(), atom, atom->name(),  (file ? file->path() : "<internal>"));
		}
	}
	
	if ( _haveLLVMObjs && !force ) {
		// <rdar://problem/9777977> don't remove combinable atoms, they may come back in lto output
		 auto notLiveIt = std::remove_if(_atoms.begin(), _atoms.end(), NotLiveLTO());
		 // add dead atoms to internal state only when map file was requested
		 if ( _options.generatedMapPath() != nullptr ) {
			 std::copy(notLiveIt, _atoms.end(), std::back_inserter(_internal.deadAtoms));
		 }
		_atoms.erase(notLiveIt, _atoms.end());
		_symbolTable.removeDeadAtoms();
	}
	else {
		 auto notLiveIt = std::remove_if(_atoms.begin(), _atoms.end(), NotLive());
		 // add dead atoms to internal state only when map file was requested
		 if ( _options.generatedMapPath() != nullptr ) {
			 std::copy(notLiveIt, _atoms.end(), std::back_inserter(_internal.deadAtoms));
		 }
		_atoms.erase(notLiveIt, _atoms.end());
	}

	if ( log ) {
		fprintf(stderr, "deadStripOptimize() %ld remaining atoms\n", _atoms.size());
		for (const ld::Atom* atom : _atoms) {
			fprintf(stderr, "  live=%d  atom=%p  name=%s\n", atom->live(), atom, atom->name());
		}
	}
}


// This is called when LTO is used but -dead_strip is not used.
// Some undefines were eliminated by LTO, but others were not.
void Resolver::remainingUndefines(std::vector<std::string_view>& undefs)
{
	// search all atoms for references that are unbound
	for (std::vector<const ld::Atom*>::const_iterator it=_atoms.begin(); it != _atoms.end(); ++it) {
		const ld::Atom* atom = *it;
		for (ld::Fixup::iterator fit=atom->fixupsBegin(); fit != atom->fixupsEnd(); ++fit) {
			switch ( (ld::Fixup::TargetBinding)fit->binding ) {
				case ld::Fixup::bindingByNameUnbound:
					assert(0 && "should not be by-name this late");
					undefs.push_back(fit->u.name);
					break;
				case ld::Fixup::bindingsIndirectlyBound:
					if ( _internal.indirectBindingTable[fit->u.bindingIndex] == NULL ) {
						undefs.push_back(_symbolTable.indirectName(fit->u.bindingIndex));
					}
					break;
				case ld::Fixup::bindingByContentBound:
				case ld::Fixup::bindingNone:
				case ld::Fixup::bindingDirectlyBound:
					break;
			}
		}
	}
	// look for any initial undefines that are still undefined
	for (Options::UndefinesIterator uit=_options.initialUndefinesBegin(); uit != _options.initialUndefinesEnd(); ++uit) {
		if ( ! _symbolTable.hasName(*uit) ) {
			undefs.push_back(*uit);
		}
	}

	std::sort(undefs.begin(), undefs.end());
	undefs.erase(std::unique(undefs.begin(), undefs.end()), undefs.end());

	for (std::string_view undef: undefs) {
		fprintf(stderr, "undef: %.*s\n", (int)undef.size(), undef.data());
	}
}

void Resolver::liveUndefines(std::vector<std::string_view>& undefs)
{
	// search all live atoms for references that are unbound
	for (std::vector<const ld::Atom*>::const_iterator it=_atoms.begin(); it != _atoms.end(); ++it) {
		const ld::Atom* atom = *it;
		if ( ! atom->live() )
			continue;
		for (ld::Fixup::iterator fit=atom->fixupsBegin(); fit != atom->fixupsEnd(); ++fit) {
			switch ( (ld::Fixup::TargetBinding)fit->binding ) {
				case ld::Fixup::bindingByNameUnbound:
					assert(0 && "should not be by-name this late");
					undefs.push_back(fit->u.name);
					break;
				case ld::Fixup::bindingsIndirectlyBound:
					if ( _internal.indirectBindingTable[fit->u.bindingIndex] == NULL ) {
						undefs.push_back(_symbolTable.indirectName(fit->u.bindingIndex));
					}
					break;
				case ld::Fixup::bindingByContentBound:
				case ld::Fixup::bindingNone:
				case ld::Fixup::bindingDirectlyBound:
					break;
			}
		}
	}
	// look for any initial undefines that are still undefined
	for (Options::UndefinesIterator uit=_options.initialUndefinesBegin(); uit != _options.initialUndefinesEnd(); ++uit) {
		if ( ! _symbolTable.hasName(*uit) ) {
			undefs.push_back(*uit);
		}
	}
	
	std::sort(undefs.begin(), undefs.end());
	undefs.erase(std::unique(undefs.begin(), undefs.end()), undefs.end());
}



// <rdar://problem/8252819> warn when .objc_class_name_* symbol missing
class ExportedObjcClass
{
public:
	ExportedObjcClass(const Options& opt) : _options(opt)  {}

	bool operator()(std::string_view name) const {
		if ( name.starts_with(".objc_class_name_") && _options.shouldExport(name.data()) ) {
			warning("ignoring undefined symbol %.*s from -exported_symbols_list", (int)name.size(), name.data());
			return true;
		}
		std::string_view::size_type s = name.find("CLASS_$_");
		if ( s != name.npos ) {
			std::stringstream tempStream;
			tempStream << ".objc_class_name_" << name.substr(s + 8);
			std::string temp = tempStream.str();
			if ( _options.wasRemovedExport(temp.c_str()) ) {
				warning("ignoring undefined symbol %s from -exported_symbols_list", temp.c_str());
				return true;
			}
		}
		return false;
	}
private:
	const Options& _options;
};


// temp hack for undefined aliases
class UndefinedAlias
{
public:
	UndefinedAlias(const Options& opt) : _aliases(opt.cmdLineAliases()) {}

	bool operator()(std::string_view name) const {
		for (const Options::AliasPair& aliasPair : _aliases) {
			if ( aliasPair.realName == name ) {
				warning("undefined base symbol '%.*s' for alias '%s'", (int)name.size(), name.data(), aliasPair.alias);
				return true;
			}
		}
		return false;
	}
private:
	const std::vector<Options::AliasPair>&	_aliases;
};



static const char* pathLeafName(const char* path)
{
	const char* shortPath = strrchr(path, '/');
	if ( shortPath == NULL )
		return path;
	else
		return &shortPath[1];
}

bool Resolver::printReferencedBy(const char* name, SymbolTable::IndirectBindingSlot slot)
{
	unsigned foundReferenceCount = 0;
	for (std::vector<const ld::Atom*>::const_iterator it=_atoms.begin(); it != _atoms.end(); ++it) {
		const ld::Atom* atom = *it;
		for (ld::Fixup::iterator fit=atom->fixupsBegin(); fit != atom->fixupsEnd(); ++fit) {
			if ( fit->binding == ld::Fixup::bindingsIndirectlyBound ) {
				if ( fit->u.bindingIndex == slot ) {
					if ( atom->contentType() == ld::Atom::typeNonLazyPointer ) {
						const ld::Atom* existingAtom;
						unsigned int nlSlot = _symbolTable.findSlotForReferences(atom, &existingAtom);
						if ( printReferencedBy(name, nlSlot) )
							++foundReferenceCount;
					}
					else if ( atom->contentType() == ld::Atom::typeCFI ) {
						fprintf(stderr, "      Dwarf Exception Unwind Info (__eh_frame) in %s\n", pathLeafName(atom->safeFilePath()));
						++foundReferenceCount;
					}
					else {
						fprintf(stderr, "      %s in %s\n", _options.demangleSymbol(atom->name()), pathLeafName(atom->safeFilePath()));
						++foundReferenceCount;
						break; // if undefined used twice in a function, only show first
					}
				}
			}
		}
		if ( foundReferenceCount > 6 ) {
			fprintf(stderr, "      ...\n");
			break; // only show first six uses of undefined symbol
		}
	}
	return (foundReferenceCount != 0);
}

void Resolver::removeUnusedAliases(std::vector<std::string_view>& unresolvableUndefines)
{
	if ( !_options.haveCmdLineAliases() )
		return;

	UndefinedAlias undefinedAliases(_options);
	auto isUnusedAlias = ^(std::string_view& name) {
		if ( !undefinedAliases(name) )
			return false;

		// Check if this alias has uses
		unsigned int slot = _symbolTable.findSlotForName(name);
		for (std::vector<const ld::Atom*>::const_iterator it=_atoms.begin(); it != _atoms.end(); ++it) {
			const ld::Atom* atom = *it;
			for (ld::Fixup::iterator fit=atom->fixupsBegin(); fit != atom->fixupsEnd(); ++fit) {
				if ( fit->binding == ld::Fixup::bindingsIndirectlyBound ) {
					if ( fit->u.bindingIndex == slot )
						return false;
				}
			}
		}

		return true;
	};

	unresolvableUndefines.erase(std::remove_if(unresolvableUndefines.begin(), unresolvableUndefines.end(),
											   isUnusedAlias), unresolvableUndefines.end());
}

void Resolver::checkUndefines(bool force)
{
	// when using LTO, undefines are checked after bitcode is optimized
	if ( _haveLLVMObjs && !force )
		return;

	// error out on any remaining undefines
	bool doPrint = true;
	bool doError = true;
	switch ( _options.undefinedTreatment() ) {
		case Options::kUndefinedError:
			break;
		case Options::kUndefinedDynamicLookup:
			doError = false;
			break;
		case Options::kUndefinedWarning:
			doError = false;
			break;
		case Options::kUndefinedSuppress:
			doError = false;
			doPrint = false;
			break;
	}
	std::vector<std::string_view> unresolvableUndefines;
	if ( _options.deadCodeStrip() )
		this->liveUndefines(unresolvableUndefines);
    else if( _haveLLVMObjs ) 
		this->remainingUndefines(unresolvableUndefines); // <rdar://problem/10052396> LTO may have eliminated need for some undefines
	else	
		_symbolTable.undefines(unresolvableUndefines);

	// <rdar://problem/8252819> assert when .objc_class_name_* symbol missing
	if ( _options.hasExportMaskList() ) {
		unresolvableUndefines.erase(std::remove_if(unresolvableUndefines.begin(), unresolvableUndefines.end(), ExportedObjcClass(_options)), unresolvableUndefines.end());
	}

	// hack to temporarily make missing aliases a warning
	removeUnusedAliases(unresolvableUndefines);
	
	const int unresolvableCount = unresolvableUndefines.size();
	int unresolvableExportsCount = 0;
	if ( unresolvableCount != 0 ) {
		if ( doPrint ) {
			for (const auto& lib : _internal.missingLinkerOptionLibraries)
				warning("Could not find or use auto-linked library '%s'", lib);
			for (const auto& frm : _internal.missingLinkerOptionFrameworks)
				warning("Could not find or use auto-linked framework '%s'", frm);
			if ( _options.printArchPrefix() )
				fprintf(stderr, "Undefined symbols for architecture %s:\n", _options.architectureName());
			else
				fprintf(stderr, "Undefined symbols:\n");
			for (const std::string_view& name : unresolvableUndefines) {
				unsigned int slot = _symbolTable.findSlotForName(name);
				fprintf(stderr, "  \"%s\", referenced from:\n", _options.demangleSymbol(name.data()));
				// scan all atoms for references
				bool foundAtomReference = printReferencedBy(name.data(), slot);
				// scan command line options
				if  ( !foundAtomReference ) {
					// might be from -init command line option
					if ( (_options.initFunctionName() != NULL) && (name == _options.initFunctionName()) ) {
						fprintf(stderr, "     -init command line option\n");
					}
					// or might be from exported symbol option
					else if ( _options.hasExportMaskList() && _options.shouldExport(name.data()) ) {
						fprintf(stderr, "     -exported_symbol[s_list] command line option\n");
					}
					// or might be from re-exported symbol option
					else if ( _options.hasReExportList() && _options.shouldReExport(name.data()) ) {
						fprintf(stderr, "     -reexported_symbols_list command line option\n");
					}
					else if ( (_options.outputKind() == Options::kDynamicExecutable)
							&& (_options.entryName() != NULL) && (name == _options.entryName()) ) {
						fprintf(stderr, "     implicit entry/start for main executable\n");
					}
					else {
						bool isInitialUndefine = false;
						for (Options::UndefinesIterator uit=_options.initialUndefinesBegin(); uit != _options.initialUndefinesEnd(); ++uit) {
							if ( *uit == name ) {
								isInitialUndefine = true;
								break;
							}
						}
						if ( isInitialUndefine )
							fprintf(stderr, "     -u command line option\n");
					}
					++unresolvableExportsCount;
				}

				// be helpful and check for typos
				bool printedStart = false;
				for (const ld::Atom* atom : _symbolTable) {
					if ( (atom != NULL) && (atom->symbolTableInclusion() == ld::Atom::symbolTableIn) && (std::string_view(atom->name()).find(name) != name.npos) ) {
						if ( ! printedStart ) {
							fprintf(stderr, "     (maybe you meant: %s", _options.demangleSymbol(atom->name()));
							printedStart = true;
						}
						else {
							fprintf(stderr, ", %s ", _options.demangleSymbol(atom->name()));
						}
					}
				}
				if ( printedStart )
					fprintf(stderr, ")\n");
				// <rdar://problem/8989530> Add comment to error message when __ZTV symbols are undefined
				if ( name.starts_with("__ZTV") ) {
					fprintf(stderr, "  NOTE: a missing vtable usually means the first non-inline virtual member function has no definition.\n");
				}

				// <rdar://77282026> Diagnose C function references from C++ without extern "C"
				// For example, C++ declaration for function `foo()` has mangled name `__Z3foov`,
				// but when implemented in C it's called `_foo`.
				if ( resemblesMangledCppSymbol(name.data()) ) {
					const char* demangled = demangleSymbol(name.data());

					// `__Z3foov` demangles into `foo()`, so take the substring up unto the
					// opening bracket and add a leading `_` to match the C mangling
					if ( const char* bracket = strstr(demangled, "(") ) {
						const std::string cname = '_' + std::string(demangled, bracket);

						ld::File::AtomSinkHandler handler;

						if ( const ld::Atom* atom = _symbolTable.atomForName(cname.c_str()) ) {
							handler.atoms.push_back(atom);
						} else if ( doError ) {
							// Search all libraries if the potential C function name hasn't been
							// found already in the symbol table.
							// We do this only when `doError` is true, otherwise searching might load
							// additional object files and alter the link.
							_inputFiles.searchLibraries(cname.c_str(), true, true, false, handler);
						}

						if ( !handler.atoms.empty() )  {
							const ld::Atom& firstAtom = *handler.atoms.front();

							fprintf(stderr, "     (found %s in %s, declaration possibly missing extern \"C\")\n",
										firstAtom.name(), firstAtom.safeFilePath());
						}
					}
				}
			}
		}
		if ( doError ) 
			throw "symbol(s) not found";
	}
	
}



void Resolver::checkDylibSymbolCollisions()
{
	for (const ld::Atom* atom: _symbolTable.atoms()) {
		if ( atom == NULL )
			continue;
		if ( atom->scope() == ld::Atom::scopeGlobal ) {
			// <rdar://problem/5048861> No warning about tentative definition conflicting with dylib definition
			// for each tentative definition in symbol table look for dylib that exports same symbol name
			if ( atom->definition() == ld::Atom::definitionTentative ) {
				_inputFiles.searchLibraries(atom->name(), true, false, false, *this);
			}
		}
	}

	// Record any overrides of weak symbols in any linked dylib.
	// First collect dylibs that have *any* weak symbols, and only then process
	// them concurrently. Most dylibs won't have weak symbols so this is generally
	// faster.
	std::vector<ld::dylib::File*> weakDefDylibs;
	for (ld::dylib::File* dylib: _inputFiles.getAllDylibs()) {
		if ( dylib->implicitlyLinked() || dylib->explicitlyLinked() ) {
			if ( dylib->hasWeakExternals() )
				weakDefDylibs.push_back(dylib);
		}
	}

	dispatch_apply(weakDefDylibs.size(), DISPATCH_APPLY_AUTO, ^(size_t index) {
			ld::dylib::File* dylib = weakDefDylibs[index];

			dylib->forEachExportedSymbol(^(const char *symbolName, bool weakDef) {
				if ( !weakDef )
					return;

				if (const ld::Atom* atom = _symbolTable.atomForName(symbolName); atom != nullptr
						&& (atom->scope() == ld::Atom::scopeGlobal)
						&& (atom->definition() == ld::Atom::definitionRegular)
						&& (atom->symbolTableInclusion() == ld::Atom::symbolTableIn))
					(const_cast<ld::Atom*>(atom))->setOverridesDylibsWeakDef();
			});
	});
}


const ld::Atom* Resolver::entryPoint(bool searchArchives)
{
	const char* symbolName = NULL;
	bool makingDylib = false;
	switch ( _options.outputKind() ) {
		case Options::kDynamicExecutable:
		case Options::kStaticExecutable:
		case Options::kDyld:
		case Options::kPreload:
			symbolName = _options.entryName();
			break;
		case Options::kDynamicLibrary:
			symbolName = _options.initFunctionName();
			makingDylib = true;
			break;
		case Options::kObjectFile:
		case Options::kDynamicBundle:
		case Options::kKextBundle:
			return NULL;
			break;
	}
	if ( symbolName != NULL ) {
		SymbolTable::IndirectBindingSlot slot = _symbolTable.findSlotForName(symbolName);
		if ( (_internal.indirectBindingTable[slot] == NULL) && searchArchives ) {
			// <rdar://problem/7043256> ld64 can not find a -e entry point from an archive				
			_inputFiles.searchLibraries(symbolName, false, true, false, *this);
		}
		if ( _internal.indirectBindingTable[slot] == NULL ) {
			if ( strcmp(symbolName, "start") == 0 )
				throwf("entry point (%s) undefined.  Usually in crt1.o", symbolName);
			else
				throwf("entry point (%s) undefined.", symbolName);
		}
		else if ( _internal.indirectBindingTable[slot]->definition() == ld::Atom::definitionProxy ) {
			if ( makingDylib ) 
				throwf("-init function (%s) found in linked dylib, must be in dylib being linked", symbolName);
		}
		return _internal.indirectBindingTable[slot];
	}
	return NULL;
}


bool Resolver::diagnoseAtomsWithUnalignedPointers() const {
	bool anyFound = false;

	const uint64_t pointerSize = (_options.architecture() & CPU_ARCH_ABI64) ? 8 : 4;
	for (const ld::Atom* atom : _atoms) {
		// Skip atoms whose alignment is smaller than the pointer size. A pointer
		// within such an atom may or may not end up aligned depending on the final
		// atom location, so it's ambiguous.
		if ( (1ULL << atom->alignment().powerOf2) < pointerSize ) {
		 // Just continue, MachO parser already warned about insufficient alignment.
			continue;
		}

		const uint64_t alignmentOffset = atom->alignment().modulus;

		bool haveSubtractor = false;
		const ld::Fixup::iterator fixupsEnd = atom->fixupsEnd();
		for (ld::Fixup::iterator fit=atom->fixupsBegin(); fit != fixupsEnd; ++fit) {
			if ( fit->firstInCluster() )
				haveSubtractor = false;
			if ( fit->kind == ld::Fixup::kindSubtractTargetAddress )
				haveSubtractor = true;
			const uint64_t alignmentMod = ((alignmentOffset + fit->offsetInAtom) % pointerSize);
			if ( ( alignmentMod != 0) && !haveSubtractor ) {
				if ( (fit->kind == ld::Fixup::kindStoreTargetAddressLittleEndian64)
#if SUPPORT_ARCH_arm64e
						|| (fit->kind == ld::Fixup::kindStoreTargetAddressLittleEndianAuth64)
#endif
						|| (fit->kind == ld::Fixup::kindStoreLittleEndian64) ) {
					anyFound = true;
				}
			}
		}
	}

	return anyFound;
}


void Resolver::fillInHelpersInInternalState()
{
	// look up well known atoms
	bool needsStubHelper = true;
	switch ( _options.outputKind() ) {
		case Options::kDynamicExecutable:
		case Options::kDynamicLibrary:
		case Options::kDynamicBundle:
			needsStubHelper = true;
			break;
		case Options::kDyld:
		case Options::kKextBundle:
		case Options::kObjectFile:
		case Options::kStaticExecutable:
		case Options::kPreload:
			needsStubHelper = false;
			break;
	}
	
	_internal.classicBindingHelper = NULL;
	// FIXME: What about fMakeThreadedStartsSection?
	if ( needsStubHelper && !_options.makeCompressedDyldInfo() && !_options.makeChainedFixups() ) { 
		// "dyld_stub_binding_helper" comes from .o file, so should already exist in symbol table
		if ( _symbolTable.hasName("dyld_stub_binding_helper") ) {
			SymbolTable::IndirectBindingSlot slot = _symbolTable.findSlotForName("dyld_stub_binding_helper");
			_internal.classicBindingHelper = _internal.indirectBindingTable[slot];
		}
	}
	
	_internal.lazyBindingHelper = NULL;	
	_internal.compressedFastBinderProxy = NULL;
	// FIXME: What about fMakeThreadedStartsSection?
	if ( needsStubHelper && _options.makeCompressedDyldInfo() && !_options.noLazyBinding()) {
		// "dyld_stub_binder" comes from libSystem.dylib so will need to manually resolve
		if ( !_symbolTable.hasName("dyld_stub_binder") ) {
			_inputFiles.searchLibraries("dyld_stub_binder", true, false, false, *this);
		}
		if ( _symbolTable.hasName("dyld_stub_binder") ) {
			SymbolTable::IndirectBindingSlot slot = _symbolTable.findSlotForName("dyld_stub_binder");
			_internal.compressedFastBinderProxy = _internal.indirectBindingTable[slot];
		}
		if ( _internal.compressedFastBinderProxy == NULL ) {
			if ( _options.undefinedTreatment() != Options::kUndefinedError ) {
				// make proxy
				_internal.compressedFastBinderProxy = new UndefinedProxyAtom("dyld_stub_binder");
				this->doAtom(*_internal.compressedFastBinderProxy);
			}
		}
	}
}


void Resolver::fillInInternalState()
{
	// store atoms into their final section
	for (std::vector<const ld::Atom*>::iterator it = _atoms.begin(); it != _atoms.end(); ++it) {
		_internal.addAtom(**it);
	}
	
	// <rdar://problem/7783918> make sure there is a __text section so that codesigning works
	if ( (_options.outputKind() == Options::kDynamicLibrary) || (_options.outputKind() == Options::kDynamicBundle) )
		_internal.getFinalSection(*new ld::Section("__TEXT", "__text", ld::Section::typeCode));

	// Don't allow swift frameworks to link other swift frameworks.
	if ( _internal.someObjectFileHasSwift && _internal.firstSwiftDylibFile != nullptr )
		throwf("linking swift frameworks against other swift frameworks (%s) is not permitted",
			   _internal.firstSwiftDylibFile->path());
}

void Resolver::fillInEntryPoint()
{
	_internal.entryPoint = this->entryPoint(true);
}

void Resolver::syncAliases()
{
	if ( !_haveAliases || (_options.outputKind() == Options::kObjectFile) )
		return;
	
	// Set attributes of alias to match its found target
	for (std::vector<const ld::Atom*>::iterator it = _atoms.begin(); it != _atoms.end(); ++it) {
		const ld::Atom* atom = *it;
		if ( atom->section().type() == ld::Section::typeTempAlias ) {
			assert(atom->fixupsBegin() != atom->fixupsEnd());
			for (ld::Fixup::iterator fit = atom->fixupsBegin(); fit != atom->fixupsEnd(); ++fit) {
				const ld::Atom* target;
				ld::Atom::Scope scope;
				assert(fit->kind == ld::Fixup::kindNoneFollowOn);
				switch ( fit->binding ) {
					case ld::Fixup::bindingByNameUnbound:
						break;
					case ld::Fixup::bindingsIndirectlyBound:
						target = _internal.indirectBindingTable[fit->u.bindingIndex];
						assert(target != NULL);
						scope = atom->scope();
						(const_cast<Atom*>(atom))->setAttributesFromAtom(*target);
						// alias has same attributes as target, except for scope
						(const_cast<Atom*>(atom))->setScope(scope);
						break;
					default:
						assert(0 && "internal error: unexpected alias binding");
				}
			}
		}
	}
}

void Resolver::removeCoalescedAwayAtoms()
{
	const bool log = false;
	if ( log ) {
		fprintf(stderr, "removeCoalescedAwayAtoms() starts with %lu atoms\n", _atoms.size());
	}
	_atoms.erase(std::remove_if(_atoms.begin(), _atoms.end(), AtomCoalescedAway()), _atoms.end());
	if ( log ) {
		fprintf(stderr, "removeCoalescedAwayAtoms() after removing coalesced atoms, %lu remain\n", _atoms.size());
		for (std::vector<const ld::Atom*>::const_iterator it=_atoms.begin(); it != _atoms.end(); ++it) {
			fprintf(stderr, "  atom=%p %s\n", *it, (*it)->name());
		}
	}
}

// Note: this list should come from libLTO.dylib
// It is a list of symbols the backend for libLTO.dylib might generate
// and for statically linked firmware, we need to load the impl from archives
// before running LTO compilation.
static const char* sSoftSymbolNames[] = { "___udivdi3", "___udivsi3", "___divsi3", "___muldi3",
										  "___gtdf2", "___ltdf2",
										   "_memset", "_strcpy",  "_snprintf", "___sanitize_trap" };

void Resolver::linkTimeOptimize()
{
	// only do work here if some llvm obj files where loaded
	if ( ! _haveLLVMObjs )
		return;

#ifdef LTO_SUPPORT
	// when building firmware with LTO, make sure all surprise symbols libLTO might generate are loaded if possible
	switch ( _options.outputKind() ) {
		case Options::kDynamicExecutable:
		case Options::kDynamicLibrary:
		case Options::kDynamicBundle:
		case Options::kObjectFile:
		case Options::kKextBundle:
		case Options::kDyld:
			break;
		case Options::kStaticExecutable:
		case Options::kPreload:
			for (const char* softName : sSoftSymbolNames ) {
				if ( ! _symbolTable.hasName(softName) ) {
					_inputFiles.searchLibraries(softName, false, true, false, *this);
				}
			}
			break;
	}

	// <rdar://problem/15314161> LTO: Symbol multiply defined error should specify exactly where the symbol is found
    _symbolTable.checkDuplicateSymbols();

	// run LLVM lto code-gen
	lto::OptimizeOptions optOpt;
	optOpt.outputFilePath				= _options.outputFilePath();
	optOpt.tmpObjectFilePath			= _options.tempLtoObjectPath();
	optOpt.ltoCachePath					= _options.ltoCachePath();
	optOpt.ltoPruneIntervalOverwrite	= _options.ltoPruneIntervalOverwrite();
	optOpt.ltoPruneInterval				= _options.ltoPruneInterval();
	optOpt.ltoPruneAfter				= _options.ltoPruneAfter();
	optOpt.ltoMaxCacheSize				= _options.ltoMaxCacheSize();
	optOpt.preserveAllGlobals			= _options.allGlobalsAreDeadStripRoots() || _options.hasExportRestrictList();
	optOpt.verbose						= _options.verbose();
	optOpt.saveTemps					= _options.saveTempFiles();
	optOpt.ltoCodegenOnly					= _options.ltoCodegenOnly();
	optOpt.pie							= _options.positionIndependentExecutable();
	optOpt.mainExecutable				= _options.linkingMainExecutable();;
	optOpt.staticExecutable 			= (_options.outputKind() == Options::kStaticExecutable);
	optOpt.preload						= (_options.outputKind() == Options::kPreload);
	optOpt.relocatable					= (_options.outputKind() == Options::kObjectFile);
	optOpt.allowTextRelocs				= _options.allowTextRelocs();
	optOpt.linkerDeadStripping			= _options.deadCodeStrip();
	optOpt.needsUnwindInfoSection		= _options.needsUnwindInfoSection();
	optOpt.keepDwarfUnwind				= _options.keepDwarfUnwind();
	optOpt.verboseOptimizationHints     = _options.verboseOptimizationHints();
	optOpt.armUsesZeroCostExceptions    = _options.armUsesZeroCostExceptions();
	optOpt.simulator					= _options.targetIOSSimulator();
	optOpt.internalSDK					= _options.internalSDK();
	optOpt.avoidMisalignedPointers      = (_options.architecture() & CPU_ARCH_ABI64) && _options.makeChainedFixups() && _options.dyldLoadsOutput();

#if SUPPORT_ARCH_arm64e
	optOpt.supportsAuthenticatedPointers = _options.supportsAuthenticatedPointers();
#endif
	optOpt.bitcodeBundle				= (_options.bundleBitcode() && (_options.bitcodeKind() != Options::kBitcodeMarker));
	optOpt.maxDefaultCommonAlignment	= _options.maxDefaultCommonAlign();
	optOpt.arch							= _options.architecture();
	optOpt.mcpu							= _options.mcpuLTO();
	optOpt.platforms					= _options.platforms();
	optOpt.llvmOptions					= &_options.llvmOptions();
	optOpt.initialUndefines				= &_options.initialUndefines();
	optOpt.keepPrivateExterns			= _options.keepPrivateExterns();
	
	std::vector<const ld::Atom*>		newAtoms;
	std::vector<const char*>			additionalUndefines; 
	if ( ! lto::optimize(_atoms, _internal, optOpt, *this, newAtoms, additionalUndefines) )
		return; // if nothing done
	_ltoCodeGenFinished = true;
	
	// add all newly created atoms to _atoms and update symbol table
	for(std::vector<const ld::Atom*>::iterator it = newAtoms.begin(); it != newAtoms.end(); ++it)
		this->doAtom(**it);

	// some atoms might have been optimized way (marked coalesced), remove them
	this->removeCoalescedAwayAtoms();

	// run through all atoms again and make sure newly codegened atoms have references bound
	for (std::vector<const ld::Atom*>::const_iterator it=_atoms.begin(); it != _atoms.end(); ++it) 
		this->convertReferencesToIndirect(**it);

	// adjust section of any new
	for (std::vector<const AliasAtom*>::const_iterator it=_aliasesFromCmdLine.begin(); it != _aliasesFromCmdLine.end(); ++it) {
		const AliasAtom* aliasAtom = *it;
		// update fields in AliasAtom to match newly constructed mach-o atom
		aliasAtom->setFinalAliasOf();
	}
	
	// <rdar://problem/14609792> add any auto-link libraries requested by LTO output to dylibs to search
	_inputFiles.addLinkerOptionLibraries(_internal, *this);
	_inputFiles.createIndirectDylibs();

	// resolve new undefines (e.g calls to _malloc and _memcpy that llvm compiler conjures up)
	for(std::vector<const char*>::iterator uit = additionalUndefines.begin(); uit != additionalUndefines.end(); ++uit) {
		const char *targetName = *uit;
		// these symbols may or may not already be in linker's symbol table
		if ( ! _symbolTable.hasName(targetName) ) {
			_inputFiles.searchLibraries(targetName, true, true, false, *this);
		}
	}

	// if -dead_strip on command line
	if ( _options.deadCodeStrip() ) {
		// re-compute dead code
		this->deadStripOptimize(true);
	}

	// <rdar://problem/12386559> if -exported_symbols_list on command line, re-force scope
	if ( _options.hasExportMaskList() ) {
		for (std::vector<const ld::Atom*>::const_iterator it=_atoms.begin(); it != _atoms.end(); ++it) {
			const ld::Atom* atom = *it;
			if ( atom->scope() == ld::Atom::scopeGlobal ) {
				if ( !_options.shouldExport(atom->name()) ) {
					(const_cast<ld::Atom*>(atom))->setScope(ld::Atom::scopeLinkageUnit);
				}
			}
		}
	}
	
	if ( _options.outputKind() == Options::kObjectFile ) {
		// if -r mode, add proxies for new undefines (e.g. ___stack_chk_fail)
		this->resolveAllUndefines();
	}
	else {
		// <rdar://problem/33853815> remove undefs from LTO objects that gets optimized away
		std::unordered_set<const ld::Atom*> mustPreserve;
		if ( _internal.classicBindingHelper != NULL )
			mustPreserve.insert(_internal.classicBindingHelper);
		if ( _internal.compressedFastBinderProxy != NULL )
			mustPreserve.insert(_internal.compressedFastBinderProxy);
		if ( _internal.lazyBindingHelper != NULL )
			mustPreserve.insert(_internal.lazyBindingHelper);
		if ( _internal.objcMsgSendProxy == nullptr ) {
			// handle when auto-linking from LTO is only way libobjc.dylib is brought in
			for (const char* undefName : additionalUndefines) {
				if ( strncmp(undefName, "_objc_msgSend$", 14) == 0 ) {
					// make sure obj_msgSend is available for use by objc_stubs pass
					if ( !_symbolTable.hasName("_objc_msgSend") ) {
						_inputFiles.searchLibraries("_objc_msgSend", true, false, false, *this);
						if ( _symbolTable.hasName("_objc_msgSend") ) {
							SymbolTable::IndirectBindingSlot slot = _symbolTable.findSlotForName("_objc_msgSend");
							_internal.objcMsgSendProxy = _internal.indirectBindingTable[slot];
							_internal.objcMsgSendSlot  = slot;
						}
					}
					break;
				}
			}
		}
		if ( _internal.objcMsgSendProxy != nullptr )
			mustPreserve.insert(_internal.objcMsgSendProxy);
		if ( const ld::Atom* entry = this->entryPoint(true) )
			mustPreserve.insert(entry);
		for (Options::UndefinesIterator uit=_options.initialUndefinesBegin(); uit != _options.initialUndefinesEnd(); ++uit) {
			SymbolTable::IndirectBindingSlot slot = _symbolTable.findSlotForName(*uit);
			if ( _internal.indirectBindingTable[slot] != NULL )
				mustPreserve.insert(_internal.indirectBindingTable[slot]);
		}
		_symbolTable.removeDeadUndefs(_atoms, mustPreserve);

		// last chance to check for undefines
		this->resolveAllUndefines();
		this->checkUndefines(true);

		// check new code does not override some dylib
		this->checkDylibSymbolCollisions();

	}
#else
    throwf("no builtin LTO support"); // ld64-port
#endif /* LTO_SUPPORT */
}


void Resolver::tweakWeakness()
{
	// <rdar://problem/7977374> Add command line options to control symbol weak-def bit on exported symbols			
	if ( _options.hasWeakBitTweaks() ) {
		for (std::vector<ld::Internal::FinalSection*>::iterator sit = _internal.sections.begin(); sit != _internal.sections.end(); ++sit) {
			ld::Internal::FinalSection* sect = *sit;
			for (std::vector<const ld::Atom*>::iterator ait = sect->atoms.begin(); ait != sect->atoms.end(); ++ait) {
				const ld::Atom* atom = *ait;
				if ( atom->definition() != ld::Atom::definitionRegular ) 
					continue;
				const char* name = atom->name();
				if ( atom->scope() == ld::Atom::scopeGlobal ) {
					if ( atom->combine() == ld::Atom::combineNever ) {
						if ( _options.forceWeak(name) )
							(const_cast<ld::Atom*>(atom))->setCombine(ld::Atom::combineByName);
					}
					else if ( atom->combine() == ld::Atom::combineByName ) {
						if ( _options.forceNotWeak(name) )
							(const_cast<ld::Atom*>(atom))->setCombine(ld::Atom::combineNever);
					}
				}
				else {
					if ( _options.forceWeakNonWildCard(name) )
						warning("cannot force to be weak, non-external symbol %s", name);
					else if ( _options.forceNotWeakNonWildcard(name) )
						warning("cannot force to be not-weak, non-external symbol %s", name);
				}
			}
		}
	}
}

void Resolver::buildArchivesList()
{
	// Determine which archives were linked and update the internal state.
	_inputFiles.archives(_internal);
}

void Resolver::dumpAtoms() 
{
	fprintf(stderr, "Resolver all atoms:\n");
	for (std::vector<const ld::Atom*>::const_iterator it=_atoms.begin(); it != _atoms.end(); ++it) {
		const ld::Atom* atom = *it;
		fprintf(stderr, "  %p name=%s, def=%d\n", atom, atom->name(), atom->definition());
	}
}


void Resolver::checkChainedFixupsBounds()
{
	// disable chained fixups on 32-bit arch if binary too big
	if ( ! _options.makeChainedFixups() )
		return;

	// On x86_64 implicitly disable chained fixups when there are unaligned pointers,
	// on other platform they're not allowed, if there're any we will detect them
	// when writing the output file.
	if ( _options.architecture() == CPU_TYPE_X86_64 && diagnoseAtomsWithUnalignedPointers() ) {
		warning("disabling chained fixups because of unaligned pointers");
		_internal.cantUseChainedFixups = true;
	}

	if ( _options.architecture() & CPU_ARCH_ABI64 )
		return;
	uint64_t totalSize = 0;
	for (const ld::Atom* atom : _atoms) {
		totalSize += atom->size();
	}
	// <rdar://73537908> TEXT+DATA limited to 64MB, we don't have alignment info here to be conservative and max at 60MB
	bool tooBig = (totalSize > 60*1024*1024);

	// TEMP: disable chained fixups on 32-bit arch if it contains Darwin Test metadata
	bool hasDtMetaData = false;
	for (ld::Internal::FinalSection* sect: _internal.sections) {
		if ( (strcmp(sect->sectionName(), "__dt_tests") == 0) && (strncmp(sect->segmentName(), "__DATA", 6) == 0) )
			hasDtMetaData = true;
	}

	if ( tooBig || hasDtMetaData ) {
		_internal.cantUseChainedFixups = true;
		switch ( _options.outputKind() ) {
			case Options::kDynamicExecutable:
			case Options::kDynamicLibrary:
			case Options::kDynamicBundle:
			case Options::kObjectFile:
			case Options::kDyld:
				if ( tooBig )
					warning("output will be > 64MB, so chained fixups being disabled. Use -no_fixup_chains to silence this warning");
				break;
			case Options::kStaticExecutable:
			case Options::kKextBundle:
			case Options::kPreload:
				throwf("binary is too big to use -fixup_chains");
		}
	}
}

void Resolver::writeDotOutput()
{
	const char* dotOutFilePath = _options.dotOutputFile();
	if ( dotOutFilePath != NULL ) {
		FILE* out = fopen(dotOutFilePath, "w");
		if ( out != NULL ) {
			// print header
			fprintf(out, "digraph dg\n{\n");
			fprintf(out, "\tconcentrate = true;\n");
			fprintf(out, "\trankdir = LR;\n");

			// print each atom as a node
			for (std::vector<const ld::Atom*>::const_iterator it=_atoms.begin(); it != _atoms.end(); ++it) {
				const ld::Atom* atom = *it;
				if ( atom->contentType() != ld::Atom::typeStub ) {
					const char* name = atom->name();
					if ( atom->definition() == ld::Atom::definitionProxy ) {
						fprintf(out, "\taddr%p [ shape = plaintext, label = \"%s\" ];\n", atom, name);
					}
					else if ( atom->contentType() == ld::Atom::typeCString ) {
						char cstring[atom->size()+2];
						atom->copyRawContent((uint8_t*)cstring);
						fprintf(out, "\taddr%p [ label = \"string: '", atom);
						for (const char* s=cstring; *s != '\0'; ++s) {
							if ( !isprint(*s) || *s == '"' )
								fprintf(out, "\\x%02hhx", *s);
							else
								fputc(*s, out);
						}
						fprintf(out, "'\" ];\n");
					}
					else {
						fprintf(out, "\taddr%p [ label = \"%s\" ];\n", atom, name);
					}
				}
			}
			fprintf(out, "\n");

			// print each reference as an edge
			for (std::vector<const ld::Atom*>::const_iterator it=_atoms.begin(); it != _atoms.end(); ++it) {
				const ld::Atom* fromAtom = *it;
				const ld::Atom* targetAtom = NULL;
				if ( fromAtom->contentType() != ld::Atom::typeStub ) {
					Set<const ld::Atom*> seenTargets;
					for (ld::Fixup::iterator fit=fromAtom->fixupsBegin(); fit != fromAtom->fixupsEnd(); ++fit) {
						switch ( fit->binding  ) {
							case ld::Fixup::bindingDirectlyBound:
							// no longer defined case ld::Fixup::bindingByNameBound:
							case ld::Fixup::bindingByContentBound:
								if ( seenTargets.count(fit->u.target) == 0 ) {
									seenTargets.insert(fit->u.target);
									fprintf(out, "\taddr%p -> addr%p;\n", fromAtom, fit->u.target);
								}
								break;
							case ld::Fixup::bindingsIndirectlyBound:
								targetAtom = _internal.indirectBindingTable[fit->u.bindingIndex];
								if ( seenTargets.count(targetAtom) == 0 ) {
									seenTargets.insert(targetAtom);
									fprintf(out, "\taddr%p -> addr%p;\n", fromAtom, targetAtom);
								}
								break;
							case ld::Fixup::bindingNone: // see  referenceTargetAtomName()
							case ld::Fixup::bindingByNameUnbound:
								// ??
								break;
						}
					}
				}
			}
			fprintf(out, "\n");

			// push all imports to bottom of graph
			fprintf(out, "{ rank = same; ");
			for (std::vector<const ld::Atom*>::const_iterator it=_atoms.begin(); it != _atoms.end(); ++it) {
				const ld::Atom* atom = *it;
				if ( atom->contentType() != ld::Atom::typeStub ) {
					if ( atom->definition() == ld::Atom::definitionProxy  ) {
						fprintf(out, "addr%p; ", atom);
					}
				}
			}
			fprintf(out, "};\n ");

			// print footer
			fprintf(out, "}\n");
			fclose(out);
		}
		else {
			warning("could not write dot output file: %s", dotOutFilePath);
		}
	}
}
void Resolver::resolve()
{
	this->initializeState();
	this->buildAtomList();
	this->addInitialUndefines();
	this->fillInHelpersInInternalState();
	this->resolveAllUndefines();
	this->deadStripOptimize();
	this->checkUndefines();
	this->checkDylibSymbolCollisions();
	this->syncAliases();
	this->removeCoalescedAwayAtoms();
	this->fillInEntryPoint();
	this->linkTimeOptimize();
	this->fillInInternalState();
	this->tweakWeakness();
    _symbolTable.checkDuplicateSymbols();
	this->buildArchivesList();
	this->checkChainedFixupsBounds();
	this->writeDotOutput();
}



} // namespace tool 
} // namespace ld 



