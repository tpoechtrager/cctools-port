/* -*- mode: C++; c-basic-offset: 4; tab-width: 4 -*-*
 *
 * Copyright (c) 2009-2011 Apple Inc. All rights reserved.
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
#include <sys/sysctl.h>
#include <sys/param.h>
#include <sys/mount.h>
#include <fcntl.h>
#include <errno.h>
#include <limits.h>
#include <unistd.h>
#include <mach/mach_time.h>
#include <mach/vm_statistics.h>
#include <mach/mach_init.h>
#include <mach/mach_host.h>
#include <uuid/uuid.h>
#include <dlfcn.h>
#include <mach-o/dyld.h>
#include <mach-o/fat.h>

#include <string>
#include <map>
#include <set>
#include <string>
#include <vector>
#include <list>
#include <algorithm>
#include <unordered_set>

#include <CommonCrypto/CommonDigest.h>
#include <AvailabilityMacros.h>

#include "MachOTrie.hpp"

#include "Options.h"

#include "OutputFile.h"
#include "Architectures.hpp"
#include "HeaderAndLoadCommands.hpp"
#include "LinkEdit.hpp"
#include "LinkEditClassic.hpp"


namespace ld {
namespace tool {


OutputFile::OutputFile(const Options& opts) 
	:
		hasWeakExternalSymbols(false), usesWeakExternalSymbols(false), overridesWeakExternalSymbols(false), 
		_noReExportedDylibs(false), hasThreadLocalVariableDefinitions(false), pieDisabled(false), hasDataInCode(false), 
		headerAndLoadCommandsSection(NULL),
		rebaseSection(NULL), bindingSection(NULL), weakBindingSection(NULL), 
		lazyBindingSection(NULL), exportSection(NULL), 
		splitSegInfoSection(NULL), functionStartsSection(NULL), 
		dataInCodeSection(NULL), dependentDRsSection(NULL), 
		symbolTableSection(NULL), stringPoolSection(NULL), 
		localRelocationsSection(NULL), externalRelocationsSection(NULL), 
		sectionRelocationsSection(NULL), 
		indirectSymbolTableSection(NULL), 
		_options(opts),
		_hasDyldInfo(opts.makeCompressedDyldInfo()),
		_hasSymbolTable(true),
		_hasSectionRelocations(opts.outputKind() == Options::kObjectFile),
		_hasSplitSegInfo(opts.sharedRegionEligible()),
		_hasFunctionStartsInfo(opts.addFunctionStarts()),
		_hasDataInCodeInfo(opts.addDataInCodeInfo()),
		_hasDependentDRInfo(opts.needsDependentDRInfo()),
		_hasDynamicSymbolTable(true),
		_hasLocalRelocations(!opts.makeCompressedDyldInfo()),
		_hasExternalRelocations(!opts.makeCompressedDyldInfo()),
		_encryptedTEXTstartOffset(0),
		_encryptedTEXTendOffset(0),
		_localSymbolsStartIndex(0),
		_localSymbolsCount(0),
		_globalSymbolsStartIndex(0),
		_globalSymbolsCount(0),
		_importSymbolsStartIndex(0),
		_importSymbolsCount(0),
		_sectionsRelocationsAtom(NULL),
		_localRelocsAtom(NULL),
		_externalRelocsAtom(NULL),
		_symbolTableAtom(NULL),
		_indirectSymbolTableAtom(NULL),
		_rebasingInfoAtom(NULL),
		_bindingInfoAtom(NULL),
		_lazyBindingInfoAtom(NULL),
		_weakBindingInfoAtom(NULL),
		_exportInfoAtom(NULL),
		_splitSegInfoAtom(NULL),
		_functionStartsAtom(NULL),
		_dataInCodeAtom(NULL),
		_dependentDRInfoAtom(NULL)
{
}

void OutputFile::dumpAtomsBySection(ld::Internal& state, bool printAtoms)
{
	fprintf(stderr, "SORTED:\n");
	for (std::vector<ld::Internal::FinalSection*>::iterator it = state.sections.begin(); it != state.sections.end(); ++it) {
		fprintf(stderr, "final section %p %s/%s %s start addr=0x%08llX, size=0x%08llX, alignment=%02d, fileOffset=0x%08llX\n", 
				(*it), (*it)->segmentName(), (*it)->sectionName(), (*it)->isSectionHidden() ? "(hidden)" : "", 
				(*it)->address, (*it)->size, (*it)->alignment, (*it)->fileOffset);
		if ( printAtoms ) {
			std::vector<const ld::Atom*>& atoms = (*it)->atoms;
			for (std::vector<const ld::Atom*>::iterator ait = atoms.begin(); ait != atoms.end(); ++ait) {
				fprintf(stderr, "   %p (0x%04llX) %s\n", *ait, (*ait)->size(), (*ait)->name());
			}
		}
	}
	fprintf(stderr, "DYLIBS:\n");
	for (std::vector<ld::dylib::File*>::iterator it=state.dylibs.begin(); it != state.dylibs.end(); ++it )
		fprintf(stderr, "  %s\n", (*it)->installPath());
}	

void OutputFile::write(ld::Internal& state)
{
	this->buildDylibOrdinalMapping(state);
	this->addLoadCommands(state);
	this->addLinkEdit(state);
	this->setSectionSizesAndAlignments(state);
	this->setLoadCommandsPadding(state);
	this->assignFileOffsets(state);
	this->assignAtomAddresses(state);
	this->synthesizeDebugNotes(state);
	this->buildSymbolTable(state);
	this->generateLinkEditInfo(state);
	this->makeSplitSegInfo(state);
	this->updateLINKEDITAddresses(state);
	//this->dumpAtomsBySection(state, false);
	this->writeOutputFile(state);
	this->writeMapFile(state);
}

bool OutputFile::findSegment(ld::Internal& state, uint64_t addr, uint64_t* start, uint64_t* end, uint32_t* index)
{
	uint32_t segIndex = 0;
	ld::Internal::FinalSection* segFirstSection = NULL;
	ld::Internal::FinalSection* lastSection = NULL;
	for (std::vector<ld::Internal::FinalSection*>::iterator it = state.sections.begin(); it != state.sections.end(); ++it) {
		ld::Internal::FinalSection* sect = *it;
		if ( (segFirstSection == NULL ) || strcmp(segFirstSection->segmentName(), sect->segmentName()) != 0 ) {
			if ( segFirstSection != NULL ) {
				//fprintf(stderr, "findSegment(0x%llX) seg changed to %s\n", addr, sect->segmentName());
				if ( (addr >= segFirstSection->address) && (addr < lastSection->address+lastSection->size) ) {
					*start = segFirstSection->address;
					*end = lastSection->address+lastSection->size;
					*index = segIndex;
					return true;
				}
				++segIndex;
			}
			segFirstSection = sect;
		}
		lastSection = sect;
	}
	return false;
}


void OutputFile::assignAtomAddresses(ld::Internal& state)
{
	const bool log = false;
	if ( log ) fprintf(stderr, "assignAtomAddresses()\n");
	for (std::vector<ld::Internal::FinalSection*>::iterator sit = state.sections.begin(); sit != state.sections.end(); ++sit) {
		ld::Internal::FinalSection* sect = *sit;
		if ( log ) fprintf(stderr, "  section=%s/%s\n", sect->segmentName(), sect->sectionName());
		for (std::vector<const ld::Atom*>::iterator ait = sect->atoms.begin(); ait != sect->atoms.end(); ++ait) {
			const ld::Atom* atom = *ait;
			if ( log ) fprintf(stderr, "    atom=%p, name=%s\n", atom, atom->name());
			switch ( sect-> type() ) {
				case ld::Section::typeImportProxies:
					// want finalAddress() of all proxy atoms to be zero
					(const_cast<ld::Atom*>(atom))->setSectionStartAddress(0);
					break;
				case ld::Section::typeAbsoluteSymbols:
					// want finalAddress() of all absolute atoms to be value of abs symbol
					(const_cast<ld::Atom*>(atom))->setSectionStartAddress(0);
					break;
				case ld::Section::typeLinkEdit:
					// linkedit layout is assigned later
					break;
				default:
					(const_cast<ld::Atom*>(atom))->setSectionStartAddress(sect->address);
					break;
			}
		}
	}
}

void OutputFile::updateLINKEDITAddresses(ld::Internal& state)
{
	if ( _options.makeCompressedDyldInfo() ) {
		// build dylb rebasing info  
		assert(_rebasingInfoAtom != NULL);
		_rebasingInfoAtom->encode();
		
		// build dyld binding info  
		assert(_bindingInfoAtom != NULL);
		_bindingInfoAtom->encode();
		
		// build dyld lazy binding info  
		assert(_lazyBindingInfoAtom != NULL);
		_lazyBindingInfoAtom->encode();
		
		// build dyld weak binding info  
		assert(_weakBindingInfoAtom != NULL);
		_weakBindingInfoAtom->encode();
		
		// build dyld export info  
		assert(_exportInfoAtom != NULL);
		_exportInfoAtom->encode();
	}
	
	if ( _options.sharedRegionEligible() ) {
		// build split seg info  
		assert(_splitSegInfoAtom != NULL);
		_splitSegInfoAtom->encode();
	}

	if ( _options.addFunctionStarts() ) {
		// build function starts info  
		assert(_functionStartsAtom != NULL);
		_functionStartsAtom->encode();
	}

	if ( _options.addDataInCodeInfo() ) {
		// build data-in-code info  
		assert(_dataInCodeAtom != NULL);
		_dataInCodeAtom->encode();
	}
	
	if ( _options.needsDependentDRInfo() ) {
		// build dependent dylib DR info  
		assert(_dependentDRInfoAtom != NULL);
		_dependentDRInfoAtom->encode();
	}

	// build classic symbol table  
	assert(_symbolTableAtom != NULL);
	_symbolTableAtom->encode();
	assert(_indirectSymbolTableAtom != NULL);
	_indirectSymbolTableAtom->encode();

	// add relocations to .o files
	if ( _options.outputKind() == Options::kObjectFile ) {
		assert(_sectionsRelocationsAtom != NULL);
		_sectionsRelocationsAtom->encode();
	}

	if ( ! _options.makeCompressedDyldInfo() ) {
		// build external relocations 
		assert(_externalRelocsAtom != NULL);
		_externalRelocsAtom->encode();
		// build local relocations 
		assert(_localRelocsAtom != NULL);
		_localRelocsAtom->encode();
	}

	// update address and file offsets now that linkedit content has been generated
	uint64_t curLinkEditAddress = 0;
	uint64_t curLinkEditfileOffset = 0;
	for (std::vector<ld::Internal::FinalSection*>::iterator sit = state.sections.begin(); sit != state.sections.end(); ++sit) {
		ld::Internal::FinalSection* sect = *sit;
		if ( sect->type() != ld::Section::typeLinkEdit ) 
			continue;
		if ( curLinkEditAddress == 0 ) {
			curLinkEditAddress = sect->address;
			curLinkEditfileOffset = sect->fileOffset;
		}
		uint16_t maxAlignment = 0;
		uint64_t offset = 0;
		for (std::vector<const ld::Atom*>::iterator ait = sect->atoms.begin(); ait != sect->atoms.end(); ++ait) {
			const ld::Atom* atom = *ait;
			//fprintf(stderr, "setting linkedit atom offset for %s\n", atom->name());
			if ( atom->alignment().powerOf2 > maxAlignment )
				maxAlignment = atom->alignment().powerOf2;
			// calculate section offset for this atom
			uint64_t alignment = 1 << atom->alignment().powerOf2;
			uint64_t currentModulus = (offset % alignment);
			uint64_t requiredModulus = atom->alignment().modulus;
			if ( currentModulus != requiredModulus ) {
				if ( requiredModulus > currentModulus )
					offset += requiredModulus-currentModulus;
				else
					offset += requiredModulus+alignment-currentModulus;
			}
			(const_cast<ld::Atom*>(atom))->setSectionOffset(offset);
			(const_cast<ld::Atom*>(atom))->setSectionStartAddress(curLinkEditAddress);
			offset += atom->size();
		}
		sect->size = offset;
		// section alignment is that of a contained atom with the greatest alignment
		sect->alignment = maxAlignment;
		sect->address = curLinkEditAddress;
		sect->fileOffset = curLinkEditfileOffset;
		curLinkEditAddress += sect->size;
		curLinkEditfileOffset += sect->size;
	}
	
	_fileSize = state.sections.back()->fileOffset + state.sections.back()->size;
}

void OutputFile::setSectionSizesAndAlignments(ld::Internal& state)
{
	for (std::vector<ld::Internal::FinalSection*>::iterator sit = state.sections.begin(); sit != state.sections.end(); ++sit) {
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
				if ( _options.pageAlignDataAtoms() && ( strcmp(atom->section().segmentName(), "__DATA") == 0) ) { 
					switch ( atom->section().type() ) {
						case ld::Section::typeUnclassified:
						case ld::Section::typeTentativeDefs:
						case ld::Section::typeZeroFill:
							pagePerAtom = true;
							if ( atomAlignmentPowerOf2 < 12 )
								atomAlignmentPowerOf2 = 12;
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
				uint64_t requiredModulus = atom->alignment().modulus;
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

void OutputFile::setLoadCommandsPadding(ld::Internal& state)
{
	// In other sections, any extra space is put and end of segment.
	// In __TEXT segment, any extra space is put after load commands to allow post-processing of load commands
	// Do a reverse layout of __TEXT segment to determine padding size and adjust section size
	uint64_t paddingSize = 0;
	switch ( _options.outputKind() ) {
		case Options::kDyld:
			// dyld itself has special padding requirements.  We want the beginning __text section to start at a stable address
			assert(strcmp(state.sections[1]->sectionName(),"__text") == 0);
			state.sections[1]->alignment = 12; // page align __text
			break;
		case Options::kObjectFile:
			// mach-o .o files need no padding between load commands and first section
			// but leave enough room that the object file could be signed
			paddingSize = 32;
			break;
		case Options::kPreload:
			// mach-o MH_PRELOAD files need no padding between load commands and first section
			paddingSize = 0;
		default:
			// work backwards from end of segment and lay out sections so that extra room goes to padding atom
			uint64_t addr = 0;
			for (std::vector<ld::Internal::FinalSection*>::reverse_iterator it = state.sections.rbegin(); it != state.sections.rend(); ++it) {
				ld::Internal::FinalSection* sect = *it;
				if ( strcmp(sect->segmentName(), "__TEXT") != 0 ) 
					continue;
				if ( sect == headerAndLoadCommandsSection ) {
					addr -= headerAndLoadCommandsSection->size;
					paddingSize = addr % _options.segmentAlignment();
					break;
				}
				addr -= sect->size;
				addr = addr & (0 - (1 << sect->alignment));
			}
	
			// if command line requires more padding than this
			uint32_t minPad = _options.minimumHeaderPad();
			if ( _options.maxMminimumHeaderPad() ) {
				// -headerpad_max_install_names means there should be room for every path load command to grow to 1204 bytes
				uint32_t altMin = _dylibsToLoad.size() * MAXPATHLEN;
				if ( _options.outputKind() ==  Options::kDynamicLibrary )
					altMin += MAXPATHLEN;
				if ( altMin > minPad )
					minPad = altMin;
			}
			if ( paddingSize < minPad ) {
				int extraPages = (minPad - paddingSize + _options.segmentAlignment() - 1)/_options.segmentAlignment();
				paddingSize += extraPages * _options.segmentAlignment();
			}
			
			if ( _options.makeEncryptable() ) {
				// load commands must be on a separate non-encrypted page
				int loadCommandsPage = (headerAndLoadCommandsSection->size + minPad)/_options.segmentAlignment();
				int textPage = (headerAndLoadCommandsSection->size + paddingSize)/_options.segmentAlignment();
				if ( loadCommandsPage == textPage ) {
					paddingSize += _options.segmentAlignment();
					textPage += 1;
				}
				// remember start for later use by load command
				_encryptedTEXTstartOffset = textPage*_options.segmentAlignment();
			}
			break;
	}
	// add padding to size of section
	headerAndLoadCommandsSection->size += paddingSize;
}


uint64_t OutputFile::pageAlign(uint64_t addr)
{
	const uint64_t alignment = _options.segmentAlignment();
	return ((addr+alignment-1) & (-alignment)); 
}

uint64_t OutputFile::pageAlign(uint64_t addr, uint64_t pageSize)
{
	return ((addr+pageSize-1) & (-pageSize)); 
}


void OutputFile::assignFileOffsets(ld::Internal& state)
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
	for (std::vector<ld::Internal::FinalSection*>::iterator it = state.sections.begin(); it != state.sections.end(); ++it) {
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
	for (std::vector<ld::Internal::FinalSection*>::iterator it = state.sections.begin(); it != state.sections.end(); ++it) {
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
		for (std::vector<ld::Internal::FinalSection*>::iterator sit = state.sections.begin(); sit != state.sections.end(); ++sit) {
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
		for (std::vector<ld::Internal::FinalSection*>::iterator it = state.sections.begin(); it != state.sections.end(); ++it) {
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
	for (std::vector<ld::Internal::FinalSection*>::iterator it = state.sections.begin(); it != state.sections.end(); ++it) {
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

	// remember total file size
	_fileSize = fileOffset;
}


static const char* makeName(const ld::Atom& atom)
{
	static char buffer[4096];
	switch ( atom.symbolTableInclusion() ) {
		case ld::Atom::symbolTableNotIn:
		case ld::Atom::symbolTableNotInFinalLinkedImages:
			sprintf(buffer, "%s@0x%08llX", atom.name(), atom.objectAddress());
			break;
		case ld::Atom::symbolTableIn:
		case ld::Atom::symbolTableInAndNeverStrip:
		case ld::Atom::symbolTableInAsAbsolute:
		case ld::Atom::symbolTableInWithRandomAutoStripLabel:
			strlcpy(buffer, atom.name(), 4096);
			break;
	}
	return buffer;
}

static const char* referenceTargetAtomName(ld::Internal& state, const ld::Fixup* ref)
{
	switch ( ref->binding ) {
		case ld::Fixup::bindingNone:
			return "NO BINDING";
		case ld::Fixup::bindingByNameUnbound:
			return (char*)(ref->u.target);
		case ld::Fixup::bindingByContentBound:
		case ld::Fixup::bindingDirectlyBound:
			return makeName(*((ld::Atom*)(ref->u.target)));
		case ld::Fixup::bindingsIndirectlyBound:
			return makeName(*state.indirectBindingTable[ref->u.bindingIndex]);
	}
	return "BAD BINDING";
}

bool OutputFile::targetIsThumb(ld::Internal& state, const ld::Fixup* fixup)
{
	switch ( fixup->binding ) {
		case ld::Fixup::bindingByContentBound:
		case ld::Fixup::bindingDirectlyBound:
			return fixup->u.target->isThumb();
		case ld::Fixup::bindingsIndirectlyBound:
			return state.indirectBindingTable[fixup->u.bindingIndex]->isThumb();
		default:
			break;
	}
	throw "unexpected binding";
}

uint64_t OutputFile::addressOf(const ld::Internal& state, const ld::Fixup* fixup, const ld::Atom** target)
{
	if ( !_options.makeCompressedDyldInfo() ) {
		// For external relocations the classic mach-o format
		// has addend only stored in the content.  That means
		// that the address of the target is not used.
		if ( fixup->contentAddendOnly )
			return 0;
	}
	switch ( fixup->binding ) {
		case ld::Fixup::bindingNone:
			throw "unexpected bindingNone";
		case ld::Fixup::bindingByNameUnbound:
			throw "unexpected bindingByNameUnbound";
		case ld::Fixup::bindingByContentBound:
		case ld::Fixup::bindingDirectlyBound:
			*target = fixup->u.target;
			return (*target)->finalAddress();
		case ld::Fixup::bindingsIndirectlyBound:
			*target = state.indirectBindingTable[fixup->u.bindingIndex];
		#ifndef NDEBUG
			if ( ! (*target)->finalAddressMode() ) {
				throwf("reference to symbol (which has not been assigned an address) %s", (*target)->name());
			}
		#endif
			return (*target)->finalAddress();
	}
	throw "unexpected binding";
}

uint64_t OutputFile::sectionOffsetOf(const ld::Internal& state, const ld::Fixup* fixup)
{
	const ld::Atom* target = NULL;
	switch ( fixup->binding ) {
		case ld::Fixup::bindingNone:
			throw "unexpected bindingNone";
		case ld::Fixup::bindingByNameUnbound:
			throw "unexpected bindingByNameUnbound";
		case ld::Fixup::bindingByContentBound:
		case ld::Fixup::bindingDirectlyBound:
			target = fixup->u.target;
			break;
		case ld::Fixup::bindingsIndirectlyBound:
			target = state.indirectBindingTable[fixup->u.bindingIndex];
			break;
	}
	assert(target != NULL);
	
	uint64_t targetAddress = target->finalAddress();
	for (std::vector<ld::Internal::FinalSection*>::const_iterator it = state.sections.begin(); it != state.sections.end(); ++it) {
		const ld::Internal::FinalSection* sect = *it;
		if ( (sect->address <= targetAddress) && (targetAddress < (sect->address+sect->size)) )
			return targetAddress - sect->address;
	}
	throw "section not found for section offset";
}



uint64_t OutputFile::tlvTemplateOffsetOf(const ld::Internal& state, const ld::Fixup* fixup)
{
	const ld::Atom* target = NULL;
	switch ( fixup->binding ) {
		case ld::Fixup::bindingNone:
			throw "unexpected bindingNone";
		case ld::Fixup::bindingByNameUnbound:
			throw "unexpected bindingByNameUnbound";
		case ld::Fixup::bindingByContentBound:
		case ld::Fixup::bindingDirectlyBound:
			target = fixup->u.target;
			break;
		case ld::Fixup::bindingsIndirectlyBound:
			target = state.indirectBindingTable[fixup->u.bindingIndex];
			break;
	}
	assert(target != NULL);
	
	for (std::vector<ld::Internal::FinalSection*>::const_iterator it = state.sections.begin(); it != state.sections.end(); ++it) {
		const ld::Internal::FinalSection* sect = *it;
		switch ( sect->type() ) {
			case ld::Section::typeTLVInitialValues:
			case ld::Section::typeTLVZeroFill:
				return target->finalAddress() - sect->address;
			default:
				break;
		}
	}
	throw "section not found for tlvTemplateOffsetOf";
}

void OutputFile::printSectionLayout(ld::Internal& state)
{
	// show layout of final image
	fprintf(stderr, "final section layout:\n");
	for (std::vector<ld::Internal::FinalSection*>::iterator it = state.sections.begin(); it != state.sections.end(); ++it) {
		if ( (*it)->isSectionHidden() )
			continue;
		fprintf(stderr, "    %s/%s addr=0x%08llX, size=0x%08llX, fileOffset=0x%08llX, type=%d\n", 
				(*it)->segmentName(), (*it)->sectionName(), 
				(*it)->address, (*it)->size, (*it)->fileOffset, (*it)->type());
	}
}


void OutputFile::rangeCheck8(int64_t displacement, ld::Internal& state, const ld::Atom* atom, const ld::Fixup* fixup)
{
	if ( (displacement > 127) || (displacement < -128) ) {
		// show layout of final image
		printSectionLayout(state);
		
		const ld::Atom* target;	
		throwf("8-bit reference out of range (%lld max is +/-127B): from %s (0x%08llX) to %s (0x%08llX)", 
				displacement, atom->name(), atom->finalAddress(), referenceTargetAtomName(state, fixup), 
				addressOf(state, fixup, &target));
	}
}

void OutputFile::rangeCheck16(int64_t displacement, ld::Internal& state, const ld::Atom* atom, const ld::Fixup* fixup)
{
	const int64_t thirtyTwoKLimit  = 0x00007FFF;
	if ( (displacement > thirtyTwoKLimit) || (displacement < (-thirtyTwoKLimit)) ) {
		// show layout of final image
		printSectionLayout(state);
		
		const ld::Atom* target;	
		throwf("16-bit reference out of range (%lld max is +/-32KB): from %s (0x%08llX) to %s (0x%08llX)", 
				displacement, atom->name(), atom->finalAddress(), referenceTargetAtomName(state, fixup),  
				addressOf(state, fixup, &target));
	}
}

void OutputFile::rangeCheckBranch32(int64_t displacement, ld::Internal& state, const ld::Atom* atom, const ld::Fixup* fixup)
{
	const int64_t twoGigLimit  = 0x7FFFFFFF;
	if ( (displacement > twoGigLimit) || (displacement < (-twoGigLimit)) ) {
		// show layout of final image
		printSectionLayout(state);
		
		const ld::Atom* target;	
		throwf("32-bit branch out of range (%lld max is +/-2GB): from %s (0x%08llX) to %s (0x%08llX)",
				displacement, atom->name(), atom->finalAddress(), referenceTargetAtomName(state, fixup), 
				addressOf(state, fixup, &target));
	}
}


void OutputFile::rangeCheckAbsolute32(int64_t displacement, ld::Internal& state, const ld::Atom* atom, const ld::Fixup* fixup)
{
	const int64_t fourGigLimit  = 0xFFFFFFFF;
	if ( displacement > fourGigLimit ) {
		// <rdar://problem/9610466> cannot enforce 32-bit range checks on 32-bit archs because assembler loses sign information
		//  .long _foo - 0xC0000000
		// is encoded in mach-o the same as:
		//  .long _foo + 0x40000000
		// so if _foo lays out to 0xC0000100, the first is ok, but the second is not.  
		if ( (_options.architecture() == CPU_TYPE_ARM) || (_options.architecture() == CPU_TYPE_I386) ) {
			// Unlikely userland code does funky stuff like this, so warn for them, but not warn for -preload or -static
			if ( (_options.outputKind() != Options::kPreload) && (_options.outputKind() != Options::kStaticExecutable) ) {
				warning("32-bit absolute address out of range (0x%08llX max is 4GB): from %s + 0x%08X (0x%08llX) to 0x%08llX", 
						displacement, atom->name(), fixup->offsetInAtom, atom->finalAddress(), displacement);
			}
			return;
		}
		// show layout of final image
		printSectionLayout(state);
		
		const ld::Atom* target;	
		if ( fixup->binding == ld::Fixup::bindingNone )
			throwf("32-bit absolute address out of range (0x%08llX max is 4GB): from %s + 0x%08X (0x%08llX) to 0x%08llX", 
				displacement, atom->name(), fixup->offsetInAtom, atom->finalAddress(), displacement);
		else
			throwf("32-bit absolute address out of range (0x%08llX max is 4GB): from %s + 0x%08X (0x%08llX) to %s (0x%08llX)", 
				displacement, atom->name(), fixup->offsetInAtom, atom->finalAddress(), referenceTargetAtomName(state, fixup), 
				addressOf(state, fixup, &target));
	}
}


void OutputFile::rangeCheckRIP32(int64_t displacement, ld::Internal& state, const ld::Atom* atom, const ld::Fixup* fixup)
{
	const int64_t twoGigLimit  = 0x7FFFFFFF;
	if ( (displacement > twoGigLimit) || (displacement < (-twoGigLimit)) ) {	
		// show layout of final image
		printSectionLayout(state);
		
		const ld::Atom* target;	
		throwf("32-bit RIP relative reference out of range (%lld max is +/-4GB): from %s (0x%08llX) to %s (0x%08llX)", 
				displacement, atom->name(), atom->finalAddress(), referenceTargetAtomName(state, fixup), 
				addressOf(state, fixup, &target));
	}
}

void OutputFile::rangeCheckARM12(int64_t displacement, ld::Internal& state, const ld::Atom* atom, const ld::Fixup* fixup)
{
	if ( (displacement > 4092LL) || (displacement < (-4092LL)) ) {
		// show layout of final image
		printSectionLayout(state);
		
		const ld::Atom* target;	
		throwf("ARM ldr 12-bit displacement out of range (%lld max is +/-4096B): from %s (0x%08llX) to %s (0x%08llX)", 
				displacement, atom->name(), atom->finalAddress(), referenceTargetAtomName(state, fixup), 
				addressOf(state, fixup, &target));
	}
}


void OutputFile::rangeCheckARMBranch24(int64_t displacement, ld::Internal& state, const ld::Atom* atom, const ld::Fixup* fixup)
{
	if ( (displacement > 33554428LL) || (displacement < (-33554432LL)) ) {
		// show layout of final image
		printSectionLayout(state);
		
		const ld::Atom* target;	
		throwf("b/bl/blx ARM branch out of range (%lld max is +/-32MB): from %s (0x%08llX) to %s (0x%08llX)", 
				displacement, atom->name(), atom->finalAddress(), referenceTargetAtomName(state, fixup), 
				addressOf(state, fixup, &target));
	}
}

void OutputFile::rangeCheckThumbBranch22(int64_t displacement, ld::Internal& state, const ld::Atom* atom, const ld::Fixup* fixup)
{
	// thumb2 supports a larger displacement
	if ( _options.preferSubArchitecture() && _options.archSupportsThumb2() ) {
		if ( (displacement > 16777214LL) || (displacement < (-16777216LL)) ) {
			// show layout of final image
			printSectionLayout(state);
			
			const ld::Atom* target;	
			throwf("b/bl/blx thumb2 branch out of range (%lld max is +/-16MB): from %s (0x%08llX) to %s (0x%08llX)", 
					displacement, atom->name(), atom->finalAddress(), referenceTargetAtomName(state, fixup), 
				addressOf(state, fixup, &target));
		}
	}
	else {
		if ( (displacement > 4194302LL) || (displacement < (-4194304LL)) ) {
			// show layout of final image
			printSectionLayout(state);
			
			const ld::Atom* target;	
			throwf("b/bl/blx thumb1 branch out of range (%lld max is +/-4MB): from %s (0x%08llX) to %s (0x%08llX)", 
					displacement, atom->name(), atom->finalAddress(), referenceTargetAtomName(state, fixup), 
				addressOf(state, fixup, &target));
		}
	}
}





uint16_t OutputFile::get16LE(uint8_t* loc) { return LittleEndian::get16(*(uint16_t*)loc); }
void     OutputFile::set16LE(uint8_t* loc, uint16_t value) { LittleEndian::set16(*(uint16_t*)loc, value); }

uint32_t OutputFile::get32LE(uint8_t* loc) { return LittleEndian::get32(*(uint32_t*)loc); }
void     OutputFile::set32LE(uint8_t* loc, uint32_t value) { LittleEndian::set32(*(uint32_t*)loc, value); }

uint64_t OutputFile::get64LE(uint8_t* loc) { return LittleEndian::get64(*(uint64_t*)loc); }
void     OutputFile::set64LE(uint8_t* loc, uint64_t value) { LittleEndian::set64(*(uint64_t*)loc, value); }

uint16_t OutputFile::get16BE(uint8_t* loc) { return BigEndian::get16(*(uint16_t*)loc); }
void     OutputFile::set16BE(uint8_t* loc, uint16_t value) { BigEndian::set16(*(uint16_t*)loc, value); }

uint32_t OutputFile::get32BE(uint8_t* loc) { return BigEndian::get32(*(uint32_t*)loc); }
void     OutputFile::set32BE(uint8_t* loc, uint32_t value) { BigEndian::set32(*(uint32_t*)loc, value); }

uint64_t OutputFile::get64BE(uint8_t* loc) { return BigEndian::get64(*(uint64_t*)loc); }
void     OutputFile::set64BE(uint8_t* loc, uint64_t value) { BigEndian::set64(*(uint64_t*)loc, value); }

void OutputFile::applyFixUps(ld::Internal& state, uint64_t mhAddress, const ld::Atom* atom, uint8_t* buffer)
{
	//fprintf(stderr, "applyFixUps() on %s\n", atom->name());
	int64_t accumulator = 0;
	const ld::Atom* toTarget = NULL;	
	const ld::Atom* fromTarget;
	int64_t delta;
	uint32_t instruction;
	uint32_t newInstruction;
	bool is_bl;
	bool is_blx;
	bool is_b;
	bool thumbTarget = false;
	for (ld::Fixup::iterator fit = atom->fixupsBegin(), end=atom->fixupsEnd(); fit != end; ++fit) {
		uint8_t* fixUpLocation = &buffer[fit->offsetInAtom];
		switch ( (ld::Fixup::Kind)(fit->kind) ) { 
			case ld::Fixup::kindNone:
			case ld::Fixup::kindNoneFollowOn:
			case ld::Fixup::kindNoneGroupSubordinate:
			case ld::Fixup::kindNoneGroupSubordinateFDE:
			case ld::Fixup::kindNoneGroupSubordinateLSDA:
			case ld::Fixup::kindNoneGroupSubordinatePersonality:
				break;
			case ld::Fixup::kindSetTargetAddress:
				accumulator = addressOf(state, fit, &toTarget);			
				thumbTarget = targetIsThumb(state, fit);
				if ( thumbTarget ) 
					accumulator |= 1;
				if ( fit->contentAddendOnly || fit->contentDetlaToAddendOnly )
					accumulator = 0;
				break;
			case ld::Fixup::kindSubtractTargetAddress:
				delta = addressOf(state, fit, &fromTarget);
				if ( ! fit->contentAddendOnly )
					accumulator -= delta;
				break;
			case ld::Fixup::kindAddAddend:
				// <rdar://problem/8342028> ARM main executables main contain .long constants pointing
				// into themselves such as jump tables.  These .long should not have thumb bit set
				// even though the target is a thumb instruction. We can tell it is an interior pointer
				// because we are processing an addend. 
				if ( thumbTarget && (toTarget == atom) && ((int32_t)fit->u.addend > 0) ) {
					accumulator &= (-2);
					//warning("removing thumb bit from intra-atom pointer in %s %s+0x%0X", 
					//		atom->section().sectionName(), atom->name(), fit->offsetInAtom);
				}
				accumulator += fit->u.addend;
				break;
			case ld::Fixup::kindSubtractAddend:
				accumulator -= fit->u.addend;
				break;
			case ld::Fixup::kindSetTargetImageOffset:
				accumulator = addressOf(state, fit, &toTarget) - mhAddress;
				break;
			case ld::Fixup::kindSetTargetSectionOffset:
				accumulator = sectionOffsetOf(state, fit);
				break;
			case ld::Fixup::kindSetTargetTLVTemplateOffset:
				accumulator = tlvTemplateOffsetOf(state, fit);
				break;
			case ld::Fixup::kindStore8:
				*fixUpLocation += accumulator;
				break;
			case ld::Fixup::kindStoreLittleEndian16:
				set16LE(fixUpLocation, accumulator);
				break;
			case ld::Fixup::kindStoreLittleEndianLow24of32:
				set32LE(fixUpLocation, (get32LE(fixUpLocation) & 0xFF000000) | (accumulator & 0x00FFFFFF) );
				break;
			case ld::Fixup::kindStoreLittleEndian32:
				rangeCheckAbsolute32(accumulator, state, atom, fit);
				set32LE(fixUpLocation, accumulator);
				break;
			case ld::Fixup::kindStoreLittleEndian64:
				set64LE(fixUpLocation, accumulator);
				break;
			case ld::Fixup::kindStoreBigEndian16:
				set16BE(fixUpLocation, accumulator);
				break;
			case ld::Fixup::kindStoreBigEndianLow24of32:
				set32BE(fixUpLocation, (get32BE(fixUpLocation) & 0xFF000000) | (accumulator & 0x00FFFFFF) );
				break;
			case ld::Fixup::kindStoreBigEndian32:
				rangeCheckAbsolute32(accumulator, state, atom, fit);
				set32BE(fixUpLocation, accumulator);
				break;
			case ld::Fixup::kindStoreBigEndian64:
				set64BE(fixUpLocation, accumulator);
				break;
			case ld::Fixup::kindStoreX86PCRel8:
			case ld::Fixup::kindStoreX86BranchPCRel8:
				if ( fit->contentAddendOnly )
					delta = accumulator;
				else
					delta = accumulator - (atom->finalAddress() + fit->offsetInAtom + 1);
				rangeCheck8(delta, state, atom, fit);
				*fixUpLocation = delta;
				break;
			case ld::Fixup::kindStoreX86PCRel16:
				if ( fit->contentAddendOnly )
					delta = accumulator;
				else
					delta = accumulator - (atom->finalAddress() + fit->offsetInAtom + 2);
				rangeCheck16(delta, state, atom, fit);
				set16LE(fixUpLocation, delta);
				break;
			case ld::Fixup::kindStoreX86BranchPCRel32:
				if ( fit->contentAddendOnly )
					delta = accumulator;
				else
					delta = accumulator - (atom->finalAddress() + fit->offsetInAtom + 4);
				rangeCheckBranch32(delta, state, atom, fit);
				set32LE(fixUpLocation, delta);
				break;
			case ld::Fixup::kindStoreX86PCRel32GOTLoad:
			case ld::Fixup::kindStoreX86PCRel32GOT:
			case ld::Fixup::kindStoreX86PCRel32:
			case ld::Fixup::kindStoreX86PCRel32TLVLoad:
				if ( fit->contentAddendOnly )
					delta = accumulator;
				else
					delta = accumulator - (atom->finalAddress() + fit->offsetInAtom + 4);
				rangeCheckRIP32(delta, state, atom, fit);
				set32LE(fixUpLocation, delta);
				break;
			case ld::Fixup::kindStoreX86PCRel32_1:
				if ( fit->contentAddendOnly )
					delta = accumulator - 1;
				else
					delta = accumulator - (atom->finalAddress() + fit->offsetInAtom + 5);
				rangeCheckRIP32(delta, state, atom, fit);
				set32LE(fixUpLocation, delta);
				break;
			case ld::Fixup::kindStoreX86PCRel32_2:
				if ( fit->contentAddendOnly )
					delta = accumulator - 2;
				else
					delta = accumulator - (atom->finalAddress() + fit->offsetInAtom + 6);
				rangeCheckRIP32(delta, state, atom, fit);
				set32LE(fixUpLocation, delta);
				break;
			case ld::Fixup::kindStoreX86PCRel32_4:
				if ( fit->contentAddendOnly )
					delta = accumulator - 4;
				else
					delta = accumulator - (atom->finalAddress() + fit->offsetInAtom + 8);
				rangeCheckRIP32(delta, state, atom, fit);
				set32LE(fixUpLocation, delta);
				break;
			case ld::Fixup::kindStoreX86Abs32TLVLoad:
				set32LE(fixUpLocation, accumulator);
				break;
			case ld::Fixup::kindStoreX86Abs32TLVLoadNowLEA:
				assert(_options.outputKind() != Options::kObjectFile);
				// TLV entry was optimized away, change movl instruction to a leal
				if ( fixUpLocation[-1] != 0xA1 )
					throw "TLV load reloc does not point to a movl instruction";
				fixUpLocation[-1] = 0xB8;
				set32LE(fixUpLocation, accumulator);
				break;
			case ld::Fixup::kindStoreX86PCRel32GOTLoadNowLEA:
				assert(_options.outputKind() != Options::kObjectFile);
				// GOT entry was optimized away, change movq instruction to a leaq
				if ( fixUpLocation[-2] != 0x8B )
					throw "GOT load reloc does not point to a movq instruction";
				fixUpLocation[-2] = 0x8D;
				delta = accumulator - (atom->finalAddress() + fit->offsetInAtom + 4);
				rangeCheckRIP32(delta, state, atom, fit);
				set32LE(fixUpLocation, delta);
				break;
			case ld::Fixup::kindStoreX86PCRel32TLVLoadNowLEA:
				assert(_options.outputKind() != Options::kObjectFile);
				// TLV entry was optimized away, change movq instruction to a leaq
				if ( fixUpLocation[-2] != 0x8B )
					throw "TLV load reloc does not point to a movq instruction";
				fixUpLocation[-2] = 0x8D;
				delta = accumulator - (atom->finalAddress() + fit->offsetInAtom + 4);
				rangeCheckRIP32(delta, state, atom, fit);
				set32LE(fixUpLocation, delta);
				break;
			case ld::Fixup::kindStoreTargetAddressARMLoad12:
				accumulator = addressOf(state, fit, &toTarget);
				// fall into kindStoreARMLoad12 case
			case ld::Fixup::kindStoreARMLoad12:
				delta = accumulator - (atom->finalAddress() + fit->offsetInAtom + 8);
				rangeCheckARM12(delta, state, atom, fit);
				instruction = get32LE(fixUpLocation);
				if ( delta >= 0 ) {
					newInstruction = instruction & 0xFFFFF000;
					newInstruction |= ((uint32_t)delta & 0xFFF);
				}
				else {
					newInstruction = instruction & 0xFF7FF000;
					newInstruction |= ((uint32_t)(-delta) & 0xFFF);
				}
				set32LE(fixUpLocation, newInstruction);
				break;
			case ld::Fixup::kindDtraceExtra:
				break;
			case ld::Fixup::kindStoreX86DtraceCallSiteNop:
				if ( _options.outputKind() != Options::kObjectFile ) {
					// change call site to a NOP
					fixUpLocation[-1] = 0x90;	// 1-byte nop
					fixUpLocation[0] = 0x0F;	// 4-byte nop 
					fixUpLocation[1] = 0x1F;
					fixUpLocation[2] = 0x40;
					fixUpLocation[3] = 0x00;
				}
				break;
			case ld::Fixup::kindStoreX86DtraceIsEnableSiteClear:
				if ( _options.outputKind() != Options::kObjectFile ) {
					// change call site to a clear eax
					fixUpLocation[-1] = 0x33;		// xorl eax,eax
					fixUpLocation[0] = 0xC0;
					fixUpLocation[1] = 0x90;		// 1-byte nop
					fixUpLocation[2] = 0x90;		// 1-byte nop
					fixUpLocation[3] = 0x90;		// 1-byte nop
				}
				break;
			case ld::Fixup::kindStoreARMDtraceCallSiteNop:
				if ( _options.outputKind() != Options::kObjectFile ) {
					// change call site to a NOP
					set32LE(fixUpLocation, 0xE1A00000);
				}
				break;
			case ld::Fixup::kindStoreARMDtraceIsEnableSiteClear:
				if ( _options.outputKind() != Options::kObjectFile ) {
					// change call site to 'eor r0, r0, r0'
					set32LE(fixUpLocation, 0xE0200000);
				}
				break;
			case ld::Fixup::kindStoreThumbDtraceCallSiteNop:
				if ( _options.outputKind() != Options::kObjectFile ) {
					// change 32-bit blx call site to two thumb NOPs
					set32LE(fixUpLocation, 0x46C046C0);
				}
				break;
			case ld::Fixup::kindStoreThumbDtraceIsEnableSiteClear:
				if ( _options.outputKind() != Options::kObjectFile ) {
					// change 32-bit blx call site to 'nop', 'eor r0, r0'
					set32LE(fixUpLocation, 0x46C04040);
				}
				break;
			case ld::Fixup::kindLazyTarget:
				break;
			case ld::Fixup::kindSetLazyOffset:
				assert(fit->binding == ld::Fixup::bindingDirectlyBound);
				accumulator = this->lazyBindingInfoOffsetForLazyPointerAddress(fit->u.target->finalAddress());
				break;
			case ld::Fixup::kindDataInCodeStartData:
			case ld::Fixup::kindDataInCodeStartJT8:
			case ld::Fixup::kindDataInCodeStartJT16:
			case ld::Fixup::kindDataInCodeStartJT32:
			case ld::Fixup::kindDataInCodeStartJTA32:
			case ld::Fixup::kindDataInCodeEnd:
				break;
			case ld::Fixup::kindStoreTargetAddressLittleEndian32:
				accumulator = addressOf(state, fit, &toTarget);
				thumbTarget = targetIsThumb(state, fit);
				if ( thumbTarget ) 
					accumulator |= 1;
				if ( fit->contentAddendOnly )
					accumulator = 0;
				rangeCheckAbsolute32(accumulator, state, atom, fit);
				set32LE(fixUpLocation, accumulator);
				break;
			case ld::Fixup::kindStoreTargetAddressLittleEndian64:
				accumulator = addressOf(state, fit, &toTarget);
				if ( fit->contentAddendOnly )
					accumulator = 0;
				set64LE(fixUpLocation, accumulator);
				break;
			case ld::Fixup::kindStoreTargetAddressBigEndian32:
				accumulator = addressOf(state, fit, &toTarget);
				if ( fit->contentAddendOnly )
					accumulator = 0;
				set32BE(fixUpLocation, accumulator);
				break;
			case ld::Fixup::kindStoreTargetAddressBigEndian64:
				accumulator = addressOf(state, fit, &toTarget);
				if ( fit->contentAddendOnly )
					accumulator = 0;
				set64BE(fixUpLocation, accumulator);
				break;
			case ld::Fixup::kindSetTargetTLVTemplateOffsetLittleEndian32:
				accumulator = tlvTemplateOffsetOf(state, fit);
				set32LE(fixUpLocation, accumulator);
				break;
			case ld::Fixup::kindSetTargetTLVTemplateOffsetLittleEndian64:
				accumulator = tlvTemplateOffsetOf(state, fit);
				set64LE(fixUpLocation, accumulator);
				break;
			case ld::Fixup::kindStoreTargetAddressX86PCRel32:
			case ld::Fixup::kindStoreTargetAddressX86BranchPCRel32:
			case ld::Fixup::kindStoreTargetAddressX86PCRel32GOTLoad:
			case ld::Fixup::kindStoreTargetAddressX86PCRel32TLVLoad:
				accumulator = addressOf(state, fit, &toTarget);	
				if ( fit->contentDetlaToAddendOnly )
					accumulator = 0;
				if ( fit->contentAddendOnly )
					delta = 0;
				else
					delta = accumulator - (atom->finalAddress() + fit->offsetInAtom + 4);
				rangeCheckRIP32(delta, state, atom, fit);
				set32LE(fixUpLocation, delta);
				break;
			case ld::Fixup::kindStoreTargetAddressX86Abs32TLVLoad:
				set32LE(fixUpLocation, accumulator);
				break;
			case ld::Fixup::kindStoreTargetAddressX86Abs32TLVLoadNowLEA:
				// TLV entry was optimized away, change movl instruction to a leal
				if ( fixUpLocation[-1] != 0xA1 )
					throw "TLV load reloc does not point to a movl <abs-address>,<reg> instruction";
				fixUpLocation[-1] = 0xB8;
				accumulator = addressOf(state, fit, &toTarget);
				set32LE(fixUpLocation, accumulator);
				break;
			case ld::Fixup::kindStoreTargetAddressX86PCRel32GOTLoadNowLEA:
				// GOT entry was optimized away, change movq instruction to a leaq
				if ( fixUpLocation[-2] != 0x8B )
					throw "GOT load reloc does not point to a movq instruction";
				fixUpLocation[-2] = 0x8D;
				accumulator = addressOf(state, fit, &toTarget);
				delta = accumulator - (atom->finalAddress() + fit->offsetInAtom + 4);
				rangeCheckRIP32(delta, state, atom, fit);
				set32LE(fixUpLocation, delta);
				break;
			case ld::Fixup::kindStoreTargetAddressX86PCRel32TLVLoadNowLEA:
				// TLV entry was optimized away, change movq instruction to a leaq
				if ( fixUpLocation[-2] != 0x8B )
					throw "TLV load reloc does not point to a movq instruction";
				fixUpLocation[-2] = 0x8D;
				accumulator = addressOf(state, fit, &toTarget);
				delta = accumulator - (atom->finalAddress() + fit->offsetInAtom + 4);
				rangeCheckRIP32(delta, state, atom, fit);
				set32LE(fixUpLocation, delta);
				break;
			case ld::Fixup::kindStoreTargetAddressARMBranch24:
				accumulator = addressOf(state, fit, &toTarget);
				thumbTarget = targetIsThumb(state, fit);
				if ( thumbTarget ) 
					accumulator |= 1;
				if ( fit->contentDetlaToAddendOnly )
					accumulator = 0;
				// fall into kindStoreARMBranch24 case
			case ld::Fixup::kindStoreARMBranch24:
				// The pc added will be +8 from the pc
				delta = accumulator - (atom->finalAddress() + fit->offsetInAtom + 8);
				rangeCheckARMBranch24(delta, state, atom, fit);
				instruction = get32LE(fixUpLocation);
				// Make sure we are calling arm with bl, thumb with blx			
				is_bl = ((instruction & 0xFF000000) == 0xEB000000);
				is_blx = ((instruction & 0xFE000000) == 0xFA000000);
				is_b = !is_blx && ((instruction & 0x0F000000) == 0x0A000000);
				if ( is_bl && thumbTarget ) {
					uint32_t opcode = 0xFA000000;
					uint32_t disp = (uint32_t)(delta >> 2) & 0x00FFFFFF;
					uint32_t h_bit = (uint32_t)(delta << 23) & 0x01000000;
					newInstruction = opcode | h_bit | disp;
				} 
				else if ( is_blx && !thumbTarget ) {
					uint32_t opcode = 0xEB000000;
					uint32_t disp = (uint32_t)(delta >> 2) & 0x00FFFFFF;
					newInstruction = opcode | disp;
				} 
				else if ( is_b && thumbTarget ) {
					if ( fit->contentDetlaToAddendOnly )
						newInstruction = (instruction & 0xFF000000) | ((uint32_t)(delta >> 2) & 0x00FFFFFF);
					else
						throwf("no pc-rel bx arm instruction. Can't fix up branch to %s in %s",
								referenceTargetAtomName(state, fit), atom->name());
				} 
				else if ( !is_bl && !is_blx && thumbTarget ) {
					throwf("don't know how to convert instruction %x referencing %s to thumb",
						 instruction, referenceTargetAtomName(state, fit));
				}
				else {
					newInstruction = (instruction & 0xFF000000) | ((uint32_t)(delta >> 2) & 0x00FFFFFF);
				}
				set32LE(fixUpLocation, newInstruction);
				break;
			case ld::Fixup::kindStoreTargetAddressThumbBranch22:
				accumulator = addressOf(state, fit, &toTarget);
				thumbTarget = targetIsThumb(state, fit);
				if ( thumbTarget ) 
					accumulator |= 1;
				if ( fit->contentDetlaToAddendOnly )
					accumulator = 0;
				// fall into kindStoreThumbBranch22 case
			case ld::Fixup::kindStoreThumbBranch22:
				instruction = get32LE(fixUpLocation);
				is_bl = ((instruction & 0xD000F800) == 0xD000F000);
				is_blx = ((instruction & 0xD000F800) == 0xC000F000);
				is_b = ((instruction & 0xD000F800) == 0x9000F000);
				// If the target is not thumb, we will be generating a blx instruction
				// Since blx cannot have the low bit set, set bit[1] of the target to
				// bit[1] of the base address, so that the difference is a multiple of
				// 4 bytes.
				if ( !thumbTarget && !fit->contentDetlaToAddendOnly ) {
				  accumulator &= -3ULL;
				  accumulator |= ((atom->finalAddress() + fit->offsetInAtom ) & 2LL);
				}
				// The pc added will be +4 from the pc
				delta = accumulator - (atom->finalAddress() + fit->offsetInAtom + 4);
				rangeCheckThumbBranch22(delta, state, atom, fit);
				if ( _options.preferSubArchitecture() && _options.archSupportsThumb2() ) {
					// The instruction is really two instructions:
					// The lower 16 bits are the first instruction, which contains the high
					//   11 bits of the displacement.
					// The upper 16 bits are the second instruction, which contains the low
					//   11 bits of the displacement, as well as differentiating bl and blx.
					uint32_t s = (uint32_t)(delta >> 24) & 0x1;
					uint32_t i1 = (uint32_t)(delta >> 23) & 0x1;
					uint32_t i2 = (uint32_t)(delta >> 22) & 0x1;
					uint32_t imm10 = (uint32_t)(delta >> 12) & 0x3FF;
					uint32_t imm11 = (uint32_t)(delta >> 1) & 0x7FF;
					uint32_t j1 = (i1 == s);
					uint32_t j2 = (i2 == s);
					if ( is_bl ) {
						if ( thumbTarget )
							instruction = 0xD000F000; // keep bl
						else
							instruction = 0xC000F000; // change to blx
					} 
					else if ( is_blx ) {
						if ( thumbTarget )
							instruction = 0xD000F000; // change to bl
						else
							instruction = 0xC000F000; // keep blx
					}
					else if ( is_b ) {
						instruction = 0x9000F000; // keep b
						if ( !thumbTarget && !fit->contentDetlaToAddendOnly ) {
							throwf("armv7 has no pc-rel bx thumb instruction. Can't fix up branch to %s in %s",
									referenceTargetAtomName(state, fit), atom->name());
						}
					} 
					else {
						if ( !thumbTarget ) 
							throwf("don't know how to convert branch instruction %x referencing %s to bx",
									instruction, referenceTargetAtomName(state, fit));
						instruction = 0x9000F000; // keep b
					} 
					uint32_t nextDisp = (j1 << 13) | (j2 << 11) | imm11;
					uint32_t firstDisp = (s << 10) | imm10;
					newInstruction = instruction | (nextDisp << 16) | firstDisp;
					//warning("s=%d, j1=%d, j2=%d, imm10=0x%0X, imm11=0x%0X, instruction=0x%08X, first=0x%04X, next=0x%04X, new=0x%08X, disp=0x%llX for %s to %s\n",
					//	s, j1, j2, imm10, imm11, instruction, firstDisp, nextDisp, newInstruction, delta, atom->name(), toTarget->name());
					set32LE(fixUpLocation, newInstruction);				
				}
				else {
					// The instruction is really two instructions:
					// The lower 16 bits are the first instruction, which contains the high
					//   11 bits of the displacement.
					// The upper 16 bits are the second instruction, which contains the low
					//   11 bits of the displacement, as well as differentiating bl and blx.
					uint32_t firstDisp = (uint32_t)(delta >> 12) & 0x7FF;
					uint32_t nextDisp = (uint32_t)(delta >> 1) & 0x7FF;
					if ( is_bl && !thumbTarget ) {
						instruction = 0xE800F000;
					} 
					else if ( is_blx && thumbTarget ) {
						instruction = 0xF800F000;
					} 
					else if ( is_b ) {
						instruction = 0x9000F000; // keep b
						if ( !thumbTarget && !fit->contentDetlaToAddendOnly ) {
							throwf("armv6 has no pc-rel bx thumb instruction. Can't fix up branch to %s in %s",
									referenceTargetAtomName(state, fit), atom->name());
						}
					}
					else {
						instruction = instruction & 0xF800F800;
					}
					newInstruction = instruction | (nextDisp << 16) | firstDisp;
					set32LE(fixUpLocation, newInstruction);				
				}
				break;
			case ld::Fixup::kindStoreARMLow16:
				{
					uint32_t imm4 = (accumulator & 0x0000F000) >> 12;
					uint32_t imm12 = accumulator & 0x00000FFF;
					instruction = get32LE(fixUpLocation);
					newInstruction = (instruction & 0xFFF0F000) | (imm4 << 16) | imm12;
					set32LE(fixUpLocation, newInstruction);		
				}
				break;
			case ld::Fixup::kindStoreARMHigh16:
				{
					uint32_t imm4  = (accumulator & 0xF0000000) >> 28;
					uint32_t imm12 = (accumulator & 0x0FFF0000) >> 16;
					instruction = get32LE(fixUpLocation);
					newInstruction = (instruction & 0xFFF0F000) | (imm4 << 16) | imm12;
					set32LE(fixUpLocation, newInstruction);		
				}
				break;
			case ld::Fixup::kindStoreThumbLow16:
				{
					uint32_t imm4 = (accumulator & 0x0000F000) >> 12;
					uint32_t i =    (accumulator & 0x00000800) >> 11;
					uint32_t imm3 = (accumulator & 0x00000700) >> 8;
					uint32_t imm8 =  accumulator & 0x000000FF;
					instruction = get32LE(fixUpLocation);
					newInstruction = (instruction & 0x8F00FBF0) | imm4 | (i << 10) | (imm3 << 28) | (imm8 << 16);
					set32LE(fixUpLocation, newInstruction);		
				}
				break;
			case ld::Fixup::kindStoreThumbHigh16:
				{
					uint32_t imm4 = (accumulator & 0xF0000000) >> 28;
					uint32_t i =    (accumulator & 0x08000000) >> 27;
					uint32_t imm3 = (accumulator & 0x07000000) >> 24;
					uint32_t imm8 = (accumulator & 0x00FF0000) >> 16;
					instruction = get32LE(fixUpLocation);
					newInstruction = (instruction & 0x8F00FBF0) | imm4 | (i << 10) | (imm3 << 28) | (imm8 << 16);
					set32LE(fixUpLocation, newInstruction);		
				}
				break;
		}
	}
}

void OutputFile::copyNoOps(uint8_t* from, uint8_t* to, bool thumb)
{
	switch ( _options.architecture() ) {
		case CPU_TYPE_I386:
		case CPU_TYPE_X86_64:
			for (uint8_t* p=from; p < to; ++p)
				*p = 0x90;
			break;
		case CPU_TYPE_ARM:
			if ( thumb ) {
				for (uint8_t* p=from; p < to; p += 2)
					OSWriteLittleInt16((uint16_t*)p, 0, 0x46c0);
			}
			else {
				for (uint8_t* p=from; p < to; p += 4)
					OSWriteLittleInt32((uint32_t*)p, 0, 0xe1a00000);
			}
			break;
		default:
			for (uint8_t* p=from; p < to; ++p)
				*p = 0x00;
			break;
	}
}

bool OutputFile::takesNoDiskSpace(const ld::Section* sect)
{
	switch ( sect->type() ) {
		case ld::Section::typeZeroFill:
		case ld::Section::typeTLVZeroFill:
			return _options.optimizeZeroFill();
		case ld::Section::typePageZero:
		case ld::Section::typeStack:
		case ld::Section::typeAbsoluteSymbols:
		case ld::Section::typeTentativeDefs:
			return true;
		default:
			break;
	}
	return false;
}

bool OutputFile::hasZeroForFileOffset(const ld::Section* sect)
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

void OutputFile::writeAtoms(ld::Internal& state, uint8_t* wholeBuffer)
{
	// have each atom write itself
	uint64_t fileOffsetOfEndOfLastAtom = 0;
	uint64_t mhAddress = 0;
	bool lastAtomUsesNoOps = false;
	for (std::vector<ld::Internal::FinalSection*>::iterator sit = state.sections.begin(); sit != state.sections.end(); ++sit) {
		ld::Internal::FinalSection* sect = *sit;
		if ( sect->type() == ld::Section::typeMachHeader )
			mhAddress = sect->address;
		if ( takesNoDiskSpace(sect) )
			continue;
		const bool sectionUsesNops = (sect->type() == ld::Section::typeCode);
		//fprintf(stderr, "file offset=0x%08llX, section %s\n", sect->fileOffset, sect->sectionName());
		std::vector<const ld::Atom*>& atoms = sect->atoms;
		bool lastAtomWasThumb = false;
		for (std::vector<const ld::Atom*>::iterator ait = atoms.begin(); ait != atoms.end(); ++ait) {
			const ld::Atom* atom = *ait;
			if ( atom->definition() == ld::Atom::definitionProxy )
				continue;
			try {
				uint64_t fileOffset = atom->finalAddress() - sect->address + sect->fileOffset;
				// check for alignment padding between atoms
				if ( (fileOffset != fileOffsetOfEndOfLastAtom) && lastAtomUsesNoOps ) {
					this->copyNoOps(&wholeBuffer[fileOffsetOfEndOfLastAtom], &wholeBuffer[fileOffset], lastAtomWasThumb);
				}
				// copy atom content
				atom->copyRawContent(&wholeBuffer[fileOffset]);
				// apply fix ups
				this->applyFixUps(state, mhAddress, atom, &wholeBuffer[fileOffset]);
				fileOffsetOfEndOfLastAtom = fileOffset+atom->size();
				lastAtomUsesNoOps = sectionUsesNops;
				lastAtomWasThumb = atom->isThumb();
			}
			catch (const char* msg) {
				if ( atom->file() != NULL )
					throwf("%s in '%s' from %s", msg, atom->name(), atom->file()->path());
				else
					throwf("%s in '%s'", msg, atom->name());
			}
		}
	}
}


void OutputFile::computeContentUUID(ld::Internal& state, uint8_t* wholeBuffer)
{
	const bool log = false;
	if ( (_options.outputKind() != Options::kObjectFile) || state.someObjectFileHasDwarf ) {
		uint8_t digest[CC_MD5_DIGEST_LENGTH];
		uint32_t	stabsStringsOffsetStart;
		uint32_t	tabsStringsOffsetEnd;
		uint32_t	stabsOffsetStart;
		uint32_t	stabsOffsetEnd;
		if ( _symbolTableAtom->hasStabs(stabsStringsOffsetStart, tabsStringsOffsetEnd, stabsOffsetStart, stabsOffsetEnd) ) {
			// find two areas of file that are stabs info and should not contribute to checksum
			uint64_t stringPoolFileOffset = 0;
			uint64_t symbolTableFileOffset = 0;
			for (std::vector<ld::Internal::FinalSection*>::iterator sit = state.sections.begin(); sit != state.sections.end(); ++sit) {
				ld::Internal::FinalSection* sect = *sit;
				if ( sect->type() == ld::Section::typeLinkEdit ) {
					if ( strcmp(sect->sectionName(), "__string_pool") == 0 )
						stringPoolFileOffset = sect->fileOffset;
					else if ( strcmp(sect->sectionName(), "__symbol_table") == 0 )
						symbolTableFileOffset = sect->fileOffset;
				}
			}
			uint64_t firstStabNlistFileOffset  = symbolTableFileOffset + stabsOffsetStart;
			uint64_t lastStabNlistFileOffset   = symbolTableFileOffset + stabsOffsetEnd;
			uint64_t firstStabStringFileOffset = stringPoolFileOffset  + stabsStringsOffsetStart;
			uint64_t lastStabStringFileOffset  = stringPoolFileOffset  + tabsStringsOffsetEnd;
			if ( log ) fprintf(stderr, "firstStabNlistFileOffset=0x%08llX\n", firstStabNlistFileOffset);
			if ( log ) fprintf(stderr, "lastStabNlistFileOffset=0x%08llX\n", lastStabNlistFileOffset);
			if ( log ) fprintf(stderr, "firstStabStringFileOffset=0x%08llX\n", firstStabStringFileOffset);
			if ( log ) fprintf(stderr, "lastStabStringFileOffset=0x%08llX\n", lastStabStringFileOffset);
			assert(firstStabNlistFileOffset <= firstStabStringFileOffset);
			
			CC_MD5_CTX md5state;
			CC_MD5_Init(&md5state);
			// checksum everything up to first stabs nlist
			if ( log ) fprintf(stderr, "checksum 0x%08X -> 0x%08llX\n", 0, firstStabNlistFileOffset);
			CC_MD5_Update(&md5state, &wholeBuffer[0], firstStabNlistFileOffset);
			// checkusm everything after last stabs nlist and up to first stabs string
			if ( log ) fprintf(stderr, "checksum 0x%08llX -> 0x%08llX\n", lastStabNlistFileOffset, firstStabStringFileOffset);
			CC_MD5_Update(&md5state, &wholeBuffer[lastStabNlistFileOffset], firstStabStringFileOffset-lastStabNlistFileOffset);
			// checksum everything after last stabs string to end of file
			if ( log ) fprintf(stderr, "checksum 0x%08llX -> 0x%08llX\n", lastStabStringFileOffset, _fileSize);
			CC_MD5_Update(&md5state, &wholeBuffer[lastStabStringFileOffset], _fileSize-lastStabStringFileOffset);
			CC_MD5_Final(digest, &md5state);
			if ( log ) fprintf(stderr, "uuid=%02X, %02X, %02X, %02X, %02X, %02X, %02X, %02X\n", digest[0], digest[1], digest[2], 
							   digest[3], digest[4], digest[5], digest[6],  digest[7]);
		}
		else {
			CC_MD5(wholeBuffer, _fileSize, digest);
		}
		// <rdar://problem/6723729> LC_UUID uuids should conform to RFC 4122 UUID version 4 & UUID version 5 formats
		digest[6] = ( digest[6] & 0x0F ) | ( 3 << 4 );
		digest[8] = ( digest[8] & 0x3F ) | 0x80;
		// update buffer with new UUID
		_headersAndLoadCommandAtom->setUUID(digest);
		_headersAndLoadCommandAtom->recopyUUIDCommand();
	}
}
	
	
void OutputFile::writeOutputFile(ld::Internal& state)
{
	// for UNIX conformance, error if file exists and is not writable
	if ( (access(_options.outputFilePath(), F_OK) == 0) && (access(_options.outputFilePath(), W_OK) == -1) )
		throwf("can't write output file: %s", _options.outputFilePath());

	mode_t permissions = 0777;
	if ( _options.outputKind() == Options::kObjectFile )
		permissions = 0666;
	mode_t umask = ::umask(0);
	::umask(umask); // put back the original umask
	permissions &= ~umask;
	// Calling unlink first assures the file is gone so that open creates it with correct permissions
	// It also handles the case where __options.outputFilePath() file is not writable but its directory is
	// And it means we don't have to truncate the file when done writing (in case new is smaller than old)
	// Lastly, only delete existing file if it is a normal file (e.g. not /dev/null).
	struct stat stat_buf;
	bool outputIsRegularFile = false;
	bool outputIsMappableFile = false;
	if ( stat(_options.outputFilePath(), &stat_buf) != -1 ) {
		if (stat_buf.st_mode & S_IFREG) {
			outputIsRegularFile = true;
			// <rdar://problem/12264302> Don't use mmap on non-hfs volumes
#if 0
			struct statfs fsInfo;
			if ( statfs(_options.outputFilePath(), &fsInfo) != -1 ) {
				if ( strcmp(fsInfo.f_fstypename, "hfs") == 0) {
					(void)unlink(_options.outputFilePath());
					outputIsMappableFile = true;
				}
			}
			else {
#endif
				outputIsMappableFile = false;
#if 0
			}
#endif
		} 
		else {
			outputIsRegularFile = false;
		}
	}
	else {
		// special files (pipes, devices, etc) must already exist
		outputIsRegularFile = true;
		// output file does not exist yet
		char dirPath[PATH_MAX];
		strcpy(dirPath, _options.outputFilePath());
		char* end = strrchr(dirPath, '/');
		if ( end != NULL ) {
			end[1] = '\0';
#if 0
			struct statfs fsInfo;
			if ( statfs(dirPath, &fsInfo) != -1 ) {
				if ( strcmp(fsInfo.f_fstypename, "hfs") == 0) {
					outputIsMappableFile = true;
				}
			}
#endif
		}
	}
	
	//fprintf(stderr, "outputIsMappableFile=%d, outputIsRegularFile=%d, path=%s\n", outputIsMappableFile, outputIsRegularFile, _options.outputFilePath());
	
	int fd;
	// Construct a temporary path of the form {outputFilePath}.ld_XXXXXX
	const char filenameTemplate[] = ".ld_XXXXXX";
	char tmpOutput[PATH_MAX];
	uint8_t *wholeBuffer;
	if ( outputIsRegularFile && outputIsMappableFile ) {
		strcpy(tmpOutput, _options.outputFilePath());
		// If the path is too long to add a suffix for a temporary name then
		// just fall back to using the output path. 
		if (strlen(tmpOutput)+strlen(filenameTemplate) < PATH_MAX) {
			strcat(tmpOutput, filenameTemplate);
			fd = mkstemp(tmpOutput);
		} 
		else {
			fd = open(tmpOutput, O_RDWR|O_CREAT, permissions);
		}
		if ( fd == -1 ) 
			throwf("can't open output file for writing: %s, errno=%d", tmpOutput, errno);
		ftruncate(fd, _fileSize);
		
		wholeBuffer = (uint8_t *)mmap(NULL, _fileSize, PROT_WRITE|PROT_READ, MAP_SHARED, fd, 0);
		if ( wholeBuffer == MAP_FAILED )
			throwf("can't create buffer of %llu bytes for output", _fileSize);
	} 
	else {
		if ( outputIsRegularFile )
			fd = open(_options.outputFilePath(),  O_RDWR|O_CREAT, permissions);
		else
			fd = open(_options.outputFilePath(),  O_WRONLY);
		if ( fd == -1 ) 
			throwf("can't open output file for writing: %s, errno=%d", _options.outputFilePath(), errno);
		// try to allocate buffer for entire output file content
		wholeBuffer = (uint8_t*)calloc(_fileSize, 1);
		if ( wholeBuffer == NULL )
			throwf("can't create buffer of %llu bytes for output", _fileSize);
	}
	
	if ( _options.UUIDMode() == Options::kUUIDRandom ) {
		uint8_t bits[16];
		::uuid_generate_random(bits);
		_headersAndLoadCommandAtom->setUUID(bits);
	}

	writeAtoms(state, wholeBuffer);
	
	// compute UUID 
	if ( _options.UUIDMode() == Options::kUUIDContent )
		computeContentUUID(state, wholeBuffer);

	if ( outputIsRegularFile && outputIsMappableFile ) {
		if ( ::chmod(tmpOutput, permissions) == -1 ) {
			unlink(tmpOutput);
			throwf("can't set permissions on output file: %s, errno=%d", tmpOutput, errno);
		}
		if ( ::rename(tmpOutput, _options.outputFilePath()) == -1 && strcmp(tmpOutput, _options.outputFilePath()) != 0) {
			unlink(tmpOutput);
			throwf("can't move output file in place, errno=%d", errno);
		}
	} 
	else {
		if ( ::write(fd, wholeBuffer, _fileSize) == -1 ) {
			throwf("can't write to output file: %s, errno=%d", _options.outputFilePath(), errno);
		}
	}
}

struct AtomByNameSorter
{	
	 bool operator()(const ld::Atom* left, const ld::Atom* right)
	 {
          return (strcmp(left->name(), right->name()) < 0);
	 }
};

class NotInSet
{
public:
	NotInSet(const std::set<const ld::Atom*>& theSet) : _set(theSet)  {}

	bool operator()(const ld::Atom* atom) const {
		return ( _set.count(atom) == 0 );
	}
private:
	const std::set<const ld::Atom*>&  _set;
};


void OutputFile::buildSymbolTable(ld::Internal& state)
{
	unsigned int machoSectionIndex = 0;
	for (std::vector<ld::Internal::FinalSection*>::iterator sit = state.sections.begin(); sit != state.sections.end(); ++sit) {
		ld::Internal::FinalSection* sect = *sit;
		bool setMachoSectionIndex = !sect->isSectionHidden() && (sect->type() != ld::Section::typeTentativeDefs);
		if ( setMachoSectionIndex ) 
			++machoSectionIndex;
		for (std::vector<const ld::Atom*>::iterator ait = sect->atoms.begin(); ait != sect->atoms.end(); ++ait) {
			const ld::Atom* atom = *ait;
			if ( setMachoSectionIndex ) 
				(const_cast<ld::Atom*>(atom))->setMachoSection(machoSectionIndex);
			else if ( sect->type() == ld::Section::typeMachHeader )
				(const_cast<ld::Atom*>(atom))->setMachoSection(1); // __mh_execute_header is not in any section by needs n_sect==1
			else if ( sect->type() == ld::Section::typeLastSection )
				(const_cast<ld::Atom*>(atom))->setMachoSection(machoSectionIndex); // use section index of previous section
			else if ( sect->type() == ld::Section::typeFirstSection )
				(const_cast<ld::Atom*>(atom))->setMachoSection(machoSectionIndex+1); // use section index of next section
				
			// in -r mode, clarify symbolTableNotInFinalLinkedImages
			if ( _options.outputKind() == Options::kObjectFile ) {
				if ( _options.architecture() == CPU_TYPE_X86_64 ) {
					// x86_64 .o files need labels on anonymous literal strings
					if ( (sect->type() == ld::Section::typeCString) && (atom->combine() == ld::Atom::combineByNameAndContent) ) {
						(const_cast<ld::Atom*>(atom))->setSymbolTableInclusion(ld::Atom::symbolTableIn);
						_localAtoms.push_back(atom);
						continue;
					}
				}
				if ( sect->type() == ld::Section::typeCFI ) {
					if ( _options.removeEHLabels() )
						(const_cast<ld::Atom*>(atom))->setSymbolTableInclusion(ld::Atom::symbolTableNotIn);
					else
						(const_cast<ld::Atom*>(atom))->setSymbolTableInclusion(ld::Atom::symbolTableIn);
				}
				if ( atom->symbolTableInclusion() == ld::Atom::symbolTableNotInFinalLinkedImages )
					(const_cast<ld::Atom*>(atom))->setSymbolTableInclusion(ld::Atom::symbolTableIn);
			}

			// TEMP work around until <rdar://problem/7702923> goes in
			if ( (atom->symbolTableInclusion() == ld::Atom::symbolTableInAndNeverStrip)
				&& (atom->scope() == ld::Atom::scopeLinkageUnit)
				&& (_options.outputKind() == Options::kDynamicLibrary) ) {
					(const_cast<ld::Atom*>(atom))->setScope(ld::Atom::scopeGlobal);
			}
			
			// <rdar://problem/6783167> support auto hidden weak symbols: .weak_def_can_be_hidden
			if ( atom->autoHide() && (_options.outputKind() != Options::kObjectFile) ) {
				// adding auto-hide symbol to .exp file should keep it global
				if ( !_options.hasExportMaskList() || !_options.shouldExport(atom->name()) )
					(const_cast<ld::Atom*>(atom))->setScope(ld::Atom::scopeLinkageUnit);
			}
			
			// <rdar://problem/8626058> ld should consistently warn when resolvers are not exported
			if ( (atom->contentType() == ld::Atom::typeResolver) && (atom->scope() == ld::Atom::scopeLinkageUnit) )
				warning("resolver functions should be external, but '%s' is hidden", atom->name());
			
			if ( sect->type() == ld::Section::typeImportProxies ) {
				if ( atom->combine() == ld::Atom::combineByName )
					this->usesWeakExternalSymbols = true;
				// alias proxy is a re-export with a name change, don't import changed name
				if ( ! atom->isAlias() )
					_importedAtoms.push_back(atom);
				// scope of proxies are usually linkage unit, so done
				// if scope is global, we need to re-export it too
				if ( atom->scope() == ld::Atom::scopeGlobal )
					_exportedAtoms.push_back(atom);
				continue;
			}
			if ( atom->symbolTableInclusion() == ld::Atom::symbolTableNotInFinalLinkedImages ) {
				assert(_options.outputKind() != Options::kObjectFile);
				continue;  // don't add to symbol table
			}
			if ( atom->symbolTableInclusion() == ld::Atom::symbolTableNotIn ) {
				continue;  // don't add to symbol table
			}
			if ( (atom->symbolTableInclusion() == ld::Atom::symbolTableInWithRandomAutoStripLabel) 
				&& (_options.outputKind() != Options::kObjectFile) ) {
				continue;  // don't add to symbol table
			}
			
			if ( (atom->definition() == ld::Atom::definitionTentative) && (_options.outputKind() == Options::kObjectFile) ) {
				if ( _options.makeTentativeDefinitionsReal() ) {
					// -r -d turns tentative defintions into real def
					_exportedAtoms.push_back(atom);
				}
				else {
					// in mach-o object files tentative defintions are stored like undefined symbols
					_importedAtoms.push_back(atom);
				}
				continue;
			}

			switch ( atom->scope() ) {
				case ld::Atom::scopeTranslationUnit:
					if ( _options.keepLocalSymbol(atom->name()) ) {	
						_localAtoms.push_back(atom);
					}
					else {
						if ( _options.outputKind() == Options::kObjectFile ) {
							(const_cast<ld::Atom*>(atom))->setSymbolTableInclusion(ld::Atom::symbolTableInWithRandomAutoStripLabel);
							_localAtoms.push_back(atom);
						}
						else
							(const_cast<ld::Atom*>(atom))->setSymbolTableInclusion(ld::Atom::symbolTableNotIn);
					}	
					break;
				case ld::Atom::scopeGlobal:
					_exportedAtoms.push_back(atom);
					break;
				case ld::Atom::scopeLinkageUnit:
					if ( _options.outputKind() == Options::kObjectFile ) {
						if ( _options.keepPrivateExterns() ) {
							assert( (atom->combine() == ld::Atom::combineNever) || (atom->combine() == ld::Atom::combineByName) );
							_exportedAtoms.push_back(atom);
						}
						else if ( _options.keepLocalSymbol(atom->name()) ) {
							_localAtoms.push_back(atom);
						}
						else {
							(const_cast<ld::Atom*>(atom))->setSymbolTableInclusion(ld::Atom::symbolTableInWithRandomAutoStripLabel);
							_localAtoms.push_back(atom);
						}
					}
					else {
						if ( _options.keepLocalSymbol(atom->name()) ) 
							_localAtoms.push_back(atom);
						// <rdar://problem/5804214> ld should never have a symbol in the non-lazy indirect symbol table with index 0
						// this works by making __mh_execute_header be a local symbol which takes symbol index 0
						else if ( (atom->symbolTableInclusion() == ld::Atom::symbolTableInAndNeverStrip) && !_options.makeCompressedDyldInfo() )
							_localAtoms.push_back(atom);
						else
							(const_cast<ld::Atom*>(atom))->setSymbolTableInclusion(ld::Atom::symbolTableNotIn);
					}
					break;
			}
		}
	}
	
	// <rdar://problem/6978069> ld adds undefined symbol from .exp file to binary
	if ( (_options.outputKind() == Options::kKextBundle) && _options.hasExportRestrictList() ) {
		// search for referenced undefines
		std::set<const ld::Atom*> referencedProxyAtoms;
		for (std::vector<ld::Internal::FinalSection*>::iterator sit=state.sections.begin(); sit != state.sections.end(); ++sit) {
			ld::Internal::FinalSection* sect = *sit;
			for (std::vector<const ld::Atom*>::iterator ait=sect->atoms.begin();  ait != sect->atoms.end(); ++ait) {
				const ld::Atom* atom = *ait;
				for (ld::Fixup::iterator fit = atom->fixupsBegin(), end=atom->fixupsEnd(); fit != end; ++fit) {
					switch ( fit->binding ) {
						case ld::Fixup::bindingsIndirectlyBound:
							referencedProxyAtoms.insert(state.indirectBindingTable[fit->u.bindingIndex]);
							break;
						case ld::Fixup::bindingDirectlyBound:
							referencedProxyAtoms.insert(fit->u.target);
							break;
						default:
							break;
					}
				}
			}
		}
		// remove any unreferenced _importedAtoms
		_importedAtoms.erase(std::remove_if(_importedAtoms.begin(), _importedAtoms.end(), NotInSet(referencedProxyAtoms)), _importedAtoms.end());			
	}
	
	// sort by name
	std::sort(_exportedAtoms.begin(), _exportedAtoms.end(), AtomByNameSorter());
	std::sort(_importedAtoms.begin(), _importedAtoms.end(), AtomByNameSorter());
}

void OutputFile::addPreloadLinkEdit(ld::Internal& state)
{
	switch ( _options.architecture() ) {
#if SUPPORT_ARCH_i386
		case CPU_TYPE_I386:
			if ( _hasLocalRelocations ) {
				_localRelocsAtom = new LocalRelocationsAtom<x86>(_options, state, *this);
				localRelocationsSection = state.addAtom(*_localRelocsAtom);
			}
			if ( _hasExternalRelocations ) {
				_externalRelocsAtom = new ExternalRelocationsAtom<x86>(_options, state, *this);
				externalRelocationsSection = state.addAtom(*_externalRelocsAtom);
			}
			if ( _hasSymbolTable ) {
				_indirectSymbolTableAtom = new IndirectSymbolTableAtom<x86>(_options, state, *this);
				indirectSymbolTableSection = state.addAtom(*_indirectSymbolTableAtom);
				_symbolTableAtom = new SymbolTableAtom<x86>(_options, state, *this);
				symbolTableSection = state.addAtom(*_symbolTableAtom);
				_stringPoolAtom = new StringPoolAtom(_options, state, *this, 4);
				stringPoolSection = state.addAtom(*_stringPoolAtom);
			}
			break;
#endif
#if SUPPORT_ARCH_x86_64
		case CPU_TYPE_X86_64:
			if ( _hasLocalRelocations ) {
				_localRelocsAtom = new LocalRelocationsAtom<x86_64>(_options, state, *this);
				localRelocationsSection = state.addAtom(*_localRelocsAtom);
			}
			if ( _hasExternalRelocations ) {
				_externalRelocsAtom = new ExternalRelocationsAtom<x86_64>(_options, state, *this);
				externalRelocationsSection = state.addAtom(*_externalRelocsAtom);
			}
			if ( _hasSymbolTable ) {
				_indirectSymbolTableAtom = new IndirectSymbolTableAtom<x86_64>(_options, state, *this);
				indirectSymbolTableSection = state.addAtom(*_indirectSymbolTableAtom);
				_symbolTableAtom = new SymbolTableAtom<x86_64>(_options, state, *this);
				symbolTableSection = state.addAtom(*_symbolTableAtom);
				_stringPoolAtom = new StringPoolAtom(_options, state, *this, 4);
				stringPoolSection = state.addAtom(*_stringPoolAtom);
			}
			break;
#endif
#if SUPPORT_ARCH_arm_any
		case CPU_TYPE_ARM:
			if ( _hasLocalRelocations ) {
				_localRelocsAtom = new LocalRelocationsAtom<arm>(_options, state, *this);
				localRelocationsSection = state.addAtom(*_localRelocsAtom);
			}
			if ( _hasExternalRelocations ) {
				_externalRelocsAtom = new ExternalRelocationsAtom<arm>(_options, state, *this);
				externalRelocationsSection = state.addAtom(*_externalRelocsAtom);
			}
			if ( _hasSymbolTable ) {
				_indirectSymbolTableAtom = new IndirectSymbolTableAtom<arm>(_options, state, *this);
				indirectSymbolTableSection = state.addAtom(*_indirectSymbolTableAtom);
				_symbolTableAtom = new SymbolTableAtom<arm>(_options, state, *this);
				symbolTableSection = state.addAtom(*_symbolTableAtom);
				_stringPoolAtom = new StringPoolAtom(_options, state, *this, 4);
				stringPoolSection = state.addAtom(*_stringPoolAtom);
			}
			break;
#endif
		default:
			throw "architecture not supported for -preload";
	}

}


void OutputFile::addLinkEdit(ld::Internal& state)
{
	// for historical reasons, -preload orders LINKEDIT content differently
	if  ( _options.outputKind() == Options::kPreload ) 
		return addPreloadLinkEdit(state);
	
	switch ( _options.architecture() ) {
#if SUPPORT_ARCH_i386
		case CPU_TYPE_I386:
			if ( _hasSectionRelocations ) {
				_sectionsRelocationsAtom = new SectionRelocationsAtom<x86>(_options, state, *this);
				sectionRelocationsSection = state.addAtom(*_sectionsRelocationsAtom);
			}
			if ( _hasDyldInfo ) {
				_rebasingInfoAtom = new RebaseInfoAtom<x86>(_options, state, *this);
				rebaseSection = state.addAtom(*_rebasingInfoAtom);
				
				_bindingInfoAtom = new BindingInfoAtom<x86>(_options, state, *this);
				bindingSection = state.addAtom(*_bindingInfoAtom);
				
				_weakBindingInfoAtom = new WeakBindingInfoAtom<x86>(_options, state, *this);
				weakBindingSection = state.addAtom(*_weakBindingInfoAtom);
				
				_lazyBindingInfoAtom = new LazyBindingInfoAtom<x86>(_options, state, *this);
				lazyBindingSection = state.addAtom(*_lazyBindingInfoAtom);
				
				_exportInfoAtom = new ExportInfoAtom<x86>(_options, state, *this);
				exportSection = state.addAtom(*_exportInfoAtom);
			}
			if ( _hasLocalRelocations ) {
				_localRelocsAtom = new LocalRelocationsAtom<x86>(_options, state, *this);
				localRelocationsSection = state.addAtom(*_localRelocsAtom);
			}
			if  ( _hasSplitSegInfo ) {
				_splitSegInfoAtom = new SplitSegInfoAtom<x86>(_options, state, *this);
				splitSegInfoSection = state.addAtom(*_splitSegInfoAtom);
			}
			if ( _hasFunctionStartsInfo ) {
				_functionStartsAtom = new FunctionStartsAtom<x86>(_options, state, *this);
				functionStartsSection = state.addAtom(*_functionStartsAtom);
			}
			if ( _hasDataInCodeInfo ) {
				_dataInCodeAtom = new DataInCodeAtom<x86>(_options, state, *this);
				dataInCodeSection = state.addAtom(*_dataInCodeAtom);
			}
			if ( _hasDependentDRInfo ) {
				_dependentDRInfoAtom = new DependentDRAtom<x86>(_options, state, *this);
				dependentDRsSection = state.addAtom(*_dependentDRInfoAtom);
			}
			if ( _hasSymbolTable ) {
				_symbolTableAtom = new SymbolTableAtom<x86>(_options, state, *this);
				symbolTableSection = state.addAtom(*_symbolTableAtom);
			}
			if ( _hasExternalRelocations ) {
				_externalRelocsAtom = new ExternalRelocationsAtom<x86>(_options, state, *this);
				externalRelocationsSection = state.addAtom(*_externalRelocsAtom);
			}
			if ( _hasSymbolTable ) {
				_indirectSymbolTableAtom = new IndirectSymbolTableAtom<x86>(_options, state, *this);
				indirectSymbolTableSection = state.addAtom(*_indirectSymbolTableAtom);
				_stringPoolAtom = new StringPoolAtom(_options, state, *this, 4);
				stringPoolSection = state.addAtom(*_stringPoolAtom);
			}
			break;
#endif
#if SUPPORT_ARCH_x86_64
		case CPU_TYPE_X86_64:
			if ( _hasSectionRelocations ) {
				_sectionsRelocationsAtom = new SectionRelocationsAtom<x86_64>(_options, state, *this);
				sectionRelocationsSection = state.addAtom(*_sectionsRelocationsAtom);
			}
			if ( _hasDyldInfo ) {
				_rebasingInfoAtom = new RebaseInfoAtom<x86_64>(_options, state, *this);
				rebaseSection = state.addAtom(*_rebasingInfoAtom);
				
				_bindingInfoAtom = new BindingInfoAtom<x86_64>(_options, state, *this);
				bindingSection = state.addAtom(*_bindingInfoAtom);
				
				_weakBindingInfoAtom = new WeakBindingInfoAtom<x86_64>(_options, state, *this);
				weakBindingSection = state.addAtom(*_weakBindingInfoAtom);
				
				_lazyBindingInfoAtom = new LazyBindingInfoAtom<x86_64>(_options, state, *this);
				lazyBindingSection = state.addAtom(*_lazyBindingInfoAtom);
				
				_exportInfoAtom = new ExportInfoAtom<x86_64>(_options, state, *this);
				exportSection = state.addAtom(*_exportInfoAtom);
			}
			if ( _hasLocalRelocations ) {
				_localRelocsAtom = new LocalRelocationsAtom<x86_64>(_options, state, *this);
				localRelocationsSection = state.addAtom(*_localRelocsAtom);
			}
			if  ( _hasSplitSegInfo ) {
				_splitSegInfoAtom = new SplitSegInfoAtom<x86_64>(_options, state, *this);
				splitSegInfoSection = state.addAtom(*_splitSegInfoAtom);
			}
			if ( _hasFunctionStartsInfo ) {
				_functionStartsAtom = new FunctionStartsAtom<x86_64>(_options, state, *this);
				functionStartsSection = state.addAtom(*_functionStartsAtom);
			}
			if ( _hasDataInCodeInfo ) {
				_dataInCodeAtom = new DataInCodeAtom<x86_64>(_options, state, *this);
				dataInCodeSection = state.addAtom(*_dataInCodeAtom);
			}
			if ( _hasDependentDRInfo ) {
				_dependentDRInfoAtom = new DependentDRAtom<x86_64>(_options, state, *this);
				dependentDRsSection = state.addAtom(*_dependentDRInfoAtom);
			}
			if ( _hasSymbolTable ) {
				_symbolTableAtom = new SymbolTableAtom<x86_64>(_options, state, *this);
				symbolTableSection = state.addAtom(*_symbolTableAtom);
			}
			if ( _hasExternalRelocations ) {
				_externalRelocsAtom = new ExternalRelocationsAtom<x86_64>(_options, state, *this);
				externalRelocationsSection = state.addAtom(*_externalRelocsAtom);
			}
			if ( _hasSymbolTable ) {
				_indirectSymbolTableAtom = new IndirectSymbolTableAtom<x86_64>(_options, state, *this);
				indirectSymbolTableSection = state.addAtom(*_indirectSymbolTableAtom);
				_stringPoolAtom = new StringPoolAtom(_options, state, *this, 8);
				stringPoolSection = state.addAtom(*_stringPoolAtom);
			}
			break;
#endif
#if SUPPORT_ARCH_arm_any
		case CPU_TYPE_ARM:
			if ( _hasSectionRelocations ) {
				_sectionsRelocationsAtom = new SectionRelocationsAtom<arm>(_options, state, *this);
				sectionRelocationsSection = state.addAtom(*_sectionsRelocationsAtom);
			}
			if ( _hasDyldInfo ) {
				_rebasingInfoAtom = new RebaseInfoAtom<arm>(_options, state, *this);
				rebaseSection = state.addAtom(*_rebasingInfoAtom);
				
				_bindingInfoAtom = new BindingInfoAtom<arm>(_options, state, *this);
				bindingSection = state.addAtom(*_bindingInfoAtom);
				
				_weakBindingInfoAtom = new WeakBindingInfoAtom<arm>(_options, state, *this);
				weakBindingSection = state.addAtom(*_weakBindingInfoAtom);
				
				_lazyBindingInfoAtom = new LazyBindingInfoAtom<arm>(_options, state, *this);
				lazyBindingSection = state.addAtom(*_lazyBindingInfoAtom);
				
				_exportInfoAtom = new ExportInfoAtom<arm>(_options, state, *this);
				exportSection = state.addAtom(*_exportInfoAtom);
			}
			if ( _hasLocalRelocations ) {
				_localRelocsAtom = new LocalRelocationsAtom<arm>(_options, state, *this);
				localRelocationsSection = state.addAtom(*_localRelocsAtom);
			}
			if  ( _hasSplitSegInfo ) {
				_splitSegInfoAtom = new SplitSegInfoAtom<arm>(_options, state, *this);
				splitSegInfoSection = state.addAtom(*_splitSegInfoAtom);
			}
			if ( _hasFunctionStartsInfo ) {
				_functionStartsAtom = new FunctionStartsAtom<arm>(_options, state, *this);
				functionStartsSection = state.addAtom(*_functionStartsAtom);
			}
			if ( _hasDataInCodeInfo ) {
				_dataInCodeAtom = new DataInCodeAtom<arm>(_options, state, *this);
				dataInCodeSection = state.addAtom(*_dataInCodeAtom);
			}
			if ( _hasDependentDRInfo ) {
				_dependentDRInfoAtom = new DependentDRAtom<arm>(_options, state, *this);
				dependentDRsSection = state.addAtom(*_dependentDRInfoAtom);
			}
			if ( _hasSymbolTable ) {
				_symbolTableAtom = new SymbolTableAtom<arm>(_options, state, *this);
				symbolTableSection = state.addAtom(*_symbolTableAtom);
			}
			if ( _hasExternalRelocations ) {
				_externalRelocsAtom = new ExternalRelocationsAtom<arm>(_options, state, *this);
				externalRelocationsSection = state.addAtom(*_externalRelocsAtom);
			}
			if ( _hasSymbolTable ) {
				_indirectSymbolTableAtom = new IndirectSymbolTableAtom<arm>(_options, state, *this);
				indirectSymbolTableSection = state.addAtom(*_indirectSymbolTableAtom);
				_stringPoolAtom = new StringPoolAtom(_options, state, *this, 4);
				stringPoolSection = state.addAtom(*_stringPoolAtom);
			}
			break;
#endif
		default:
			throw "unknown architecture";
	}
}

void OutputFile::addLoadCommands(ld::Internal& state)
{
	switch ( _options.architecture() ) {
#if SUPPORT_ARCH_x86_64
		case CPU_TYPE_X86_64:
			_headersAndLoadCommandAtom = new HeaderAndLoadCommandsAtom<x86_64>(_options, state, *this);
			headerAndLoadCommandsSection = state.addAtom(*_headersAndLoadCommandAtom);
			break;
#endif
#if SUPPORT_ARCH_arm_any
		case CPU_TYPE_ARM:
			_headersAndLoadCommandAtom = new HeaderAndLoadCommandsAtom<arm>(_options, state, *this);
			headerAndLoadCommandsSection = state.addAtom(*_headersAndLoadCommandAtom);
			break;
#endif
#if SUPPORT_ARCH_i386
		case CPU_TYPE_I386:
			_headersAndLoadCommandAtom = new HeaderAndLoadCommandsAtom<x86>(_options, state, *this);
			headerAndLoadCommandsSection = state.addAtom(*_headersAndLoadCommandAtom);
			break;
#endif
		default:
			throw "unknown architecture";
	}
}

uint32_t OutputFile::dylibCount()
{
	return _dylibsToLoad.size();
}

const ld::dylib::File* OutputFile::dylibByOrdinal(unsigned int ordinal)
{
	assert( ordinal > 0 );
	assert( ordinal <= _dylibsToLoad.size() );
	return _dylibsToLoad[ordinal-1];
}

bool OutputFile::hasOrdinalForInstallPath(const char* path, int* ordinal)
{
	for (std::map<const ld::dylib::File*, int>::const_iterator it = _dylibToOrdinal.begin(); it != _dylibToOrdinal.end(); ++it) {
		const char* installPath = it->first->installPath();
		if ( (installPath != NULL) && (strcmp(path, installPath) == 0) ) {
			*ordinal = it->second;
			return true;
		}
	}
	return false;
}

uint32_t OutputFile::dylibToOrdinal(const ld::dylib::File* dylib)
{
	return _dylibToOrdinal[dylib];
}


void OutputFile::buildDylibOrdinalMapping(ld::Internal& state)
{
	// count non-public re-exported dylibs
	unsigned int nonPublicReExportCount = 0;
	for (std::vector<ld::dylib::File*>::iterator it = state.dylibs.begin(); it != state.dylibs.end(); ++it) {
		ld::dylib::File* aDylib = *it;
		if ( aDylib->willBeReExported() && ! aDylib->hasPublicInstallName() ) 
			++nonPublicReExportCount;
	}
	
	// look at each dylib supplied in state
	bool hasReExports = false;
	bool haveLazyDylibs = false;
	for (std::vector<ld::dylib::File*>::iterator it = state.dylibs.begin(); it != state.dylibs.end(); ++it) {
		ld::dylib::File* aDylib = *it;
		int ordinal;
		if ( aDylib == state.bundleLoader ) {
			_dylibToOrdinal[aDylib] = BIND_SPECIAL_DYLIB_MAIN_EXECUTABLE;
		}
		else if ( this->hasOrdinalForInstallPath(aDylib->installPath(), &ordinal) ) {
			// already have a dylib with that install path, map all uses to that ordinal
			_dylibToOrdinal[aDylib] = ordinal;
		}
		else if ( aDylib->willBeLazyLoadedDylib() ) {
			// all lazy dylib need to be at end of ordinals
			haveLazyDylibs = true;
		}
		else if ( aDylib->willBeReExported() && ! aDylib->hasPublicInstallName() && (nonPublicReExportCount >= 2) ) {
			_dylibsToLoad.push_back(aDylib);
			_dylibToOrdinal[aDylib] = BIND_SPECIAL_DYLIB_SELF;
		}
		else {
			// first time this install path seen, create new ordinal
			_dylibsToLoad.push_back(aDylib);
			_dylibToOrdinal[aDylib] = _dylibsToLoad.size();
		}
		if ( aDylib->explicitlyLinked() && aDylib->willBeReExported() )
			hasReExports = true;
	}
	if ( haveLazyDylibs ) {
		// second pass to determine ordinals for lazy loaded dylibs
		for (std::vector<ld::dylib::File*>::iterator it = state.dylibs.begin(); it != state.dylibs.end(); ++it) {
			ld::dylib::File* aDylib = *it;
			if ( aDylib->willBeLazyLoadedDylib() ) {
				int ordinal;
				if ( this->hasOrdinalForInstallPath(aDylib->installPath(), &ordinal) ) {
					// already have a dylib with that install path, map all uses to that ordinal
					_dylibToOrdinal[aDylib] = ordinal;
				}
				else {
					// first time this install path seen, create new ordinal
					_dylibsToLoad.push_back(aDylib);
					_dylibToOrdinal[aDylib] = _dylibsToLoad.size();
				}
			}
		}
	}
	_noReExportedDylibs = !hasReExports;
	//fprintf(stderr, "dylibs:\n");
	//for (std::map<const ld::dylib::File*, int>::const_iterator it = _dylibToOrdinal.begin(); it != _dylibToOrdinal.end(); ++it) {
	//	fprintf(stderr, " %p ord=%u, install_name=%s\n",it->first, it->second, it->first->installPath());
	//}
}

uint32_t OutputFile::lazyBindingInfoOffsetForLazyPointerAddress(uint64_t lpAddress)
{
	return _lazyPointerAddressToInfoOffset[lpAddress];
}

void OutputFile::setLazyBindingInfoOffset(uint64_t lpAddress, uint32_t lpInfoOffset)
{
	_lazyPointerAddressToInfoOffset[lpAddress] = lpInfoOffset;
}

int OutputFile::compressedOrdinalForAtom(const ld::Atom* target)
{
	// flat namespace images use zero for all ordinals
	if ( _options.nameSpace() != Options::kTwoLevelNameSpace )
		return BIND_SPECIAL_DYLIB_FLAT_LOOKUP;

	// handle -interposable
	if ( target->definition() == ld::Atom::definitionRegular )
		return BIND_SPECIAL_DYLIB_SELF;

	// regular ordinal
	const ld::dylib::File* dylib = dynamic_cast<const ld::dylib::File*>(target->file());
	if ( dylib != NULL ) {
		std::map<const ld::dylib::File*, int>::iterator pos = _dylibToOrdinal.find(dylib);
		if ( pos != _dylibToOrdinal.end() )
			return pos->second;
		assert(0 && "dylib not assigned ordinal");
	}
	
	// handle undefined dynamic_lookup
	if ( _options.undefinedTreatment() == Options::kUndefinedDynamicLookup )
		return BIND_SPECIAL_DYLIB_FLAT_LOOKUP;
	
	// handle -U _foo
	if ( _options.allowedUndefined(target->name()) )
		return BIND_SPECIAL_DYLIB_FLAT_LOOKUP;

	throw "can't find ordinal for imported symbol";
}


bool OutputFile::isPcRelStore(ld::Fixup::Kind kind)
{
	switch ( kind ) { 
		case ld::Fixup::kindStoreX86BranchPCRel8:
		case ld::Fixup::kindStoreX86BranchPCRel32:
		case ld::Fixup::kindStoreX86PCRel8:
		case ld::Fixup::kindStoreX86PCRel16:
		case ld::Fixup::kindStoreX86PCRel32:
		case ld::Fixup::kindStoreX86PCRel32_1:
		case ld::Fixup::kindStoreX86PCRel32_2:
		case ld::Fixup::kindStoreX86PCRel32_4:
		case ld::Fixup::kindStoreX86PCRel32GOTLoad:
		case ld::Fixup::kindStoreX86PCRel32GOTLoadNowLEA:
		case ld::Fixup::kindStoreX86PCRel32GOT:
		case ld::Fixup::kindStoreX86PCRel32TLVLoad:
		case ld::Fixup::kindStoreX86PCRel32TLVLoadNowLEA:
		case ld::Fixup::kindStoreARMBranch24:
		case ld::Fixup::kindStoreThumbBranch22:
		case ld::Fixup::kindStoreARMLoad12:
		case ld::Fixup::kindStoreTargetAddressX86PCRel32:
		case ld::Fixup::kindStoreTargetAddressX86PCRel32GOTLoad:
		case ld::Fixup::kindStoreTargetAddressX86PCRel32GOTLoadNowLEA:
		case ld::Fixup::kindStoreTargetAddressX86PCRel32TLVLoad:
		case ld::Fixup::kindStoreTargetAddressX86PCRel32TLVLoadNowLEA:
		case ld::Fixup::kindStoreTargetAddressARMBranch24:
		case ld::Fixup::kindStoreTargetAddressThumbBranch22:
		case ld::Fixup::kindStoreTargetAddressARMLoad12:
			return true;
		case ld::Fixup::kindStoreTargetAddressX86BranchPCRel32:
			return (_options.outputKind() != Options::kKextBundle);
		default:
			break;
	}
	return false;
}

bool OutputFile::isStore(ld::Fixup::Kind kind)
{
	switch ( kind ) { 
		case ld::Fixup::kindNone:
		case ld::Fixup::kindNoneFollowOn:
		case ld::Fixup::kindNoneGroupSubordinate:
		case ld::Fixup::kindNoneGroupSubordinateFDE:
		case ld::Fixup::kindNoneGroupSubordinateLSDA:
		case ld::Fixup::kindNoneGroupSubordinatePersonality:
		case ld::Fixup::kindSetTargetAddress:
		case ld::Fixup::kindSubtractTargetAddress:
		case ld::Fixup::kindAddAddend:
		case ld::Fixup::kindSubtractAddend:
		case ld::Fixup::kindSetTargetImageOffset:
		case ld::Fixup::kindSetTargetSectionOffset:
			return false;
		default:
			break;
	}
	return true;
}


bool OutputFile::setsTarget(ld::Fixup::Kind kind)
{
	switch ( kind ) { 
		case ld::Fixup::kindSetTargetAddress:
		case ld::Fixup::kindLazyTarget:
		case ld::Fixup::kindStoreTargetAddressLittleEndian32:
		case ld::Fixup::kindStoreTargetAddressLittleEndian64:
		case ld::Fixup::kindStoreTargetAddressBigEndian32:
		case ld::Fixup::kindStoreTargetAddressBigEndian64:
		case ld::Fixup::kindStoreTargetAddressX86PCRel32:
		case ld::Fixup::kindStoreTargetAddressX86BranchPCRel32:
		case ld::Fixup::kindStoreTargetAddressX86PCRel32GOTLoad:
		case ld::Fixup::kindStoreTargetAddressX86PCRel32GOTLoadNowLEA:
		case ld::Fixup::kindStoreTargetAddressX86PCRel32TLVLoad:
		case ld::Fixup::kindStoreTargetAddressX86Abs32TLVLoad:
		case ld::Fixup::kindStoreTargetAddressARMBranch24:
		case ld::Fixup::kindStoreTargetAddressThumbBranch22:
		case ld::Fixup::kindStoreTargetAddressARMLoad12:
			return true;
		case ld::Fixup::kindStoreX86DtraceCallSiteNop:
		case ld::Fixup::kindStoreX86DtraceIsEnableSiteClear:
		case ld::Fixup::kindStoreARMDtraceCallSiteNop:
		case ld::Fixup::kindStoreARMDtraceIsEnableSiteClear:
		case ld::Fixup::kindStoreThumbDtraceCallSiteNop:
		case ld::Fixup::kindStoreThumbDtraceIsEnableSiteClear:
			return (_options.outputKind() == Options::kObjectFile);
		default:
			break;
	}
	return false;
}

bool OutputFile::isPointerToTarget(ld::Fixup::Kind kind)
{
	switch ( kind ) { 
		case ld::Fixup::kindSetTargetAddress:
		case ld::Fixup::kindStoreTargetAddressLittleEndian32:
		case ld::Fixup::kindStoreTargetAddressLittleEndian64:
		case ld::Fixup::kindStoreTargetAddressBigEndian32:
		case ld::Fixup::kindStoreTargetAddressBigEndian64:
		case ld::Fixup::kindLazyTarget:
			return true;
		default:
			break;
	}
	return false;
}
bool OutputFile::isPointerFromTarget(ld::Fixup::Kind kind)
{
	switch ( kind ) { 
		case ld::Fixup::kindSubtractTargetAddress:
			return true;
		default:
			break;
	}
	return false;
}


uint64_t OutputFile::lookBackAddend(ld::Fixup::iterator fit)
{
	uint64_t addend = 0;
	switch ( fit->clusterSize ) {
		case ld::Fixup::k1of1:
		case ld::Fixup::k1of2:
		case ld::Fixup::k2of2:
			break;
		case ld::Fixup::k2of3:
			--fit;
			switch ( fit->kind ) {
				case ld::Fixup::kindAddAddend:
					addend += fit->u.addend;
					break;
				case ld::Fixup::kindSubtractAddend:
					addend -= fit->u.addend;
					break;
				default:
					throw "unexpected fixup kind for binding";
			}
			break;
		case ld::Fixup::k1of3:
			++fit;
			switch ( fit->kind ) {
				case ld::Fixup::kindAddAddend:
					addend += fit->u.addend;
					break;
				case ld::Fixup::kindSubtractAddend:
					addend -= fit->u.addend;
					break;
				default:
					throw "unexpected fixup kind for binding";
			}
			break;
		default:
			throw "unexpected fixup cluster size for binding";
	}
	return addend;
}





void OutputFile::generateLinkEditInfo(ld::Internal& state)
{
	for (std::vector<ld::Internal::FinalSection*>::iterator sit = state.sections.begin(); sit != state.sections.end(); ++sit) {
		ld::Internal::FinalSection* sect = *sit;
		bool objc1ClassRefSection = ( (sect->type() == ld::Section::typeCStringPointer) 
									&& (strcmp(sect->sectionName(), "__cls_refs") == 0)
									&& (strcmp(sect->segmentName(), "__OBJC") == 0) );
		for (std::vector<const ld::Atom*>::iterator ait = sect->atoms.begin(); ait != sect->atoms.end(); ++ait) {
			const ld::Atom*		atom = *ait;
			
			// Record regular atoms that override a dylib's weak definitions 
			if ( (atom->scope() == ld::Atom::scopeGlobal) && atom->overridesDylibsWeakDef() ) {
				if ( _options.makeCompressedDyldInfo() ) {
					uint8_t wtype = BIND_TYPE_OVERRIDE_OF_WEAKDEF_IN_DYLIB;
					bool nonWeakDef = (atom->combine() == ld::Atom::combineNever);
					_weakBindingInfo.push_back(BindingInfo(wtype, atom->name(), nonWeakDef, atom->finalAddress(), 0));
				}
				this->overridesWeakExternalSymbols = true;
				if ( _options.warnWeakExports()	)
					warning("overrides weak external symbol: %s", atom->name());
			}
			
			ld::Fixup*			fixupWithTarget = NULL;
			ld::Fixup*			fixupWithMinusTarget = NULL;
			ld::Fixup*			fixupWithStore = NULL;
			const ld::Atom*		target = NULL;
			const ld::Atom*		minusTarget = NULL;
			uint64_t			targetAddend = 0;
			uint64_t			minusTargetAddend = 0;
			for (ld::Fixup::iterator fit = atom->fixupsBegin(); fit != atom->fixupsEnd(); ++fit) {
				if ( fit->firstInCluster() ) {
					fixupWithTarget = NULL;
					fixupWithMinusTarget = NULL;
					fixupWithStore = NULL;
					target = NULL;
					minusTarget = NULL;
					targetAddend = 0;
					minusTargetAddend = 0;
				}
				if ( this->setsTarget(fit->kind) ) {
					switch ( fit->binding ) {
						case ld::Fixup::bindingNone:
						case ld::Fixup::bindingByNameUnbound:
							break;
						case ld::Fixup::bindingByContentBound:
						case ld::Fixup::bindingDirectlyBound:
							fixupWithTarget = fit;
							target = fit->u.target;
							break;
						case ld::Fixup::bindingsIndirectlyBound:
							fixupWithTarget = fit;
							target = state.indirectBindingTable[fit->u.bindingIndex];
							break;
					}
					assert(target != NULL);
				}
				switch ( fit->kind ) {
					case ld::Fixup::kindAddAddend:
						targetAddend = fit->u.addend;
						break;
					case ld::Fixup::kindSubtractAddend:
						minusTargetAddend = fit->u.addend;
						break;
					case ld::Fixup::kindSubtractTargetAddress:
						switch ( fit->binding ) {
							case ld::Fixup::bindingNone:
							case ld::Fixup::bindingByNameUnbound:
								break;
							case ld::Fixup::bindingByContentBound:
							case ld::Fixup::bindingDirectlyBound:
								fixupWithMinusTarget = fit;
								minusTarget = fit->u.target;
								break;
							case ld::Fixup::bindingsIndirectlyBound:
								fixupWithMinusTarget = fit;
								minusTarget = state.indirectBindingTable[fit->u.bindingIndex];
								break;
						}
						assert(minusTarget != NULL);
						break;
					case ld::Fixup::kindDataInCodeStartData:
					case ld::Fixup::kindDataInCodeStartJT8:
					case ld::Fixup::kindDataInCodeStartJT16:
					case ld::Fixup::kindDataInCodeStartJT32:
					case ld::Fixup::kindDataInCodeStartJTA32:
					case ld::Fixup::kindDataInCodeEnd:
						hasDataInCode = true;
						break;
					default:
                        break;    
				}
				if ( this->isStore(fit->kind) ) {
					fixupWithStore = fit;
				}
				if ( fit->lastInCluster() ) {
					if ( (fixupWithStore != NULL) && (target != NULL) ) {
						if ( _options.outputKind() == Options::kObjectFile ) {
							this->addSectionRelocs(state, sect, atom, fixupWithTarget, fixupWithMinusTarget, fixupWithStore,
													target, minusTarget, targetAddend, minusTargetAddend);
						}
						else {
							if ( _options.makeCompressedDyldInfo() ) {
								this->addDyldInfo(state, sect, atom, fixupWithTarget, fixupWithMinusTarget, fixupWithStore,
													target, minusTarget, targetAddend, minusTargetAddend);
							}
							else { 
								this->addClassicRelocs(state, sect, atom, fixupWithTarget, fixupWithMinusTarget, fixupWithStore,
													target, minusTarget, targetAddend, minusTargetAddend);
							}
						}
					}
					else if ( objc1ClassRefSection && (target != NULL) && (fixupWithStore == NULL) ) {
						// check for class refs to lazy loaded dylibs
						const ld::dylib::File* dylib = dynamic_cast<const ld::dylib::File*>(target->file());
						if ( (dylib != NULL) && dylib->willBeLazyLoadedDylib() )
							throwf("illegal class reference to %s in lazy loaded dylib %s", target->name(), dylib->path());
					}
				}
			}
		}
	}
}


void OutputFile::noteTextReloc(const ld::Atom* atom, const ld::Atom* target) 
{
	if ( (atom->contentType() == ld::Atom::typeStub) || (atom->contentType() == ld::Atom::typeStubHelper) ) {
		// silently let stubs (synthesized by linker) use text relocs
	}
	else if ( _options.allowTextRelocs() ) {
		if ( _options.warnAboutTextRelocs() )
			warning("text reloc in %s to %s", atom->name(), target->name());
	} 
	else if ( _options.positionIndependentExecutable() && (_options.outputKind() == Options::kDynamicExecutable) 
		&& ((_options.iOSVersionMin() >= ld::iOS_4_3) || (_options.macosxVersionMin() >= ld::mac10_7)) ) {
		if ( ! this->pieDisabled ) {
			warning("PIE disabled. Absolute addressing (perhaps -mdynamic-no-pic) not allowed in code signed PIE, "
				"but used in %s from %s. " 
				"To fix this warning, don't compile with -mdynamic-no-pic or link with -Wl,-no_pie", 
				atom->name(), atom->file()->path());
		}
		this->pieDisabled = true;
	}
	else if ( (target->scope() == ld::Atom::scopeGlobal) && (target->combine() == ld::Atom::combineByName) ) {
		throwf("illegal text-relocoation (direct reference) to (global,weak) %s in %s from %s in %s", target->name(), target->file()->path(), atom->name(), atom->file()->path());
	}
	else {
		throwf("illegal text-relocation to %s in %s from %s in %s", target->name(), target->file()->path(), atom->name(), atom->file()->path());
	}
}

void OutputFile::addDyldInfo(ld::Internal& state,  ld::Internal::FinalSection* sect, const ld::Atom* atom,  
								ld::Fixup* fixupWithTarget, ld::Fixup* fixupWithMinusTarget, ld::Fixup* fixupWithStore,
								const ld::Atom* target, const ld::Atom* minusTarget, 
								uint64_t targetAddend, uint64_t minusTargetAddend)
{
	if ( sect->isSectionHidden() )
		return;

	// no need to rebase or bind PCRel stores
	if ( this->isPcRelStore(fixupWithStore->kind) ) {
		// as long as target is in same linkage unit
		if ( (target == NULL) || (target->definition() != ld::Atom::definitionProxy) ) {
			// make sure target is not global and weak
			if ( (target->scope() == ld::Atom::scopeGlobal) && (target->combine() == ld::Atom::combineByName) && (target->definition() == ld::Atom::definitionRegular)) {
				if ( (atom->section().type() == ld::Section::typeCFI)
					|| (atom->section().type() == ld::Section::typeDtraceDOF)
					|| (atom->section().type() == ld::Section::typeUnwindInfo) ) {
					// ok for __eh_frame and __uwind_info to use pointer diffs to global weak symbols
					return;
				}
				// Have direct reference to weak-global.  This should be an indrect reference
				const char* demangledName = strdup(_options.demangleSymbol(atom->name()));
				warning("direct access in %s to global weak symbol %s means the weak symbol cannot be overridden at runtime. "
						"This was likely caused by different translation units being compiled with different visibility settings.",
						  demangledName, _options.demangleSymbol(target->name()));
			}
			return;
		}
	}

	// no need to rebase or bind PIC internal pointer diff
	if ( minusTarget != NULL ) {
		// with pointer diffs, both need to be in same linkage unit
		assert(minusTarget->definition() != ld::Atom::definitionProxy);
		assert(target != NULL);
		assert(target->definition() != ld::Atom::definitionProxy);
		if ( target == minusTarget ) {
			// This is a compile time constant and could have been optimized away by compiler
			return;
		}
		
		// check if target of pointer-diff is global and weak
		if ( (target->scope() == ld::Atom::scopeGlobal) && (target->combine() == ld::Atom::combineByName) && (target->definition() == ld::Atom::definitionRegular) ) {
			if ( (atom->section().type() == ld::Section::typeCFI)
				|| (atom->section().type() == ld::Section::typeDtraceDOF)
				|| (atom->section().type() == ld::Section::typeUnwindInfo) ) {
				// ok for __eh_frame and __uwind_info to use pointer diffs to global weak symbols
				return;
			}
			// Have direct reference to weak-global.  This should be an indrect reference
			const char* demangledName = strdup(_options.demangleSymbol(atom->name()));
			warning("direct access in %s to global weak symbol %s means the weak symbol cannot be overridden at runtime. "
					"This was likely caused by different translation units being compiled with different visibility settings.",
					 demangledName, _options.demangleSymbol(target->name()));
		}
		return;
	}

	// no need to rebase or bind an atom's references to itself if the output is not slidable
	if ( (atom == target) && !_options.outputSlidable() )
		return;

	// cluster has no target, so needs no rebasing or binding	
	if ( target == NULL )
		return; 

	bool inReadOnlySeg = ( strcmp(sect->segmentName(), "__TEXT") == 0 );
	bool needsRebase = false;
	bool needsBinding = false;
	bool needsLazyBinding = false;
	bool needsWeakBinding = false;

	uint8_t	rebaseType = REBASE_TYPE_POINTER;
	uint8_t type = BIND_TYPE_POINTER;
	const ld::dylib::File* dylib = dynamic_cast<const ld::dylib::File*>(target->file());
    bool weak_import = (fixupWithTarget->weakImport || ((dylib != NULL) && dylib->forcedWeakLinked()));
	uint64_t address =  atom->finalAddress() + fixupWithTarget->offsetInAtom;
	uint64_t addend = targetAddend - minusTargetAddend;

	// special case lazy pointers
	if ( fixupWithTarget->kind == ld::Fixup::kindLazyTarget ) {
		assert(fixupWithTarget->u.target == target);
		assert(addend == 0);
		// lazy dylib lazy pointers do not have any dyld info
		if ( atom->section().type() == ld::Section::typeLazyDylibPointer )
			return;
		// lazy binding to weak definitions are done differently
		// they are directly bound to target, then have a weak bind in case of a collision
		if ( target->combine() == ld::Atom::combineByName ) {
			if ( target->definition() == ld::Atom::definitionProxy ) {
				// weak def exported from another dylib
				// must non-lazy bind to it plus have weak binding info in case of collision
				needsBinding = true;
				needsWeakBinding = true;
			}
			else {
				// weak def in this linkage unit.  
				// just rebase, plus have weak binding info in case of collision
				// this will be done by other cluster on lazy pointer atom
			}
		}
		else if ( (target->contentType() == ld::Atom::typeResolver) && (target->scope() != ld::Atom::scopeGlobal) ) {
			// <rdar://problem/8553647> Hidden resolver functions should not have lazy binding info
			needsLazyBinding = false;
		}
		else {
			// normal case of a pointer to non-weak-def symbol, so can lazily bind
			needsLazyBinding = true;
		}
	}
	else {
		// everything except lazy pointers
		switch ( target->definition() ) {
			case ld::Atom::definitionProxy:
				if ( (dylib != NULL) && dylib->willBeLazyLoadedDylib() )
					throwf("illegal data reference to %s in lazy loaded dylib %s", target->name(), dylib->path());
				if ( target->contentType() == ld::Atom::typeTLV ) {
					if ( sect->type() != ld::Section::typeTLVPointers )
						throwf("illegal data reference in %s to thread local variable %s in dylib %s", 
								atom->name(), target->name(), dylib->path());
				}
				if ( inReadOnlySeg ) 
					type = BIND_TYPE_TEXT_ABSOLUTE32;
				needsBinding = true;
				if ( target->combine() == ld::Atom::combineByName ) 
					needsWeakBinding = true;
				break;
			case ld::Atom::definitionRegular:
			case ld::Atom::definitionTentative:
				// only slideable images need rebasing info
				if ( _options.outputSlidable() ) {
					needsRebase = true;
				}
				// references to internal symbol never need binding
				if ( target->scope() != ld::Atom::scopeGlobal ) 
					break;
				// reference to global weak def needs weak binding
				if ( (target->combine() == ld::Atom::combineByName) && (target->definition() == ld::Atom::definitionRegular) )
					needsWeakBinding = true;
				else if ( _options.outputKind() == Options::kDynamicExecutable ) {
					// in main executables, the only way regular symbols are indirected is if -interposable is used
					if ( _options.interposable(target->name()) ) {
						needsRebase = false;
						needsBinding = true;
					}
				}
				else {
					// for flat-namespace or interposable two-level-namespace
					// all references to exported symbols get indirected
					if ( (_options.nameSpace() != Options::kTwoLevelNameSpace) || _options.interposable(target->name()) ) {
						// <rdar://problem/5254468> no external relocs for flat objc classes
						if ( strncmp(target->name(), ".objc_class_", 12) == 0 )
							break;
						// no rebase info for references to global symbols that will have binding info
						needsRebase = false;
						needsBinding = true;
					}
				}
				break;
			case ld::Atom::definitionAbsolute:
				break;
		}
	}
	
	// record dyld info for this cluster
	if ( needsRebase ) {
		if ( inReadOnlySeg ) {
			noteTextReloc(atom, target);
			sect->hasLocalRelocs = true;  // so dyld knows to change permissions on __TEXT segment
			rebaseType = REBASE_TYPE_TEXT_ABSOLUTE32;
		}
		if ( (addend != 0) && _options.sharedRegionEligible() ) {
			// make sure the addend does not cause the pointer to point outside the target's segment
			// if it does, update_dyld_shared_cache will not be able to put this dylib into the shared cache
			uint64_t targetAddress = target->finalAddress();
			for (std::vector<ld::Internal::FinalSection*>::iterator sit = state.sections.begin(); sit != state.sections.end(); ++sit) {
				ld::Internal::FinalSection* sct = *sit;
				uint64_t sctEnd = (sct->address+sct->size);
				if ( (sct->address <= targetAddress) && (targetAddress < sctEnd) ) {
					if ( (targetAddress+addend) > sctEnd ) {
						warning("data symbol %s from %s has pointer to %s + 0x%08llX. "  
								"That large of an addend may disable %s from being put in the dyld shared cache.", 
								atom->name(), atom->file()->path(), target->name(), addend, _options.installPath() );
					}
				}
			}
		}
		_rebaseInfo.push_back(RebaseInfo(rebaseType, address));
	}
	if ( needsBinding ) {
		if ( inReadOnlySeg ) {
			noteTextReloc(atom, target);
			sect->hasExternalRelocs = true; // so dyld knows to change permissions on __TEXT segment
		}
		_bindingInfo.push_back(BindingInfo(type, this->compressedOrdinalForAtom(target), target->name(), weak_import, address, addend));
	}
	if ( needsLazyBinding ) {
		if ( _options.bindAtLoad() )
			_bindingInfo.push_back(BindingInfo(type, this->compressedOrdinalForAtom(target), target->name(), weak_import, address, addend));
		else
			_lazyBindingInfo.push_back(BindingInfo(type, this->compressedOrdinalForAtom(target), target->name(), weak_import, address, addend));
	}
	if ( needsWeakBinding )
		_weakBindingInfo.push_back(BindingInfo(type, 0, target->name(), false, address, addend));
}


void OutputFile::addClassicRelocs(ld::Internal& state, ld::Internal::FinalSection* sect, const ld::Atom* atom, 
								ld::Fixup* fixupWithTarget, ld::Fixup* fixupWithMinusTarget, ld::Fixup* fixupWithStore,
								const ld::Atom* target, const ld::Atom* minusTarget, 
								uint64_t targetAddend, uint64_t minusTargetAddend)
{
	if ( sect->isSectionHidden() )
		return;
	
	// non-lazy-pointer section is encoded in indirect symbol table - not using relocations
	if ( sect->type() == ld::Section::typeNonLazyPointer ) {
		// except kexts and static pie which *do* use relocations
		switch (_options.outputKind()) {
			case Options::kKextBundle:
				break;
			case Options::kStaticExecutable:
				if ( _options.positionIndependentExecutable() )
					break;
				// else fall into default case
			default:
				assert(target != NULL);
				assert(fixupWithTarget != NULL);
				return;
		}
	}
	
	// no need to rebase or bind PCRel stores
	if ( this->isPcRelStore(fixupWithStore->kind) ) {
		// as long as target is in same linkage unit
		if ( (target == NULL) || (target->definition() != ld::Atom::definitionProxy) )
			return;
	}

	// no need to rebase or bind PIC internal pointer diff
	if ( minusTarget != NULL ) {
		// with pointer diffs, both need to be in same linkage unit
		assert(minusTarget->definition() != ld::Atom::definitionProxy);
		assert(target != NULL);
		assert(target->definition() != ld::Atom::definitionProxy);
		// make sure target is not global and weak
		if ( (target->scope() == ld::Atom::scopeGlobal) && (target->combine() == ld::Atom::combineByName)
				&& (atom->section().type() != ld::Section::typeCFI)
				&& (atom->section().type() != ld::Section::typeDtraceDOF)
				&& (atom->section().type() != ld::Section::typeUnwindInfo) 
				&& (minusTarget != target) ) {
			// ok for __eh_frame and __uwind_info to use pointer diffs to global weak symbols
			throwf("bad codegen, pointer diff in %s to global weak symbol %s", atom->name(), target->name());
		}
		return;
	}

	// cluster has no target, so needs no rebasing or binding	
	if ( target == NULL )
		return; 

	assert(_localRelocsAtom != NULL);
	uint64_t relocAddress =  atom->finalAddress() + fixupWithTarget->offsetInAtom - _localRelocsAtom->relocBaseAddress(state);

	bool inReadOnlySeg = ( strcmp(sect->segmentName(), "__TEXT") == 0 );
	bool needsLocalReloc = false;
	bool needsExternReloc = false;

	switch ( fixupWithStore->kind ) {
		case ld::Fixup::kindLazyTarget:
			// lazy pointers don't need relocs
			break;
		case ld::Fixup::kindStoreLittleEndian32:
		case ld::Fixup::kindStoreLittleEndian64:
		case ld::Fixup::kindStoreBigEndian32:
		case ld::Fixup::kindStoreBigEndian64:
		case ld::Fixup::kindStoreTargetAddressLittleEndian32:
		case ld::Fixup::kindStoreTargetAddressLittleEndian64:
		case ld::Fixup::kindStoreTargetAddressBigEndian32:
		case ld::Fixup::kindStoreTargetAddressBigEndian64:
			// is pointer 
			switch ( target->definition() ) {
				case ld::Atom::definitionProxy:
					needsExternReloc = true;
					break;
				case ld::Atom::definitionRegular:
				case ld::Atom::definitionTentative:
					// only slideable images need local relocs
					if ( _options.outputSlidable() ) 
						needsLocalReloc = true;
					// references to internal symbol never need binding
					if ( target->scope() != ld::Atom::scopeGlobal ) 
						break;
					// reference to global weak def needs weak binding in dynamic images
					if ( (target->combine() == ld::Atom::combineByName) 
						&& (target->definition() == ld::Atom::definitionRegular)
						&& (_options.outputKind() != Options::kStaticExecutable)
						&& (_options.outputKind() != Options::kPreload) ) {
						needsExternReloc = true;
					}
					else if ( _options.outputKind() == Options::kDynamicExecutable ) {
						// in main executables, the only way regular symbols are indirected is if -interposable is used
						if ( _options.interposable(target->name()) ) 
							needsExternReloc = true;
					}
					else {
						// for flat-namespace or interposable two-level-namespace
						// all references to exported symbols get indirected
						if ( (_options.nameSpace() != Options::kTwoLevelNameSpace) || _options.interposable(target->name()) ) {
							// <rdar://problem/5254468> no external relocs for flat objc classes
							if ( strncmp(target->name(), ".objc_class_", 12) == 0 )
								break;
							// no rebase info for references to global symbols that will have binding info
							needsExternReloc = true;
						}
					}
					if ( needsExternReloc )
						needsLocalReloc = false;
					break;
				case ld::Atom::definitionAbsolute:
					break;
			}
			if ( needsExternReloc ) {
				if ( inReadOnlySeg )
					noteTextReloc(atom, target);
				const ld::dylib::File* dylib = dynamic_cast<const ld::dylib::File*>(target->file());
				if ( (dylib != NULL) && dylib->willBeLazyLoadedDylib() )
					throwf("illegal data reference to %s in lazy loaded dylib %s", target->name(), dylib->path());
				_externalRelocsAtom->addExternalPointerReloc(relocAddress, target);
				sect->hasExternalRelocs = true;
				fixupWithTarget->contentAddendOnly = true;
			}
			else if ( needsLocalReloc ) {
				assert(target != NULL);
				if ( inReadOnlySeg )
					noteTextReloc(atom, target);
				_localRelocsAtom->addPointerReloc(relocAddress, target->machoSection());
				sect->hasLocalRelocs = true;
			}
			break;
		case ld::Fixup::kindStoreTargetAddressX86BranchPCRel32:
			if ( _options.outputKind() == Options::kKextBundle ) {
				assert(target != NULL);
				if ( target->definition() == ld::Atom::definitionProxy ) {
					_externalRelocsAtom->addExternalCallSiteReloc(relocAddress, target);
					fixupWithStore->contentAddendOnly = true;
				}
			}
			break;
		
		case ld::Fixup::kindStoreARMLow16:
		case ld::Fixup::kindStoreThumbLow16:
			// no way to encode rebasing of binding for these instructions
			if ( _options.outputSlidable() || (target->definition() == ld::Atom::definitionProxy) )
				throwf("no supported runtime lo16 relocation in %s from %s to %s", atom->name(), atom->file()->path(), target->name());
			break;
				
		case ld::Fixup::kindStoreARMHigh16:
		case ld::Fixup::kindStoreThumbHigh16:
			// no way to encode rebasing of binding for these instructions
			if ( _options.outputSlidable() || (target->definition() == ld::Atom::definitionProxy) )
				throwf("no supported runtime hi16 relocation in %s from %s to %s", atom->name(), atom->file()->path(), target->name());
			break;

		default:
			break;
	}
}


bool OutputFile::useExternalSectionReloc(const ld::Atom* atom, const ld::Atom* target, ld::Fixup* fixupWithTarget)
{
	if ( _options.architecture() == CPU_TYPE_X86_64 ) {
		// x86_64 uses external relocations for everthing that has a symbol
		return ( target->symbolTableInclusion() != ld::Atom::symbolTableNotIn );
	}
	
	// <rdar://problem/9513487> support arm branch interworking in -r mode 
	if ( (_options.architecture() == CPU_TYPE_ARM) && (_options.outputKind() == Options::kObjectFile) ) {
		if ( atom->isThumb() != target->isThumb() ) {
			switch ( fixupWithTarget->kind ) {
				// have branch that switches mode, then might be 'b' not 'bl'
				// Force external relocation, since no way to do local reloc for 'b'
				case ld::Fixup::kindStoreTargetAddressThumbBranch22 :
				case ld::Fixup::kindStoreTargetAddressARMBranch24:
					return true;
				default:
					break;
			}
		}
	}
	
	if ( (_options.architecture() == CPU_TYPE_I386) && (_options.outputKind() == Options::kObjectFile) ) {
		if ( target->contentType() == ld::Atom::typeTLV ) 
			return true;
	}

	// most architectures use external relocations only for references
	// to a symbol in another translation unit or for references to "weak symbols" or tentative definitions
	assert(target != NULL);
	if ( target->definition() == ld::Atom::definitionProxy )
		return true;
	if ( (target->definition() == ld::Atom::definitionTentative) && ! _options.makeTentativeDefinitionsReal() )
		return true;
	if ( target->scope() != ld::Atom::scopeGlobal )
		return false;
	if ( (target->combine() == ld::Atom::combineByName) && (target->definition() == ld::Atom::definitionRegular) )
		return true;
	return false;
}




void OutputFile::addSectionRelocs(ld::Internal& state, ld::Internal::FinalSection* sect, const ld::Atom* atom, 
								ld::Fixup* fixupWithTarget, ld::Fixup* fixupWithMinusTarget, ld::Fixup* fixupWithStore,
								const ld::Atom* target, const ld::Atom* minusTarget, 
								uint64_t targetAddend, uint64_t minusTargetAddend)
{
	if ( sect->isSectionHidden() )
		return;
	
	// in -r mode where there will be no labels on __eh_frame section, there is no need for relocations
	if ( (sect->type() == ld::Section::typeCFI) && _options.removeEHLabels() )
		return;
		
	// non-lazy-pointer section is encoded in indirect symbol table - not using relocations
	if ( sect->type() == ld::Section::typeNonLazyPointer ) 
		return;

	// tentative defs don't have any relocations
	if ( sect->type() == ld::Section::typeTentativeDefs ) 
		return;

	assert(target != NULL);
	assert(fixupWithTarget != NULL);
	bool targetUsesExternalReloc = this->useExternalSectionReloc(atom, target, fixupWithTarget);
	bool minusTargetUsesExternalReloc = (minusTarget != NULL) && this->useExternalSectionReloc(atom, minusTarget, fixupWithMinusTarget);
	
	// in x86_64 .o files an external reloc means the content contains just the addend
	if ( _options.architecture() == CPU_TYPE_X86_64 ) {
		if ( targetUsesExternalReloc ) {
			fixupWithTarget->contentAddendOnly = true;
			fixupWithStore->contentAddendOnly = true;
		}
		if ( minusTargetUsesExternalReloc )
			fixupWithMinusTarget->contentAddendOnly = true;
	}
	else {
		// for other archs, content is addend only with (non pc-rel) pointers
		// pc-rel instructions are funny. If the target is _foo+8 and _foo is 
		// external, then the pc-rel instruction *evalutates* to the address 8.
		if ( targetUsesExternalReloc ) {
			// TLV support for i386 acts like RIP relative addressing
			// The addend is the offset from the PICBase to the end of the instruction 
			if ( (_options.architecture() == CPU_TYPE_I386) 
				 && (_options.outputKind() == Options::kObjectFile)
			     && (fixupWithStore->kind == ld::Fixup::kindStoreX86PCRel32TLVLoad) ) {
				fixupWithTarget->contentAddendOnly = true;
				fixupWithStore->contentAddendOnly = true;
			}
			else if ( isPcRelStore(fixupWithStore->kind) ) {
				fixupWithTarget->contentDetlaToAddendOnly = true;
				fixupWithStore->contentDetlaToAddendOnly = true;
			}
			else if ( minusTarget == NULL ){
				fixupWithTarget->contentAddendOnly = true;
				fixupWithStore->contentAddendOnly = true;
			}
		}
	}
	
	if ( fixupWithStore != NULL ) {
		_sectionsRelocationsAtom->addSectionReloc(sect, fixupWithStore->kind, atom, fixupWithStore->offsetInAtom, 
													targetUsesExternalReloc, minusTargetUsesExternalReloc,
													target, targetAddend, minusTarget, minusTargetAddend);
	}

}


void OutputFile::makeSplitSegInfo(ld::Internal& state)
{
	if ( !_options.sharedRegionEligible() )
		return;
		
	for (std::vector<ld::Internal::FinalSection*>::iterator sit = state.sections.begin(); sit != state.sections.end(); ++sit) {
		ld::Internal::FinalSection* sect = *sit;
		if ( sect->isSectionHidden() )
			continue;
		if ( strcmp(sect->segmentName(), "__TEXT") != 0 )
			continue;
		for (std::vector<const ld::Atom*>::iterator ait = sect->atoms.begin(); ait != sect->atoms.end(); ++ait) {
			const ld::Atom* atom = *ait;
			const ld::Atom* target = NULL;
			const ld::Atom* fromTarget = NULL;
            uint64_t accumulator = 0;
            bool thumbTarget;
			bool hadSubtract = false;
			for (ld::Fixup::iterator fit = atom->fixupsBegin(), end=atom->fixupsEnd(); fit != end; ++fit) {
				if ( fit->firstInCluster() ) 
					target = NULL;
				if ( this->setsTarget(fit->kind) ) {
					accumulator = addressOf(state, fit, &target);			
					thumbTarget = targetIsThumb(state, fit);
					if ( thumbTarget ) 
						accumulator |= 1;
				}
				switch ( fit->kind ) {
					case ld::Fixup::kindSubtractTargetAddress:
                        accumulator -= addressOf(state, fit, &fromTarget);
						hadSubtract = true;
						break;
                    case ld::Fixup::kindAddAddend:
						accumulator += fit->u.addend;
						break;
                    case ld::Fixup::kindSubtractAddend:
						accumulator -= fit->u.addend;
						break;
					case ld::Fixup::kindStoreBigEndian32:
					case ld::Fixup::kindStoreLittleEndian32:
					case ld::Fixup::kindStoreLittleEndian64:
					case ld::Fixup::kindStoreTargetAddressLittleEndian32:
					case ld::Fixup::kindStoreTargetAddressLittleEndian64:
						// if no subtract, then this is an absolute pointer which means
						// there is also a text reloc which update_dyld_shared_cache will use.
						if ( ! hadSubtract )
							break;
						// fall through
					case ld::Fixup::kindStoreX86PCRel32:
					case ld::Fixup::kindStoreX86PCRel32_1:
					case ld::Fixup::kindStoreX86PCRel32_2:
					case ld::Fixup::kindStoreX86PCRel32_4:
					case ld::Fixup::kindStoreX86PCRel32GOTLoad:
					case ld::Fixup::kindStoreX86PCRel32GOTLoadNowLEA:
					case ld::Fixup::kindStoreX86PCRel32GOT:
					case ld::Fixup::kindStoreTargetAddressX86PCRel32:
					case ld::Fixup::kindStoreTargetAddressX86PCRel32GOTLoad:
					case ld::Fixup::kindStoreTargetAddressX86PCRel32GOTLoadNowLEA:
                    case ld::Fixup::kindStoreARMLow16:
                    case ld::Fixup::kindStoreThumbLow16: 
						assert(target != NULL);
						if ( strcmp(sect->segmentName(), target->section().segmentName()) != 0 ) {	
							_splitSegInfos.push_back(SplitSegInfoEntry(atom->finalAddress()+fit->offsetInAtom,fit->kind));
						}
						break;
                    case ld::Fixup::kindStoreARMHigh16: 
                    case ld::Fixup::kindStoreThumbHigh16: 
						assert(target != NULL);
						if ( strcmp(sect->segmentName(), target->section().segmentName()) != 0 ) {
                            // hi16 needs to know upper 4-bits of low16 to compute carry
                            uint32_t extra = (accumulator >> 12) & 0xF;
 							_splitSegInfos.push_back(SplitSegInfoEntry(atom->finalAddress()+fit->offsetInAtom,fit->kind, extra));
						}
						break;
					case ld::Fixup::kindSetTargetImageOffset:
						accumulator = addressOf(state, fit, &target);			
						assert(target != NULL);
						hadSubtract = true;
						break;
					default:
						break;
				}
			}
		}
	}
}


void OutputFile::writeMapFile(ld::Internal& state)
{
	if ( _options.generatedMapPath() != NULL ) {
		FILE* mapFile = fopen(_options.generatedMapPath(), "w"); 
		if ( mapFile != NULL ) {
			// write output path
			fprintf(mapFile, "# Path: %s\n", _options.outputFilePath());
			// write output architecure
			fprintf(mapFile, "# Arch: %s\n", _options.architectureName());
			// write UUID
			//if ( fUUIDAtom != NULL ) {
			//	const uint8_t* uuid = fUUIDAtom->getUUID();
			//	fprintf(mapFile, "# UUID: %2X %2X %2X %2X %2X %2X %2X %2X %2X %2X %2X %2X %2X %2X %2X %2X \n",
			//		uuid[0], uuid[1], uuid[2],  uuid[3],  uuid[4],  uuid[5],  uuid[6],  uuid[7],
			//		uuid[8], uuid[9], uuid[10], uuid[11], uuid[12], uuid[13], uuid[14], uuid[15]);
			//}
			// write table of object files
			std::map<const ld::File*, ld::File::Ordinal> readerToOrdinal;
			std::map<ld::File::Ordinal, const ld::File*> ordinalToReader;
			std::map<const ld::File*, uint32_t> readerToFileOrdinal;
			for (std::vector<ld::Internal::FinalSection*>::iterator sit = state.sections.begin(); sit != state.sections.end(); ++sit) {
				ld::Internal::FinalSection* sect = *sit;
				if ( sect->isSectionHidden() ) 
					continue;
				for (std::vector<const ld::Atom*>::iterator ait = sect->atoms.begin(); ait != sect->atoms.end(); ++ait) {
					const ld::Atom* atom = *ait;
					const ld::File* reader = atom->file();
					if ( reader == NULL )
						continue;
					ld::File::Ordinal readerOrdinal = reader->ordinal();
					std::map<const ld::File*, ld::File::Ordinal>::iterator pos = readerToOrdinal.find(reader);
					if ( pos == readerToOrdinal.end() ) {
						readerToOrdinal[reader] = readerOrdinal;
						ordinalToReader[readerOrdinal] = reader;
					}
				}
			}
			fprintf(mapFile, "# Object files:\n");
			fprintf(mapFile, "[%3u] %s\n", 0, "linker synthesized");
			uint32_t fileIndex = 1;
			for(std::map<ld::File::Ordinal, const ld::File*>::iterator it = ordinalToReader.begin(); it != ordinalToReader.end(); ++it) {
				fprintf(mapFile, "[%3u] %s\n", fileIndex, it->second->path());
				readerToFileOrdinal[it->second] = fileIndex++;
			}
			// write table of sections
			fprintf(mapFile, "# Sections:\n");
			fprintf(mapFile, "# Address\tSize    \tSegment\tSection\n"); 
			for (std::vector<ld::Internal::FinalSection*>::iterator sit = state.sections.begin(); sit != state.sections.end(); ++sit) {
				ld::Internal::FinalSection* sect = *sit;
				if ( sect->isSectionHidden() ) 
					continue;
				fprintf(mapFile, "0x%08llX\t0x%08llX\t%s\t%s\n", sect->address, sect->size, 
							sect->segmentName(), sect->sectionName());
			}
			// write table of symbols
			fprintf(mapFile, "# Symbols:\n");
			fprintf(mapFile, "# Address\tSize    \tFile  Name\n"); 
			for (std::vector<ld::Internal::FinalSection*>::iterator sit = state.sections.begin(); sit != state.sections.end(); ++sit) {
				ld::Internal::FinalSection* sect = *sit;
				if ( sect->isSectionHidden() ) 
					continue;
				//bool isCstring = (sect->type() == ld::Section::typeCString);
				for (std::vector<const ld::Atom*>::iterator ait = sect->atoms.begin(); ait != sect->atoms.end(); ++ait) {
					char buffer[4096];
					const ld::Atom* atom = *ait;
					const char* name = atom->name();
					// don't add auto-stripped aliases to .map file
					if ( (atom->size() == 0) && (atom->symbolTableInclusion() == ld::Atom::symbolTableNotInFinalLinkedImages) )
						continue;
					if ( atom->contentType() == ld::Atom::typeCString ) {
						strcpy(buffer, "literal string: ");
						strlcat(buffer, (char*)atom->rawContentPointer(), 4096);
						name = buffer;
					}
					else if ( (atom->contentType() == ld::Atom::typeCFI) && (strcmp(name, "FDE") == 0) ) {
						for (ld::Fixup::iterator fit = atom->fixupsBegin(); fit != atom->fixupsEnd(); ++fit) {
							if ( (fit->kind == ld::Fixup::kindSetTargetAddress) && (fit->clusterSize == ld::Fixup::k1of4) ) {
								assert(fit->binding == ld::Fixup::bindingDirectlyBound);
								if ( fit->u.target->section().type() == ld::Section::typeCode) {
									strcpy(buffer, "FDE for: ");
									strlcat(buffer, fit->u.target->name(), 4096);
									name = buffer;
								}
							}
						}
					}
					else if ( atom->contentType() == ld::Atom::typeNonLazyPointer ) {
						strcpy(buffer, "non-lazy-pointer");
						for (ld::Fixup::iterator fit = atom->fixupsBegin(); fit != atom->fixupsEnd(); ++fit) {
							if ( fit->binding == ld::Fixup::bindingsIndirectlyBound ) {
								strcpy(buffer, "non-lazy-pointer-to: ");
								strlcat(buffer, state.indirectBindingTable[fit->u.bindingIndex]->name(), 4096);
								break;
							}
							else if ( fit->binding == ld::Fixup::bindingDirectlyBound ) {
								strcpy(buffer, "non-lazy-pointer-to-local: ");
								strlcat(buffer, fit->u.target->name(), 4096);
								break;
							}
						}
						name = buffer;
					}
					fprintf(mapFile, "0x%08llX\t0x%08llX\t[%3u] %s\n", atom->finalAddress(), atom->size(), 
							readerToFileOrdinal[atom->file()], name);
				}
			}
			fclose(mapFile);
		}
		else {
			warning("could not write map file: %s\n", _options.generatedMapPath());
		}
	}
}


// used to sort atoms with debug notes
class DebugNoteSorter
{
public:
	bool operator()(const ld::Atom* left, const ld::Atom* right) const
	{
		// first sort by reader
		ld::File::Ordinal leftFileOrdinal  = left->file()->ordinal();
		ld::File::Ordinal rightFileOrdinal = right->file()->ordinal();
		if ( leftFileOrdinal!= rightFileOrdinal)
			return (leftFileOrdinal < rightFileOrdinal);

		// then sort by atom objectAddress
		uint64_t leftAddr  = left->finalAddress();
		uint64_t rightAddr = right->finalAddress();
		return leftAddr < rightAddr;
	}
};


const char* OutputFile::assureFullPath(const char* path)
{
	if ( path[0] == '/' )
		return path;
	char cwdbuff[MAXPATHLEN];
	if ( getcwd(cwdbuff, MAXPATHLEN) != NULL ) {
		char* result;
		asprintf(&result, "%s/%s", cwdbuff, path);
		if ( result != NULL )
			return result;
	}
	return path;
}

void OutputFile::synthesizeDebugNotes(ld::Internal& state)
{
	// -S means don't synthesize debug map
	if ( _options.debugInfoStripping() == Options::kDebugInfoNone )
		return;
	// make a vector of atoms that come from files compiled with dwarf debug info
	std::vector<const ld::Atom*> atomsNeedingDebugNotes;
	std::set<const ld::Atom*> atomsWithStabs;
	atomsNeedingDebugNotes.reserve(1024);
	const ld::relocatable::File* objFile = NULL;
	bool objFileHasDwarf = false;
	bool objFileHasStabs = false;
	for (std::vector<ld::Internal::FinalSection*>::iterator sit = state.sections.begin(); sit != state.sections.end(); ++sit) {
		ld::Internal::FinalSection* sect = *sit;
		for (std::vector<const ld::Atom*>::iterator ait = sect->atoms.begin(); ait != sect->atoms.end(); ++ait) {
			const ld::Atom* atom = *ait;
			// no stabs for atoms that would not be in the symbol table
			if ( atom->symbolTableInclusion() == ld::Atom::symbolTableNotIn )
				continue;
			if ( atom->symbolTableInclusion() == ld::Atom::symbolTableNotInFinalLinkedImages )
				continue;
			if ( atom->symbolTableInclusion() == ld::Atom::symbolTableInWithRandomAutoStripLabel )
				continue;
			// no stabs for absolute symbols
			if ( atom->definition() == ld::Atom::definitionAbsolute ) 
				continue;
			// no stabs for .eh atoms
			if ( atom->contentType() == ld::Atom::typeCFI )
				continue;
			// no stabs for string literal atoms
			if ( atom->contentType() == ld::Atom::typeCString )
				continue;
			// no stabs for kernel dtrace probes
			if ( (_options.outputKind() == Options::kStaticExecutable) && (strncmp(atom->name(), "__dtrace_probe$", 15) == 0) )
				continue;
			const ld::File* file = atom->file();
			if ( file != NULL ) {
				if ( file != objFile ) {
					objFileHasDwarf = false;
					objFileHasStabs = false;
					objFile = dynamic_cast<const ld::relocatable::File*>(file);
					if ( objFile != NULL ) {
						switch ( objFile->debugInfo() ) {
							case ld::relocatable::File::kDebugInfoNone:
								break;
							case ld::relocatable::File::kDebugInfoDwarf:
								objFileHasDwarf = true;
								break;
							case ld::relocatable::File::kDebugInfoStabs:
							case ld::relocatable::File::kDebugInfoStabsUUID:
								objFileHasStabs = true;
								break;
						}
					}
				}
				if ( objFileHasDwarf )
					atomsNeedingDebugNotes.push_back(atom);
				if ( objFileHasStabs )
					atomsWithStabs.insert(atom);
			}
		}
	}
	
	// sort by file ordinal then atom ordinal
	std::sort(atomsNeedingDebugNotes.begin(), atomsNeedingDebugNotes.end(), DebugNoteSorter());

	// synthesize "debug notes" and add them to master stabs vector
	const char* dirPath = NULL;
	const char* filename = NULL;
	bool wroteStartSO = false;
	state.stabs.reserve(atomsNeedingDebugNotes.size()*4);
	std::unordered_set<const char*, CStringHash, CStringEquals>  seenFiles;
	for (std::vector<const ld::Atom*>::iterator it=atomsNeedingDebugNotes.begin(); it != atomsNeedingDebugNotes.end(); it++) {
		const ld::Atom* atom = *it;
		const ld::File* atomFile = atom->file();
		const ld::relocatable::File* atomObjFile = dynamic_cast<const ld::relocatable::File*>(atomFile);
		//fprintf(stderr, "debug note for %s\n", atom->name());
    const char* newPath = atom->translationUnitSource();
    if ( newPath != NULL ) {
      const char* newDirPath;
      const char* newFilename;
      const char* lastSlash = strrchr(newPath, '/');
      if ( lastSlash == NULL ) 
        continue;
      newFilename = lastSlash+1;
      char* temp = strdup(newPath);
      newDirPath = temp;
      // gdb like directory SO's to end in '/', but dwarf DW_AT_comp_dir usually does not have trailing '/'
      temp[lastSlash-newPath+1] = '\0';
			// need SO's whenever the translation unit source file changes
			if ( (filename == NULL) || (strcmp(newFilename,filename) != 0) ) {
				if ( filename != NULL ) {
					// translation unit change, emit ending SO
					ld::relocatable::File::Stab endFileStab;
					endFileStab.atom		= NULL;
					endFileStab.type		= N_SO;
					endFileStab.other		= 1;
					endFileStab.desc		= 0;
					endFileStab.value		= 0;
					endFileStab.string		= "";
					state.stabs.push_back(endFileStab);
				}
				// new translation unit, emit start SO's
				ld::relocatable::File::Stab dirPathStab;
				dirPathStab.atom		= NULL;
				dirPathStab.type		= N_SO;
				dirPathStab.other		= 0;
				dirPathStab.desc		= 0;
				dirPathStab.value		= 0;
				dirPathStab.string		= newDirPath;
				state.stabs.push_back(dirPathStab);
				ld::relocatable::File::Stab fileStab;
				fileStab.atom		= NULL;
				fileStab.type		= N_SO;
				fileStab.other		= 0;
				fileStab.desc		= 0;
				fileStab.value		= 0;
				fileStab.string		= newFilename;
				state.stabs.push_back(fileStab);
				// Synthesize OSO for start of file
				ld::relocatable::File::Stab objStab;
				objStab.atom		= NULL;
				objStab.type		= N_OSO;
				// <rdar://problem/6337329> linker should put cpusubtype in n_sect field of nlist entry for N_OSO debug note entries
				objStab.other		= atomFile->cpuSubType(); 
				objStab.desc		= 1;
				if ( atomObjFile != NULL ) {
					objStab.string	= assureFullPath(atomObjFile->debugInfoPath());
					objStab.value	= atomObjFile->debugInfoModificationTime();
				}
				else {
					objStab.string	= assureFullPath(atomFile->path());
					objStab.value	= atomFile->modificationTime();
				}
				state.stabs.push_back(objStab);
				wroteStartSO = true;
				// add the source file path to seenFiles so it does not show up in SOLs
				seenFiles.insert(newFilename);
				char* fullFilePath;
				asprintf(&fullFilePath, "%s%s", newDirPath, newFilename);
				// add both leaf path and full path
				seenFiles.insert(fullFilePath);
			}
			filename = newFilename;
			dirPath = newDirPath;
			if ( atom->section().type() == ld::Section::typeCode ) {
				// Synthesize BNSYM and start FUN stabs
				ld::relocatable::File::Stab beginSym;
				beginSym.atom		= atom;
				beginSym.type		= N_BNSYM;
				beginSym.other		= 1;
				beginSym.desc		= 0;
				beginSym.value		= 0;
				beginSym.string		= "";
				state.stabs.push_back(beginSym);
				ld::relocatable::File::Stab startFun;
				startFun.atom		= atom;
				startFun.type		= N_FUN;
				startFun.other		= 1;
				startFun.desc		= 0;
				startFun.value		= 0;
				startFun.string		= atom->name();
				state.stabs.push_back(startFun);
				// Synthesize any SOL stabs needed
				const char* curFile = NULL;
				for (ld::Atom::LineInfo::iterator lit = atom->beginLineInfo(); lit != atom->endLineInfo(); ++lit) {
					if ( lit->fileName != curFile ) {
						if ( seenFiles.count(lit->fileName) == 0 ) {
							seenFiles.insert(lit->fileName);
							ld::relocatable::File::Stab sol;
							sol.atom		= 0;
							sol.type		= N_SOL;
							sol.other		= 0;
							sol.desc		= 0;
							sol.value		= 0;
							sol.string		= lit->fileName;
							state.stabs.push_back(sol);
						}
						curFile = lit->fileName;
					}
				}
				// Synthesize end FUN and ENSYM stabs
				ld::relocatable::File::Stab endFun;
				endFun.atom			= atom;
				endFun.type			= N_FUN;
				endFun.other		= 0;
				endFun.desc			= 0;
				endFun.value		= 0;
				endFun.string		= "";
				state.stabs.push_back(endFun);
				ld::relocatable::File::Stab endSym;
				endSym.atom			= atom;
				endSym.type			= N_ENSYM;
				endSym.other		= 1;
				endSym.desc			= 0;
				endSym.value		= 0;
				endSym.string		= "";
				state.stabs.push_back(endSym);
			}
			else {
				ld::relocatable::File::Stab globalsStab;
				const char* name = atom->name();
				if ( atom->scope() == ld::Atom::scopeTranslationUnit ) {
					// Synthesize STSYM stab for statics
					globalsStab.atom		= atom;
					globalsStab.type		= N_STSYM;
					globalsStab.other		= 1;
					globalsStab.desc		= 0;
					globalsStab.value		= 0;
					globalsStab.string		= name;
					state.stabs.push_back(globalsStab);
				}
				else {
					// Synthesize GSYM stab for other globals
					globalsStab.atom		= atom;
					globalsStab.type		= N_GSYM;
					globalsStab.other		= 1;
					globalsStab.desc		= 0;
					globalsStab.value		= 0;
					globalsStab.string		= name;
					state.stabs.push_back(globalsStab);
				}
			}
		}
	}

	if ( wroteStartSO ) {
		//  emit ending SO
		ld::relocatable::File::Stab endFileStab;
		endFileStab.atom		= NULL;
		endFileStab.type		= N_SO;
		endFileStab.other		= 1;
		endFileStab.desc		= 0;
		endFileStab.value		= 0;
		endFileStab.string		= "";
		state.stabs.push_back(endFileStab);
	}
	
	// copy any stabs from .o file 
	std::set<const ld::File*> filesSeenWithStabs;
	for (std::set<const ld::Atom*>::iterator it=atomsWithStabs.begin(); it != atomsWithStabs.end(); it++) {
		const ld::Atom* atom = *it;
		objFile = dynamic_cast<const ld::relocatable::File*>(atom->file());
		if ( objFile != NULL ) {
			if ( filesSeenWithStabs.count(objFile) == 0 ) {
				filesSeenWithStabs.insert(objFile);
				const std::vector<ld::relocatable::File::Stab>* stabs = objFile->stabs();
				if ( stabs != NULL ) {
					for(std::vector<ld::relocatable::File::Stab>::const_iterator sit = stabs->begin(); sit != stabs->end(); ++sit) {
						ld::relocatable::File::Stab stab = *sit;
						// ignore stabs associated with atoms that were dead stripped or coalesced away
						if ( (sit->atom != NULL) && (atomsWithStabs.count(sit->atom) == 0) )
							continue;
						// <rdar://problem/8284718> Value of N_SO stabs should be address of first atom from translation unit
						if ( (stab.type == N_SO) && (stab.string != NULL) && (stab.string[0] != '\0') ) {
							stab.atom = atom;
						}
						state.stabs.push_back(stab);
					}
				}
			}
		}
	}
	
}


} // namespace tool 
} // namespace ld 

