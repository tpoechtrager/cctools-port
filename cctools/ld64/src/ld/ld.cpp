/* -*- mode: C++; c-basic-offset: 4; tab-width: 4 -*-*
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
 
// start temp HACK for cross builds
//extern "C" double log2 ( double );
//#define __MATH__
// end temp HACK for cross builds


#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/sysctl.h>
#include <fcntl.h>
#include <errno.h>
#include <limits.h>
#include <unistd.h>
#if !defined(__FreeBSD__) && !defined(__NetBSD__) && !defined(__OpenBSD__)
#include <execinfo.h>
#endif /* !__FreeBSD__ && !__NetBSD__ && !__OpenBSD__ */
#include <mach/mach_time.h>
#include <mach/vm_statistics.h>
#include <mach/mach_init.h>
#include <mach/mach_host.h>
#include <dlfcn.h>
#include <mach-o/dyld.h>
#include <dlfcn.h>
#include <AvailabilityMacros.h>

#include <string>
#include <map>
#include <set>
#include <string>
#include <vector>
#include <list>
#include <algorithm>
#include <unordered_map>
#include <cxxabi.h>

#include "Options.h"

#include "MachOFileAbstraction.hpp"
#include "Architectures.hpp"
#include "ld.hpp"

#include "InputFiles.h"
#include "Resolver.h"
#include "OutputFile.h"
#include "Snapshot.h"

#include "passes/stubs/make_stubs.h"
#include "passes/dtrace_dof.h"
#include "passes/got.h"
#include "passes/tlvp.h"
#include "passes/huge.h"
#include "passes/compact_unwind.h"
#include "passes/order.h"
#include "passes/branch_island.h"
#include "passes/branch_shim.h"
#include "passes/objc.h"
#include "passes/dylibs.h"

#include "parsers/archive_file.h"
#include "parsers/macho_relocatable_file.h"
#include "parsers/macho_dylib_file.h"
#include "parsers/lto_file.h"
#include "parsers/opaque_section_file.h"


struct PerformanceStatistics {
	uint64_t						startTool;
	uint64_t						startInputFileProcessing;
	uint64_t						startResolver;
	uint64_t						startDylibs;
	uint64_t						startPasses;
	uint64_t						startOutput;
	uint64_t						startDone;
	vm_statistics_data_t			vmStart;
	vm_statistics_data_t			vmEnd;
};


class InternalState : public ld::Internal
{
public:
											InternalState(const Options& opts) : _options(opts), _atomsOrderedInSections(false) { }
	virtual	ld::Internal::FinalSection*		addAtom(const ld::Atom& atom);
	virtual ld::Internal::FinalSection*		getFinalSection(const ld::Section&);
	
	uint64_t								assignFileOffsets();
	void									setSectionSizesAndAlignments();
	void									sortSections();
	void									markAtomsOrdered() { _atomsOrderedInSections = true; }
	virtual									~InternalState() {}
private:

	class FinalSection : public ld::Internal::FinalSection 
	{
	public:
							FinalSection(const ld::Section& sect, uint32_t sectionsSeen, bool objFile);
		static int					sectionComparer(const void* l, const void* r);
		static const ld::Section&	outputSection(const ld::Section& sect, bool mergeZeroFill);
		static const ld::Section&	objectOutputSection(const ld::Section& sect, const Options&);
	private:
		friend class InternalState;
		static uint32_t		sectionOrder(const ld::Section& sect, uint32_t sectionsSeen);
		static uint32_t		segmentOrder(const ld::Section& sect, bool objFile);
		uint32_t			_segmentOrder;
		uint32_t			_sectionOrder;

		static std::vector<const char*> _s_segmentsSeen;
		static ld::Section		_s_DATA_data;
		static ld::Section		_s_DATA_const;
		static ld::Section		_s_TEXT_text;
		static ld::Section		_s_TEXT_const;
		static ld::Section		_s_DATA_nl_symbol_ptr;
		static ld::Section		_s_DATA_common;
		static ld::Section		_s_DATA_zerofill;
	};
	
	bool hasZeroForFileOffset(const ld::Section* sect);
	uint64_t pageAlign(uint64_t addr);
	uint64_t pageAlign(uint64_t addr, uint64_t pageSize);
	
	struct SectionHash {
		size_t operator()(const ld::Section*) const;
	};
	struct SectionEquals {
		bool operator()(const ld::Section* left, const ld::Section* right) const;
	};
	typedef std::unordered_map<const ld::Section*, FinalSection*, SectionHash, SectionEquals> SectionInToOut;
	

	SectionInToOut			_sectionInToFinalMap;
	const Options&			_options;
	bool					_atomsOrderedInSections;
};

ld::Section	InternalState::FinalSection::_s_DATA_data( "__DATA", "__data",  ld::Section::typeUnclassified);
ld::Section	InternalState::FinalSection::_s_DATA_const("__DATA", "__const", ld::Section::typeUnclassified);
ld::Section	InternalState::FinalSection::_s_TEXT_text( "__TEXT", "__text",  ld::Section::typeCode);
ld::Section	InternalState::FinalSection::_s_TEXT_const("__TEXT", "__const", ld::Section::typeUnclassified);
ld::Section	InternalState::FinalSection::_s_DATA_nl_symbol_ptr("__DATA", "__nl_symbol_ptr", ld::Section::typeNonLazyPointer);
ld::Section	InternalState::FinalSection::_s_DATA_common("__DATA", "__common", ld::Section::typeZeroFill);
ld::Section	InternalState::FinalSection::_s_DATA_zerofill("__DATA", "__zerofill", ld::Section::typeZeroFill);
std::vector<const char*> InternalState::FinalSection::_s_segmentsSeen;


