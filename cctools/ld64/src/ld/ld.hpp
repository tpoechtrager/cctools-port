/* -*- mode: C++; c-basic-offset: 4; tab-width: 4 -*-
 *
 * Copyright (c) 2005-2010 Apple Inc. All rights reserved.
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


#ifndef __LD_HPP__
#define __LD_HPP__

#include <stdint.h>
#include <math.h>
#include <unistd.h>
#include <assert.h>
#include "configure.h"

#include <vector>
#include <unordered_set>

#include "configure.h"

namespace ld {

//
// ld::File 
//
// Abstract base class for all object or library files the linker processes.
// 
// forEachAtom() iterates over the Atoms in the order they occur in the file.
//
// justInTimeforEachAtom(name) iterates over lazily created Atoms.  For instance if
// File is a static library, justInTimeforEachAtom() will iterate over the base set
// of Atoms from the archive member implementing 'name'.
//
class File
{
public:
	enum ObjcConstraint { objcConstraintNone, objcConstraintRetainRelease, 
							objcConstraintRetainReleaseOrGC, objcConstraintGC,
							objcConstraintRetainReleaseForSimulator };
	
	class AtomHandler {
	public:
		virtual				~AtomHandler() {}
		virtual void		doAtom(const class Atom&) = 0;
		virtual void		doFile(const class File&) = 0;
	};

	//
	// ld::File::Ordinal 
	//
	// Codifies the rules of ordering input files for symbol precedence. These are:
	// - Input files listed on the command line are ordered according to their index in the argument list.
	// - Input files listed in a file list are ordered first at the index of the file list argument, then
	//   by index in the file list
	// - Input files extracted from archives are ordered using the ordinal of the archive itself plus the
	//   index of the object file within the archive
	// - Indirect dylibs are ordered after all input files derived from the command line, in the order that
	//   they are discovered.
	// - The LTO object file is last.
	//
	class Ordinal
	{
	private:
		// The actual numeric ordinal. Lower values have higher precedence and a zero value is invalid.
		// The 64 bit ordinal is broken into 4 16 bit chunks. The high 16 bits are a "partition" that
		// is used to distinguish major ordinal groups: command line, indirect dylib, LTO.
		// The remaining chunks are used according to the partition (see below).
		uint64_t	_ordinal;
		
		Ordinal (uint64_t ordinal) : _ordinal(ordinal) {}
		
		enum { kArgListPartition=0, kIndirectDylibPartition=1, kLTOPartition = 2, kLinkerOptionPartition = 2, InvalidParition=0xffff };
		Ordinal(uint16_t partition, uint16_t majorIndex, uint16_t minorIndex, uint16_t counter) {
			_ordinal = ((uint64_t)partition<<48) | ((uint64_t)majorIndex<<32) | ((uint64_t)minorIndex<<16) | ((uint64_t)counter<<0);
		}
		
		const uint16_t	partition() const		{ return (_ordinal>>48)&0xffff; }
		const uint16_t	majorIndex() const		{ return (_ordinal>>32)&0xffff; }
		const uint16_t	minorIndex() const		{ return (_ordinal>>16)&0xffff; }
		const uint16_t	counter() const			{ return (_ordinal>>00)&0xffff; }
		
		const Ordinal nextMajorIndex()		const { assert(majorIndex() < 0xffff); return Ordinal(_ordinal+((uint64_t)1<<32)); }
		const Ordinal nextMinorIndex()		const { assert(minorIndex() < 0xffff); return Ordinal(_ordinal+((uint64_t)1<<16)); }
		const Ordinal nextCounter()		const { assert(counter() < 0xffff); return Ordinal(_ordinal+((uint64_t)1<<0)); }
		
	public:
		Ordinal() : _ordinal(0) {};
		
		static const Ordinal NullOrdinal()		{ return Ordinal((uint64_t)0); }
		
		const bool validOrdinal() const { return _ordinal != 0; }
		
		bool operator ==(const Ordinal& rhs) const { return _ordinal == rhs._ordinal; }
		bool operator !=(const Ordinal& rhs) const {	return _ordinal != rhs._ordinal; }
		bool operator < (const Ordinal& rhs) const { return _ordinal < rhs._ordinal; }
		bool operator > (const Ordinal& rhs) const { return _ordinal > rhs._ordinal; }
		
		// For ordinals derived from the command line args the partition is ArgListPartition
		// The majorIndex is the arg index that pulls in the file, file list, or archive.
		// The minorIndex is used for files pulled in by a file list and the value is the index of the file in the file list.
		// The counter is used for .a files and the value is the index of the object in the archive.
		// Thus, an object pulled in from a .a that was listed in a file list could use all three fields.
		static const Ordinal makeArgOrdinal(uint16_t argIndex) { return Ordinal(kArgListPartition, argIndex, 0, 0); };
		const Ordinal nextFileListOrdinal() const { return nextMinorIndex(); }
		const Ordinal archiveOrdinalWithMemberIndex(uint16_t index) const { return Ordinal(kArgListPartition, majorIndex(), minorIndex(), index); }
		
		// For indirect libraries the partition is IndirectDylibPartition and the counter is used or order the libraries.
		static const ld::File::Ordinal indirectDylibBase() { return Ordinal(kIndirectDylibPartition, 0, 0, 0); }
		const Ordinal nextIndirectDylibOrdinal() const { return nextCounter(); }
		
		// For the LTO mach-o the partition is LTOPartition. As there is only one LTO file no other fields are needed.
		static const ld::File::Ordinal LTOOrdinal()			{ return Ordinal(kLTOPartition, 0, 0, 0); }

		// For linker options embedded in object files
		static const ld::File::Ordinal linkeOptionBase() { return Ordinal(kIndirectDylibPartition, 1, 0, 0); }
		const Ordinal nextLinkerOptionOrdinal() { nextCounter(); return *this; };
		
	};
	
	typedef enum { Reloc, Dylib, Archive, Other } Type;
	
										File(const char* pth, time_t modTime, Ordinal ord, Type type)
											: _path(pth), _modTime(modTime), _ordinal(ord), _type(type) { }
	virtual								~File() {}
			const char*					path() const			{ return _path; }
			time_t						modificationTime() const{ return _modTime; }
	Ordinal								ordinal() const			{ return _ordinal; }
	virtual bool						forEachAtom(AtomHandler&) const = 0;
	virtual bool						justInTimeforEachAtom(const char* name, AtomHandler&) const = 0;
	virtual ObjcConstraint				objCConstraint() const			{ return objcConstraintNone; }
	virtual uint8_t						swiftVersion() const			{ return 0; }
	virtual uint32_t					cpuSubType() const		{ return 0; }
	virtual uint32_t					subFileCount() const	{ return 1; }
    bool								fileExists() const     { return _modTime != 0; }
	Type								type() const { return _type; }
private:
	const char*							_path;
	time_t								_modTime;
	const Ordinal						_ordinal;
	const Type							_type;
};


//
// minumum OS versions
//
enum MacVersionMin { macVersionUnset=0, mac10_4=0x000A0400, mac10_5=0x000A0500, 
						mac10_6=0x000A0600, mac10_7=0x000A0700, mac10_8=0x000A0800,
						mac10_9=0x000A0900, mac10_Future=0x10000000 };
enum IOSVersionMin { iOSVersionUnset=0, iOS_2_0=0x00020000, iOS_3_1=0x00030100, 
						iOS_4_2=0x00040200, iOS_4_3=0x00040300, iOS_5_0=0x00050000,
						iOS_6_0=0x00060000, iOS_7_0=0x00070000, iOS_8_0=0x00080000,
						iOS_9_0=0x00090000, iOS_Future=0x10000000};
 
namespace relocatable {
	//
	// ld::relocatable::File 
	//
	// Abstract base class for object files the linker processes.
	// 
	// debugInfo() returns if the object file contains debugger information (stabs or dwarf).
	//
	// stabs() lazily creates a vector of Stab objects for each atom
	//
	// canScatterAtoms() true for all compiler generated code.  Hand written assembly can opt-in
	// via .subsections_via_symbols directive.  When true it means the linker can break up section
	// content at symbol boundaries and do optimizations like coalescing, dead code stripping, or
	// apply order files.
	//
	// optimize() used by libLTO to lazily generate code from llvm bit-code files
	// 
	class File : public ld::File
	{
	public:
		enum DebugInfoKind { kDebugInfoNone=0, kDebugInfoStabs=1, kDebugInfoDwarf=2, kDebugInfoStabsUUID=3 };
		struct Stab {
			const class Atom*	atom;
			uint8_t				type;
			uint8_t				other;
			uint16_t			desc;
			uint32_t			value;
			const char*			string;
		};
		typedef const std::vector< std::vector<const char*> > LinkerOptionsList;

											File(const char* pth, time_t modTime, Ordinal ord)
												: ld::File(pth, modTime, ord, Reloc) { }
		virtual								~File() {}
		virtual DebugInfoKind				debugInfo() const = 0;
		virtual const char*					debugInfoPath() const { return path(); }
		virtual time_t						debugInfoModificationTime() const { return modificationTime(); }
		virtual const std::vector<Stab>*	stabs() const = 0;
		virtual bool						canScatterAtoms() const = 0;
		virtual bool						hasLongBranchStubs()		{ return false; }
		virtual LinkerOptionsList*			linkerOptions() const = 0;
	};
} // namespace relocatable


namespace dylib {

	//
	// ld::dylib::File 
	//
	// Abstract base class for dynamic shared libraries read by the linker processes.
	//
	class File : public ld::File
	{
	public:
		class DylibHandler
		{
		public:
			virtual				~DylibHandler()	{}
			virtual File*		findDylib(const char* installPath, const char* fromPath) = 0;
		};
			
											File(const char* pth, time_t modTime, Ordinal ord)
												: ld::File(pth, modTime, ord, Dylib), _dylibInstallPath(NULL),
												_dylibTimeStamp(0), _dylibCurrentVersion(0), _dylibCompatibilityVersion(0),
												_explicitlyLinked(false), _implicitlyLinked(false),
												_lazyLoadedDylib(false), _forcedWeakLinked(false), _reExported(false),
												_upward(false), _dead(false) { }
				const char*					installPath() const			{ return _dylibInstallPath; }
				uint32_t					timestamp() const			{ return _dylibTimeStamp; }
				uint32_t					currentVersion() const		{ return _dylibCurrentVersion; }
				uint32_t					compatibilityVersion() const{ return _dylibCompatibilityVersion; }
				void						setExplicitlyLinked()		{ _explicitlyLinked = true; }
				bool						explicitlyLinked() const	{ return _explicitlyLinked; }
				void						setImplicitlyLinked()		{ _implicitlyLinked = true; }
				bool						implicitlyLinked() const	{ return _implicitlyLinked; }
				
				// attributes of how dylib will be used when linked
				void						setWillBeLazyLoadedDylb()		{ _lazyLoadedDylib = true; }
				bool						willBeLazyLoadedDylib() const	{ return _lazyLoadedDylib; }
				void						setForcedWeakLinked()			{ _forcedWeakLinked = true; }
				bool						forcedWeakLinked() const		{ return _forcedWeakLinked; }
															
				void						setWillBeReExported()			{ _reExported = true; }
				bool						willBeReExported() const		{ return _reExported; }
				void						setWillBeUpwardDylib()			{ _upward = true; }
				bool						willBeUpwardDylib() const		{ return _upward; }
				void						setWillBeRemoved(bool value)	{ _dead = value; }
				bool						willRemoved() const				{ return _dead; }
				
		virtual void						processIndirectLibraries(DylibHandler* handler, bool addImplicitDylibs) = 0;
		virtual bool						providedExportAtom() const = 0;
		virtual const char*					parentUmbrella() const = 0;
		virtual const std::vector<const char*>*	allowableClients() const = 0;
		virtual bool						hasWeakExternals() const = 0;
		virtual bool						deadStrippable() const = 0;
		virtual bool						hasWeakDefinition(const char* name) const = 0;
		virtual bool						hasPublicInstallName() const = 0;
		virtual bool						allSymbolsAreWeakImported() const = 0;
		virtual const void*					codeSignatureDR() const = 0;
		virtual bool						installPathVersionSpecific() const { return false; }
		virtual bool						appExtensionSafe() const = 0;
		virtual MacVersionMin				macMinVersion() const { return macVersionUnset; }
		virtual IOSVersionMin				iOSMinVersion() const { return iOSVersionUnset; }

	protected:
		const char*							_dylibInstallPath;
		uint32_t							_dylibTimeStamp;
		uint32_t							_dylibCurrentVersion;
		uint32_t							_dylibCompatibilityVersion;
		bool								_explicitlyLinked;
		bool								_implicitlyLinked;
		bool								_lazyLoadedDylib;
		bool								_forcedWeakLinked;
		bool								_reExported;
		bool								_upward;
		bool								_dead;
	};
} // namespace dylib


namespace archive {
	//
	// ld::archive::File 
	//
	// Abstract base class for static libraries read by the linker processes.
	//
	class File : public ld::File
	{
	public:
											File(const char* pth, time_t modTime, Ordinal ord)
												: ld::File(pth, modTime, ord, Archive) { }
		virtual								~File() {}
		virtual bool						justInTimeDataOnlyforEachAtom(const char* name, AtomHandler&) const = 0;
	};
} // namespace archive 


//
// ld::Section
//
class Section
{
public:
	enum Type { typeUnclassified, typeCode, typePageZero, typeImportProxies, typeLinkEdit, typeMachHeader, typeStack,
				typeLiteral4, typeLiteral8, typeLiteral16, typeConstants, typeTempLTO, typeTempAlias,
				typeCString, typeNonStdCString, typeCStringPointer, typeUTF16Strings, typeCFString, typeObjC1Classes,
				typeCFI, typeLSDA, typeDtraceDOF, typeUnwindInfo, typeObjCClassRefs, typeObjC2CategoryList,
				typeZeroFill, typeTentativeDefs, typeLazyPointer, typeStub, typeNonLazyPointer, typeDyldInfo, 
				typeLazyDylibPointer, typeStubHelper, typeInitializerPointers, typeTerminatorPointers,
				typeStubClose, typeLazyPointerClose, typeAbsoluteSymbols, 
				typeTLVDefs, typeTLVZeroFill, typeTLVInitialValues, typeTLVInitializerPointers, typeTLVPointers,
				typeFirstSection, typeLastSection, typeDebug };


					Section(const char* sgName, const char* sctName,
								Type t, bool hidden=false)
								: _segmentName(sgName), _sectionName(sctName),
								_type(t), _hidden(hidden)  {}
					Section(const Section& sect)
								: _segmentName(sect.segmentName()), _sectionName(sect.sectionName()),
								_type(sect.type()), _hidden(sect.isSectionHidden())  {}
								
	bool			operator==(const Section& rhs) const { return ( (_hidden==rhs._hidden) &&
														(strcmp(_segmentName, rhs._segmentName)==0) &&
														(strcmp(_sectionName, rhs._sectionName)==0) ); }
	bool			operator!=(const Section& rhs) const { return ! (*this == rhs); }
	const char*			segmentName() const			{ return _segmentName; }
	const char*			sectionName() const			{ return _sectionName; }
	Type				type() const				{ return _type; }
	bool				isSectionHidden() const		{ return _hidden; }
	
private:
	const char*			_segmentName;
	const char*			_sectionName;
	Type				_type;
	bool				_hidden;
};



//
// ld::Fixup
//
// A Fixup describes how part of an Atom's content must be fixed up.  For instance,
// an instruction may contain a displacement to another Atom that must be 
// fixed up by the linker.  
//
// A Fixup my reference another Atom. There are two kinds of references: direct and by-name.  
// With a direct reference, the target is bound by the File that created it. 
// For instance a reference to a static would produce a direct reference.  
// A by-name reference requires the linker to find the target Atom with the 
// required name in order to be bound.
//
// For a link to succeed all Fixup must be bound.
//
// A Reference also has a fix-up-offset.  This is the offset into the content of the
// Atom holding the reference where the fix-up (relocation) will be applied.
//
//
struct Fixup 
{
	enum TargetBinding { bindingNone, bindingByNameUnbound, bindingDirectlyBound, bindingByContentBound, bindingsIndirectlyBound };
	enum Cluster { k1of1, k1of2, k2of2, k1of3, k2of3, k3of3, k1of4, k2of4, k3of4, k4of4, k1of5, k2of5, k3of5, k4of5, k5of5 };
	enum Kind	{	kindNone, kindNoneFollowOn, 
					// grouping
					kindNoneGroupSubordinate, 
					kindNoneGroupSubordinateFDE, kindNoneGroupSubordinateLSDA, kindNoneGroupSubordinatePersonality,
					// value calculations
					kindSetTargetAddress,
					kindSubtractTargetAddress,
					kindAddAddend,
					kindSubtractAddend,
					kindSetTargetImageOffset,
					kindSetTargetSectionOffset,
					kindSetTargetTLVTemplateOffset,
					// pointer store kinds (of current calculated value)
					kindStore8,
					kindStoreLittleEndian16,
					kindStoreLittleEndianLow24of32,
					kindStoreLittleEndian32,
					kindStoreLittleEndian64,
					kindStoreBigEndian16,
					kindStoreBigEndianLow24of32,
					kindStoreBigEndian32,
					kindStoreBigEndian64,
					// Intel specific store kinds
					kindStoreX86BranchPCRel8, kindStoreX86BranchPCRel32, 
					kindStoreX86PCRel8, kindStoreX86PCRel16,  
					kindStoreX86PCRel32, kindStoreX86PCRel32_1, kindStoreX86PCRel32_2, kindStoreX86PCRel32_4, 
					kindStoreX86PCRel32GOTLoad, kindStoreX86PCRel32GOTLoadNowLEA, kindStoreX86PCRel32GOT, 
					kindStoreX86PCRel32TLVLoad, kindStoreX86PCRel32TLVLoadNowLEA,
					kindStoreX86Abs32TLVLoad, kindStoreX86Abs32TLVLoadNowLEA,
					// ARM specific store kinds
					kindStoreARMBranch24, kindStoreThumbBranch22, 
					kindStoreARMLoad12,
					kindStoreARMLow16, kindStoreARMHigh16, 
					kindStoreThumbLow16, kindStoreThumbHigh16, 
#if SUPPORT_ARCH_arm64
					// ARM64 specific store kinds
					kindStoreARM64Branch26,  
					kindStoreARM64Page21, kindStoreARM64PageOff12,
					kindStoreARM64GOTLoadPage21, kindStoreARM64GOTLoadPageOff12,
					kindStoreARM64GOTLeaPage21, kindStoreARM64GOTLeaPageOff12,
					kindStoreARM64TLVPLoadPage21, kindStoreARM64TLVPLoadPageOff12,
					kindStoreARM64TLVPLoadNowLeaPage21, kindStoreARM64TLVPLoadNowLeaPageOff12,
					kindStoreARM64PointerToGOT, kindStoreARM64PCRelToGOT,
#endif
					// dtrace probes
					kindDtraceExtra,
					kindStoreX86DtraceCallSiteNop, kindStoreX86DtraceIsEnableSiteClear,
					kindStoreARMDtraceCallSiteNop, kindStoreARMDtraceIsEnableSiteClear,
					kindStoreARM64DtraceCallSiteNop, kindStoreARM64DtraceIsEnableSiteClear,
					kindStoreThumbDtraceCallSiteNop, kindStoreThumbDtraceIsEnableSiteClear,
					// lazy binding
					kindLazyTarget, kindSetLazyOffset,
					// islands
					kindIslandTarget,
					// data-in-code markers
					kindDataInCodeStartData, kindDataInCodeStartJT8, kindDataInCodeStartJT16, 
					kindDataInCodeStartJT32, kindDataInCodeStartJTA32, kindDataInCodeEnd,
					// linker optimzation hints
					kindLinkerOptimizationHint,
					// pointer store combinations
					kindStoreTargetAddressLittleEndian32,	// kindSetTargetAddress + kindStoreLittleEndian32
					kindStoreTargetAddressLittleEndian64,	// kindSetTargetAddress + kindStoreLittleEndian64
					kindStoreTargetAddressBigEndian32,		// kindSetTargetAddress + kindStoreBigEndian32
					kindStoreTargetAddressBigEndian64,		// kindSetTargetAddress + kindStoreBigEndian364
					kindSetTargetTLVTemplateOffsetLittleEndian32,  // kindSetTargetTLVTemplateOffset + kindStoreLittleEndian32
					kindSetTargetTLVTemplateOffsetLittleEndian64,  // kindSetTargetTLVTemplateOffset + kindStoreLittleEndian64
					// Intel value calculation and store combinations
					kindStoreTargetAddressX86PCRel32,		// kindSetTargetAddress + kindStoreX86PCRel32
					kindStoreTargetAddressX86BranchPCRel32, // kindSetTargetAddress + kindStoreX86BranchPCRel32
					kindStoreTargetAddressX86PCRel32GOTLoad,// kindSetTargetAddress + kindStoreX86PCRel32GOTLoad
					kindStoreTargetAddressX86PCRel32GOTLoadNowLEA,// kindSetTargetAddress + kindStoreX86PCRel32GOTLoadNowLEA
					kindStoreTargetAddressX86PCRel32TLVLoad, // kindSetTargetAddress + kindStoreX86PCRel32TLVLoad
					kindStoreTargetAddressX86PCRel32TLVLoadNowLEA, // kindSetTargetAddress + kindStoreX86PCRel32TLVLoadNowLEA
					kindStoreTargetAddressX86Abs32TLVLoad,		// kindSetTargetAddress + kindStoreX86Abs32TLVLoad
					kindStoreTargetAddressX86Abs32TLVLoadNowLEA,	// kindSetTargetAddress + kindStoreX86Abs32TLVLoadNowLEA
					// ARM value calculation and store combinations
					kindStoreTargetAddressARMBranch24,		// kindSetTargetAddress + kindStoreARMBranch24
					kindStoreTargetAddressThumbBranch22,	// kindSetTargetAddress + kindStoreThumbBranch22
					kindStoreTargetAddressARMLoad12,		// kindSetTargetAddress + kindStoreARMLoad12
#if SUPPORT_ARCH_arm64
					// ARM64 value calculation and store combinations
					kindStoreTargetAddressARM64Branch26,		// kindSetTargetAddress + kindStoreARM64Branch26
					kindStoreTargetAddressARM64Page21,			// kindSetTargetAddress + kindStoreARM64Page21
					kindStoreTargetAddressARM64PageOff12,		// kindSetTargetAddress + kindStoreARM64PageOff12
					kindStoreTargetAddressARM64GOTLoadPage21,	// kindSetTargetAddress + kindStoreARM64GOTLoadPage21
					kindStoreTargetAddressARM64GOTLoadPageOff12,// kindSetTargetAddress + kindStoreARM64GOTLoadPageOff12
					kindStoreTargetAddressARM64GOTLeaPage21,	// kindSetTargetAddress + kindStoreARM64GOTLeaPage21
					kindStoreTargetAddressARM64GOTLeaPageOff12,	// kindSetTargetAddress + kindStoreARM64GOTLeaPageOff12
					kindStoreTargetAddressARM64TLVPLoadPage21,	// kindSetTargetAddress + kindStoreARM64TLVPLoadPage21
					kindStoreTargetAddressARM64TLVPLoadPageOff12,// kindSetTargetAddress + kindStoreARM64TLVPLoadPageOff12
					kindStoreTargetAddressARM64TLVPLoadNowLeaPage21,	// kindSetTargetAddress + kindStoreARM64TLVPLoadNowLeaPage21
					kindStoreTargetAddressARM64TLVPLoadNowLeaPageOff12,	// kindSetTargetAddress + kindStoreARM64TLVPLoadNowLeaPageOff12
#endif
			};

	union {
		const Atom*	target;
		const char*	name;
		uint64_t	addend;
		uint32_t	bindingIndex;
	} u;
	uint32_t		offsetInAtom;
	Kind			kind : 8;
	Cluster			clusterSize : 4;
	bool			weakImport : 1;
	TargetBinding	binding : 3;
	bool			contentAddendOnly : 1;
	bool			contentDetlaToAddendOnly : 1;
	bool			contentIgnoresAddend : 1;
	
	typedef Fixup*		iterator;

	Fixup() :
		offsetInAtom(0), kind(kindNone), clusterSize(k1of1), weakImport(false), 
		binding(bindingNone),  
		contentAddendOnly(false), contentDetlaToAddendOnly(false), contentIgnoresAddend(false) { u.target = NULL; }

	Fixup(Kind k, Atom* targetAtom) :
		offsetInAtom(0), kind(k), clusterSize(k1of1), weakImport(false), 
		binding(Fixup::bindingDirectlyBound),  
		contentAddendOnly(false), contentDetlaToAddendOnly(false), contentIgnoresAddend(false)  
			{ assert(targetAtom != NULL); u.target = targetAtom; }

	Fixup(uint32_t off, Cluster c, Kind k) :
		offsetInAtom(off), kind(k), clusterSize(c), weakImport(false), 
		binding(Fixup::bindingNone),  
		contentAddendOnly(false), contentDetlaToAddendOnly(false), contentIgnoresAddend(false)  
			{ u.addend = 0; }

	Fixup(uint32_t off, Cluster c, Kind k, bool weakIm, const char* name) :
		offsetInAtom(off), kind(k), clusterSize(c), weakImport(weakIm), 
		binding(Fixup::bindingByNameUnbound),  
		contentAddendOnly(false), contentDetlaToAddendOnly(false), contentIgnoresAddend(false) 
			{ assert(name != NULL); u.name = name; }
		
	Fixup(uint32_t off, Cluster c, Kind k, TargetBinding b, const char* name) :
		offsetInAtom(off), kind(k), clusterSize(c), weakImport(false), binding(b),  
		contentAddendOnly(false), contentDetlaToAddendOnly(false), contentIgnoresAddend(false) 
			{ assert(name != NULL); u.name = name; }
		
	Fixup(uint32_t off, Cluster c, Kind k, const Atom* targetAtom) :
		offsetInAtom(off), kind(k), clusterSize(c), weakImport(false), 
		binding(Fixup::bindingDirectlyBound),  
		contentAddendOnly(false), contentDetlaToAddendOnly(false), contentIgnoresAddend(false) 
			{ assert(targetAtom != NULL); u.target = targetAtom; }
		
	Fixup(uint32_t off, Cluster c, Kind k, TargetBinding b, const Atom* targetAtom) :
		offsetInAtom(off), kind(k), clusterSize(c), weakImport(false), binding(b),  
		contentAddendOnly(false), contentDetlaToAddendOnly(false), contentIgnoresAddend(false) 
			{ assert(targetAtom != NULL); u.target = targetAtom; }
		
	Fixup(uint32_t off, Cluster c, Kind k, uint64_t addend) :
		offsetInAtom(off), kind(k), clusterSize(c), weakImport(false), 
		binding(Fixup::bindingNone),  
		contentAddendOnly(false), contentDetlaToAddendOnly(false), contentIgnoresAddend(false) 
			{ u.addend = addend; }
			
	Fixup(Kind k, uint32_t lohKind, uint32_t off1, uint32_t off2) :
		offsetInAtom(off1), kind(k), clusterSize(k1of1),  
		weakImport(false), binding(Fixup::bindingNone), contentAddendOnly(false), 
		contentDetlaToAddendOnly(false), contentIgnoresAddend(false) {
			assert(k == kindLinkerOptimizationHint);
			LOH_arm64 extra;
			extra.addend = 0;
			extra.info.kind = lohKind;
			extra.info.count = 1;
			extra.info.delta1 = 0;
			extra.info.delta2 = (off2 - off1) >> 2;
			u.addend = extra.addend; 
		}
			

	bool firstInCluster() const { 
		switch (clusterSize) {
			case k1of1:
			case k1of2:
			case k1of3:
			case k1of4:
			case k1of5:
				return true;
			default:
				break;
		}
		return false;
	}
	
	bool lastInCluster() const { 
		switch (clusterSize) {
			case k1of1:
			case k2of2:
			case k3of3:
			case k4of4:
			case k5of5:
				return true;
			default:
				break;
		}
		return false;
	}
	
	union LOH_arm64 {
		uint64_t	addend;
		struct {
			unsigned	kind	:  6,
						count	:  2,	// 00 => 1 addr, 11 => 4 addrs
						delta1 : 14,	// 16-bit delta, low 2 bits assumed zero
						delta2 : 14,
						delta3 : 14,
						delta4 : 14;	
		} info;
	};
	
};

//
// ld::Atom
//
// An atom is the fundamental unit of linking.  A C function or global variable is an atom.
// An atom has content and attributes. The content of a function atom is the instructions
// that implement the function.  The content of a global variable atom is its initial bits.
//
// Name:
// The name of an atom is the label name generated by the compiler.  A C compiler names foo()
// as _foo.  A C++ compiler names foo() as __Z3foov.
// The name refers to the first byte of the content.  An atom cannot have multiple entry points.
// Such code is modeled as multiple atoms, each having a "follow on" reference to the next.
// A "follow on" reference is a contraint to the linker to the atoms must be laid out contiguously.
//
// Scope:
// An atom is in one of three scopes: translation-unit, linkage-unit, or global.  These correspond
// to the C visibility of static, hidden, default.
//
// DefinitionKind:
// An atom is one of five defintion kinds:
//	regular			Most atoms.
//	weak			C++ compiler makes some functions weak if there might be multiple copies
//					that the linker needs to coalesce.
//	tentative		A straggler from ancient C when the extern did not exist. "int foo;" is ambiguous.
//					It could be a prototype or it could be a definition.
//	external		This is a "proxy" atom produced by a dylib reader.  It has no content.  It exists
//					so that the graph of Atoms can be complete.
//	external-weak	Same as external, but the definition in the dylib is weak.
//
// SymbolTableInclusion:
// An atom may or may not be in the symbol table in an object file.
//  in				Most atoms for functions or global data
//	not-in			Anonymous atoms such literal c-strings, or other compiler generated data
//  not-in-final	Atom whose name should not be in the symbol table of final linkd image (e.g. 'l' labels .eh labels)
//	in-never-strip	Atom whose name the strip tool should never remove (e.g. REFERENCED_DYNAMICALLY in mach-o)
//
// ContentType:
// Some atoms require specially processing by the linker based on their content.  For instance, zero-fill data
// atom are group together at the end of the DATA segment to reduce disk size.
//
// ObjectAddress:
// For reproducability, the linker lays out atoms in the order they occurred in the source (object) files.
// The objectAddress() method returns the address of an atom in the object file so that the linker 
// can arrange the atoms.
//
//
class Atom
{
public:
	enum Scope { scopeTranslationUnit, scopeLinkageUnit, scopeGlobal };
	enum Definition { definitionRegular, definitionTentative, definitionAbsolute, definitionProxy };
	enum Combine { combineNever, combineByName, combineByNameAndContent, combineByNameAndReferences };
	enum ContentType { typeUnclassified, typeZeroFill, typeCString, typeCFI, typeLSDA, typeSectionStart, 
					typeSectionEnd, typeBranchIsland, typeLazyPointer, typeStub, typeNonLazyPointer, 
					typeLazyDylibPointer, typeStubHelper, typeInitializerPointers, typeTerminatorPointers,
					typeLTOtemporary, typeResolver,
					typeTLV, typeTLVZeroFill, typeTLVInitialValue, typeTLVInitializerPointers };

	enum SymbolTableInclusion { symbolTableNotIn, symbolTableNotInFinalLinkedImages, symbolTableIn,
								symbolTableInAndNeverStrip, symbolTableInAsAbsolute, 
								symbolTableInWithRandomAutoStripLabel };
	enum WeakImportState { weakImportUnset, weakImportTrue, weakImportFalse };
	
	struct Alignment { 
					Alignment(int p2, int m=0) : powerOf2(p2), modulus(m) {}
		uint8_t		trailingZeros() const { return (modulus==0) ? powerOf2 : __builtin_ctz(modulus); }
		uint16_t	powerOf2;  
		uint16_t	modulus; 
	};
	struct LineInfo {
		const char* fileName;
		uint32_t	atomOffset;
		uint32_t	lineNumber;
		
		typedef LineInfo* iterator;
	};
	struct UnwindInfo {
		uint32_t	startOffset;
		uint32_t	unwindInfo;
		
		typedef UnwindInfo* iterator;
	};
 
											Atom(const Section& sect, Definition d, Combine c, Scope s, ContentType ct, 
												SymbolTableInclusion i, bool dds, bool thumb, bool al, Alignment a) :  
													_section(&sect), _address(0), _alignmentModulus(a.modulus), 
													_alignmentPowerOf2(a.powerOf2), _definition(d), _combine(c),   
													_dontDeadStrip(dds), _thumb(thumb), _alias(al), _autoHide(false), 
													_contentType(ct), _symbolTableInclusion(i),
													_scope(s), _mode(modeSectionOffset), 
													_overridesADylibsWeakDef(false), _coalescedAway(false),
													_live(false), _machoSection(0), _weakImportState(weakImportUnset)
													 {
													#ifndef NDEBUG
														switch ( _combine ) {
															case combineByNameAndContent:
															case combineByNameAndReferences:
																assert(_symbolTableInclusion != symbolTableIn);
																assert(_scope != scopeGlobal);
                                                                break;
                                                            case combineByName:
                                                            case combineNever:
                                                                break;
														};
													#endif
													 }
	virtual									~Atom() {}

	const Section&							section() const				{ return *_section; }
	Definition								definition() const			{ return _definition; }
	Combine									combine() const				{ return _combine; }
	Scope									scope() const				{ return _scope; }
	ContentType								contentType() const			{ return _contentType; }
	SymbolTableInclusion					symbolTableInclusion() const{ return _symbolTableInclusion; }
	bool									dontDeadStrip() const		{ return _dontDeadStrip; }
	bool									isThumb() const				{ return _thumb; }
	bool									isAlias() const				{ return _alias; }
	Alignment								alignment() const			{ return Alignment(_alignmentPowerOf2, _alignmentModulus); }
	bool									overridesDylibsWeakDef() const	{ return _overridesADylibsWeakDef; }
	bool									coalescedAway() const		{ return _coalescedAway; }
	bool									weakImported() const		{ return _weakImportState == weakImportTrue; }
	WeakImportState							weakImportState() const		{ return _weakImportState; }
	bool									autoHide() const			{ return _autoHide; }
	bool									live() const				{ return _live; }
	uint8_t									machoSection() const		{ assert(_machoSection != 0); return _machoSection; }

	void									setScope(Scope s)			{ _scope = s; }
	void									setSymbolTableInclusion(SymbolTableInclusion i)			
																		{ _symbolTableInclusion = i; }
	void									setCombine(Combine c)		{ _combine = c; }
	void									setOverridesDylibsWeakDef()	{ _overridesADylibsWeakDef = true; }
	void									setCoalescedAway()			{ _coalescedAway = true; }
	void									setWeakImportState(bool w)	{ assert(_definition == definitionProxy); _weakImportState = ( w ? weakImportTrue : weakImportFalse); }
	void									setAutoHide()				{ _autoHide = true; }
	void									setLive()					{ _live = true; }
	void									setLive(bool value)			{ _live = value; }
	void									setMachoSection(unsigned x) { assert(x != 0); assert(x < 256); _machoSection = x; }
	void									setSectionOffset(uint64_t o){ assert(_mode == modeSectionOffset); _address = o; _mode = modeSectionOffset; }
	void									setSectionStartAddress(uint64_t a) { assert(_mode == modeSectionOffset); _address += a; _mode = modeFinalAddress; }
	uint64_t								sectionOffset() const		{ assert(_mode == modeSectionOffset); return _address; }
	uint64_t								finalAddress() const		{ assert(_mode == modeFinalAddress); return _address; }
#ifndef NDEBUG
	bool									finalAddressMode() const    { return (_mode == modeFinalAddress); }
#endif
	virtual const File*						file() const = 0;
	virtual const char*						translationUnitSource() const { return NULL; }
	virtual const char*						name() const = 0;
	virtual uint64_t						objectAddress() const = 0;
	virtual uint64_t						size() const = 0;
	virtual void							copyRawContent(uint8_t buffer[]) const = 0;
	virtual const uint8_t*					rawContentPointer() const { return NULL; }
	virtual unsigned long					contentHash(const class IndirectBindingTable&) const { return 0; }
	virtual bool							canCoalesceWith(const Atom& rhs, const class IndirectBindingTable&) const { return false; }
	virtual Fixup::iterator					fixupsBegin() const	{ return NULL; }
	virtual Fixup::iterator					fixupsEnd() const	{ return NULL; }
	bool									hasFixupsOfKind(Fixup::Kind kind) const {
		for (ld::Fixup::iterator fit = fixupsBegin(), end=fixupsEnd(); fit != end; ++fit) {
			if ( fit->kind == kind ) return true;
		}
		return false;
	}
	virtual void							setFile(const File* f)		{ }
	
	virtual UnwindInfo::iterator			beginUnwind() const { return NULL; }
	virtual UnwindInfo::iterator			endUnwind() const	{ return NULL; }
	virtual LineInfo::iterator				beginLineInfo() const { return NULL; }
	virtual LineInfo::iterator				endLineInfo() const { return NULL; }
											
											void setAttributesFromAtom(const Atom& a) { 
													_section = a._section; 
													_alignmentModulus = a._alignmentModulus;
													_alignmentPowerOf2 = a._alignmentPowerOf2;
													_definition = a._definition;
													_combine = a._combine;
													_dontDeadStrip = a._dontDeadStrip;
													_thumb = a._thumb;
													_autoHide = a._autoHide;
													_contentType = a._contentType;
													_symbolTableInclusion = a._symbolTableInclusion;
													_scope = a._scope;
													_mode = a._mode;
													_overridesADylibsWeakDef = a._overridesADylibsWeakDef;
													_coalescedAway = a._coalescedAway;
													_weakImportState = a._weakImportState;
												}

protected:
	enum AddressMode { modeSectionOffset, modeFinalAddress };

	const Section *						_section;
	uint64_t							_address;
	uint16_t							_alignmentModulus;
	uint8_t								_alignmentPowerOf2;
	Definition							_definition : 2;
	Combine								_combine : 2;
	bool								_dontDeadStrip : 1;
	bool								_thumb : 1; 
	bool								_alias : 1;
	int									_autoHide : 1;
	ContentType							_contentType : 5;
	SymbolTableInclusion				_symbolTableInclusion : 3;
	Scope								_scope : 2;
	AddressMode							_mode: 2;
	bool								_overridesADylibsWeakDef : 1;
	bool								_coalescedAway : 1;
	bool								_live : 1;
	unsigned							_machoSection : 8;
	WeakImportState						_weakImportState : 2;
};


class IndirectBindingTable
{
public:	
	virtual const char*			indirectName(uint32_t bindingIndex) const = 0;
	virtual const ld::Atom*		indirectAtom(uint32_t bindingIndex) const = 0;
};


	
// utility classes for using std::unordered_map with c-strings
struct CStringHash {
	size_t operator()(const char* __s) const {
		size_t __h = 0;
		for ( ; *__s; ++__s)
			__h = 5 * __h + *__s;
		return __h;
	};
};
struct CStringEquals
{
	bool operator()(const char* left, const char* right) const { return (strcmp(left, right) == 0); }
};

typedef	std::unordered_set<const char*, ld::CStringHash, ld::CStringEquals>  CStringSet;

class Internal
{
public:
	class FinalSection : public ld::Section {
	public:
										FinalSection(const Section& sect) : Section(sect), address(0),
												fileOffset(0), size(0), alignment(0),
												indirectSymTabStartIndex(0), indirectSymTabElementSize(0),
												relocStart(0), relocCount(0), 
												hasLocalRelocs(false), hasExternalRelocs(false) {}
		std::vector<const Atom*>		atoms;
		uint64_t						address;
		uint64_t						fileOffset;
		uint64_t						size;
		uint32_t						alignmentPaddingBytes;
		uint8_t							alignment;
		uint32_t						indirectSymTabStartIndex;
		uint32_t						indirectSymTabElementSize;
		uint32_t						relocStart;
		uint32_t						relocCount;
		bool							hasLocalRelocs;
		bool							hasExternalRelocs;
	};
	
	virtual uint64_t					assignFileOffsets() = 0;
	virtual void						setSectionSizesAndAlignments() = 0;
	virtual ld::Internal::FinalSection*	addAtom(const Atom&) = 0;
	virtual ld::Internal::FinalSection* getFinalSection(const ld::Section& inputSection) = 0;
	virtual								~Internal() {}
										Internal() : bundleLoader(NULL),
											entryPoint(NULL), classicBindingHelper(NULL),
											lazyBindingHelper(NULL), compressedFastBinderProxy(NULL),
											objcObjectConstraint(ld::File::objcConstraintNone), 
											objcDylibConstraint(ld::File::objcConstraintNone), 
											swiftVersion(0), cpuSubType(0), 
											allObjectFilesScatterable(true), 
											someObjectFileHasDwarf(false), usingHugeSections(false),
											hasThreadLocalVariableDefinitions(false),
											hasWeakExternalSymbols(false),
											someObjectHasOptimizationHints(false) { }
										
	std::vector<FinalSection*>					sections;
	std::vector<ld::dylib::File*>				dylibs;
	std::vector<ld::relocatable::File::Stab>	stabs;
	CStringSet									linkerOptionLibraries;
	CStringSet									linkerOptionFrameworks;
	std::vector<const ld::Atom*>				indirectBindingTable;
	const ld::dylib::File*						bundleLoader;
	const Atom*									entryPoint;
	const Atom*									classicBindingHelper;
	const Atom*									lazyBindingHelper;
	const Atom*									compressedFastBinderProxy;
	ld::File::ObjcConstraint					objcObjectConstraint;
	ld::File::ObjcConstraint					objcDylibConstraint;
	uint8_t										swiftVersion;
	uint32_t									cpuSubType;
	bool										allObjectFilesScatterable;
	bool										someObjectFileHasDwarf;
	bool										usingHugeSections;
	bool										hasThreadLocalVariableDefinitions;
	bool										hasWeakExternalSymbols;
	bool										someObjectHasOptimizationHints;
};











} // namespace ld 

#endif // __LD_HPP__
