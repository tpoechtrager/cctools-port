/* -*- mode: C++; c-basic-offset: 4; tab-width: 4 -*-*
 *
 * Copyright (c) 2009 Apple Inc. All rights reserved.
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

#ifndef __RESOLVER_H__
#define __RESOLVER_H__

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

#include <vector>
#include <unordered_set>
#include <string_view>

#include "Options.h"
#include "ld.hpp"
#include "SymbolTable.h"
#include "Containers.h"

namespace ld {
namespace tool {




class Resolver : public ld::File::AtomHandler
{
public:
							Resolver(const Options& opts, InputFiles& inputs, ld::Internal& state) 
								: _options(opts), _inputFiles(inputs), _internal(state), 
								  _symbolTable(opts, state.indirectBindingTable, inputs.count()),
								  _haveLLVMObjs(false),
								  _completedInitialObjectFiles(false),
								  _ltoCodeGenFinished(false),
								  _haveAliases(false), _havellvmProfiling(false),
								  _printWhyLive(opts.printWhyLive()),
								  _synthesizeObjcMsgSendStubs(opts.dyldLoadsOutput()),
								  _needsObjcMsgSendProxy(false) {}
								

		virtual void		doAtom(const ld::Atom&);
		virtual void		doFile(const class File&);
		
		void				resolve();


private:
	struct WhyLiveBackChain
	{
		WhyLiveBackChain*	previous;
		const ld::Atom*		referer;
	};

	void					initializeState();
	void					buildAtomList();
	void					addInitialUndefines();
	void					deadStripOptimize(bool force=false);
	template<typename T>
	void					forEachDeadStripRoot(std::vector<const ld::Atom*>& dontDeadStripIfReferencesLive,
												 bool force,
												 T callback);
	bool					atomIsDeadStripRoot(const ld::Atom*, bool forceDeadStrip) const;
	void					resolveCurrentUndefines();
	void					resolveAllUndefines();
	void					removeUnusedAliases(std::vector<std::string_view>& unresolvableUndefines);
	void					checkUndefines(bool force=false);
	void					checkDylibSymbolCollisions();
	void					tentativeOverrideOfDylib(ld::Atom&);
	void					fillInInternalState();
	void					fillInHelpersInInternalState();
	void					removeCoalescedAwayAtoms();
	void					syncAliases();
	void					fillInEntryPoint();
	void					linkTimeOptimize();
	void					convertReferencesToIndirect(const ld::Atom& atom);
	const ld::Atom*			entryPoint(bool searchArchives);
	bool					diagnoseAtomsWithUnalignedPointers() const;
	void					markLive(const ld::Atom& atom, WhyLiveBackChain* previous);
	bool					isDtraceProbe(ld::Fixup::Kind kind);
	void					liveUndefines(std::vector<std::string_view>&);
	void					remainingUndefines(std::vector<std::string_view>&);
	bool					printReferencedBy(const char* name, SymbolTable::IndirectBindingSlot slot);
	void					tweakWeakness();
	void					buildArchivesList();
	void					doLinkerOption(const std::vector<const char*>& linkerOption, const char* fileName);
	void					dumpAtoms();
	void					checkChainedFixupsBounds();
	void					writeDotOutput();

	class AtomCoalescedAway {
	public:
		bool operator()(const ld::Atom* atom) const {
			return atom->coalescedAway();
		}
	};

	const Options&					_options;
	InputFiles&						_inputFiles;
	ld::Internal&					_internal;
	std::vector<const ld::Atom*>	_atoms;
	std::vector<const class AliasAtom*>	_aliasesFromCmdLine;
	SymbolTable						_symbolTable;
	bool							_haveLLVMObjs;
	bool							_completedInitialObjectFiles;
	bool							_ltoCodeGenFinished;
	bool							_haveAliases;
	bool							_havellvmProfiling;
	bool							_printWhyLive;
	bool							_synthesizeObjcMsgSendStubs;
	bool							_needsObjcMsgSendProxy;
};


class DeadStripResolver   
{
public:

	
private:

};

} // namespace tool 
} // namespace ld 



#endif // __RESOLVER_H__