size_t InternalState::SectionHash::operator()(const ld::Section* sect) const
{
	size_t hash = 0;	
	ld::CStringHash temp;
	hash += temp.operator()(sect->segmentName());
	hash += temp.operator()(sect->sectionName());
	return hash;
}

bool InternalState::SectionEquals::operator()(const ld::Section* left, const ld::Section* right) const
{
	return (*left == *right);
}


InternalState::FinalSection::FinalSection(const ld::Section& sect, uint32_t sectionsSeen, bool objFile)
	: ld::Internal::FinalSection(sect), 
	  _segmentOrder(segmentOrder(sect, objFile)),
	  _sectionOrder(sectionOrder(sect, sectionsSeen))
{
	//fprintf(stderr, "FinalSection(%s, %s) _segmentOrder=%d, _sectionOrder=%d\n", 
	//		this->segmentName(), this->sectionName(), _segmentOrder, _sectionOrder);
}

const ld::Section& InternalState::FinalSection::outputSection(const ld::Section& sect, bool mergeZeroFill)
{
	// merge sections in final linked image
	switch ( sect.type() ) {
		case ld::Section::typeLiteral4:
		case ld::Section::typeLiteral8:
		case ld::Section::typeLiteral16:
			return _s_TEXT_const;
		case ld::Section::typeUnclassified:
			if ( strcmp(sect.segmentName(), "__DATA") == 0 ) {
				if ( strcmp(sect.sectionName(), "__datacoal_nt") == 0 )
					return _s_DATA_data;
				if ( strcmp(sect.sectionName(), "__const_coal") == 0 )
					return _s_DATA_const;
			}
			else if ( strcmp(sect.segmentName(), "__TEXT") == 0 ) {
				if ( strcmp(sect.sectionName(), "__const_coal") == 0 )
					return _s_TEXT_const;
			}
			break;
		case ld::Section::typeZeroFill:
			if ( mergeZeroFill )
				return _s_DATA_zerofill;
			break;
		case ld::Section::typeCode:
			if ( strcmp(sect.segmentName(), "__TEXT") == 0 ) {
				if ( strcmp(sect.sectionName(), "__textcoal_nt") == 0 )
					return _s_TEXT_text;
				else if ( strcmp(sect.sectionName(), "__StaticInit") == 0 )
					return _s_TEXT_text;
			}
			break;
		case ld::Section::typeNonLazyPointer:
			if ( strcmp(sect.segmentName(), "__DATA") == 0 ) {
				if ( strcmp(sect.sectionName(), "__nl_symbol_ptr") == 0 )
					return _s_DATA_nl_symbol_ptr;
			}
			else if ( strcmp(sect.segmentName(), "__IMPORT") == 0 ) {
				if ( strcmp(sect.sectionName(), "__pointers") == 0 )
					return _s_DATA_nl_symbol_ptr; 
			}
			break;
		case ld::Section::typeTentativeDefs:
			if ( mergeZeroFill )
				return _s_DATA_zerofill;
			else
				return _s_DATA_common;
			break;
			// FIX ME: more 
		default:
			break;
	}
	return sect;
}

const ld::Section& InternalState::FinalSection::objectOutputSection(const ld::Section& sect, const Options& options)
{
  	const std::vector<Options::SectionRename>& renames = options.sectionRenames();
	for ( std::vector<Options::SectionRename>::const_iterator it=renames.begin(); it != renames.end(); ++it) {
	  if ( (strcmp(sect.sectionName(), it->fromSection) == 0) && (strcmp(sect.segmentName(), it->fromSegment) == 0) ) {
		ld::Section* s = new ld::Section(it->toSegment, it->toSection, sect.type());
		return *s;
	  }
	}


	// in -r mode the only section that ever changes is __tenative -> __common with -d option
	if ( (sect.type() == ld::Section::typeTentativeDefs) && options.makeTentativeDefinitionsReal())
		return _s_DATA_common;
	return sect;
}

uint32_t InternalState::FinalSection::segmentOrder(const ld::Section& sect, bool objFile)
{
	if ( strcmp(sect.segmentName(), "__PAGEZERO") == 0 ) 
		return 0;
	if ( strcmp(sect.segmentName(), "__HEADER") == 0 ) // only used with -preload
		return 0;
	if ( strcmp(sect.segmentName(), "__TEXT") == 0 ) 
		return 1;
	// in -r mode, want __DATA  last so zerofill sections are at end
	if ( strcmp(sect.segmentName(), "__DATA") == 0 ) 
		return (objFile ? 5 : 2);
	if ( strcmp(sect.segmentName(), "__OBJC") == 0 ) 
		return 3;
	if ( strcmp(sect.segmentName(), "__IMPORT") == 0 ) 
		return 4;
	
	// layout non-standard segments in order seen (+10 to shift beyond standard segments)
	for (uint32_t i=0; i < _s_segmentsSeen.size(); ++i) {
		if ( strcmp(_s_segmentsSeen[i], sect.segmentName()) == 0 )
			return i+10;
	}
	_s_segmentsSeen.push_back(sect.segmentName());
	return _s_segmentsSeen.size()-1+10;
}

