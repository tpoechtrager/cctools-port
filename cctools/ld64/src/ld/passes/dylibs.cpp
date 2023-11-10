/* -*- mode: C++; c-basic-offset: 4; tab-width: 4 -*-
 *
 * Copyright (c) 2010 Apple Inc. All rights reserved.
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
#include <dlfcn.h>
#include <mach/machine.h>
#include <sys/types.h>
#include <sys/stat.h>

#include <vector>
#include <map>

#include "ld.hpp"
#include "dylibs.h"

namespace ld {
namespace passes {
namespace dylibs {


class WillBeUsed
{
public:
	bool operator()(ld::dylib::File* dylib) const {
		return dylib->willRemoved();
	}
};

class OptimizedAway
{
public:
	OptimizedAway(const std::set<const ld::Atom*>& oa) : _dead(oa) {}
	bool operator()(const ld::Atom* atom) const {
		return ( _dead.count(atom) != 0 );
	}
private:
	const std::set<const ld::Atom*>& _dead;
};

static bool fileExists(const char* path)
{
	struct stat statBuffer;
	return ( ::stat(path, &statBuffer) == 0 );
}



class SoftLinkAuditAtom : public ld::Atom
{
public:
											SoftLinkAuditAtom(const ld::Atom* replacee, const char* newString)
				: ld::Atom(_s_section, ld::Atom::definitionRegular, ld::Atom::combineNever,
							ld::Atom::scopeTranslationUnit, ld::Atom::typeCString,
							symbolTableIn, true, false, false, ld::Atom::Alignment(1)),
				_file(replacee->file()),
				_name(strdup(replacee->name())),
				_content(strdup(newString)),
				_addr(replacee->objectAddress())
					{ setAttributesFromAtom(*replacee); }

	virtual const ld::File*					file() const					{ return _file; }
	virtual const char*						name() const					{ return _name; }
	virtual uint64_t						size() const					{ return strlen(_content)+1; }
	virtual uint64_t						objectAddress() const 			{ return _addr; }
	virtual void							copyRawContent(uint8_t buffer[]) const { strcpy((char*)buffer, _content); }
	virtual void							setScope(Scope)					{ }
	virtual const uint8_t*					rawContentPointer() const 		{ return (uint8_t*)_content; }

private:
	const ld::File*							_file;
	const char*								_name;
	const char*								_content;
	uint64_t								_addr;

	static ld::Section						_s_section;
};

ld::Section SoftLinkAuditAtom::_s_section("__TEXT", "__dlopen_cstrs", ld::Section::typeNonStdCString);


class CStringAtom : public ld::Atom
{
public:
											CStringAtom(const ld::Atom* replacee, const char* newString)
				: ld::Atom(_s_section, ld::Atom::definitionRegular, ld::Atom::combineNever,
							ld::Atom::scopeTranslationUnit, ld::Atom::typeCString,
							symbolTableIn, true, false, false, ld::Atom::Alignment(1)),
				_file(replacee->file()),
				_name(strdup(replacee->name())),
				_content(strdup(newString)),
				_addr(replacee->objectAddress())
					{ setAttributesFromAtom(*replacee); }

	virtual const ld::File*					file() const					{ return _file; }
	virtual const char*						name() const					{ return _name; }
	virtual uint64_t						size() const					{ return strlen(_content)+1; }
	virtual uint64_t						objectAddress() const 			{ return _addr; }
	virtual void							copyRawContent(uint8_t buffer[]) const { strcpy((char*)buffer, _content); }
	virtual void							setScope(Scope)					{ }

private:
	const ld::File*							_file;
	const char*								_name;
	const char*								_content;
	uint64_t								_addr;

	static ld::Section						_s_section;
};

ld::Section CStringAtom::_s_section("__TEXT", "__cstring", ld::Section::typeCString);

//
// The softlink macros build a function named "<blah>Library" which does a once-only dlopen() of blah.
// We need to find that function and switch the string literal path it uses.
// If the function is not found, or the a string literal matching the path is not found, this function does nothing.
//
static void replaceStringLiteral(ld::Internal& state, const char* path, const char* betterPath, std::set<const ld::Atom*>& deadAtoms)
{
	if ( const char* lastSlash = strrchr(path, '/') ) {
		const char* leafName = lastSlash+1;
		char softlinkHelperFunction[PATH_MAX];
		strlcpy(softlinkHelperFunction, "_", PATH_MAX);
		strlcat(softlinkHelperFunction, leafName, PATH_MAX);
		strlcat(softlinkHelperFunction, "Library", PATH_MAX);
		for (ld::Internal::FinalSection* sect : state.sections) {
			if ( sect->type() != ld::Section::typeCode )
				continue;
			for (const ld::Atom* atom : sect->atoms) {
				if ( strcmp(atom->name(), softlinkHelperFunction) != 0 )
					continue;
				for (ld::Fixup::iterator fit = atom->fixupsBegin(), end=atom->fixupsEnd(); fit != end; ++fit) {
					if ( fit->binding == ld::Fixup::bindingsIndirectlyBound ) {
						const ld::Atom* target = state.indirectBindingTable[fit->u.bindingIndex];
						if ( target->contentType() == ld::Atom::typeCString ) {
							if ( const char* targetStr = (char*)target->rawContentPointer() ) {
								if ( strcmp(targetStr, path) == 0 ) {
									const ld::Atom* betterTarget = new CStringAtom(target, betterPath);
									state.indirectBindingTable[fit->u.bindingIndex] = betterTarget;
									deadAtoms.insert(target);
									state.addAtom(*betterTarget);
								}
							}
						}
					}
				}
			}
		}
	}
}

static void replaceAuditStringLiteral(ld::Internal& state, const char* path, const ld::Atom* betterAtom)
{
	for (ld::Internal::FinalSection* sect : state.sections) {
		if ( strncmp(sect->segmentName(), "__DATA", 6) != 0 )
			continue;
		for (const ld::Atom* atom : sect->atoms) {
			for (ld::Fixup::iterator fit = atom->fixupsBegin(), end=atom->fixupsEnd(); fit != end; ++fit) {
				if ( fit->binding == ld::Fixup::bindingDirectlyBound ) {
					const ld::Atom* target = fit->u.target;
					if ( target->contentType() == ld::Atom::typeCString ) {
						if ( const char* targetStr = (char*)target->rawContentPointer() ) {
							if ( strcmp(targetStr, path) == 0 ) {
								fit->u.target = betterAtom;
							}
						}
					}
				}
			}
		}
	}
}

static void replaceGlueUseOfAuditString(ld::Internal& state, const ld::Atom* orgAuditAtom, const ld::Atom* newAuditAtom)
{
	for (ld::Internal::FinalSection* sect : state.sections) {
		if ( sect->type() != ld::Section::typeCode )
			continue;
		for (const ld::Atom* atom : sect->atoms) {
			for (ld::Fixup::iterator fit = atom->fixupsBegin(), end=atom->fixupsEnd(); fit != end; ++fit) {
				if ( fit->binding == ld::Fixup::bindingDirectlyBound ) {
					if ( fit->u.target == orgAuditAtom ) {
						fit->u.target = newAuditAtom;
					}
				}
			}
		}
	}
}

static void pathAddTbdExtension(char* path)
{
	const std::string_view dylibSuffix = ".dylib";
	if ( std::string_view sv(path); sv.ends_with(dylibSuffix) )
		*(path + sv.size() - dylibSuffix.size()) = '\0';
	strlcat(path, ".tbd", PATH_MAX);
}

static void auditSoftlink(const ld::Atom* atom, unsigned int atomIndex, std::set<const ld::Atom*>& deadAtoms, const Options& opts, const char* path, ld::Internal::FinalSection& sect, ld::Internal& state, const char* str)
{
	for (const char* sdkPath : opts.sdkPaths()) {
		char fullPath[PATH_MAX];
		// check to see if there is a symlink in PrivateFrameworks to Frameworks
		bool symLinkInPF = false;
		if ( strncmp(path, "/System/Library/PrivateFrameworks/", 34) == 0 ) {
			strlcpy(fullPath, sdkPath, PATH_MAX);
			strlcat(fullPath, path, PATH_MAX);
			char* lastSlash = strrchr(fullPath, '/');
			*lastSlash = '\0';
			struct stat statBuffer;
			if ( ::lstat(fullPath, &statBuffer) == 0 ) {
				if ( S_ISLNK(statBuffer.st_mode) )
					symLinkInPF = true;
			}
		}
		strlcpy(fullPath, sdkPath, PATH_MAX);
		strlcat(fullPath, path, PATH_MAX);
		if ( fileExists(fullPath) && !symLinkInPF )
			continue;

		pathAddTbdExtension(fullPath);
		if ( fileExists(fullPath) && !symLinkInPF )
			continue;

		char realPath[PATH_MAX];
		// rdar://93854103 (Suppress "softlinked <framework> is symlink to real framework location" warning for frameworks moved to cryptex)
		bool skip = ( (realpath(fullPath, realPath) != nullptr) && (strstr(realPath, "/System/Cryptex") != nullptr) );
		if ( skip )
			continue;

		if ( symLinkInPF )
			warning("softlinked '%s' is symlink to real framework location", path);
		else
			warning("softlinked '%s' does not exist in SDK", path);
		// see if switching to /S/L/Frameworks helps (most common error)
		if ( strncmp(path, "/System/Library/Frameworks/", 27) != 0 ) {
			if ( const char* s = strstr(path, "Frameworks/") ) {
				char altPath[PATH_MAX];
				strlcpy(altPath, sdkPath, PATH_MAX);
				strlcat(altPath, "/System/Library/Frameworks/", PATH_MAX);
				strlcat(altPath, &s[11], PATH_MAX);
				bool foundAlt = fileExists(altPath);
				if ( !foundAlt ) {
					pathAddTbdExtension(altPath);
					foundAlt = fileExists(altPath);
				}
				if ( foundAlt ) {
					char altStr[PATH_MAX];
					strncpy(altStr, str, 16);
					strcpy(&altStr[16], "/System/Library/Frameworks/");
					strcat(altStr, &s[11]);
					// switch audit atom to use new path
					const ld::Atom* newAtom = new SoftLinkAuditAtom(atom, altStr);
					sect.atoms[atomIndex] = newAtom;
					// switch dlopen string to use new path
					replaceStringLiteral(state, path, &altStr[16], deadAtoms);
					// switch paths array to use new path
					replaceAuditStringLiteral(state, str, newAtom);
					// if this was built for older OS, _sl_dlopen glue is inlined
					replaceGlueUseOfAuditString(state, atom, newAtom);
					return;
				}
			}
		}
	}
}

void doPass(const Options& opts, ld::Internal& state)
{
//	const bool log = false;
	
	// clear "willRemoved" bit on all dylibs
	for (std::vector<ld::dylib::File*>::iterator it = state.dylibs.begin(); it != state.dylibs.end(); ++it) {
		ld::dylib::File* aDylib = *it;
		aDylib->setWillBeRemoved(false);
	}
	for (std::vector<ld::dylib::File*>::iterator it = state.dylibs.begin(); it != state.dylibs.end(); ++it) {
		ld::dylib::File* aDylib = *it;
		// set "willRemoved" bit on implicit dylibs that did not provide any exports
		if ( aDylib->implicitlyLinked() && !aDylib->explicitlyLinked() && !aDylib->providedExportAtom() && !aDylib->neededDylib() )
			aDylib->setWillBeRemoved(true);
		// set "willRemoved" bit on dead strippable explicit dylibs that did not provide any exports
		if ( aDylib->explicitlyLinked() && aDylib->deadStrippable() && !aDylib->providedExportAtom() && !aDylib->neededDylib() )
			aDylib->setWillBeRemoved(true);
		// set "willRemoved" bit on any unused explicit when -dead_strip_dylibs is used
		if ( opts.deadStripDylibs() && !aDylib->providedExportAtom() && !aDylib->neededDylib() )
			aDylib->setWillBeRemoved(true);
		// <rdar://problem/48642080> Warn when dylib links itself
		if ( (opts.outputKind() == Options::kDynamicLibrary) && !aDylib->willRemoved() ) {
			if ( strcmp(opts.installPath(), aDylib->installPath()) == 0 )
				warning("%s is linking with itself", opts.installPath());
		}
		// <rdar://problem/45501357> linker should warn about unused libraries/frameworks
		if ( opts.warnUnusedDylibs() && !aDylib->neededDylib() ) {
			if ( aDylib->explicitlyLinked() && !aDylib->providedExportAtom() && !aDylib->willBeReExported()
				&& (strncmp(aDylib->installPath(), "/usr/lib/libSystem.", 19) != 0)
				&& (strncmp(aDylib->installPath(), "/usr/lib/libc++.", 16) != 0)
				&& (strncmp(aDylib->installPath(), "/System/Library/Frameworks/Foundation.framework/", 48) != 0) ) {
				// don't warn if this dylib re-exports another one that does have used symbols
				if ( !aDylib->hasReExportedDependentsThatProvidedExportAtom() )
					warning("linking with (%s) but not using any symbols from it", aDylib->installPath());
			}
		}
	}
	// remove unused dylibs
	state.dylibs.erase(std::remove_if(state.dylibs.begin(), state.dylibs.end(), WillBeUsed()), state.dylibs.end());
	
	
	// <rdar://problem/9441273> automatically weak-import dylibs when all symbols from it are weak-imported
	for (std::vector<ld::Internal::FinalSection*>::iterator sit=state.sections.begin(); sit != state.sections.end(); ++sit) {
		ld::Internal::FinalSection* sect = *sit;
		for (std::vector<const ld::Atom*>::iterator ait=sect->atoms.begin();  ait != sect->atoms.end(); ++ait) {
			const ld::Atom* atom = *ait;
			const ld::Atom* target = NULL;
			bool targetIsWeakImport = false;
			for (ld::Fixup::iterator fit = atom->fixupsBegin(), end=atom->fixupsEnd(); fit != end; ++fit) {
				if ( fit->firstInCluster() ) 
					target = NULL;
				switch ( fit->binding ) {
					case ld::Fixup::bindingsIndirectlyBound:
						target = state.indirectBindingTable[fit->u.bindingIndex];
						targetIsWeakImport = fit->weakImport;
						break;
					case ld::Fixup::bindingDirectlyBound:
						target = fit->u.target;
						targetIsWeakImport = fit->weakImport;
						break;
                    default:
                        break;
				}
				if ( (target != NULL) && (target->definition() == ld::Atom::definitionProxy) ) {
					if ( targetIsWeakImport && !opts.allowWeakImports() )
						throwf("weak import of symbol '%s' not supported because of option: -no_weak_imports", target->name());
					ld::Atom::WeakImportState curWI = target->weakImportState();
					if ( curWI == ld::Atom::weakImportUnset ) {
						// first use of this proxy, set weak-import based on this usage
						(const_cast<ld::Atom*>(target))->setWeakImportState(targetIsWeakImport);
					}
					else {
						// proxy already has weak-importness set, check for weakness mismatch
						bool curIsWeakImport = (curWI == ld::Atom::weakImportTrue);
						if ( curIsWeakImport != targetIsWeakImport ) {
							// found mismatch
							switch ( opts.weakReferenceMismatchTreatment() ) {
								case Options::kWeakReferenceMismatchError:
									throwf("mismatching weak references for symbol: %s", target->name());
								case Options::kWeakReferenceMismatchWeak:
									(const_cast<ld::Atom*>(target))->setWeakImportState(true);
									break;
								case Options::kWeakReferenceMismatchNonWeak:
									(const_cast<ld::Atom*>(target))->setWeakImportState(false);
									break;
							}
						}
					}
				}
			}
		}
	}


	// rdar://84530285 (ld can elide __swift_FORCE_LOAD_$ symbols in final output)
	// To ensure overlays are loaded at runtime, the swift compiler generates __swift_FORCE_LOAD_$*
	// We need to perserve the symbols for clients to link against, but we don't need dyld
	// to actually set the pointers. Instead in this pass, we change the references to be an empty
	// pointer size piece of data, with no fixup on it.
	std::set<const ld::Atom*> deadAtoms;
	for (ld::Internal::FinalSection* sect : state.sections) {
		if ( (strncmp(sect->segmentName(), "__DATA", 6) != 0) || (strcmp(sect->sectionName(), "__const") != 0) )
			continue;
		for (const ld::Atom* refAtom : sect->atoms) {
			// normally all uses of FORCE_LOAD symbols are within FORCE_LOAD data const
			// but if ld -r -x is used, then the containing symbols in data const lose their name and are symbolTableNotInFinalLinkedImages
			if ( (strncmp(refAtom->name(), "__swift_FORCE_LOAD_$_swift", 26) != 0) && (refAtom->symbolTableInclusion() != ld::Atom::symbolTableNotInFinalLinkedImages) )
				continue;
			for (ld::Fixup::iterator fit=refAtom->fixupsBegin(); fit != refAtom->fixupsEnd(); ++fit) {
				if ( (fit->clusterSize == Fixup::k1of1) && (fit->offsetInAtom == 0) && (fit->binding == ld::Fixup::bindingsIndirectlyBound) ) {
					const ld::Atom* targetAtom = state.indirectBindingTable[fit->u.bindingIndex];
					if ( strncmp(targetAtom->name(), "__swift_FORCE_LOAD_$_swift", 26) != 0 )
						continue;
					if ( targetAtom->definition() != ld::Atom::definitionProxy )
						continue;
					// only elide if the target is a dylib
					if ( const ld::File* targetFile = targetAtom->file() ) {
						if ( const ld::dylib::File* targetDylib = dynamic_cast<const ld::dylib::File*>(targetFile) ) {
							// turn this fixup into a nop.  This will remove the bind in the final image
							// <rdar://99457165> only nop the fixup, without dead stripping the target atom,
							//   to ensure correct libraries are linked in.
							fit->kind     = ld::Fixup::kindNone;
							fit->binding  = ld::Fixup::bindingNone;
							fit->u.target = nullptr;
						}
					}
				}
			}
		}
	}

	if ( opts.internalSDK() ) {
		for (ld::Internal::FinalSection* sect : state.sections) {
			if ( sect->type() != ld::Section::typeNonStdCString )
				continue;
			if ( (strncmp(sect->segmentName(), "__TEXT", 6) != 0) || (strcmp(sect->sectionName(), "__dlopen_cstrs") != 0) )
				continue;
			unsigned int atomIndex = 0;
			for (const ld::Atom* atom : sect->atoms) {
				const char* str = (char*)(atom->rawContentPointer());
				// softlink magic strings look like:  "softlink:r:path:/System/Library/PrivateFrameworks/Foo.framework/Foo"
				if ( strncmp(str, "softlink:", 9) == 0 ) {
					if ( strncmp(&str[11], "path:", 5) == 0 ) {
						const char* path = &str[16];
						// see if softlink path actually exists in SDK
						auditSoftlink(atom, atomIndex, deadAtoms, opts, path, *sect, state, str);
					}
				}
				++atomIndex;
			}
		}
	}

	// remove dead atoms
	for (ld::Internal::FinalSection* sect : state.sections ) {
		sect->atoms.erase(std::remove_if(sect->atoms.begin(), sect->atoms.end(), OptimizedAway(deadAtoms)), sect->atoms.end());
	}

}


} // namespace dylibs
} // namespace passes 
} // namespace ld 
