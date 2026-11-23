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


void doPass(const Options& opts, ld::Internal& state)
{
//	const bool log = false;
	
	// ld64-port: Several dylib files can share one install name: the SDK TBDs of the Swift overlays
	// (libswiftDarwin, libswift_DarwinFoundation1-3, libswift_Builtin_float) get /usr/lib/swift/libswiftDarwin.dylib
	// through $ld$previous$ when the deployment target is older than macOS 15.  Only the first file per install name
	// is in state.dylibs, so a symbol used from another one does not mark that file as used and it would be removed
	// below, leaving the atoms of the other files without an ordinal.  Keep a dylib whose install name is used by
	// any import atom.
	std::set<std::string> usedInstallPaths;
	for (const ld::Internal::FinalSection* sect : state.sections) {
		if ( sect->type() != ld::Section::typeImportProxies )
			continue;
		for (const ld::Atom* atom : sect->atoms) {
			const ld::dylib::File* dylib = dynamic_cast<const ld::dylib::File*>(atom->file());
			if ( (dylib != NULL) && (dylib->installPath() != NULL) )
				usedInstallPaths.insert(dylib->installPath());
		}
	}
	// ld64-port end

	// clear "willRemoved" bit on all dylibs
	for (std::vector<ld::dylib::File*>::iterator it = state.dylibs.begin(); it != state.dylibs.end(); ++it) {
		ld::dylib::File* aDylib = *it;
		aDylib->setWillBeRemoved(false);
	}
	for (std::vector<ld::dylib::File*>::iterator it = state.dylibs.begin(); it != state.dylibs.end(); ++it) {
		ld::dylib::File* aDylib = *it;
		const bool installPathUsed = ( aDylib->installPath() != NULL ) && ( usedInstallPaths.count(aDylib->installPath()) != 0 ); // ld64-port
		// set "willRemoved" bit on implicit dylibs that did not provide any exports
		if ( aDylib->implicitlyLinked() && !aDylib->explicitlyLinked() && !aDylib->providedExportAtom() && !aDylib->neededDylib() && !installPathUsed ) // ld64-port: && !installPathUsed
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
	
}


} // namespace dylibs
} // namespace passes 
} // namespace ld 