uint32_t InternalState::FinalSection::sectionOrder(const ld::Section& sect, uint32_t sectionsSeen)
{
	if ( sect.type() == ld::Section::typeFirstSection )
		return 0;
	if ( sect.type() == ld::Section::typeMachHeader )
		return 1;
	if ( sect.type() == ld::Section::typeLastSection )
		return INT_MAX;
	if ( strcmp(sect.segmentName(), "__TEXT") == 0 ) {
		switch ( sect.type() ) {
			case ld::Section::typeCode:
				// <rdar://problem/8346444> make __text always be first "code" section
				if ( strcmp(sect.sectionName(), "__text") == 0 )
					return 10;
				else
					return 11;
			case ld::Section::typeStub:
				return 12;
			case ld::Section::typeStubHelper:
				return 13;
			case ld::Section::typeLSDA:
				return INT_MAX-3;
			case ld::Section::typeUnwindInfo:
				return INT_MAX-2;
			case ld::Section::typeCFI:
				return INT_MAX-1;
			case ld::Section::typeStubClose:
				return INT_MAX;
			default:
				return sectionsSeen+20;
		}
	}
	else if ( strcmp(sect.segmentName(), "__DATA") == 0 ) {
		switch ( sect.type() ) {
			case ld::Section::typeLazyPointerClose:
				return 8;
			case ld::Section::typeDyldInfo:
				return 9;
			case ld::Section::typeNonLazyPointer:
				return 10;
			case ld::Section::typeLazyPointer:
				return 11;
			case ld::Section::typeInitializerPointers:
				return 12;
			case ld::Section::typeTerminatorPointers:
				return 13;
			case ld::Section::typeTLVInitialValues:
				return INT_MAX-4; // need TLV zero-fill to follow TLV init values
			case ld::Section::typeTLVZeroFill:
				return INT_MAX-3;
			case ld::Section::typeZeroFill:
				// make sure __huge is always last zerofill section
				if ( strcmp(sect.sectionName(), "__huge") == 0 )
					return INT_MAX-1;
				else
					return INT_MAX-2;
			default:
				// <rdar://problem/14348664> __DATA,__const section should be near __mod_init_func not __data
				if ( strcmp(sect.sectionName(), "__const") == 0 )
					return 14;
				// <rdar://problem/7435296> Reorder sections to reduce page faults in object files
				else if ( strcmp(sect.sectionName(), "__objc_classlist") == 0 ) 
					return 20;
				else if ( strcmp(sect.sectionName(), "__objc_nlclslist") == 0 ) 
					return 21;
				else if ( strcmp(sect.sectionName(), "__objc_catlist") == 0 ) 
					return 22;
				else if ( strcmp(sect.sectionName(), "__objc_protolist") == 0 ) 
					return 23;
				else if ( strcmp(sect.sectionName(), "__objc_imageinfo") == 0 ) 
					return 24;
				else if ( strcmp(sect.sectionName(), "__objc_const") == 0 ) 
					return 25;
				else if ( strcmp(sect.sectionName(), "__objc_selrefs") == 0 ) 
					return 26;
				else if ( strcmp(sect.sectionName(), "__objc_msgrefs") == 0 ) 
					return 27;
				else if ( strcmp(sect.sectionName(), "__objc_protorefs") == 0 ) 
					return 28;
				else if ( strcmp(sect.sectionName(), "__objc_classrefs") == 0 ) 
					return 29;
				else if ( strcmp(sect.sectionName(), "__objc_superrefs") == 0 ) 
					return 30;
				else if ( strcmp(sect.sectionName(), "__objc_data") == 0 ) 
					return 31;
				else
					return sectionsSeen+40;
		}
	}
	// make sure zerofill in any other section is at end of segment
	if ( sect.type() == ld::Section::typeZeroFill )
		return INT_MAX-1;
	return sectionsSeen+20;
}

#ifndef NDEBUG
static void validateFixups(const ld::Atom& atom)
{
	//fprintf(stderr, "validateFixups %s\n", atom.name());
	bool lastWasClusterEnd = true;
	ld::Fixup::Cluster lastClusterSize = ld::Fixup::k1of1;
	uint32_t curClusterOffsetInAtom = 0;
	for (ld::Fixup::iterator fit=atom.fixupsBegin(); fit != atom.fixupsEnd(); ++fit) {
		//fprintf(stderr, "  fixup offset=%d, cluster=%d\n", fit->offsetInAtom, fit->clusterSize);
		assert((fit->offsetInAtom <= atom.size()) || (fit->offsetInAtom == 0));
		if ( fit->firstInCluster() ) {
			assert(lastWasClusterEnd);
			curClusterOffsetInAtom = fit->offsetInAtom;
			lastWasClusterEnd = (fit->clusterSize == ld::Fixup::k1of1);
		}
		else {
			assert(!lastWasClusterEnd);
			assert(fit->offsetInAtom == curClusterOffsetInAtom);
			switch ((ld::Fixup::Cluster)fit->clusterSize) {
				case ld::Fixup::k1of1:
				case ld::Fixup::k1of2:
				case ld::Fixup::k1of3:
				case ld::Fixup::k1of4:
				case ld::Fixup::k1of5:
					lastWasClusterEnd = false;
					break;
				case ld::Fixup::k2of2:
					assert(lastClusterSize = ld::Fixup::k1of2);
					lastWasClusterEnd = true;
					break;
				case ld::Fixup::k2of3:
					assert(lastClusterSize = ld::Fixup::k1of3);
					lastWasClusterEnd = false;
					break;
				case ld::Fixup::k2of4:
					assert(lastClusterSize = ld::Fixup::k1of4);
					lastWasClusterEnd = false;
					break;
				case ld::Fixup::k2of5:
					assert(lastClusterSize = ld::Fixup::k1of5);
					lastWasClusterEnd = false;
					break;
				case ld::Fixup::k3of3:
					assert(lastClusterSize = ld::Fixup::k2of3);
					lastWasClusterEnd = true;
					break;
				case ld::Fixup::k3of4:
					assert(lastClusterSize = ld::Fixup::k2of4);
					lastWasClusterEnd = false;
					break;
				case ld::Fixup::k3of5:
					assert(lastClusterSize = ld::Fixup::k2of5);
					lastWasClusterEnd = false;
					break;
				case ld::Fixup::k4of4:
					assert(lastClusterSize = ld::Fixup::k3of4);
					lastWasClusterEnd = true;
					break;
				case ld::Fixup::k4of5:
					assert(lastClusterSize = ld::Fixup::k3of5);
					lastWasClusterEnd = false;
					break;
				case ld::Fixup::k5of5:
					assert(lastClusterSize = ld::Fixup::k4of5);
					lastWasClusterEnd = true;
					break;
			}
		}
		lastClusterSize = fit->clusterSize;
		if ( fit->binding == ld::Fixup::bindingDirectlyBound ) {
			assert(fit->u.target != NULL);
		}
	}
	switch (lastClusterSize) {
		case ld::Fixup::k1of1:
		case ld::Fixup::k2of2:
		case ld::Fixup::k3of3:
		case ld::Fixup::k4of4:
		case ld::Fixup::k5of5:
			break;
		default:
			assert(0 && "last fixup was not end of cluster");
			break;
	}
}
#endif

