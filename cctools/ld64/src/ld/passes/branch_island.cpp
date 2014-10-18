/* -*- mode: C++; c-basic-offset: 4; tab-width: 4 -*-
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


#include <stdint.h>
#include <math.h>
#include <unistd.h>
#include <dlfcn.h>
#include <libkern/OSByteOrder.h>

#include <vector>
#include <map>

#include "MachOFileAbstraction.hpp"
#include "ld.hpp"
#include "branch_island.h"

namespace ld {
namespace passes {
namespace branch_island {




struct TargetAndOffset { const ld::Atom* atom; uint32_t offset; };
class TargetAndOffsetComparor
{
public:
	bool operator()(const TargetAndOffset& left, const TargetAndOffset& right) const
	{
		if ( left.atom != right.atom )
			return ( left.atom < right.atom );
		return ( left.offset < right.offset );
	}
};


static bool _s_log = false;
static ld::Section _s_text_section("__TEXT", "__text", ld::Section::typeCode);



class ARMtoARMBranchIslandAtom : public ld::Atom {
public:
											ARMtoARMBranchIslandAtom(const char* nm, const ld::Atom* target, TargetAndOffset finalTarget)
				: ld::Atom(_s_text_section, ld::Atom::definitionRegular, ld::Atom::combineNever,
							ld::Atom::scopeLinkageUnit, ld::Atom::typeBranchIsland, 
							ld::Atom::symbolTableIn, false, false, false, ld::Atom::Alignment(2)), 
				_name(nm),
				_target(target),
				_finalTarget(finalTarget) { }

	virtual const ld::File*					file() const					{ return NULL; }
	virtual const char*						name() const					{ return _name; }
	virtual uint64_t						size() const					{ return 4; }
	virtual uint64_t						objectAddress() const			{ return 0; }
	virtual void							copyRawContent(uint8_t buffer[]) const {
		int64_t displacement = _target->finalAddress() - this->finalAddress() - 8;
		if ( _target->contentType() == ld::Atom::typeBranchIsland ) {
			// an ARM branch can branch farther than a thumb branch.  The branch
			// island generation was conservative and put islands every thumb
			// branch distance apart.  Check to see if this is a an island
			// hopping branch that could be optimized to go directly to target.
			int64_t skipToFinalDisplacement = _finalTarget.atom->finalAddress() + _finalTarget.offset - this->finalAddress() - 8;
			if ( (skipToFinalDisplacement < 33554428LL) && (skipToFinalDisplacement > (-33554432LL)) ) {
				// can skip branch island and jump straight to target
				if (_s_log) fprintf(stderr, "%s: optimized jump to final target at 0x%08llX, thisAddr=0x%08llX\n", 
											_target->name(), _finalTarget.atom->finalAddress(), this->finalAddress());
				displacement = skipToFinalDisplacement;
			}
			else {
				// ultimate target is too far, jump to island
				if (_s_log) fprintf(stderr, "%s: jump to branch island at 0x%08llX\n", 
											_target->name(), _finalTarget.atom->finalAddress());
			}
		}
		uint32_t imm24 = (displacement >> 2) & 0x00FFFFFF;
		int32_t branchInstruction = 0xEA000000 | imm24;
		OSWriteLittleInt32(buffer, 0, branchInstruction);
	}
	virtual void							setScope(Scope)					{ }

private:
	const char*								_name;
	const ld::Atom*							_target;
	TargetAndOffset							_finalTarget;
};



class ARMtoThumb1BranchIslandAtom : public ld::Atom {
public:
											ARMtoThumb1BranchIslandAtom(const char* nm, const ld::Atom* target, TargetAndOffset finalTarget)
				: ld::Atom(_s_text_section, ld::Atom::definitionRegular, ld::Atom::combineNever,
							ld::Atom::scopeLinkageUnit, ld::Atom::typeBranchIsland, 
							ld::Atom::symbolTableIn, false, false, false, ld::Atom::Alignment(2)), 
				_name(nm),
				_target(target),
				_finalTarget(finalTarget) { }

	virtual const ld::File*					file() const					{ return NULL; }
	virtual const char*						name() const					{ return _name; }
	virtual uint64_t						size() const					{ return 16; }
	virtual uint64_t						objectAddress() const			{ return 0; }
	virtual void							copyRawContent(uint8_t buffer[]) const {
		// There is no large displacement thumb1 branch instruction.
		// Instead use ARM instructions that can jump to thumb.
		// we use a 32-bit displacement, so we can directly jump to target which means no island hopping
		int64_t displacement = _finalTarget.atom->finalAddress() + _finalTarget.offset - (this->finalAddress() + 12);
		if ( _finalTarget.atom->isThumb() )
			displacement |= 1;
		if (_s_log) fprintf(stderr, "%s: 4 ARM instruction jump to final target at 0x%08llX\n", 
										_target->name(), _finalTarget.atom->finalAddress());
		OSWriteLittleInt32(&buffer[ 0], 0, 0xe59fc004);	// 	ldr  ip, pc + 4
		OSWriteLittleInt32(&buffer[ 4], 0, 0xe08fc00c);	// 	add	 ip, pc, ip
		OSWriteLittleInt32(&buffer[ 8], 0, 0xe12fff1c);	// 	bx	 ip
		OSWriteLittleInt32(&buffer[12], 0, displacement);	// 	.long target-this		
	}
	virtual void							setScope(Scope)					{ }

private:
	const char*								_name;
	const ld::Atom*							_target;
	TargetAndOffset							_finalTarget;
};



class Thumb2toThumbBranchIslandAtom : public ld::Atom {
public:
											Thumb2toThumbBranchIslandAtom(const char* nm, const ld::Atom* target, TargetAndOffset finalTarget)
				: ld::Atom(_s_text_section, ld::Atom::definitionRegular, ld::Atom::combineNever,
							ld::Atom::scopeLinkageUnit, ld::Atom::typeBranchIsland, 
							ld::Atom::symbolTableIn, false, true, false, ld::Atom::Alignment(1)), 
				_name(nm),
				_target(target),
				_finalTarget(finalTarget) { }

	virtual const ld::File*					file() const					{ return NULL; }
	virtual const char*						name() const					{ return _name; }
	virtual uint64_t						size() const					{ return 4; }
	virtual uint64_t						objectAddress() const			{ return 0; }
	virtual void							copyRawContent(uint8_t buffer[]) const {
		int64_t displacement = _target->finalAddress() - this->finalAddress() - 4;
		if ( _target->contentType() == ld::Atom::typeBranchIsland ) {
			// an ARM branch can branch farther than a thumb branch.  The branch
			// island generation was conservative and put islands every thumb
			// branch distance apart.  Check to see if this is a an island
			// hopping branch that could be optimized to go directly to target.
			int64_t skipToFinalDisplacement = _finalTarget.atom->finalAddress() + _finalTarget.offset - this->finalAddress() - 4;
			if ( (skipToFinalDisplacement < 16777214) && (skipToFinalDisplacement > (-16777216LL)) ) {
				// can skip branch island and jump straight to target
				if (_s_log) fprintf(stderr, "%s: optimized jump to final target at 0x%08llX, thisAddr=0x%08llX\n", 
											_target->name(), _finalTarget.atom->finalAddress(), this->finalAddress());
				displacement = skipToFinalDisplacement;
			}
			else {
				// ultimate target is too far for thumb2 branch, jump to island
				if (_s_log) fprintf(stderr, "%s: jump to branch island at 0x%08llX\n", 
											_target->name(), _finalTarget.atom->finalAddress());
			}
		}
		// The instruction is really two instructions:
		// The lower 16 bits are the first instruction, which contains the high
		//   11 bits of the displacement.
		// The upper 16 bits are the second instruction, which contains the low
		//   11 bits of the displacement, as well as differentiating bl and blx.
		uint32_t s = (uint32_t)(displacement >> 24) & 0x1;
		uint32_t i1 = (uint32_t)(displacement >> 23) & 0x1;
		uint32_t i2 = (uint32_t)(displacement >> 22) & 0x1;
		uint32_t imm10 = (uint32_t)(displacement >> 12) & 0x3FF;
		uint32_t imm11 = (uint32_t)(displacement >> 1) & 0x7FF;
		uint32_t j1 = (i1 == s);
		uint32_t j2 = (i2 == s);
		uint32_t opcode = 0x9000F000;
		uint32_t nextDisp = (j1 << 13) | (j2 << 11) | imm11;
		uint32_t firstDisp = (s << 10) | imm10;
		uint32_t newInstruction = opcode | (nextDisp << 16) | firstDisp;
		//warning("s=%d, j1=%d, j2=%d, imm10=0x%0X, imm11=0x%0X, opcode=0x%08X, first=0x%04X, next=0x%04X, new=0x%08X, disp=0x%llX for %s to %s\n",
		//	s, j1, j2, imm10, imm11, opcode, firstDisp, nextDisp, newInstruction, displacement, inAtom->getDisplayName(), ref->getTarget().getDisplayName());
		OSWriteLittleInt32(buffer, 0, newInstruction);
	}
	virtual void							setScope(Scope)					{ }

private:
	const char*								_name;
	const ld::Atom*							_target;
	TargetAndOffset							_finalTarget;
};


class NoPicARMtoThumbMBranchIslandAtom : public ld::Atom {
public:
											NoPicARMtoThumbMBranchIslandAtom(const char* nm, const ld::Atom* target, TargetAndOffset finalTarget)
				: ld::Atom(_s_text_section, ld::Atom::definitionRegular, ld::Atom::combineNever,
							ld::Atom::scopeLinkageUnit, ld::Atom::typeBranchIsland, 
							ld::Atom::symbolTableIn, false, false, false, ld::Atom::Alignment(2)), 
				_name(nm),
				_target(target),
				_finalTarget(finalTarget) { }

	virtual const ld::File*					file() const					{ return NULL; }
	virtual const char*						name() const					{ return _name; }
	virtual uint64_t						size() const					{ return 8; }
	virtual uint64_t						objectAddress() const			{ return 0; }
	virtual void							copyRawContent(uint8_t buffer[]) const {
		// There is no large displacement thumb1 branch instruction.
		// Instead use ARM instructions that can jump to thumb.
		// we use a 32-bit displacement, so we can directly jump to final target which means no island hopping
		uint32_t targetAddr = _finalTarget.atom->finalAddress();
		if ( _finalTarget.atom->isThumb() )
			targetAddr |= 1;
		if (_s_log) fprintf(stderr, "%s: 2 ARM instruction jump to final target at 0x%08llX\n",
									_target->name(), _finalTarget.atom->finalAddress());
		OSWriteLittleInt32(&buffer[0], 0, 0xe51ff004);	// 	ldr	pc, [pc, #-4]
		OSWriteLittleInt32(&buffer[4], 0, targetAddr);	// 	.long target-this		
	}
	virtual void							setScope(Scope)					{ }

private:
	const char*								_name;
	const ld::Atom*							_target;
	TargetAndOffset							_finalTarget;
};


static ld::Atom* makeBranchIsland(const Options& opts, ld::Fixup::Kind kind, int islandRegion, const ld::Atom* nextTarget, TargetAndOffset finalTarget)
{
	char* name;
	if ( finalTarget.offset == 0 ) {
		if ( islandRegion == 0 )
			asprintf(&name, "%s.island", finalTarget.atom->name());
		else
			asprintf(&name, "%s.island.%d", finalTarget.atom->name(), islandRegion+1);
	}
	else {
		asprintf(&name, "%s_plus_%d.island.%d", finalTarget.atom->name(), finalTarget.offset, islandRegion);
	}

	switch ( kind ) {
		case ld::Fixup::kindStoreARMBranch24:
		case ld::Fixup::kindStoreThumbBranch22:
		case ld::Fixup::kindStoreTargetAddressARMBranch24:
		case ld::Fixup::kindStoreTargetAddressThumbBranch22:
			if ( finalTarget.atom->isThumb() ) {
				if ( opts.preferSubArchitecture() && opts.archSupportsThumb2() ) {
					return new Thumb2toThumbBranchIslandAtom(name, nextTarget, finalTarget);
				}
				else if ( opts.outputSlidable() ) {
					return new ARMtoThumb1BranchIslandAtom(name, nextTarget, finalTarget);
				}
				else {
					return new NoPicARMtoThumbMBranchIslandAtom(name, nextTarget, finalTarget);
				}
			}
			else {
				return new ARMtoARMBranchIslandAtom(name, nextTarget, finalTarget);
			}
			break;
		default:
			assert(0 && "unexpected branch kind");
			break;
	}
	return NULL;
}


static uint64_t textSizeWhenMightNeedBranchIslands(const Options& opts, bool seenThumbBranch)
{
	switch ( opts.architecture() ) {
		case CPU_TYPE_ARM:
			if ( ! seenThumbBranch )
				return 32000000;  // ARM can branch +/- 32MB
			else if ( opts.preferSubArchitecture() && opts.archSupportsThumb2() ) 
				return 16000000;  // thumb2 can branch +/- 16MB
			else
				return  4000000;  // thumb1 can branch +/- 4MB
			break;
	}
	assert(0 && "unexpected architecture");
	return 0x100000000LL;
}


static uint64_t maxDistanceBetweenIslands(const Options& opts, bool seenThumbBranch)
{
	switch ( opts.architecture() ) {
		case CPU_TYPE_ARM:
			if ( ! seenThumbBranch )
				return 30*1024*1024;	// 2MB of branch islands per 32MB
			else if ( opts.preferSubArchitecture() && opts.archSupportsThumb2() ) 
				return 14*1024*1024;	// 2MB of branch islands per 16MB
			else
				return 3500000;			// 0.5MB of branch islands per 4MB
			break;
	}
	assert(0 && "unexpected architecture");
	return 0x100000000LL;
}


//
// PowerPC can do PC relative branches as far as +/-16MB.
// If a branch target is >16MB then we insert one or more
// "branch islands" between the branch and its target that
// allows island hopping to the target.
//
// Branch Island Algorithm
//
// If the __TEXT segment < 16MB, then no branch islands needed
// Otherwise, every 14MB into the __TEXT segment a region is
// added which can contain branch islands.  Every out-of-range
// bl instruction is checked.  If it crosses a region, an island
// is added to that region with the same target and the bl is
// adjusted to target the island instead.
//
// In theory, if too many islands are added to one region, it
// could grow the __TEXT enough that other previously in-range
// bl branches could be pushed out of range.  We reduce the
// probability this could happen by placing the ranges every
// 14MB which means the region would have to be 2MB (512,000 islands)
// before any branches could be pushed out of range.
//

void doPass(const Options& opts, ld::Internal& state)
{	
	// only make branch islands in final linked images
	if ( opts.outputKind() == Options::kObjectFile )
		return;

	// only ARM needs branch islands
	switch ( opts.architecture() ) {
		case CPU_TYPE_ARM:
			break;
		default:
			return;
	}
	
	// scan to find __text section
	ld::Internal::FinalSection* textSection = NULL;
	for (std::vector<ld::Internal::FinalSection*>::iterator sit=state.sections.begin(); sit != state.sections.end(); ++sit) {
		ld::Internal::FinalSection* sect = *sit;
		if ( strcmp(sect->sectionName(), "__text") == 0 )
			textSection = sect;
	}
	if ( textSection == NULL )
		return;
	
	// assign section offsets to each atom in __text section, watch for thumb branches, and find total size
	const bool isARM = (opts.architecture() == CPU_TYPE_ARM);
	bool hasThumbBranches = false;
	uint64_t offset = 0;
	for (std::vector<const ld::Atom*>::iterator ait=textSection->atoms.begin();  ait != textSection->atoms.end(); ++ait) {
		const ld::Atom* atom = *ait;
		// check for thumb branches
		if ( isARM && ~hasThumbBranches ) {
			for (ld::Fixup::iterator fit = atom->fixupsBegin(), end=atom->fixupsEnd(); fit != end; ++fit) {
				switch ( fit->kind ) {
					case ld::Fixup::kindStoreThumbBranch22:
					case ld::Fixup::kindStoreTargetAddressThumbBranch22:
						hasThumbBranches = true;
						break;
					default:
						break;
				}
			}
		}
		// align atom
		ld::Atom::Alignment atomAlign = atom->alignment();
		uint64_t atomAlignP2 = (1 << atomAlign.powerOf2);
		uint64_t currentModulus = (offset % atomAlignP2);
		if ( currentModulus != atomAlign.modulus ) {
			if ( atomAlign.modulus > currentModulus )
				offset += atomAlign.modulus-currentModulus;
			else
				offset += atomAlign.modulus+atomAlignP2-currentModulus;		
		}
		(const_cast<ld::Atom*>(atom))->setSectionOffset(offset);
		offset += atom->size();
	}
	uint64_t totalTextSize = offset;
	if ( totalTextSize < textSizeWhenMightNeedBranchIslands(opts, hasThumbBranches) )
		return;
	if (_s_log) fprintf(stderr, "ld:  __text section size=%llu, might need branch islands\n", totalTextSize);
	
	// Figure out how many regions of branch islands will be needed, and their locations.
	// Construct a vector containing the atoms after which branch islands will be inserted,
	// taking into account follow on fixups. No atom run without an island can exceed kBetweenRegions.
	const uint64_t kBetweenRegions = maxDistanceBetweenIslands(opts, hasThumbBranches); // place regions of islands every 14MB in __text section
	std::vector<const ld::Atom*> branchIslandInsertionPoints; // atoms in the atom list after which branch islands will be inserted
	uint64_t previousIslandEndAddr = 0;
	const ld::Atom *insertionPoint;
	branchIslandInsertionPoints.reserve(totalTextSize/kBetweenRegions*2);
	for (std::vector<const ld::Atom*>::iterator it=textSection->atoms.begin(); it != textSection->atoms.end(); it++) {
		const ld::Atom* atom = *it;
		// if we move past the next atom, will the run length exceed kBetweenRegions?
		if ( atom->sectionOffset() + atom->size() - previousIslandEndAddr > kBetweenRegions ) {
			// yes. Add the last known good location (atom) for inserting a branch island.
			if ( insertionPoint == NULL )
				throwf("Unable to insert branch island. No insertion point available.");
			branchIslandInsertionPoints.push_back(insertionPoint);
			previousIslandEndAddr = insertionPoint->sectionOffset()+insertionPoint->size();
			insertionPoint = NULL;
		}
		// Can we insert an island after this atom? If so then keep track of it.
		if ( !atom->hasFixupsOfKind(ld::Fixup::kindNoneFollowOn) )
			insertionPoint = atom;
	}
	// add one more island after the last atom
	if (insertionPoint != NULL)
		branchIslandInsertionPoints.push_back(insertionPoint);
	const int kIslandRegionsCount = branchIslandInsertionPoints.size();
	if (_s_log) {
		fprintf(stderr, "ld: will use %u branch island regions\n", kIslandRegionsCount);
		for (std::vector<const ld::Atom*>::iterator it = branchIslandInsertionPoints.begin(); it != branchIslandInsertionPoints.end(); ++it) {
			const ld::Atom* atom = *it;
			const ld::File *file = atom->file();
			fprintf(stderr, "ld: branch island will be inserted at 0x%llx after %s", atom->sectionOffset()+atom->size(), atom->name());
			if (file) fprintf(stderr, " (%s)", atom->file()->path());
			fprintf(stderr, "\n");
		}
	}


	typedef std::map<TargetAndOffset,const ld::Atom*, TargetAndOffsetComparor> AtomToIsland;
    AtomToIsland* regionsMap[kIslandRegionsCount];
	std::vector<const ld::Atom*>* regionsIslands[kIslandRegionsCount];
	for(int i=0; i < kIslandRegionsCount; ++i) {
		regionsMap[i] = new AtomToIsland();
		regionsIslands[i] = new std::vector<const ld::Atom*>();
	}
	unsigned int islandCount = 0;
	
	// create islands for branches in __text that are out of range
	for (std::vector<const ld::Atom*>::iterator ait=textSection->atoms.begin(); ait != textSection->atoms.end(); ++ait) {
		const ld::Atom* atom = *ait;
		const ld::Atom* target = NULL;
		uint64_t addend = 0;
		ld::Fixup* fixupWithTarget = NULL;
		for (ld::Fixup::iterator fit = atom->fixupsBegin(), end=atom->fixupsEnd(); fit != end; ++fit) {
			if ( fit->firstInCluster() ) {
				target = NULL;
				fixupWithTarget = NULL;
				addend = 0;
			}
			switch ( fit->binding ) {
				case ld::Fixup::bindingNone:
				case ld::Fixup::bindingByNameUnbound:
					break;
				case ld::Fixup::bindingByContentBound:
				case ld::Fixup::bindingDirectlyBound:
					target = fit->u.target;
					fixupWithTarget = fit;
					break;
				case ld::Fixup::bindingsIndirectlyBound:
					target = state.indirectBindingTable[fit->u.bindingIndex];
					fixupWithTarget = fit;
					break;
			}
			bool haveBranch = false;
			switch (fit->kind) {
				case ld::Fixup::kindAddAddend:
					addend = fit->u.addend;
					break;
				case ld::Fixup::kindStoreARMBranch24:
				case ld::Fixup::kindStoreThumbBranch22:
				case ld::Fixup::kindStoreTargetAddressARMBranch24:
				case ld::Fixup::kindStoreTargetAddressThumbBranch22:
					haveBranch = true;
					break;
                default:
                    break;   
			}
			if ( haveBranch ) {
				int64_t srcAddr = atom->sectionOffset() + fit->offsetInAtom;
				int64_t dstAddr = target->sectionOffset() + addend;
				if ( target->section().type() == ld::Section::typeStub )
					dstAddr = totalTextSize;
				int64_t displacement = dstAddr - srcAddr;
				TargetAndOffset finalTargetAndOffset = { target, static_cast<uint32_t>(addend) };
				const int64_t kBranchLimit = kBetweenRegions;
				if ( displacement > kBranchLimit ) {
					// create forward branch chain
					const ld::Atom* nextTarget = target;
					for (int i=kIslandRegionsCount-1; i >=0 ; --i) {
						AtomToIsland* region = regionsMap[i];
						int64_t islandRegionAddr = kBetweenRegions * (i+1);
						if ( (srcAddr < islandRegionAddr) && (islandRegionAddr <= dstAddr) ) { 
							AtomToIsland::iterator pos = region->find(finalTargetAndOffset);
							if ( pos == region->end() ) {
								ld::Atom* island = makeBranchIsland(opts, fit->kind, i, nextTarget, finalTargetAndOffset);
								(*region)[finalTargetAndOffset] = island;
								if (_s_log) fprintf(stderr, "added island %s to region %d for %s\n", island->name(), i, atom->name());
								regionsIslands[i]->push_back(island);
								++islandCount;
								nextTarget = island;
							}
							else {
								nextTarget = pos->second;
							}
						}
					}
					if (_s_log) fprintf(stderr, "using island %s for branch to %s from %s\n", nextTarget->name(), target->name(), atom->name());
					fixupWithTarget->u.target = nextTarget;
					fixupWithTarget->binding = ld::Fixup::bindingDirectlyBound;
				}
				else if ( displacement < (-kBranchLimit) ) {
					// create back branching chain
					const ld::Atom* prevTarget = target;
					for (int i=0; i < kIslandRegionsCount ; ++i) {
						AtomToIsland* region = regionsMap[i];
						int64_t islandRegionAddr = kBetweenRegions * (i+1);
						if ( (dstAddr <= islandRegionAddr) && (islandRegionAddr < srcAddr) ) {
							AtomToIsland::iterator pos = region->find(finalTargetAndOffset);
							if ( pos == region->end() ) {
								ld::Atom* island = makeBranchIsland(opts, fit->kind, i, prevTarget, finalTargetAndOffset);
								(*region)[finalTargetAndOffset] = island;
								if (_s_log) fprintf(stderr, "added back island %s to region %d for %s\n", island->name(), i, atom->name());
								regionsIslands[i]->push_back(island);
								++islandCount;
								prevTarget = island;
							}
							else {
								prevTarget = pos->second;
							}
						}
					}
					if (_s_log) fprintf(stderr, "using back island %s for %s\n", prevTarget->name(), atom->name());
					fixupWithTarget->u.target = prevTarget;
					fixupWithTarget->binding = ld::Fixup::bindingDirectlyBound;
				}
			}
		}
	}


	// insert islands into __text section and adjust section offsets
	if ( islandCount > 0 ) {
		if ( _s_log ) fprintf(stderr, "ld: %u branch islands required in %u regions\n", islandCount, kIslandRegionsCount);
		std::vector<const ld::Atom*> newAtomList;
		newAtomList.reserve(textSection->atoms.size()+islandCount);
		
		uint64_t regionIndex = 0;
		for (std::vector<const ld::Atom*>::iterator ait=textSection->atoms.begin(); ait != textSection->atoms.end(); ait++) {
			newAtomList.push_back(*ait);
			// copy over atoms until we find an island insertion point
			// Note that the last insertion point is the last atom, so this loop never moves the iterator to atoms.end().
			while (*ait != branchIslandInsertionPoints[regionIndex]) {
				ait++;
				newAtomList.push_back(*ait);
			}
			
			// insert the branch island atoms after the insertion point atom
			std::vector<const ld::Atom*>* regionIslands = regionsIslands[regionIndex];
			for (std::vector<const ld::Atom*>::iterator rit=regionIslands->begin(); rit != regionIslands->end(); rit++) {
				const ld::Atom* islandAtom = *rit;
				newAtomList.push_back(islandAtom);
				if ( _s_log ) fprintf(stderr, "inserting island %s into __text section\n", islandAtom->name());
			}
			regionIndex++;
		}
		// swap in new list of atoms for __text section
		textSection->atoms.clear();
		textSection->atoms = newAtomList;
	}

}


} // namespace branch_island
} // namespace passes 
} // namespace ld 