ld::Internal::FinalSection* InternalState::addAtom(const ld::Atom& atom)
{
	ld::Internal::FinalSection* fs = this->getFinalSection(atom.section());
	//fprintf(stderr, "InternalState::doAtom(%p), name=%s, sect=%s, finalsect=%p\n", &atom, atom.name(), atom.section().sectionName(), fs);
#ifndef NDEBUG
	validateFixups(atom);
#endif
	if ( _atomsOrderedInSections ) {
		// make sure this atom is placed before any trailing section$end$ atom
		if ( (fs->atoms.size() > 1) && (fs->atoms.back()->contentType() == ld::Atom::typeSectionEnd) ) {
			// last atom in section$end$ atom, insert before it
			const ld::Atom* endAtom = fs->atoms.back();
			fs->atoms.pop_back();
			fs->atoms.push_back(&atom);
			fs->atoms.push_back(endAtom);
		}
		else {
			// not end atom, just append new atom
			fs->atoms.push_back(&atom);
		}
	}
	else {
		// normal case
		fs->atoms.push_back(&atom);
	}
	return fs;
}

ld::Internal::FinalSection* InternalState::getFinalSection(const ld::Section& inputSection)
{	
	const ld::Section* baseForFinalSection = &inputSection;
	
	// see if input section already has a FinalSection
	SectionInToOut::iterator pos = _sectionInToFinalMap.find(&inputSection);
	if ( pos != _sectionInToFinalMap.end() ) {
		return pos->second;
	}

	// otherwise, create a new final section
	bool objFile = false;
	switch ( _options.outputKind() ) {
		case Options::kStaticExecutable:
		case Options::kDynamicExecutable:
		case Options::kDynamicLibrary:
		case Options::kDynamicBundle:
		case Options::kDyld:
		case Options::kKextBundle:
		case Options::kPreload:
			{
				// coalesce some sections
				const ld::Section& outSect = FinalSection::outputSection(inputSection, _options.mergeZeroFill());
				pos = _sectionInToFinalMap.find(&outSect);
				if ( pos != _sectionInToFinalMap.end() ) {
					_sectionInToFinalMap[&inputSection] = pos->second;
					//fprintf(stderr, "_sectionInToFinalMap[%p] = %p\n", &inputSection, pos->second);
					return pos->second;
				}
				else if ( outSect != inputSection ) {
					// new output section created, but not in map
					baseForFinalSection = &outSect;
				}
			}
			break;
		case Options::kObjectFile:
			baseForFinalSection = &FinalSection::objectOutputSection(inputSection, _options);
			pos = _sectionInToFinalMap.find(baseForFinalSection);
			if ( pos != _sectionInToFinalMap.end() ) {
				_sectionInToFinalMap[&inputSection] = pos->second;
				//fprintf(stderr, "_sectionInToFinalMap[%p] = %p\n", &inputSection, pos->second);
				return pos->second;
			}
			objFile = true;
			break;
	}

	InternalState::FinalSection* result = new InternalState::FinalSection(*baseForFinalSection, 
																	_sectionInToFinalMap.size(), objFile);
	_sectionInToFinalMap[baseForFinalSection] = result;
	//fprintf(stderr, "_sectionInToFinalMap[%p] = %p\n", baseForFinalSection, result);
	sections.push_back(result);
	return result;
}


int InternalState::FinalSection::sectionComparer(const void* l, const void* r)
{
	const FinalSection* left  = *(FinalSection**)l;
	const FinalSection* right = *(FinalSection**)r;
	if ( left->_segmentOrder != right->_segmentOrder )
		return (left->_segmentOrder - right->_segmentOrder);
	return (left->_sectionOrder - right->_sectionOrder);
}

void InternalState::sortSections()
{
	//fprintf(stderr, "UNSORTED final sections:\n");
	//for (std::vector<ld::Internal::FinalSection*>::iterator it = sections.begin(); it != sections.end(); ++it) {
	//	fprintf(stderr, "final section %p %s/%s\n", (*it), (*it)->segmentName(), (*it)->sectionName());
	//}
	qsort(&sections[0], sections.size(), sizeof(FinalSection*), &InternalState::FinalSection::sectionComparer);
	//fprintf(stderr, "SORTED final sections:\n");
	//for (std::vector<ld::Internal::FinalSection*>::iterator it = sections.begin(); it != sections.end(); ++it) {
	//	fprintf(stderr, "final section %p %s/%s\n", (*it), (*it)->segmentName(), (*it)->sectionName());
	//}
	assert((sections[0]->type() == ld::Section::typeMachHeader) 
		|| ((sections[0]->type() == ld::Section::typeFirstSection) && (sections[1]->type() == ld::Section::typeMachHeader))
		|| ((sections[0]->type() == ld::Section::typePageZero) && (sections[1]->type() == ld::Section::typeMachHeader))
		|| ((sections[0]->type() == ld::Section::typePageZero) && (sections[1]->type() == ld::Section::typeFirstSection) && (sections[2]->type() == ld::Section::typeMachHeader)) );
	
}


bool InternalState::hasZeroForFileOffset(const ld::Section* sect)
{
	switch ( sect->type() ) {
		case ld::Section::typeZeroFill:
		case ld::Section::typeTLVZeroFill:
			return _options.optimizeZeroFill();
		case ld::Section::typePageZero:
		case ld::Section::typeStack:
		case ld::Section::typeTentativeDefs:
			return true;
		default:
			break;
	}
	return false;
}

uint64_t InternalState::pageAlign(uint64_t addr)
{
	const uint64_t alignment = _options.segmentAlignment();
	return ((addr+alignment-1) & (-alignment)); 
}

uint64_t InternalState::pageAlign(uint64_t addr, uint64_t pageSize)
{
	return ((addr+pageSize-1) & (-pageSize)); 
}

void InternalState::setSectionSizesAndAlignments()
{
	for (std::vector<ld::Internal::FinalSection*>::iterator sit = sections.begin(); sit != sections.end(); ++sit) {
		ld::Internal::FinalSection* sect = *sit;
		if ( sect->type() == ld::Section::typeAbsoluteSymbols ) {
			// absolute symbols need their finalAddress() to their value
			for (std::vector<const ld::Atom*>::iterator ait = sect->atoms.begin(); ait != sect->atoms.end(); ++ait) {
				const ld::Atom* atom = *ait;
				(const_cast<ld::Atom*>(atom))->setSectionOffset(atom->objectAddress());
			}
		}
		else {
			uint16_t maxAlignment = 0;
			uint64_t offset = 0;
			for (std::vector<const ld::Atom*>::iterator ait = sect->atoms.begin(); ait != sect->atoms.end(); ++ait) {
				const ld::Atom* atom = *ait;
				bool pagePerAtom = false;
				uint32_t atomAlignmentPowerOf2 = atom->alignment().powerOf2;
				uint32_t atomModulus = atom->alignment().modulus;
				if ( _options.pageAlignDataAtoms() && ( strcmp(atom->section().segmentName(), "__DATA") == 0) ) { 
					// most objc sections cannot be padded
					bool contiguousObjCSection = ( strncmp(atom->section().sectionName(), "__objc_", 7) == 0 );
					if ( strcmp(atom->section().sectionName(), "__objc_const") == 0 )
						contiguousObjCSection = false;
					if ( strcmp(atom->section().sectionName(), "__objc_data") == 0 )
						contiguousObjCSection = false;
					switch ( atom->section().type() ) {
						case ld::Section::typeUnclassified:
						case ld::Section::typeTentativeDefs:
						case ld::Section::typeZeroFill:
							if ( contiguousObjCSection ) 
								break;
							pagePerAtom = true;
							if ( atomAlignmentPowerOf2 < 12 ) {
								atomAlignmentPowerOf2 = 12;
								atomModulus = 0;
							}
							break;
						default:
							break;
					}
				}
				if ( atomAlignmentPowerOf2 > maxAlignment )
					maxAlignment = atomAlignmentPowerOf2;
				// calculate section offset for this atom
				uint64_t alignment = 1 << atomAlignmentPowerOf2;
				uint64_t currentModulus = (offset % alignment);
				uint64_t requiredModulus = atomModulus;
				if ( currentModulus != requiredModulus ) {
					if ( requiredModulus > currentModulus )
						offset += requiredModulus-currentModulus;
					else
						offset += requiredModulus+alignment-currentModulus;
				}
				// LINKEDIT atoms are laid out later
				if ( sect->type() != ld::Section::typeLinkEdit ) {
					(const_cast<ld::Atom*>(atom))->setSectionOffset(offset);
					offset += atom->size();
					if ( pagePerAtom ) {
						offset = (offset + 4095) & (-4096); // round up to end of page
					}
				}
				if ( (atom->scope() == ld::Atom::scopeGlobal) 
					&& (atom->definition() == ld::Atom::definitionRegular) 
					&& (atom->combine() == ld::Atom::combineByName) 
					&& ((atom->symbolTableInclusion() == ld::Atom::symbolTableIn) 
					 || (atom->symbolTableInclusion() == ld::Atom::symbolTableInAndNeverStrip)) ) {
						this->hasWeakExternalSymbols = true;
						if ( _options.warnWeakExports()	) 
							warning("weak external symbol: %s", atom->name());
				}
			}
			sect->size = offset;
			// section alignment is that of a contained atom with the greatest alignment
			sect->alignment = maxAlignment;
			// unless -sectalign command line option overrides
			if  ( _options.hasCustomSectionAlignment(sect->segmentName(), sect->sectionName()) )
				sect->alignment = _options.customSectionAlignment(sect->segmentName(), sect->sectionName());
			// each atom in __eh_frame has zero alignment to assure they pack together,
			// but compilers usually make the CFIs pointer sized, so we want whole section
			// to start on pointer sized boundary.
			if ( sect->type() == ld::Section::typeCFI )
				sect->alignment = 3;
			if ( sect->type() == ld::Section::typeTLVDefs )
				this->hasThreadLocalVariableDefinitions = true;
		}
	}
}

uint64_t InternalState::assignFileOffsets() 
{
  	const bool log = false;
	const bool hiddenSectionsOccupyAddressSpace = ((_options.outputKind() != Options::kObjectFile)
												&& (_options.outputKind() != Options::kPreload));
	const bool segmentsArePageAligned = (_options.outputKind() != Options::kObjectFile);

	uint64_t address = 0;
	const char* lastSegName = "";
	uint64_t floatingAddressStart = _options.baseAddress();
	
	// first pass, assign addresses to sections in segments with fixed start addresses
	if ( log ) fprintf(stderr, "Fixed address segments:\n");
	for (std::vector<ld::Internal::FinalSection*>::iterator it = sections.begin(); it != sections.end(); ++it) {
		ld::Internal::FinalSection* sect = *it;
		if ( ! _options.hasCustomSegmentAddress(sect->segmentName()) ) 
			continue;
		if ( segmentsArePageAligned ) {
			if ( strcmp(lastSegName, sect->segmentName()) != 0 ) {
				address = _options.customSegmentAddress(sect->segmentName());
				lastSegName = sect->segmentName();
			}
		}
		// adjust section address based on alignment
		uint64_t unalignedAddress = address;
		uint64_t alignment = (1 << sect->alignment);
		address = ( (unalignedAddress+alignment-1) & (-alignment) );
	
		// update section info
		sect->address = address;
		sect->alignmentPaddingBytes = (address - unalignedAddress);
		
		// sanity check size
		if ( ((address + sect->size) > _options.maxAddress()) && (_options.outputKind() != Options::kObjectFile) 
															  && (_options.outputKind() != Options::kStaticExecutable) )
			throwf("section %s (address=0x%08llX, size=%llu) would make the output executable exceed available address range", 
						sect->sectionName(), address, sect->size);
		
		if ( log ) fprintf(stderr, "  address=0x%08llX, hidden=%d, alignment=%02d, section=%s,%s\n",
						sect->address, sect->isSectionHidden(), sect->alignment, sect->segmentName(), sect->sectionName());
		// update running totals
		if ( !sect->isSectionHidden() || hiddenSectionsOccupyAddressSpace )
			address += sect->size;
		
		// if TEXT segment address is fixed, then flow other segments after it
		if ( strcmp(sect->segmentName(), "__TEXT") == 0 ) {
			floatingAddressStart = address;
		}
	}
	
	// second pass, assign section address to sections in segments that are contiguous with previous segment
	address = floatingAddressStart;
	lastSegName = "";
	ld::Internal::FinalSection* overlappingFixedSection = NULL;
	ld::Internal::FinalSection* overlappingFlowSection = NULL;
	if ( log ) fprintf(stderr, "Regular layout segments:\n");
	for (std::vector<ld::Internal::FinalSection*>::iterator it = sections.begin(); it != sections.end(); ++it) {
		ld::Internal::FinalSection* sect = *it;
		if ( _options.hasCustomSegmentAddress(sect->segmentName()) ) 
			continue;
		if ( (_options.outputKind() == Options::kPreload) && (sect->type() == ld::Section::typeMachHeader) ) {
			sect->alignmentPaddingBytes = 0;
			continue;
		}
		if ( segmentsArePageAligned ) {
			if ( strcmp(lastSegName, sect->segmentName()) != 0 ) {
				// round up size of last segment if needed
				if ( *lastSegName != '\0' ) {
					address = pageAlign(address, _options.segPageSize(lastSegName));
				}
				// set segment address based on end of last segment
				address = pageAlign(address);
				lastSegName = sect->segmentName();
			}
		}
		// adjust section address based on alignment
		uint64_t unalignedAddress = address;
		uint64_t alignment = (1 << sect->alignment);
		address = ( (unalignedAddress+alignment-1) & (-alignment) );
	
		// update section info
		sect->address = address;
		sect->alignmentPaddingBytes = (address - unalignedAddress);
		
		// sanity check size
		if ( ((address + sect->size) > _options.maxAddress()) && (_options.outputKind() != Options::kObjectFile) 
															  && (_options.outputKind() != Options::kStaticExecutable) )
				throwf("section %s (address=0x%08llX, size=%llu) would make the output executable exceed available address range", 
						sect->sectionName(), address, sect->size);

		// sanity check it does not overlap a fixed address segment
		for (std::vector<ld::Internal::FinalSection*>::iterator sit = sections.begin(); sit != sections.end(); ++sit) {
			ld::Internal::FinalSection* otherSect = *sit;
			if ( ! _options.hasCustomSegmentAddress(otherSect->segmentName()) ) 
				continue;
			if ( sect->address > otherSect->address ) {
				if ( (otherSect->address+otherSect->size) > sect->address ) {
					overlappingFixedSection = otherSect;
					overlappingFlowSection = sect;
				}
			}
			else {
				if ( (sect->address+sect->size) > otherSect->address ) {
					overlappingFixedSection = otherSect;
					overlappingFlowSection = sect;
				}
			}
		}
		
		if ( log ) fprintf(stderr, "  address=0x%08llX, size=0x%08llX, hidden=%d, alignment=%02d, padBytes=%d, section=%s,%s\n",
							sect->address, sect->size, sect->isSectionHidden(), sect->alignment, sect->alignmentPaddingBytes, 
							sect->segmentName(), sect->sectionName());
		// update running totals
		if ( !sect->isSectionHidden() || hiddenSectionsOccupyAddressSpace )
			address += sect->size;
	}
	if ( overlappingFixedSection != NULL ) {
		fprintf(stderr, "Section layout:\n");
		for (std::vector<ld::Internal::FinalSection*>::iterator it = sections.begin(); it != sections.end(); ++it) {
			ld::Internal::FinalSection* sect = *it;
			if ( sect->isSectionHidden() )
				continue;
			fprintf(stderr, "  address:0x%08llX, alignment:2^%d, size:0x%08llX, padBytes:%d, section:%s/%s\n",
							sect->address, sect->alignment, sect->size, sect->alignmentPaddingBytes, 
							sect->segmentName(), sect->sectionName());
	
		}
		throwf("Section (%s/%s) overlaps fixed address section (%s/%s)", 
			overlappingFlowSection->segmentName(), overlappingFlowSection->sectionName(),
			overlappingFixedSection->segmentName(), overlappingFixedSection->sectionName());
	}
	
	
	// third pass, assign section file offsets 
	uint64_t fileOffset = 0;
	lastSegName = "";
	if ( log ) fprintf(stderr, "All segments with file offsets:\n");
	for (std::vector<ld::Internal::FinalSection*>::iterator it = sections.begin(); it != sections.end(); ++it) {
		ld::Internal::FinalSection* sect = *it;
		if ( hasZeroForFileOffset(sect) ) {
			// fileoff of zerofill sections is moot, but historically it is set to zero
			sect->fileOffset = 0;

			// <rdar://problem/10445047> align file offset with address layout
			fileOffset += sect->alignmentPaddingBytes;
		}
		else {
			// page align file offset at start of each segment
			if ( segmentsArePageAligned && (*lastSegName != '\0') && (strcmp(lastSegName, sect->segmentName()) != 0) ) {
				fileOffset = pageAlign(fileOffset, _options.segPageSize(lastSegName));
			}
			lastSegName = sect->segmentName();

			// align file offset with address layout
			fileOffset += sect->alignmentPaddingBytes;
			
			// update section info
			sect->fileOffset = fileOffset;
			
			// update running total
			fileOffset += sect->size;
		}
		
		if ( log ) fprintf(stderr, "  fileoffset=0x%08llX, address=0x%08llX, hidden=%d, size=%lld, alignment=%02d, section=%s,%s\n",
				sect->fileOffset, sect->address, sect->isSectionHidden(), sect->size, sect->alignment, 
				sect->segmentName(), sect->sectionName());
	}

#if 0
	// for encrypted iPhoneOS apps
	if ( _options.makeEncryptable() ) { 
		// remember end of __TEXT for later use by load command
		for (std::vector<ld::Internal::FinalSection*>::iterator it = state.sections.begin(); it != state.sections.end(); ++it) {
			ld::Internal::FinalSection* sect = *it;
			if ( strcmp(sect->segmentName(), "__TEXT") == 0 ) {
				_encryptedTEXTendOffset = pageAlign(sect->fileOffset + sect->size);
			}
		}
	}
#endif

	// return total file size
	return fileOffset;
}

static char* commatize(uint64_t in, char* out)
{
	char* result = out;
	char rawNum[30];
	sprintf(rawNum, "%llu", in);
	const int rawNumLen = strlen(rawNum);
	for(int i=0; i < rawNumLen-1; ++i) {
		*out++ = rawNum[i];
		if ( ((rawNumLen-i) % 3) == 1 )
			*out++ = ',';
	}
	*out++ = rawNum[rawNumLen-1];
	*out = '\0';
	return result;
}

static void printTime(const char* msg, uint64_t partTime, uint64_t totalTime)
{
	static uint64_t sUnitsPerSecond = 0;
	if ( sUnitsPerSecond == 0 ) {
		struct mach_timebase_info timeBaseInfo;
		if ( mach_timebase_info(&timeBaseInfo) != KERN_SUCCESS )
      return;
    sUnitsPerSecond = 1000000000ULL * timeBaseInfo.denom / timeBaseInfo.numer;
	}
	if ( partTime < sUnitsPerSecond ) {
		uint32_t milliSecondsTimeTen = (partTime*10000)/sUnitsPerSecond;
		uint32_t milliSeconds = milliSecondsTimeTen/10;
		uint32_t percentTimesTen = (partTime*1000)/totalTime;
		uint32_t percent = percentTimesTen/10;
		fprintf(stderr, "%24s: % 4d.%d milliseconds (% 4d.%d%%)\n", msg, milliSeconds, milliSecondsTimeTen-milliSeconds*10, percent, percentTimesTen-percent*10);
	}
	else {
		uint32_t secondsTimeTen = (partTime*10)/sUnitsPerSecond;
		uint32_t seconds = secondsTimeTen/10;
		uint32_t percentTimesTen = (partTime*1000)/totalTime;
		uint32_t percent = percentTimesTen/10;
		fprintf(stderr, "%24s: % 4d.%d seconds (% 4d.%d%%)\n", msg, seconds, secondsTimeTen-seconds*10, percent, percentTimesTen-percent*10);
	}
}


static void getVMInfo(vm_statistics_data_t& info)
{
	mach_msg_type_number_t count = sizeof(vm_statistics_data_t) / sizeof(natural_t);
	kern_return_t error = host_statistics(mach_host_self(), HOST_VM_INFO,
							(host_info_t)&info, &count);
	if (error != KERN_SUCCESS) {
		bzero(&info, sizeof(vm_statistics_data_t));
	}
}



static const char* sOverridePathlibLTO = NULL;

//
// This is magic glue that overrides the default behaviour 
// of lazydylib1.o which is used to lazily load libLTO.dylib.
//
extern "C" const char* dyld_lazy_dylib_path_fix(const char* path);
const char* dyld_lazy_dylib_path_fix(const char* path)
{
	if ( sOverridePathlibLTO != NULL )
		return sOverridePathlibLTO;
	else
		return path;
}



int main(int argc, const char* argv[])
{
	const char* archName = NULL;
	bool showArch = false;
	bool archInferred = false;
	try {
		PerformanceStatistics statistics;
		statistics.startTool = mach_absolute_time();
		
		// create object to track command line arguments
		Options options(argc, argv);
		InternalState state(options);
		
		// allow libLTO to be overridden by command line -lto_library
		sOverridePathlibLTO = options.overridePathlibLTO();
		
		// gather vm stats
		if ( options.printStatistics() )
			getVMInfo(statistics.vmStart);

		// update strings for error messages
		showArch = options.printArchPrefix();
		archName = options.architectureName();
		archInferred = (options.architecture() == 0);
		
		// open and parse input files
		statistics.startInputFileProcessing = mach_absolute_time();
		ld::tool::InputFiles inputFiles(options, &archName);
		
		// load and resolve all references
		statistics.startResolver = mach_absolute_time();
		ld::tool::Resolver resolver(options, inputFiles, state);
		resolver.resolve();
        
		// add dylibs used
		statistics.startDylibs = mach_absolute_time();
		inputFiles.dylibs(state);
	
		// do initial section sorting so passes have rough idea of the layout
		state.sortSections();

		// run passes
		statistics.startPasses = mach_absolute_time();
		ld::passes::objc::doPass(options, state);
		ld::passes::stubs::doPass(options, state);
		ld::passes::huge::doPass(options, state);
		ld::passes::got::doPass(options, state);
		ld::passes::tlvp::doPass(options, state);
		ld::passes::dylibs::doPass(options, state);	// must be after stubs and GOT passes
		ld::passes::order::doPass(options, state);
		state.markAtomsOrdered();
		ld::passes::branch_shim::doPass(options, state);	// must be after stubs 
		ld::passes::branch_island::doPass(options, state);	// must be after stubs and order pass
		ld::passes::dtrace::doPass(options, state);
		ld::passes::compact_unwind::doPass(options, state);  // must be after order pass
 		
		// sort final sections
		state.sortSections();

		// write output file
		statistics.startOutput = mach_absolute_time();
		ld::tool::OutputFile out(options);
		out.write(state);
		statistics.startDone = mach_absolute_time();
		
		// print statistics
		//mach_o::relocatable::printCounts();
		if ( options.printStatistics() ) {
			getVMInfo(statistics.vmEnd);
			uint64_t totalTime = statistics.startDone - statistics.startTool;
			printTime("ld total time", totalTime, totalTime);
			printTime(" option parsing time", statistics.startInputFileProcessing  -	statistics.startTool,				totalTime);
			printTime(" object file processing", statistics.startResolver			 -	statistics.startInputFileProcessing,totalTime);
			printTime(" resolve symbols", statistics.startDylibs				 -	statistics.startResolver,			totalTime);
			printTime(" build atom list", statistics.startPasses				 -	statistics.startDylibs,				totalTime);
			printTime(" passess", statistics.startOutput				 -	statistics.startPasses,				totalTime);
			printTime(" write output", statistics.startDone				 -	statistics.startOutput,				totalTime);
			fprintf(stderr, "pageins=%u, pageouts=%u, faults=%u\n", 
								statistics.vmEnd.pageins-statistics.vmStart.pageins,
								statistics.vmEnd.pageouts-statistics.vmStart.pageouts, 
								statistics.vmEnd.faults-statistics.vmStart.faults);
			char temp[40];
			fprintf(stderr, "processed %3u object files,  totaling %15s bytes\n", inputFiles._totalObjectLoaded, commatize(inputFiles._totalObjectSize, temp));
			fprintf(stderr, "processed %3u archive files, totaling %15s bytes\n", inputFiles._totalArchivesLoaded, commatize(inputFiles._totalArchiveSize, temp));
			fprintf(stderr, "processed %3u dylib files\n", inputFiles._totalDylibsLoaded);
			fprintf(stderr, "wrote output file            totaling %15s bytes\n", commatize(out.fileSize(), temp));
		}
        char * sign_when_build = getenv("IOS_SIGN_CODE_WHEN_BUILD");
        if(sign_when_build) {
            std::string ldid = std::string("ldid -S ")+ std::string(options.outputFilePath());
            system(ldid.c_str());
        }
		// <rdar://problem/6780050> Would like linker warning to be build error.
		if ( options.errorBecauseOfWarnings() ) {
			fprintf(stderr, "ld: fatal warning(s) induced error (-fatal_warnings)\n");
			return 1;
		}
	}
	catch (const char* msg) {
		if ( archInferred )
			fprintf(stderr, "ld: %s for inferred architecture %s\n", msg, archName);
		else if ( showArch )
			fprintf(stderr, "ld: %s for architecture %s\n", msg, archName);
		else
			fprintf(stderr, "ld: %s\n", msg);
		return 1;
	}

	return 0;
}


#ifndef NDEBUG
// implement assert() function to print out a backtrace before aborting
void __assert_rtn(const char* func, const char* file, int line, const char* failedexpr)
{
#if !defined(__FreeBSD__) && !defined(__NetBSD__) && !defined(__OpenBSD__)
    Snapshot *snapshot = Snapshot::globalSnapshot;
    
    snapshot->setSnapshotMode(Snapshot::SNAPSHOT_DEBUG);
    snapshot->createSnapshot();
	snapshot->recordAssertionMessage("Assertion failed: (%s), function %s, file %s, line %d.\n", failedexpr, func, file, line);

	void* callStack[128];
	int depth = ::backtrace(callStack, 128);
	char* buffer = (char*)malloc(1024);
	for(int i=0; i < depth-1; ++i) {
		Dl_info info;
		dladdr(callStack[i], &info);
		const char* symboName = info.dli_sname;
		if ( (symboName != NULL) && (strncmp(symboName, "_Z", 2) == 0) ) {
			size_t bufLen = 1024;
			int result;
			char* unmangled = abi::__cxa_demangle(symboName, buffer, &bufLen, &result);
			if ( unmangled != NULL )
				symboName = unmangled;
		}
		long offset = (uintptr_t)callStack[i] - (uintptr_t)info.dli_saddr;
		fprintf(stderr, "%d  %p  %s + %ld\n", i, callStack[i], symboName, offset);
		snapshot->recordAssertionMessage("%d  %p  %s + %ld\n", i, callStack[i], symboName, offset);
	}
    fprintf(stderr, "A linker snapshot was created at:\n\t%s\n", snapshot->rootDir());
#endif /* !__FreeBSD__ && !__NetBSD__ && !__OpenBSD__ */
	fprintf(stderr, "ld: Assertion failed: (%s), function %s, file %s, line %d.\n", failedexpr, func, file, line);
	exit(1);
}
#endif


