/* -*- mode: C++; c-basic-offset: 4; tab-width: 4 -*- 
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
 

#include <stdint.h>
#include <stdlib.h>
#include <math.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <sys/mman.h>

#include "MachOFileAbstraction.hpp"

#include "libunwind/DwarfInstructions.hpp"
#include "libunwind/AddressSpace.hpp"
#include "libunwind/Registers.hpp"

#include <vector>
#include <set>
#include <map>
#include <algorithm>

#include "dwarf2.h"
#include "debugline.h"

#include "Architectures.hpp"
#include "ld.hpp"
#include "macho_relocatable_file.h"
#include "qsort_r.h"


extern void throwf(const char* format, ...) __attribute__ ((noreturn,format(printf, 1, 2)));
extern void warning(const char* format, ...) __attribute__((format(printf, 1, 2)));

namespace mach_o {
namespace relocatable {


// forward reference
template <typename A> class Parser;
template <typename A> class Atom;
template <typename A> class Section;
template <typename A> class CFISection;
template <typename A> class CUSection;

template <typename A>
class File : public ld::relocatable::File
{
public:
											File(const char* p, time_t mTime, const uint8_t* content, ld::File::Ordinal ord) :
												ld::relocatable::File(p,mTime,ord), _fileContent(content),
												_sectionsArray(NULL), _atomsArray(NULL),
												_sectionsArrayCount(0), _atomsArrayCount(0),
												_debugInfoKind(ld::relocatable::File::kDebugInfoNone),
												_dwarfTranslationUnitPath(NULL), 
												_dwarfDebugInfoSect(NULL), _dwarfDebugAbbrevSect(NULL), 
												_dwarfDebugLineSect(NULL), _dwarfDebugStringSect(NULL), 
												_objConstraint(ld::File::objcConstraintNone),
												_cpuSubType(0),
												_canScatterAtoms(false) {}
	virtual									~File();

	// overrides of ld::File
	virtual bool										forEachAtom(ld::File::AtomHandler&) const;
	virtual bool										justInTimeforEachAtom(const char* name, ld::File::AtomHandler&) const
																					{ return false; }
	
	// overrides of ld::relocatable::File 
	virtual ObjcConstraint								objCConstraint() const			{ return _objConstraint; }
	virtual uint32_t									cpuSubType() const				{ return _cpuSubType; }
	virtual DebugInfoKind								debugInfo() const				{ return _debugInfoKind; }
	virtual const std::vector<ld::relocatable::File::Stab>*	stabs() const					{ return &_stabs; }
	virtual bool										canScatterAtoms() const			{ return _canScatterAtoms; }
	virtual const char*									translationUnitSource() const;
	
	const uint8_t*										fileContent()					{ return _fileContent; }
private:
	friend class Atom<A>;
	friend class Section<A>;
	friend class Parser<A>;
	friend class CFISection<A>::OAS;
	
	typedef typename A::P					P;
	
	const uint8_t*							_fileContent;
	Section<A>**							_sectionsArray;
	uint8_t*								_atomsArray;
	uint32_t								_sectionsArrayCount;
	uint32_t								_atomsArrayCount;
	std::vector<ld::Fixup>					_fixups;
	std::vector<ld::Atom::UnwindInfo>		_unwindInfos;
	std::vector<ld::Atom::LineInfo>			_lineInfos;
	std::vector<ld::relocatable::File::Stab>_stabs;
	ld::relocatable::File::DebugInfoKind	_debugInfoKind;
	const char*								_dwarfTranslationUnitPath;
	const macho_section<P>*					_dwarfDebugInfoSect;
	const macho_section<P>*					_dwarfDebugAbbrevSect;
	const macho_section<P>*					_dwarfDebugLineSect;
	const macho_section<P>*					_dwarfDebugStringSect;
	ld::File::ObjcConstraint				_objConstraint;
	uint32_t								_cpuSubType;
	bool									_canScatterAtoms;
};


template <typename A>
class Section : public ld::Section
{
public:
	typedef typename A::P::uint_t	pint_t;
	typedef typename A::P			P;
	typedef typename A::P::E		E;

	virtual							~Section()					{ }
	class File<A>&					file() const				{ return _file; }
	const macho_section<P>*			machoSection() const		{ return _machOSection; }
	uint32_t						sectionNum(class Parser<A>&) const;
	virtual ld::Atom::Alignment		alignmentForAddress(pint_t addr);
	virtual ld::Atom::ContentType	contentType()				{ return ld::Atom::typeUnclassified; }
	virtual bool					dontDeadStrip()				{ return (this->_machOSection->flags() & S_ATTR_NO_DEAD_STRIP); }
	virtual	Atom<A>*				findAtomByAddress(pint_t addr) { return this->findContentAtomByAddress(addr, this->_beginAtoms, this->_endAtoms); }
	virtual bool					addFollowOnFixups() const	{ return ! _file.canScatterAtoms(); }
	virtual uint32_t				appendAtoms(class Parser<A>& parser, uint8_t* buffer, 
												struct Parser<A>::LabelAndCFIBreakIterator& it, 
												const struct Parser<A>::CFI_CU_InfoArrays&) = 0;
	virtual uint32_t				computeAtomCount(class Parser<A>& parser, 
														struct Parser<A>::LabelAndCFIBreakIterator& it, 
														const struct Parser<A>::CFI_CU_InfoArrays&) = 0;
	virtual void					makeFixups(class Parser<A>& parser, const struct Parser<A>::CFI_CU_InfoArrays&);
	virtual bool					addRelocFixup(class Parser<A>& parser, const macho_relocation_info<P>*);
	virtual unsigned long			contentHash(const class Atom<A>* atom, const ld::IndirectBindingTable& ind) const { return 0; }
	virtual bool					canCoalesceWith(const class Atom<A>* atom, const ld::Atom& rhs, 
													const ld::IndirectBindingTable& ind) const { return false; }
	static const char*				makeSectionName(const macho_section<typename A::P>* s);

protected:	
						Section(File<A>& f, const macho_section<typename A::P>* s)
							: ld::Section(makeSegmentName(s), makeSectionName(s), sectionType(s)),
								_file(f), _machOSection(s), _beginAtoms(NULL), _endAtoms(NULL), _hasAliases(false) { }
						Section(File<A>& f, const char* segName, const char* sectName, ld::Section::Type t, bool hidden=false)
							: ld::Section(segName, sectName, t, hidden), _file(f), _machOSection(NULL), 
								_beginAtoms(NULL), _endAtoms(NULL), _hasAliases(false) { }


	Atom<A>*						findContentAtomByAddress(pint_t addr, class Atom<A>* start, class Atom<A>* end);
	uint32_t						x86_64PcRelOffset(uint8_t r_type);
	static const char*				makeSegmentName(const macho_section<typename A::P>* s);
	static bool						readable(const macho_section<typename A::P>* s);
	static bool						writable(const macho_section<typename A::P>* s);
	static bool						exectuable(const macho_section<typename A::P>* s);
	static ld::Section::Type		sectionType(const macho_section<typename A::P>* s);
	
	File<A>&						_file;
	const macho_section<P>*			_machOSection;
	class Atom<A>*					_beginAtoms;
	class Atom<A>*					_endAtoms;
	bool							_hasAliases;
};


template <typename A>
class CFISection : public Section<A>
{
public:
						CFISection(Parser<A>& parser, File<A>& f, const macho_section<typename A::P>* s)
							: Section<A>(f, s) { }
	uint32_t			cfiCount();

	virtual ld::Atom::ContentType	contentType()		{ return ld::Atom::typeCFI; }
	virtual uint32_t	computeAtomCount(class Parser<A>& parser, struct Parser<A>::LabelAndCFIBreakIterator& it, const struct Parser<A>::CFI_CU_InfoArrays&);
	virtual uint32_t	appendAtoms(class Parser<A>& parser, uint8_t* buffer, struct Parser<A>::LabelAndCFIBreakIterator& it, const struct Parser<A>::CFI_CU_InfoArrays&);
	virtual void		makeFixups(class Parser<A>& parser, const struct Parser<A>::CFI_CU_InfoArrays&);
	virtual bool		addFollowOnFixups() const	{ return false; }


	///
	/// ObjectFileAddressSpace is used as a template parameter to UnwindCursor for parsing
	/// dwarf CFI information in an object file.   
	///
	class OAS
	{
	public:
			typedef typename A::P::uint_t	pint_t;
			typedef typename A::P			P;
			typedef typename A::P::E		E;
			typedef typename A::P::uint_t	sint_t;

							OAS(CFISection<A>& ehFrameSection, const uint8_t* ehFrameBuffer) : 
								_ehFrameSection(ehFrameSection),
								_ehFrameContent(ehFrameBuffer), 
								_ehFrameStartAddr(ehFrameSection.machoSection()->addr()), 
								_ehFrameEndAddr(ehFrameSection.machoSection()->addr()+ehFrameSection.machoSection()->size()) {}

			uint8_t			get8(pint_t addr) { return *((uint8_t*)mappedAddress(addr)); }
			uint16_t		get16(pint_t addr)	{ return E::get16(*((uint16_t*)mappedAddress(addr))); }
			uint32_t		get32(pint_t addr)	{ return E::get32(*((uint32_t*)mappedAddress(addr))); }
			uint64_t		get64(pint_t addr)	{ return E::get64(*((uint64_t*)mappedAddress(addr))); }
			pint_t			getP(pint_t addr)	{ return P::getP(*((pint_t*)mappedAddress(addr))); }
			uint64_t		getULEB128(pint_t& addr, pint_t end);
			int64_t			getSLEB128(pint_t& addr, pint_t end);
			pint_t			getEncodedP(pint_t& addr, pint_t end, uint8_t encoding);
	private:
		const void*			mappedAddress(pint_t addr);
		
		CFISection<A>&				_ehFrameSection;
		const uint8_t*				_ehFrameContent;
		pint_t						_ehFrameStartAddr;
		pint_t						_ehFrameEndAddr;
	};


	typedef typename A::P::uint_t			pint_t;
	typedef libunwind::CFI_Atom_Info<OAS>	CFI_Atom_Info;
	
	void				cfiParse(class Parser<A>& parser, uint8_t* buffer, CFI_Atom_Info cfiArray[], uint32_t cfiCount);
	bool				needsRelocating();

	static bool			bigEndian();
private:
	void				addCiePersonalityFixups(class Parser<A>& parser, const CFI_Atom_Info* cieInfo);
	static void			warnFunc(void* ref, uint64_t funcAddr, const char* msg);
};


template <typename A>
class CUSection : public Section<A>
{
public:
						CUSection(Parser<A>& parser, File<A>& f, const macho_section<typename A::P>* s)
							: Section<A>(f, s) { }

	typedef typename A::P::uint_t	pint_t;
	typedef typename A::P			P;
	typedef typename A::P::E		E;
					
	virtual uint32_t		computeAtomCount(class Parser<A>& parser, struct Parser<A>::LabelAndCFIBreakIterator& it, const struct Parser<A>::CFI_CU_InfoArrays&) { return 0; }
	virtual uint32_t		appendAtoms(class Parser<A>& parser, uint8_t* buffer, struct Parser<A>::LabelAndCFIBreakIterator& it, const struct Parser<A>::CFI_CU_InfoArrays&) { return 0; }
	virtual void			makeFixups(class Parser<A>& parser, const struct Parser<A>::CFI_CU_InfoArrays&);
	virtual bool			addFollowOnFixups() const	{ return false; }
	
	struct Info {
		pint_t		functionStartAddress;
		uint32_t	functionSymbolIndex;
		uint32_t	rangeLength;
		uint32_t	compactUnwindInfo;
		const char*	personality;
		pint_t		lsdaAddress;
		Atom<A>*	function;
		Atom<A>*	lsda;
	};

	uint32_t				count();
	void					parse(class Parser<A>& parser, uint32_t cnt, Info array[]);
	
	
private:
	
	const char*				personalityName(class Parser<A>& parser, const macho_relocation_info<P>* reloc);

	static int				infoSorter(const void* l, const void* r);

};


template <typename A>
class TentativeDefinitionSection : public Section<A>
{
public:
						TentativeDefinitionSection(Parser<A>& parser, File<A>& f)
							: Section<A>(f, "__DATA", "__comm/tent", ld::Section::typeTentativeDefs)  {}

	virtual ld::Atom::ContentType	contentType()		{ return ld::Atom::typeZeroFill; }
	virtual bool		addFollowOnFixups() const	{ return false; }
	virtual Atom<A>*	findAtomByAddress(typename A::P::uint_t addr) { throw "TentativeDefinitionSection::findAtomByAddress() should never be called"; }
	virtual uint32_t	computeAtomCount(class Parser<A>& parser, struct Parser<A>::LabelAndCFIBreakIterator& it, 
											const struct Parser<A>::CFI_CU_InfoArrays&);
	virtual uint32_t	appendAtoms(class Parser<A>& parser, uint8_t* buffer, 
										struct Parser<A>::LabelAndCFIBreakIterator& it, 
										const struct Parser<A>::CFI_CU_InfoArrays&);
	virtual void		makeFixups(class Parser<A>& parser, const struct Parser<A>::CFI_CU_InfoArrays&) {}
private:
	typedef typename A::P::uint_t	pint_t;
	typedef typename A::P			P;
};


template <typename A>
class AbsoluteSymbolSection : public Section<A>
{
public:
						AbsoluteSymbolSection(Parser<A>& parser, File<A>& f)
							: Section<A>(f, "__DATA", "__abs", ld::Section::typeAbsoluteSymbols, true)  {}

	virtual ld::Atom::ContentType	contentType()		{ return ld::Atom::typeUnclassified; }
	virtual bool					dontDeadStrip()		{ return false; }
	virtual ld::Atom::Alignment		alignmentForAddress(typename A::P::uint_t addr) { return ld::Atom::Alignment(0); }
	virtual bool		addFollowOnFixups() const	{ return false; }
	virtual Atom<A>*	findAtomByAddress(typename A::P::uint_t addr) { throw "AbsoluteSymbolSection::findAtomByAddress() should never be called"; }
	virtual uint32_t	computeAtomCount(class Parser<A>& parser, struct Parser<A>::LabelAndCFIBreakIterator& it, 
											const struct Parser<A>::CFI_CU_InfoArrays&);
	virtual uint32_t	appendAtoms(class Parser<A>& parser, uint8_t* buffer, 
										struct Parser<A>::LabelAndCFIBreakIterator& it, 
										const struct Parser<A>::CFI_CU_InfoArrays&);
	virtual void		makeFixups(class Parser<A>& parser, const struct Parser<A>::CFI_CU_InfoArrays&) {}
	virtual Atom<A>*	findAbsAtomForValue(typename A::P::uint_t);
	
private:
	typedef typename A::P::uint_t	pint_t;
	typedef typename A::P			P;
};


template <typename A>
class SymboledSection : public Section<A>
{
public:
						SymboledSection(Parser<A>& parser, File<A>& f, const macho_section<typename A::P>* s);
	virtual ld::Atom::ContentType	contentType() { return _type; }
	virtual bool					dontDeadStrip();
	virtual uint32_t	computeAtomCount(class Parser<A>& parser, struct Parser<A>::LabelAndCFIBreakIterator& it, 
											const struct Parser<A>::CFI_CU_InfoArrays&);
	virtual uint32_t	appendAtoms(class Parser<A>& parser, uint8_t* buffer, 
									struct Parser<A>::LabelAndCFIBreakIterator& it, 
									const struct Parser<A>::CFI_CU_InfoArrays&);
protected:
	typedef typename A::P::uint_t	pint_t;
	typedef typename A::P			P;

	ld::Atom::ContentType			_type;
};


template <typename A>
class TLVDefsSection : public SymboledSection<A>
{
public:
						TLVDefsSection(Parser<A>& parser, File<A>& f, const macho_section<typename A::P>* s) :
							SymboledSection<A>(parser, f, s) { }

private:

};


template <typename A>
class ImplicitSizeSection : public Section<A>
{
public:
						ImplicitSizeSection(Parser<A>& parser, File<A>& f, const macho_section<typename A::P>* s)
							: Section<A>(f, s) { }
	virtual uint32_t	computeAtomCount(class Parser<A>& parser, struct Parser<A>::LabelAndCFIBreakIterator& it, const struct Parser<A>::CFI_CU_InfoArrays&);
	virtual uint32_t	appendAtoms(class Parser<A>& parser, uint8_t* buffer, struct Parser<A>::LabelAndCFIBreakIterator& it, const struct Parser<A>::CFI_CU_InfoArrays&);
protected:
	typedef typename A::P::uint_t	pint_t;
	typedef typename A::P			P;
	
	virtual bool						addFollowOnFixups() const		{ return false; }
	virtual const char*					unlabeledAtomName(Parser<A>& parser, pint_t addr) = 0;
	virtual ld::Atom::SymbolTableInclusion	symbolTableInclusion()		{ return ld::Atom::symbolTableNotIn; }
	virtual	pint_t						elementSizeAtAddress(pint_t addr) = 0;
	virtual ld::Atom::Scope				scopeAtAddress(Parser<A>& parser, pint_t addr) { return ld::Atom::scopeLinkageUnit; }
	virtual bool						useElementAt(Parser<A>& parser, 
												struct Parser<A>::LabelAndCFIBreakIterator& it, pint_t addr) = 0;
	virtual ld::Atom::Definition		definition()					{ return ld::Atom::definitionRegular; }
	virtual ld::Atom::Combine			combine(Parser<A>& parser, pint_t addr) = 0;
	virtual	bool						ignoreLabel(const char* label)  { return (label[0] == 'L'); }
};

template <typename A>
class FixedSizeSection : public ImplicitSizeSection<A>
{
public:
						FixedSizeSection(Parser<A>& parser, File<A>& f, const macho_section<typename A::P>* s)
							: ImplicitSizeSection<A>(parser, f, s) { }
protected:
	typedef typename A::P::uint_t	pint_t;
	typedef typename A::P			P;
	typedef typename A::P::E		E;
	
	virtual bool					useElementAt(Parser<A>& parser, 
										struct Parser<A>::LabelAndCFIBreakIterator& it, pint_t addr) 
														{ return true; }
};


template <typename A>
class Literal4Section : public FixedSizeSection<A>
{
public:
						Literal4Section(Parser<A>& parser, File<A>& f, const macho_section<typename A::P>* s)
							: FixedSizeSection<A>(parser, f, s) {}
protected:
	typedef typename A::P::uint_t	pint_t;
	typedef typename A::P			P;

	virtual ld::Atom::Alignment		alignmentForAddress(pint_t addr)		{ return ld::Atom::Alignment(2); }
	virtual const char*				unlabeledAtomName(Parser<A>&, pint_t)	{ return "4-byte-literal"; }
	virtual	pint_t					elementSizeAtAddress(pint_t addr)		{ return 4; }
	virtual ld::Atom::Combine		combine(Parser<A>&, pint_t)				{ return ld::Atom::combineByNameAndContent; }
	virtual unsigned long			contentHash(const class Atom<A>* atom, const ld::IndirectBindingTable& ind) const;
	virtual bool					canCoalesceWith(const class Atom<A>* atom, const ld::Atom& rhs, 
													const ld::IndirectBindingTable& ind) const;
};

template <typename A>
class Literal8Section : public FixedSizeSection<A>
{
public:
						Literal8Section(Parser<A>& parser, File<A>& f, const macho_section<typename A::P>* s)
							: FixedSizeSection<A>(parser, f, s) {}
protected:
	typedef typename A::P::uint_t	pint_t;
	typedef typename A::P			P;

	virtual ld::Atom::Alignment		alignmentForAddress(pint_t addr)		{ return ld::Atom::Alignment(3); }
	virtual const char*				unlabeledAtomName(Parser<A>&, pint_t)	{ return "8-byte-literal"; }
	virtual	pint_t					elementSizeAtAddress(pint_t addr)		{ return 8; }
	virtual ld::Atom::Combine		combine(Parser<A>&, pint_t)				{ return ld::Atom::combineByNameAndContent; }
	virtual unsigned long			contentHash(const class Atom<A>* atom, const ld::IndirectBindingTable& ind) const;
	virtual bool					canCoalesceWith(const class Atom<A>* atom, const ld::Atom& rhs, 
													const ld::IndirectBindingTable& ind) const;
};

template <typename A>
class Literal16Section : public FixedSizeSection<A>
{
public:
						Literal16Section(Parser<A>& parser, File<A>& f, const macho_section<typename A::P>* s)
							: FixedSizeSection<A>(parser, f, s) {}
protected:
	typedef typename A::P::uint_t	pint_t;
	typedef typename A::P			P;

	virtual ld::Atom::Alignment		alignmentForAddress(pint_t addr)		{ return ld::Atom::Alignment(4); }
	virtual const char*				unlabeledAtomName(Parser<A>&, pint_t)	{ return "16-byte-literal"; }
	virtual	pint_t					elementSizeAtAddress(pint_t addr)		{ return 16; }
	virtual ld::Atom::Combine		combine(Parser<A>&, pint_t)				{ return ld::Atom::combineByNameAndContent; }
	virtual unsigned long			contentHash(const class Atom<A>* atom, const ld::IndirectBindingTable& ind) const;
	virtual bool					canCoalesceWith(const class Atom<A>* atom, const ld::Atom& rhs, 
													const ld::IndirectBindingTable& ind) const;
};


template <typename A>
class NonLazyPointerSection : public FixedSizeSection<A>
{
public:
						NonLazyPointerSection(Parser<A>& parser, File<A>& f, const macho_section<typename A::P>* s)
							: FixedSizeSection<A>(parser, f, s) {}
protected:
	typedef typename A::P::uint_t	pint_t;
	typedef typename A::P			P;

	virtual void					makeFixups(class Parser<A>& parser, const struct Parser<A>::CFI_CU_InfoArrays&);
	virtual ld::Atom::ContentType	contentType()							{ return ld::Atom::typeNonLazyPointer; }
	virtual ld::Atom::Alignment		alignmentForAddress(pint_t addr)		{ return ld::Atom::Alignment(log2(sizeof(pint_t))); }
	virtual const char*				unlabeledAtomName(Parser<A>&, pint_t)	{ return "non_lazy_ptr"; }
	virtual	pint_t					elementSizeAtAddress(pint_t addr)		{ return sizeof(pint_t); }
	virtual ld::Atom::Scope			scopeAtAddress(Parser<A>& parser, pint_t addr);
	virtual ld::Atom::Combine		combine(Parser<A>&, pint_t);
	virtual	bool					ignoreLabel(const char* label)			{ return true; }
	virtual unsigned long			contentHash(const class Atom<A>* atom, const ld::IndirectBindingTable& ind) const;
	virtual bool					canCoalesceWith(const class Atom<A>* atom, const ld::Atom& rhs, 
													const ld::IndirectBindingTable& ind) const;

private:
	static const char*				targetName(const class Atom<A>* atom, const ld::IndirectBindingTable& ind);
	static ld::Fixup::Kind			fixupKind();
};


template <typename A>
class CFStringSection : public FixedSizeSection<A>
{
public:
						CFStringSection(Parser<A>& parser, File<A>& f, const macho_section<typename A::P>* s)
							: FixedSizeSection<A>(parser, f, s) {}
protected:
	typedef typename A::P::uint_t	pint_t;

	virtual ld::Atom::Alignment		alignmentForAddress(pint_t addr)		{ return ld::Atom::Alignment(log2(sizeof(pint_t))); }
	virtual const char*				unlabeledAtomName(Parser<A>&, pint_t)	{ return "CFString"; }
	virtual	pint_t					elementSizeAtAddress(pint_t addr)		{ return 4*sizeof(pint_t); }
	virtual ld::Atom::Combine		combine(Parser<A>&, pint_t)				{ return ld::Atom::combineByNameAndReferences; }
	virtual	bool					ignoreLabel(const char* label)			{ return true; }
	virtual unsigned long			contentHash(const class Atom<A>* atom, const ld::IndirectBindingTable& ind) const;
	virtual bool					canCoalesceWith(const class Atom<A>* atom, const ld::Atom& rhs, 
													const ld::IndirectBindingTable& ind) const;
private:
	enum ContentType { contentUTF8, contentUTF16, contentUnknown };
	static const uint8_t*			targetContent(const class Atom<A>* atom, const ld::IndirectBindingTable& ind,
												ContentType* ct, unsigned int* count);
};


template <typename A>
class ObjC1ClassSection : public FixedSizeSection<A>
{
public:
						ObjC1ClassSection(Parser<A>& parser, File<A>& f, const macho_section<typename A::P>* s)
							: FixedSizeSection<A>(parser, f, s) {}
protected:
	typedef typename A::P::uint_t	pint_t;
	typedef typename A::P			P;
	typedef typename A::P::E		E;

	virtual ld::Atom::Scope			scopeAtAddress(Parser<A>& , pint_t )	{ return ld::Atom::scopeGlobal; }
	virtual ld::Atom::Alignment		alignmentForAddress(pint_t addr)		{ return ld::Atom::Alignment(2); }
	virtual const char*				unlabeledAtomName(Parser<A>&, pint_t);
	virtual ld::Atom::SymbolTableInclusion	symbolTableInclusion()			{ return ld::Atom::symbolTableIn; }
	virtual	pint_t					elementSizeAtAddress(pint_t addr);
	virtual ld::Atom::Combine		combine(Parser<A>&, pint_t)				{ return ld::Atom::combineNever; }
	virtual	bool					ignoreLabel(const char* label)			{ return true; }
	virtual unsigned long			contentHash(const class Atom<A>* atom, const ld::IndirectBindingTable& ind) const
																			{ return 0; }
	virtual bool					canCoalesceWith(const class Atom<A>* atom, const ld::Atom& rhs, 
													const ld::IndirectBindingTable& ind) const { return false; }
	virtual bool					addRelocFixup(class Parser<A>& parser, const macho_relocation_info<P>*);
};


template <typename A>
class ObjC2ClassRefsSection : public FixedSizeSection<A>
{
public:
						ObjC2ClassRefsSection(Parser<A>& parser, File<A>& f, const macho_section<typename A::P>* s)
							: FixedSizeSection<A>(parser, f, s) {}
protected:
	typedef typename A::P::uint_t	pint_t;

	virtual ld::Atom::Alignment		alignmentForAddress(pint_t addr)		{ return ld::Atom::Alignment(log2(sizeof(pint_t))); }
	virtual const char*				unlabeledAtomName(Parser<A>&, pint_t)	{ return "objc-class-ref"; }
	virtual	pint_t					elementSizeAtAddress(pint_t addr)		{ return sizeof(pint_t); }
	virtual ld::Atom::Combine		combine(Parser<A>&, pint_t)				{ return ld::Atom::combineByNameAndReferences; }
	virtual	bool					ignoreLabel(const char* label)			{ return true; }
	virtual unsigned long			contentHash(const class Atom<A>* atom, const ld::IndirectBindingTable& ind) const;
	virtual bool					canCoalesceWith(const class Atom<A>* atom, const ld::Atom& rhs, 
													const ld::IndirectBindingTable& ind) const;
private:
	const char*						targetClassName(const class Atom<A>* atom, const ld::IndirectBindingTable& ind) const;
};


template <typename A>
class ObjC2CategoryListSection : public FixedSizeSection<A>
{
public:
						ObjC2CategoryListSection(Parser<A>& parser, File<A>& f, const macho_section<typename A::P>* s)
							: FixedSizeSection<A>(parser, f, s) {}
protected:
	typedef typename A::P::uint_t	pint_t;

	virtual ld::Atom::Alignment		alignmentForAddress(pint_t addr)		{ return ld::Atom::Alignment(log2(sizeof(pint_t))); }
	virtual ld::Atom::Scope			scopeAtAddress(Parser<A>& parser, pint_t addr) { return ld::Atom::scopeTranslationUnit; }
	virtual const char*				unlabeledAtomName(Parser<A>&, pint_t)	{ return "objc-cat-list"; }
	virtual	pint_t					elementSizeAtAddress(pint_t addr)		{ return sizeof(pint_t); }
	virtual ld::Atom::Combine		combine(Parser<A>&, pint_t)				{ return ld::Atom::combineNever; }
	virtual	bool					ignoreLabel(const char* label)			{ return true; }
private:
	const char*						targetClassName(const class Atom<A>* atom, const ld::IndirectBindingTable& ind) const;
};


template <typename A>
class PointerToCStringSection : public FixedSizeSection<A>
{
public:
						PointerToCStringSection(Parser<A>& parser, File<A>& f, const macho_section<typename A::P>* s)
							: FixedSizeSection<A>(parser, f, s) {}
protected:
	typedef typename A::P::uint_t	pint_t;

	virtual ld::Atom::Alignment		alignmentForAddress(pint_t addr)		{ return ld::Atom::Alignment(log2(sizeof(pint_t))); }
	virtual const char*				unlabeledAtomName(Parser<A>&, pint_t)	{ return "pointer-to-literal-cstring"; }
	virtual	pint_t					elementSizeAtAddress(pint_t addr)		{ return sizeof(pint_t); }
	virtual ld::Atom::Combine		combine(Parser<A>&, pint_t)				{ return ld::Atom::combineByNameAndReferences; }
	virtual	bool					ignoreLabel(const char* label)			{ return true; }
	virtual unsigned long			contentHash(const class Atom<A>* atom, const ld::IndirectBindingTable& ind) const;
	virtual bool					canCoalesceWith(const class Atom<A>* atom, const ld::Atom& rhs, 
													const ld::IndirectBindingTable& ind) const;
	virtual const char*				targetCString(const class Atom<A>* atom, const ld::IndirectBindingTable& ind) const;
};


template <typename A>
class Objc1ClassReferences : public PointerToCStringSection<A>
{
public:
						Objc1ClassReferences(Parser<A>& parser, File<A>& f, const macho_section<typename A::P>* s)
							: PointerToCStringSection<A>(parser, f, s) {}

	typedef typename A::P::uint_t	pint_t;
	typedef typename A::P			P;

	virtual const char*				unlabeledAtomName(Parser<A>&, pint_t)	{ return "pointer-to-literal-objc-class-name"; }
	virtual bool					addRelocFixup(class Parser<A>& parser, const macho_relocation_info<P>*);
	virtual const char*				targetCString(const class Atom<A>* atom, const ld::IndirectBindingTable& ind) const;
};


template <typename A>
class CStringSection : public ImplicitSizeSection<A>
{
public:
						CStringSection(Parser<A>& parser, File<A>& f, const macho_section<typename A::P>* s)
							: ImplicitSizeSection<A>(parser, f, s) {}
protected:
	typedef typename A::P::uint_t	pint_t;
	typedef typename A::P			P;

	virtual ld::Atom::ContentType	contentType()							{ return ld::Atom::typeCString; }
	virtual	Atom<A>*				findAtomByAddress(pint_t addr);
	virtual const char*				unlabeledAtomName(Parser<A>&, pint_t)	{ return "cstring"; }
	virtual	pint_t					elementSizeAtAddress(pint_t addr);
	virtual	bool					ignoreLabel(const char* label);
	virtual bool					useElementAt(Parser<A>& parser, 
												struct Parser<A>::LabelAndCFIBreakIterator& it, pint_t addr);
	virtual ld::Atom::Combine		combine(Parser<A>&, pint_t)				{ return ld::Atom::combineByNameAndContent; }
	virtual unsigned long			contentHash(const class Atom<A>* atom, const ld::IndirectBindingTable& ind) const;
	virtual bool					canCoalesceWith(const class Atom<A>* atom, const ld::Atom& rhs, 
													const ld::IndirectBindingTable& ind) const;

};


template <typename A>
class UTF16StringSection : public SymboledSection<A>
{
public:
						UTF16StringSection(Parser<A>& parser, File<A>& f, const macho_section<typename A::P>* s)
							: SymboledSection<A>(parser, f, s) {}
protected:
	typedef typename A::P::uint_t	pint_t;
	typedef typename A::P			P;

	virtual ld::Atom::Combine		combine(Parser<A>&, pint_t)				{ return ld::Atom::combineByNameAndContent; }
	virtual unsigned long			contentHash(const class Atom<A>* atom, const ld::IndirectBindingTable& ind) const;
	virtual bool					canCoalesceWith(const class Atom<A>* atom, const ld::Atom& rhs, 
													const ld::IndirectBindingTable& ind) const;
};


//
// Atoms in mach-o files
//
template <typename A>
class Atom : public ld::Atom
{
public:
	// overrides of ld::Atom
	virtual ld::File*							file() const		{ return &sect().file(); }
	virtual const char*							translationUnitSource() const
																	{ return sect().file().translationUnitSource(); }
	virtual const char*							name() const		{ return _name; }
	virtual uint64_t							size() const		{ return _size; }
	virtual uint64_t							objectAddress() const { return _objAddress; }
	virtual void								copyRawContent(uint8_t buffer[]) const;
	virtual const uint8_t*						rawContentPointer() const { return contentPointer(); }
	virtual unsigned long						contentHash(const ld::IndirectBindingTable& ind) const 
															{ if ( _hash == 0 ) _hash = sect().contentHash(this, ind); return _hash; }
	virtual bool								canCoalesceWith(const ld::Atom& rhs, const ld::IndirectBindingTable& ind) const 
															{ return sect().canCoalesceWith(this, rhs, ind); }
	virtual ld::Fixup::iterator					fixupsBegin() const	{ return &machofile()._fixups[_fixupsStartIndex]; }
	virtual ld::Fixup::iterator					fixupsEnd()	const	{ return &machofile()._fixups[_fixupsStartIndex+_fixupsCount]; }
	virtual ld::Atom::UnwindInfo::iterator		beginUnwind() const	{ return &machofile()._unwindInfos[_unwindInfoStartIndex]; }
	virtual ld::Atom::UnwindInfo::iterator		endUnwind()	const	{ return &machofile()._unwindInfos[_unwindInfoStartIndex+_unwindInfoCount];  }
	virtual ld::Atom::LineInfo::iterator		beginLineInfo() const{ return &machofile()._lineInfos[_lineInfoStartIndex]; }
	virtual ld::Atom::LineInfo::iterator		endLineInfo() const { return &machofile()._lineInfos[_lineInfoStartIndex+_lineInfoCount];  }

private:

	enum {	kFixupStartIndexBits = 32,
			kLineInfoStartIndexBits = 32, 
			kUnwindInfoStartIndexBits = 24,
			kFixupCountBits = 24, 
			kLineInfoCountBits = 12,
			kUnwindInfoCountBits = 4
		}; // must sum to 128

public:
	// methods for all atoms from mach-o object file
			Section<A>&							sect() const			{ return (Section<A>&)section(); }
			File<A>&							machofile() const			{ return ((Section<A>*)(this->_section))->file(); }
			void								setFixupsRange(uint32_t s, uint32_t c);
			void								setUnwindInfoRange(uint32_t s, uint32_t c);
			void								extendUnwindInfoRange();
			void								setLineInfoRange(uint32_t s, uint32_t c);
			bool								roomForMoreLineInfoCount() { return (_lineInfoCount < ((1<<kLineInfoCountBits)-1)); }
			void								incrementLineInfoCount() { assert(roomForMoreLineInfoCount()); ++_lineInfoCount; }
			void								incrementFixupCount() { if (_fixupsCount == ((1 << kFixupCountBits)-1)) 
																			throwf("too may fixups in %s", name()); ++_fixupsCount; }
			const uint8_t*						contentPointer() const;
			uint32_t							fixupCount() const { return _fixupsCount; }
			void								verifyAlignment() const;
	
	typedef typename A::P						P;
	typedef typename A::P::E					E;
	typedef typename A::P::uint_t				pint_t;
												// constuct via all attributes
												Atom(Section<A>& sct, const char* nm, pint_t addr, uint64_t sz, 
													ld::Atom::Definition d, ld::Atom::Combine c, ld::Atom::Scope s, 
													ld::Atom::ContentType ct, ld::Atom::SymbolTableInclusion i, 
													bool dds, bool thumb, bool al, ld::Atom::Alignment a) 
														: ld::Atom((ld::Section&)sct, d, c, s, ct, i, dds, thumb, al, a), 
															_size(sz), _objAddress(addr), _name(nm), _hash(0), 
															_fixupsStartIndex(0), _lineInfoStartIndex(0),
															_unwindInfoStartIndex(0), _fixupsCount(0),  
															_lineInfoCount(0), _unwindInfoCount(0) { }
												// construct via symbol table entry
												Atom(Section<A>& sct, Parser<A>& parser, const macho_nlist<P>& sym, 
																uint64_t sz, bool alias=false)
														: ld::Atom((ld::Section&)sct, parser.definitionFromSymbol(sym), 
																parser.combineFromSymbol(sym), parser.scopeFromSymbol(sym),
																parser.resolverFromSymbol(sym) ? ld::Atom::typeResolver : sct.contentType(), 
																parser.inclusionFromSymbol(sym), 
																parser.dontDeadStripFromSymbol(sym) || sct.dontDeadStrip(),
																parser.isThumbFromSymbol(sym), alias, 
																sct.alignmentForAddress(sym.n_value())),
															_size(sz), _objAddress(sym.n_value()), 
															_name(parser.nameFromSymbol(sym)), _hash(0), 
															_fixupsStartIndex(0), _lineInfoStartIndex(0),
															_unwindInfoStartIndex(0), _fixupsCount(0),  
															_lineInfoCount(0), _unwindInfoCount(0) { 
																// <rdar://problem/6783167> support auto-hidden weak symbols
																if ( _scope == ld::Atom::scopeGlobal && 
																		(sym.n_desc() & (N_WEAK_DEF|N_WEAK_REF)) == (N_WEAK_DEF|N_WEAK_REF) )
																	this->setAutoHide();
																	this->verifyAlignment();
															}

private:
	friend class Parser<A>;
	friend class Section<A>;
	friend class CStringSection<A>;
	friend class AbsoluteSymbolSection<A>;
	
	pint_t										_size;
	pint_t										_objAddress;
	const char*									_name;
	mutable unsigned long						_hash;

	uint64_t									_fixupsStartIndex		: kFixupStartIndexBits,
												_lineInfoStartIndex		: kLineInfoStartIndexBits,			
												_unwindInfoStartIndex	: kUnwindInfoStartIndexBits,
												_fixupsCount			: kFixupCountBits,
												_lineInfoCount			: kLineInfoCountBits,
												_unwindInfoCount		: kUnwindInfoCountBits;

};



template <typename A>
void Atom<A>::setFixupsRange(uint32_t startIndex, uint32_t count)
{ 
	if ( count >= (1 << kFixupCountBits) ) 
		throwf("too many fixups in function %s", this->name());
	if ( startIndex >= (1 << kFixupStartIndexBits) ) 
		throwf("too many fixups in file");
	assert(((startIndex+count) <= sect().file()._fixups.size()) && "fixup index out of range");
	_fixupsStartIndex = startIndex; 
	_fixupsCount = count; 
}

template <typename A>
void Atom<A>::setUnwindInfoRange(uint32_t startIndex, uint32_t count)
{
	if ( count >= (1 << kUnwindInfoCountBits) ) 
		throwf("too many compact unwind infos in function %s", this->name());
	if ( startIndex >= (1 << kUnwindInfoStartIndexBits) ) 
		throwf("too many compact unwind infos (%d) in file", startIndex);
	assert((startIndex+count) <= sect().file()._unwindInfos.size() && "unwindinfo index out of range");
	_unwindInfoStartIndex = startIndex; 
	_unwindInfoCount = count; 
}

template <typename A>
void Atom<A>::extendUnwindInfoRange()
{
	if ( _unwindInfoCount+1 >= (1 << kUnwindInfoCountBits) ) 
		throwf("too many compact unwind infos in function %s", this->name());
	_unwindInfoCount += 1;
}

template <typename A>
void Atom<A>::setLineInfoRange(uint32_t startIndex, uint32_t count)
{ 
	assert((count < (1 << kLineInfoCountBits)) && "too many line infos");
	assert((startIndex+count) < sect().file()._lineInfos.size() && "line info index out of range");
	_lineInfoStartIndex = startIndex; 
	_lineInfoCount = count; 
}

template <typename A>
const uint8_t* Atom<A>::contentPointer() const
{
	const macho_section<P>* sct = this->sect().machoSection();
	uint32_t fileOffset = sct->offset() - sct->addr() + this->_objAddress;
	return this->sect().file().fileContent()+fileOffset;
}


template <typename A>
void Atom<A>::copyRawContent(uint8_t buffer[]) const
{
	// copy base bytes
	if ( this->contentType() == ld::Atom::typeZeroFill ) {
		bzero(buffer, _size);
	}
	else if ( _size != 0 ) {
		memcpy(buffer, this->contentPointer(), _size);
	}
}

template <>
void Atom<arm>::verifyAlignment() const
{
	if ( (this->section().type() == ld::Section::typeCode) && ! isThumb() ) {
		if ( ((_objAddress % 4) != 0) || (this->alignment().powerOf2 < 2) )
			warning("ARM function not 4-byte aligned: %s from %s", this->name(), this->file()->path());
	}
}

template <typename A>
void Atom<A>::verifyAlignment() const
{
}


template <typename A>
class Parser 
{
public:
	static bool										validFile(const uint8_t* fileContent, bool subtypeMustMatch=false, 
																cpu_subtype_t subtype=0);
	static const char*								fileKind(const uint8_t* fileContent);
	static bool										hasObjC2Categories(const uint8_t* fileContent);
	static bool										hasObjC1Categories(const uint8_t* fileContent);
	static ld::relocatable::File*					parse(const uint8_t* fileContent, uint64_t fileLength, 
															const char* path, time_t modTime, ld::File::Ordinal ordinal,
															 const ParserOptions& opts) {
																Parser p(fileContent, fileLength, path, modTime, 
																		ordinal, opts.convertUnwindInfo);
																return p.parse(opts);
														}

	typedef typename A::P						P;
	typedef typename A::P::E					E;
	typedef typename A::P::uint_t				pint_t;

	struct SourceLocation {
								SourceLocation() {}
								SourceLocation(Atom<A>* a, uint32_t o) : atom(a), offsetInAtom(o) {}
		Atom<A>*	atom;
		uint32_t	offsetInAtom;
	};

	struct TargetDesc {
		Atom<A>*	atom;		
		const char*	name;		// only used if targetAtom is NULL
		int64_t		addend;
		bool		weakImport;	// only used if targetAtom is NULL
	};

	struct FixupInAtom {
		FixupInAtom(const SourceLocation& src, ld::Fixup::Cluster c, ld::Fixup::Kind k, Atom<A>* target) :
			fixup(src.offsetInAtom, c, k, target), atom(src.atom) { src.atom->incrementFixupCount(); }
			
		FixupInAtom(const SourceLocation& src, ld::Fixup::Cluster c, ld::Fixup::Kind k, ld::Fixup::TargetBinding b, Atom<A>* target) :
			fixup(src.offsetInAtom, c, k, b, target), atom(src.atom) { src.atom->incrementFixupCount(); }
			
		FixupInAtom(const SourceLocation& src, ld::Fixup::Cluster c, ld::Fixup::Kind k, bool wi, const char* name) :
			fixup(src.offsetInAtom, c, k, wi, name), atom(src.atom) { src.atom->incrementFixupCount(); }
					
		FixupInAtom(const SourceLocation& src, ld::Fixup::Cluster c, ld::Fixup::Kind k, ld::Fixup::TargetBinding b, const char* name) :
			fixup(src.offsetInAtom, c, k, b, name), atom(src.atom) { src.atom->incrementFixupCount(); }
					
		FixupInAtom(const SourceLocation& src, ld::Fixup::Cluster c, ld::Fixup::Kind k, uint64_t addend) :
			fixup(src.offsetInAtom, c, k, addend), atom(src.atom) { src.atom->incrementFixupCount(); }

		FixupInAtom(const SourceLocation& src, ld::Fixup::Cluster c, ld::Fixup::Kind k) :
			fixup(src.offsetInAtom, c, k, (uint64_t)0), atom(src.atom) { src.atom->incrementFixupCount(); }

		ld::Fixup		fixup;
		Atom<A>*		atom;
	};

	void addFixup(const SourceLocation& src, ld::Fixup::Cluster c, ld::Fixup::Kind k, Atom<A>* target) { 
		_allFixups.push_back(FixupInAtom(src, c, k, target)); 
	}
	
	void addFixup(const SourceLocation& src, ld::Fixup::Cluster c, ld::Fixup::Kind k, ld::Fixup::TargetBinding b, Atom<A>* target) { 
		_allFixups.push_back(FixupInAtom(src, c, k, b, target)); 
	}
	
	void addFixup(const SourceLocation& src, ld::Fixup::Cluster c, ld::Fixup::Kind k, bool wi, const char* name) { 
		_allFixups.push_back(FixupInAtom(src, c, k, wi, name)); 
	}
	
	void addFixup(const SourceLocation& src, ld::Fixup::Cluster c, ld::Fixup::Kind k, ld::Fixup::TargetBinding b, const char* name) { 
		_allFixups.push_back(FixupInAtom(src, c, k, b, name)); 
	}
	
	void addFixup(const SourceLocation& src, ld::Fixup::Cluster c, ld::Fixup::Kind k, uint64_t addend) { 
		_allFixups.push_back(FixupInAtom(src, c, k, addend)); 
	}

	void addFixup(const SourceLocation& src, ld::Fixup::Cluster c, ld::Fixup::Kind k) { 
		_allFixups.push_back(FixupInAtom(src, c, k)); 
	}


	uint32_t										symbolCount() { return _symbolCount; }
	uint32_t										indirectSymbol(uint32_t indirectIndex);
	const macho_nlist<P>&							symbolFromIndex(uint32_t index);
	const char*										nameFromSymbol(const macho_nlist<P>& sym);
	ld::Atom::Scope									scopeFromSymbol(const macho_nlist<P>& sym);
	static ld::Atom::Definition						definitionFromSymbol(const macho_nlist<P>& sym);
	static ld::Atom::Combine						combineFromSymbol(const macho_nlist<P>& sym);
			ld::Atom::SymbolTableInclusion			inclusionFromSymbol(const macho_nlist<P>& sym);
	static bool										dontDeadStripFromSymbol(const macho_nlist<P>& sym);
	static bool										isThumbFromSymbol(const macho_nlist<P>& sym);
	static bool										weakImportFromSymbol(const macho_nlist<P>& sym);
	static bool										resolverFromSymbol(const macho_nlist<P>& sym);
	uint32_t										symbolIndexFromIndirectSectionAddress(pint_t,const macho_section<P>*);
	const macho_section<P>*							firstMachOSection() { return _sectionsStart; }
	const macho_section<P>*							machOSectionFromSectionIndex(uint32_t index);
	uint32_t										machOSectionCount() { return _machOSectionsCount; }
	uint32_t										undefinedStartIndex() { return _undefinedStartIndex; }
	uint32_t										undefinedEndIndex() { return _undefinedEndIndex; }
	void											addFixup(FixupInAtom f) { _allFixups.push_back(f); }
	Section<A>*										sectionForNum(unsigned int sectNum);
	Section<A>*										sectionForAddress(pint_t addr);
	Atom<A>*										findAtomByAddress(pint_t addr);
	Atom<A>*										findAtomByAddressOrNullIfStub(pint_t addr);
	Atom<A>*										findAtomByAddressOrLocalTargetOfStub(pint_t addr, uint32_t* offsetInAtom);
	Atom<A>*										findAtomByName(const char* name);	// slow!
	void											findTargetFromAddress(pint_t addr, TargetDesc& target);
	void											findTargetFromAddress(pint_t baseAddr, pint_t addr, TargetDesc& target);
	void											findTargetFromAddressAndSectionNum(pint_t addr, unsigned int sectNum,
																						TargetDesc& target);
	uint32_t										tentativeDefinitionCount() { return _tentativeDefinitionCount; }
	uint32_t										absoluteSymbolCount() { return _absoluteSymbolCount; }
	
	bool											hasStubsSection() { return (_stubsSectionNum != 0); }
	unsigned int									stubsSectionNum() { return _stubsSectionNum; }
	void											addDtraceExtraInfos(const SourceLocation& src, const char* provider);
	const char*										scanSymbolTableForAddress(uint64_t addr);
	bool											convertUnwindInfo() { return _convertUnwindInfo; }
	bool											hasDataInCodeLabels() { return _hasDataInCodeLabels; }

	macho_data_in_code_entry<P>*					dataInCodeStart() { return _dataInCodeStart; }
	macho_data_in_code_entry<P>*					dataInCodeEnd()   { return _dataInCodeEnd; }
	
	void							addFixups(const SourceLocation& src, ld::Fixup::Kind kind, const TargetDesc& target);
	void							addFixups(const SourceLocation& src, ld::Fixup::Kind kind, const TargetDesc& target, const TargetDesc& picBase);
	


	struct LabelAndCFIBreakIterator {
		typedef typename CFISection<A>::CFI_Atom_Info CFI_Atom_Info;
								LabelAndCFIBreakIterator(const uint32_t* ssa, uint32_t ssc, const pint_t* cfisa, 
														uint32_t cfisc, bool ols)
									: sortedSymbolIndexes(ssa), sortedSymbolCount(ssc), cfiStartsArray(cfisa), 
										cfiStartsCount(cfisc), fileHasOverlappingSymbols(ols),
										newSection(false), cfiIndex(0), symIndex(0) {}
		bool					next(Parser<A>& parser, uint32_t sectNum, pint_t startAddr, pint_t endAddr, 
										pint_t* addr, pint_t* size, const macho_nlist<P>** sym);
		pint_t					peek(Parser<A>& parser, pint_t startAddr, pint_t endAddr);
		void					beginSection() { newSection = true; symIndex = 0; }
		
		const uint32_t* const		sortedSymbolIndexes;
		const uint32_t				sortedSymbolCount;
		const pint_t*				cfiStartsArray;
		const uint32_t				cfiStartsCount;
		const bool					fileHasOverlappingSymbols;
		bool						newSection;
		uint32_t					cfiIndex;
		uint32_t					symIndex;
	};

	struct CFI_CU_InfoArrays {
			typedef typename CFISection<A>::CFI_Atom_Info CFI_Atom_Info;
			typedef typename CUSection<A>::Info CU_Info;
						CFI_CU_InfoArrays(const CFI_Atom_Info* cfiAr, uint32_t cfiC, CU_Info* cuAr, uint32_t cuC) 
							: cfiArray(cfiAr), cuArray(cuAr), cfiCount(cfiC), cuCount(cuC) {} 
		const CFI_Atom_Info* const	cfiArray;
			CU_Info* const			cuArray;
		const uint32_t				cfiCount;
		const uint32_t				cuCount;
	};



private:
	friend class Section<A>;
	
	enum SectionType { sectionTypeIgnore, sectionTypeLiteral4, sectionTypeLiteral8, sectionTypeLiteral16, 
						sectionTypeNonLazy, sectionTypeCFI, sectionTypeCString, sectionTypeCStringPointer, 
						sectionTypeUTF16Strings, sectionTypeCFString, sectionTypeObjC2ClassRefs, typeObjC2CategoryList,
						sectionTypeObjC1Classes, sectionTypeSymboled, sectionTypeObjC1ClassRefs,
						sectionTypeTentativeDefinitions, sectionTypeAbsoluteSymbols, sectionTypeTLVDefs,
						sectionTypeCompactUnwind };

	template <typename P>
	struct MachOSectionAndSectionClass
	{
		const macho_section<P>* sect;
		SectionType				type;
		
		static int sorter(const void* l, const void* r) {
			const MachOSectionAndSectionClass<P>* left = (MachOSectionAndSectionClass<P>*)l;
			const MachOSectionAndSectionClass<P>* right = (MachOSectionAndSectionClass<P>*)r;
			int64_t diff = left->sect->addr() - right->sect->addr();
			if ( diff == 0 )
				return 0;
			if ( diff < 0 )
				return -1;
			else
				return 1;
		}
	};
	
	struct ParserAndSectionsArray { Parser* parser; const uint32_t* sortedSectionsArray; };
	

													Parser(const uint8_t* fileContent, uint64_t fileLength, 
															const char* path, time_t modTime, 
															ld::File::Ordinal ordinal, bool convertUnwindInfo);
	ld::relocatable::File*							parse(const ParserOptions& opts);
	uint8_t											loadCommandSizeMask();
	bool											parseLoadCommands();
	void											makeSections();
	void											prescanSymbolTable();
	void											makeSortedSymbolsArray(uint32_t symArray[], const uint32_t sectionArray[]);
	void											makeSortedSectionsArray(uint32_t array[]);
	static int										pointerSorter(const void* l, const void* r);
	static int										symbolIndexSorter(void* extra, const void* l, const void* r);
	static int										sectionIndexSorter(void* extra, const void* l, const void* r);

	void											parseDebugInfo();
	void											parseStabs();
	static bool										isConstFunStabs(const char *stabStr);
	bool											read_comp_unit(const char ** name, const char ** comp_dir,
																								uint64_t *stmt_list);
	const char*										getDwarfString(uint64_t form, const uint8_t* p);
	bool											skip_form(const uint8_t ** offset, const uint8_t * end, 
																uint64_t form, uint8_t addr_size, bool dwarf64);
	

	// filled in by constructor
	const uint8_t*								_fileContent;
	uint32_t									_fileLength;
	const char*									_path;
	time_t										_modTime;
	ld::File::Ordinal							_ordinal;
	
	// filled in by parseLoadCommands()
	File<A>*									_file;
	const macho_nlist<P>*						_symbols;
	uint32_t									_symbolCount;
	const char*									_strings;
	uint32_t									_stringsSize;
	const uint32_t*								_indirectTable;
	uint32_t									_indirectTableCount;
	uint32_t									_undefinedStartIndex;
	uint32_t									_undefinedEndIndex;
	const macho_section<P>*						_sectionsStart;
	uint32_t									_machOSectionsCount;
	bool										_hasUUID;
	macho_data_in_code_entry<P>*				_dataInCodeStart;
	macho_data_in_code_entry<P>*				_dataInCodeEnd;
		
	// filled in by parse()
	CFISection<A>*								_EHFrameSection;
	CUSection<A>*								_compactUnwindSection;
	AbsoluteSymbolSection<A>*					_absoluteSection;
	uint32_t									_tentativeDefinitionCount;
	uint32_t									_absoluteSymbolCount;
	uint32_t									_symbolsInSections;
	bool										_hasLongBranchStubs;
	bool										_AppleObjc; // FSF has objc that uses different data layout
	bool										_overlappingSymbols;
	bool										_convertUnwindInfo;
	bool										_hasDataInCodeLabels;
	unsigned int								_stubsSectionNum;
	const macho_section<P>*						_stubsMachOSection;
	std::vector<const char*>					_dtraceProviderInfo;
	std::vector<FixupInAtom>					_allFixups;
};



template <typename A>
Parser<A>::Parser(const uint8_t* fileContent, uint64_t fileLength, const char* path, time_t modTime, 
					ld::File::Ordinal ordinal, bool convertDUI)
		: _fileContent(fileContent), _fileLength(fileLength), _path(path), _modTime(modTime),
			_ordinal(ordinal), _file(NULL),
			_symbols(NULL), _symbolCount(0), _strings(NULL), _stringsSize(0),
			_indirectTable(NULL), _indirectTableCount(0), 
			_undefinedStartIndex(0), _undefinedEndIndex(0), 
			_sectionsStart(NULL), _machOSectionsCount(0), _hasUUID(false), 
			_dataInCodeStart(NULL), _dataInCodeEnd(NULL),
			_EHFrameSection(NULL), _compactUnwindSection(NULL), _absoluteSection(NULL),
			_tentativeDefinitionCount(0), _absoluteSymbolCount(0),
			_symbolsInSections(0), _hasLongBranchStubs(false),  _AppleObjc(false),
			_overlappingSymbols(false), _convertUnwindInfo(convertDUI), _hasDataInCodeLabels(false), 
			_stubsSectionNum(0), _stubsMachOSection(NULL)
{
}


template <>
bool Parser<x86>::validFile(const uint8_t* fileContent, bool, cpu_subtype_t)
{
	const macho_header<P>* header = (const macho_header<P>*)fileContent;
	if ( header->magic() != MH_MAGIC )
		return false;
	if ( header->cputype() != CPU_TYPE_I386 )
		return false;
	if ( header->filetype() != MH_OBJECT )
		return false;
	return true;
}

template <>
bool Parser<x86_64>::validFile(const uint8_t* fileContent, bool, cpu_subtype_t)
{
	const macho_header<P>* header = (const macho_header<P>*)fileContent;
	if ( header->magic() != MH_MAGIC_64 )
		return false;
	if ( header->cputype() != CPU_TYPE_X86_64 )
		return false;
	if ( header->filetype() != MH_OBJECT )
		return false;
	return true;
}

template <>
bool Parser<arm>::validFile(const uint8_t* fileContent, bool subtypeMustMatch, cpu_subtype_t subtype)
{
	const macho_header<P>* header = (const macho_header<P>*)fileContent;
	if ( header->magic() != MH_MAGIC )
		return false;
	if ( header->cputype() != CPU_TYPE_ARM )
		return false;
	if ( header->filetype() != MH_OBJECT )
		return false;
	if ( subtypeMustMatch ) {
		if ( (cpu_subtype_t)header->cpusubtype() == subtype )
			return true;
		// hack until libcc_kext.a is made fat
		if ( header->cpusubtype() == CPU_SUBTYPE_ARM_ALL )
			return true;
		return false;
	}
	return true;
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
	if ( header->magic() != MH_MAGIC )
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


template <typename A>
bool Parser<A>::hasObjC2Categories(const uint8_t* fileContent)
{
	const macho_header<P>* header = (const macho_header<P>*)fileContent;
	const uint32_t cmd_count = header->ncmds();
	const macho_load_command<P>* const cmds = (macho_load_command<P>*)((char*)header + sizeof(macho_header<P>));
	const macho_load_command<P>* const cmdsEnd = (macho_load_command<P>*)((char*)header + sizeof(macho_header<P>) + header->sizeofcmds());
	const macho_load_command<P>* cmd = cmds;
	for (uint32_t i = 0; i < cmd_count; ++i) {
		if ( cmd->cmd() == macho_segment_command<P>::CMD ) {
			const macho_segment_command<P>*	segment = (macho_segment_command<P>*)cmd;
			const macho_section<P>* sectionsStart = (macho_section<P>*)((char*)segment + sizeof(macho_segment_command<P>));
			for (uint32_t si=0; si < segment->nsects(); ++si) {
				const macho_section<P>* sect = &sectionsStart[si];
				if ( (sect->size() > 0) 
					&& (strcmp(sect->sectname(), "__objc_catlist") == 0)
					&& (strcmp(sect->segname(), "__DATA") == 0) ) {
						return true;
				}
			}
		}
		cmd = (const macho_load_command<P>*)(((char*)cmd)+cmd->cmdsize());
		if ( cmd > cmdsEnd )
			throwf("malformed mach-o file, load command #%d is outside size of load commands", i);
	}
	return false;
}


template <typename A>
bool Parser<A>::hasObjC1Categories(const uint8_t* fileContent)
{
	const macho_header<P>* header = (const macho_header<P>*)fileContent;
	const uint32_t cmd_count = header->ncmds();
	const macho_load_command<P>* const cmds = (macho_load_command<P>*)((char*)header + sizeof(macho_header<P>));
	const macho_load_command<P>* const cmdsEnd = (macho_load_command<P>*)((char*)header + sizeof(macho_header<P>) + header->sizeofcmds());
	const macho_load_command<P>* cmd = cmds;
	for (uint32_t i = 0; i < cmd_count; ++i) {
		if ( cmd->cmd() == macho_segment_command<P>::CMD ) {
			const macho_segment_command<P>*	segment = (macho_segment_command<P>*)cmd;
			const macho_section<P>* sectionsStart = (macho_section<P>*)((char*)segment + sizeof(macho_segment_command<P>));
			for (uint32_t si=0; si < segment->nsects(); ++si) {
				const macho_section<P>* sect = &sectionsStart[si];
				if ( (sect->size() > 0) 
					&& (strcmp(sect->sectname(), "__category") == 0)
					&& (strcmp(sect->segname(), "__OBJC") == 0) ) {
						return true;
				}
			}
		}
		cmd = (const macho_load_command<P>*)(((char*)cmd)+cmd->cmdsize());
		if ( cmd > cmdsEnd )
			throwf("malformed mach-o file, load command #%d is outside size of load commands", i);
	}
	return false;
}

template <typename A>
int Parser<A>::pointerSorter(const void* l, const void* r)
{
	// sort references by address 
	const pint_t* left = (pint_t*)l;
	const pint_t* right = (pint_t*)r;
	return (*left - *right);
}

template <typename A>
typename A::P::uint_t Parser<A>::LabelAndCFIBreakIterator::peek(Parser<A>& parser, pint_t startAddr, pint_t endAddr)
{
	pint_t symbolAddr;
	if ( symIndex < sortedSymbolCount )
		symbolAddr = parser.symbolFromIndex(sortedSymbolIndexes[symIndex]).n_value();
	else
		symbolAddr = endAddr;
	pint_t cfiAddr;
	if ( cfiIndex < cfiStartsCount )
		cfiAddr = cfiStartsArray[cfiIndex];
	else
		cfiAddr = endAddr;
	if ( (cfiAddr < symbolAddr) && (cfiAddr >= startAddr) ) {
		if ( cfiAddr <  endAddr )
			return cfiAddr;
		else
			return endAddr;		
	}
	else  {
		if ( symbolAddr <  endAddr )
			return symbolAddr;
		else
			return endAddr;
	}
}

//
// Parses up a section into chunks based on labels and CFI information.
// Each call returns the next chunk address and size, and (if the break
// was becuase of a label, the symbol). Returns false when no more chunks.
//
template <typename A>
bool Parser<A>::LabelAndCFIBreakIterator::next(Parser<A>& parser, uint32_t sectNum, pint_t startAddr, pint_t endAddr, 
												pint_t* addr, pint_t* size, const macho_nlist<P>** symbol)
{
	// may not be a label on start of section, but need atom demarcation there
	if ( newSection ) {
		newSection = false;
		// advance symIndex until we get to the first label at or past the start of this section
		while ( symIndex < sortedSymbolCount ) {
			const macho_nlist<P>& sym = parser.symbolFromIndex(sortedSymbolIndexes[symIndex]);
			pint_t nextSymbolAddr = sym.n_value();
			//fprintf(stderr, "sectNum=%d, nextSymbolAddr=0x%08llX, name=%s\n", sectNum, (uint64_t)nextSymbolAddr, parser.nameFromSymbol(sym));
			if ( (nextSymbolAddr > startAddr) || ((nextSymbolAddr == startAddr) && (sym.n_sect() == sectNum)) )
				break;
			++symIndex;
		}
		if ( symIndex < sortedSymbolCount ) {
			const macho_nlist<P>& sym = parser.symbolFromIndex(sortedSymbolIndexes[symIndex]);
			pint_t nextSymbolAddr = sym.n_value();
			// if next symbol found is not in this section
			if ( sym.n_sect() != sectNum ) {
				// check for CFI break instead of symbol break
				if ( cfiIndex < cfiStartsCount ) {
					pint_t nextCfiAddr = cfiStartsArray[cfiIndex];
					if ( nextCfiAddr < endAddr ) {
						// use cfi
						++cfiIndex;
						*addr = nextCfiAddr;
						*size = peek(parser, startAddr, endAddr) - nextCfiAddr;
						*symbol = NULL;
						return true;
					}
				}
				*addr = startAddr;
				*size = endAddr - startAddr;
				*symbol = NULL;
				if ( startAddr == endAddr )
					return false;  // zero size section
				else
					return true;  // whole section is one atom with no label
			}
			// if also CFI break here, eat it
			if ( cfiIndex < cfiStartsCount ) {
				if ( cfiStartsArray[cfiIndex] == nextSymbolAddr )
					++cfiIndex;
			}
			if ( nextSymbolAddr == startAddr ) {
				// label at start of section, return it as chunk
				++symIndex;
				*addr = startAddr;
				*size = peek(parser, startAddr, endAddr) - startAddr;
				*symbol = &sym;
				return true;
			}
			// return chunk before first symbol
			*addr = startAddr;
			*size = nextSymbolAddr - startAddr;
			*symbol = NULL;
			return true;
		}
		// no symbols left in whole file, so entire section is one chunk
		*addr = startAddr;
		*size = endAddr - startAddr;
		*symbol = NULL;
		if ( startAddr == endAddr )
			return false;  // zero size section
		else
			return true;  // whole section is one atom with no label
	}

	while ( (symIndex < sortedSymbolCount) && (cfiIndex < cfiStartsCount) ) {
		const macho_nlist<P>& sym = parser.symbolFromIndex(sortedSymbolIndexes[symIndex]);
		pint_t nextSymbolAddr = sym.n_value();
		pint_t nextCfiAddr = cfiStartsArray[cfiIndex];
		if ( nextSymbolAddr <  nextCfiAddr ) {
			if ( nextSymbolAddr >= endAddr )
				return false;
			++symIndex;
			if ( nextSymbolAddr < startAddr )
				continue;
			*addr = nextSymbolAddr;
			*size = peek(parser, startAddr, endAddr) - nextSymbolAddr;
			*symbol = &sym;
			return true;
		}
		else if ( nextCfiAddr < nextSymbolAddr ) { 
			if ( nextCfiAddr >= endAddr )
				return false;
			++cfiIndex;
			if ( nextCfiAddr < startAddr )
				continue;
			*addr = nextCfiAddr;
			*size = peek(parser, startAddr, endAddr) - nextCfiAddr;
			*symbol = NULL;
			return true;
		}
		else {
			if ( nextCfiAddr >= endAddr )
				return false;
			++symIndex;
			++cfiIndex;
			if ( nextCfiAddr < startAddr )
				continue;
			*addr = nextCfiAddr;
			*size = peek(parser, startAddr, endAddr) - nextCfiAddr;
			*symbol = &sym;
			return true;
		}
	}
	while ( symIndex < sortedSymbolCount ) {
		const macho_nlist<P>& sym = parser.symbolFromIndex(sortedSymbolIndexes[symIndex]);
		pint_t nextSymbolAddr = sym.n_value();
		// if next symbol found is not in this section, then done with iteration
		if ( sym.n_sect() != sectNum ) 
			return false;
		++symIndex;
		if ( nextSymbolAddr < startAddr )
			continue;
		*addr = nextSymbolAddr;
		*size = peek(parser, startAddr, endAddr) - nextSymbolAddr;
		*symbol = &sym;
		return true;
	}
	while ( cfiIndex < cfiStartsCount ) {
		pint_t nextCfiAddr = cfiStartsArray[cfiIndex];
		if ( nextCfiAddr >= endAddr )
			return false;
		++cfiIndex;
		if ( nextCfiAddr < startAddr )
			continue;
		*addr = nextCfiAddr;
		*size = peek(parser, startAddr, endAddr) - nextCfiAddr;
		*symbol = NULL;
		return true;
	}
	return false;
}



template <typename A>
ld::relocatable::File* Parser<A>::parse(const ParserOptions& opts)
{
	// create file object
	_file = new File<A>(_path, _modTime, _fileContent, _ordinal);

	// respond to -t option
	if ( opts.logAllFiles )
		printf("%s\n", _path);

	// parse start of mach-o file
	if ( ! parseLoadCommands() )
		return _file;
	
	// make array of 
	uint32_t sortedSectionIndexes[_machOSectionsCount];
	this->makeSortedSectionsArray(sortedSectionIndexes);
	
	// make symbol table sorted by address
	this->prescanSymbolTable();
	uint32_t sortedSymbolIndexes[_symbolsInSections];
	this->makeSortedSymbolsArray(sortedSymbolIndexes, sortedSectionIndexes);
		
	// allocate Section<A> object for each mach-o section
	makeSections();
	
	// if it exists, do special early parsing of __compact_unwind section
	uint32_t countOfCUs = 0;
	if ( _compactUnwindSection != NULL )
		countOfCUs = _compactUnwindSection->count();
	uint8_t cuInfoBuffer[sizeof(typename CUSection<A>::Info) * countOfCUs];
	typename CUSection<A>::Info*  cuInfoArray = (typename CUSection<A>::Info*)cuInfoBuffer;
	if ( countOfCUs != 0 )
		_compactUnwindSection->parse(*this, countOfCUs, cuInfoArray);
	
	// if it exists, do special early parsing of __eh_frame section 
	// stack allocate array of CFI_Atom_Info
	uint32_t countOfCFIs = 0;
	if ( _EHFrameSection != NULL )
		countOfCFIs = _EHFrameSection->cfiCount();
	typename CFISection<A>::CFI_Atom_Info  cfiArray[countOfCFIs];
	// stack allocate (if not too large) a copy of __eh_frame to apply relocations to
	uint8_t* ehBuffer = NULL;
	uint32_t stackAllocSize = 0;
	if ( (countOfCFIs != 0) && _EHFrameSection->needsRelocating() ) {
		uint32_t sectSize = _EHFrameSection->machoSection()->size();
		if ( sectSize > 50*1024 )
			ehBuffer = (uint8_t*)malloc(sectSize);
		else
			stackAllocSize = sectSize;
	}
	uint32_t ehStackBuffer[1+stackAllocSize/4]; // make 4-byte aligned stack bufffer
	if ( ehBuffer == NULL )
		ehBuffer = (uint8_t*)&ehStackBuffer;
	uint32_t cfiStartsCount = 0;
	if ( countOfCFIs != 0 ) {
		_EHFrameSection->cfiParse(*this, ehBuffer, cfiArray, countOfCFIs);
		// count functions and lsdas
		for(uint32_t i=0; i < countOfCFIs; ++i) {
			if ( cfiArray[i].isCIE )
				continue;
			//fprintf(stderr, "cfiArray[i].func = 0x%08llX, cfiArray[i].lsda = 0x%08llX, encoding=0x%08X\n", 
			//			(uint64_t)cfiArray[i].u.fdeInfo.function.targetAddress, 
			//			(uint64_t)cfiArray[i].u.fdeInfo.lsda.targetAddress, 
			//			cfiArray[i].u.fdeInfo.compactUnwindInfo);
			if ( cfiArray[i].u.fdeInfo.function.targetAddress != CFI_INVALID_ADDRESS )
				++cfiStartsCount;
			if ( cfiArray[i].u.fdeInfo.lsda.targetAddress != CFI_INVALID_ADDRESS )
				++cfiStartsCount;
		}
	}
	CFI_CU_InfoArrays cfis(cfiArray, countOfCFIs, cuInfoArray, countOfCUs);
	
	// create sorted array of function starts and lsda starts
	pint_t cfiStartsArray[cfiStartsCount];
	uint32_t countOfFDEs = 0;
	if ( countOfCFIs != 0 ) {
		int index = 0;
		for(uint32_t i=0; i < countOfCFIs; ++i) {
			if ( cfiArray[i].isCIE )
				continue;
			if ( cfiArray[i].u.fdeInfo.function.targetAddress != CFI_INVALID_ADDRESS )
				cfiStartsArray[index++] = cfiArray[i].u.fdeInfo.function.targetAddress;
			if ( cfiArray[i].u.fdeInfo.lsda.targetAddress != CFI_INVALID_ADDRESS )
				cfiStartsArray[index++] = cfiArray[i].u.fdeInfo.lsda.targetAddress;
			++countOfFDEs;
		}
		::qsort(cfiStartsArray, cfiStartsCount, sizeof(pint_t), pointerSorter);
	#ifndef NDEBUG
		// scan for FDEs claming the same function
		for(int i=1; i < index; ++i) {
			assert( cfiStartsArray[i] != cfiStartsArray[i-1] );
		}
	#endif	
	}
	
	Section<A>** sections = _file->_sectionsArray;
	uint32_t	sectionsCount = _file->_sectionsArrayCount;

	// figure out how many atoms will be allocated and allocate
	LabelAndCFIBreakIterator breakIterator(sortedSymbolIndexes, _symbolsInSections, cfiStartsArray, 
											cfiStartsCount, _overlappingSymbols);
	uint32_t computedAtomCount = 0;
	for (uint32_t i=0; i < sectionsCount; ++i ) {
		breakIterator.beginSection();
		uint32_t count = sections[i]->computeAtomCount(*this, breakIterator, cfis);
		//const macho_section<P>* sect = sections[i]->machoSection();
		//fprintf(stderr, "computed count=%u for section %s size=%llu\n", count, sect->sectname(), (sect != NULL) ? sect->size() : 0);
		computedAtomCount += count;
	}
	//fprintf(stderr, "allocating %d atoms * sizeof(Atom<A>)=%ld, sizeof(ld::Atom)=%ld\n", computedAtomCount, sizeof(Atom<A>), sizeof(ld::Atom));
	_file->_atomsArray = new uint8_t[computedAtomCount*sizeof(Atom<A>)];
	_file->_atomsArrayCount = 0;
	
	// have each section append atoms to _atomsArray
	LabelAndCFIBreakIterator breakIterator2(sortedSymbolIndexes, _symbolsInSections, cfiStartsArray, 
												cfiStartsCount, _overlappingSymbols);
	for (uint32_t i=0; i < sectionsCount; ++i ) {
		uint8_t* atoms = _file->_atomsArray + _file->_atomsArrayCount*sizeof(Atom<A>);
		breakIterator2.beginSection();
		uint32_t count = sections[i]->appendAtoms(*this, atoms, breakIterator2, cfis);
		//fprintf(stderr, "append count=%u for section %s/%s\n", count, sections[i]->machoSection()->segname(), sections[i]->machoSection()->sectname());
		_file->_atomsArrayCount += count;
	}
	assert( _file->_atomsArrayCount == computedAtomCount && "more atoms allocated than expected");

	
	// have each section add all fix-ups for its atoms
	_allFixups.reserve(computedAtomCount*5);
	for (uint32_t i=0; i < sectionsCount; ++i )
		sections[i]->makeFixups(*this, cfis);
	
	// assign fixups start offset for each atom
	uint8_t* p = _file->_atomsArray;
	uint32_t fixupOffset = 0;
	for(int i=_file->_atomsArrayCount; i > 0; --i) {
		Atom<A>* atom = (Atom<A>*)p;
		atom->_fixupsStartIndex = fixupOffset;
		fixupOffset += atom->_fixupsCount;
		atom->_fixupsCount = 0;
		p += sizeof(Atom<A>);
	}
	assert(fixupOffset == _allFixups.size());
	_file->_fixups.reserve(fixupOffset);
	
	// copy each fixup for each atom 
	for(typename std::vector<FixupInAtom>::iterator it=_allFixups.begin(); it != _allFixups.end(); ++it) {
		uint32_t slot = it->atom->_fixupsStartIndex + it->atom->_fixupsCount;
		_file->_fixups[slot] = it->fixup;
		it->atom->_fixupsCount++;
	}
	
	// done with temp vector
	_allFixups.clear();

	// add unwind info
	_file->_unwindInfos.reserve(countOfFDEs+countOfCUs);
	for(uint32_t i=0; i < countOfCFIs; ++i) {
		if ( cfiArray[i].isCIE )
			continue;
		if ( cfiArray[i].u.fdeInfo.function.targetAddress != CFI_INVALID_ADDRESS ) {
			ld::Atom::UnwindInfo info;
			info.startOffset = 0;
			info.unwindInfo = cfiArray[i].u.fdeInfo.compactUnwindInfo;
			_file->_unwindInfos.push_back(info);
			Atom<A>* func = findAtomByAddress(cfiArray[i].u.fdeInfo.function.targetAddress);
			func->setUnwindInfoRange(_file->_unwindInfos.size()-1, 1);
		}
	}
	// apply compact infos in __LD,__compact_unwind section to each function
	// if function also has dwarf unwind, CU will override it
	Atom<A>* lastFunc = NULL;
	uint32_t lastEnd = 0;
	for(uint32_t i=0; i < countOfCUs; ++i) {
		typename CUSection<A>::Info* info = &cuInfoArray[i];
		assert(info->function != NULL);
		ld::Atom::UnwindInfo ui;
		ui.startOffset = info->functionStartAddress - info->function->objectAddress();
		ui.unwindInfo = info->compactUnwindInfo;		
		_file->_unwindInfos.push_back(ui);
		// if previous is for same function, extend range
		if ( info->function == lastFunc ) {
			if ( lastEnd != ui.startOffset ) {
				if ( lastEnd < ui.startOffset )
					warning("__LD,__compact_unwind entries for %s have a gap at offset 0x%0X", info->function->name(), lastEnd);
				else
					warning("__LD,__compact_unwind entries for %s overlap at offset 0x%0X", info->function->name(), lastEnd);
			}
			lastFunc->extendUnwindInfoRange();
		}
		else 
			info->function->setUnwindInfoRange(_file->_unwindInfos.size()-1, 1);
		lastFunc = info->function;
		lastEnd = ui.startOffset + info->rangeLength;
	}
	
	// parse dwarf debug info to get line info
	this->parseDebugInfo();

	return _file;
}



template <> uint8_t Parser<x86>::loadCommandSizeMask()		{ return 0x03; }
template <> uint8_t Parser<x86_64>::loadCommandSizeMask()	{ return 0x07; }
template <> uint8_t Parser<arm>::loadCommandSizeMask()		{ return 0x03; }

template <typename A>
bool Parser<A>::parseLoadCommands()
{
	const macho_header<P>* header = (const macho_header<P>*)_fileContent;

	// set File attributes
	_file->_canScatterAtoms = (header->flags() & MH_SUBSECTIONS_VIA_SYMBOLS);
	_file->_cpuSubType = header->cpusubtype();
	
	const macho_segment_command<P>*	segment = NULL;
	const uint8_t* const endOfFile = _fileContent + _fileLength;
	const uint32_t cmd_count = header->ncmds();
	// <rdar://problem/5394172> an empty .o file with zero load commands will crash linker
	if ( cmd_count == 0 )
		return false;
	const macho_load_command<P>* const cmds = (macho_load_command<P>*)((char*)header + sizeof(macho_header<P>));
	const macho_load_command<P>* const cmdsEnd = (macho_load_command<P>*)((char*)header + sizeof(macho_header<P>) + header->sizeofcmds());
	const macho_load_command<P>* cmd = cmds;
	for (uint32_t i = 0; i < cmd_count; ++i) {
		uint32_t size = cmd->cmdsize();
		if ( (size & this->loadCommandSizeMask()) != 0 )
			throwf("load command #%d has a unaligned size", i);
		const uint8_t* endOfCmd = ((uint8_t*)cmd)+cmd->cmdsize();
		if ( endOfCmd > (uint8_t*)cmdsEnd )
			throwf("load command #%d extends beyond the end of the load commands", i);
		if ( endOfCmd > endOfFile )
			throwf("load command #%d extends beyond the end of the file", i);
		switch (cmd->cmd()) {
		    case LC_SYMTAB:
				{
					const macho_symtab_command<P>* symtab = (macho_symtab_command<P>*)cmd;
					_symbolCount = symtab->nsyms();
					_symbols = (const macho_nlist<P>*)(_fileContent + symtab->symoff());
					_strings = (char*)_fileContent + symtab->stroff();
					_stringsSize = symtab->strsize();
					if ( (symtab->symoff() + _symbolCount*sizeof(macho_nlist<P>)) > _fileLength )
						throw "mach-o symbol table extends beyond end of file";
					if ( (_strings + _stringsSize) > (char*)endOfFile )
						throw "mach-o string pool extends beyond end of file";
					if ( _indirectTable == NULL ) {
						if ( _undefinedEndIndex == 0 ) {
							_undefinedStartIndex = 0;
							_undefinedEndIndex = symtab->nsyms();
						}
					}
				}
				break;
			case LC_DYSYMTAB:
				{
					const macho_dysymtab_command<P>* dsymtab = (macho_dysymtab_command<P>*)cmd;
					_indirectTable = (uint32_t*)(_fileContent + dsymtab->indirectsymoff());
					_indirectTableCount = dsymtab->nindirectsyms();
					if ( &_indirectTable[_indirectTableCount] > (uint32_t*)endOfFile )
						throw "indirect symbol table extends beyond end of file";
					_undefinedStartIndex = dsymtab->iundefsym();
					_undefinedEndIndex = _undefinedStartIndex + dsymtab->nundefsym();
				}
				break;
		    case LC_UUID:
				_hasUUID = true;
				break;
			case LC_DATA_IN_CODE:
				{
					const macho_linkedit_data_command<P>* dc = (macho_linkedit_data_command<P>*)cmd;
					_dataInCodeStart = (macho_data_in_code_entry<P>*)(_fileContent + dc->dataoff());
					_dataInCodeEnd = (macho_data_in_code_entry<P>*)(_fileContent + dc->dataoff() + dc->datasize());
					if ( _dataInCodeEnd > (macho_data_in_code_entry<P>*)endOfFile )
						throw "LC_DATA_IN_CODE table extends beyond end of file";
				}
			default:
				if ( cmd->cmd() == macho_segment_command<P>::CMD ) {
					if ( segment != NULL )
						throw "more than one LC_SEGMENT found in object file";
					segment = (macho_segment_command<P>*)cmd;
				}
				break;
		}
		cmd = (const macho_load_command<P>*)(((char*)cmd)+cmd->cmdsize());
		if ( cmd > cmdsEnd )
			throwf("malformed mach-o file, load command #%d is outside size of load commands", i);
	}

	// record range of sections
	if ( segment == NULL ) 
		throw "missing LC_SEGMENT";
	_sectionsStart = (macho_section<P>*)((char*)segment + sizeof(macho_segment_command<P>));
	_machOSectionsCount = segment->nsects();
	
	return true;
}


template <typename A>
void Parser<A>::prescanSymbolTable()
{
	_tentativeDefinitionCount = 0;
	_absoluteSymbolCount = 0;
	_symbolsInSections = 0;
	_hasDataInCodeLabels = false;
	for (uint32_t i=0; i < this->_symbolCount; ++i) {
		const macho_nlist<P>& sym =	symbolFromIndex(i);
		// ignore stabs
		if ( (sym.n_type() & N_STAB) != 0 )
			continue;
			
		// look at undefines
		const char* symbolName = this->nameFromSymbol(sym);
		if ( (sym.n_type() & N_TYPE) == N_UNDF ) {
			if ( sym.n_value() != 0 ) {
				// count tentative definitions
				++_tentativeDefinitionCount;
			}
			else if ( strncmp(symbolName, "___dtrace_", 10) == 0 ) {
				// any undefined starting with __dtrace_*$ that is not ___dtrace_probe$* or ___dtrace_isenabled$*
				// is extra provider info
				if ( (strncmp(&symbolName[10], "probe$", 6) != 0) && (strncmp(&symbolName[10], "isenabled$", 10) != 0) ) {
					_dtraceProviderInfo.push_back(symbolName);
				}
			}
			continue;
		}
						
		// count absolute symbols
		if ( (sym.n_type() & N_TYPE) == N_ABS ) {
			const char* absName = this->nameFromSymbol(sym);
			// ignore .objc_class_name_* symbols 
			if ( strncmp(absName, ".objc_class_name_", 17) == 0 ) {
				_AppleObjc = true;
				continue;
			}
			// ignore .objc_class_name_* symbols 
			if ( strncmp(absName, ".objc_category_name_", 20) == 0 )
				continue;
			// ignore empty *.eh symbols
			if ( strcmp(&absName[strlen(absName)-3], ".eh") == 0 )
				continue;
			++_absoluteSymbolCount;
		}
		
		// only look at definitions
		if ( (sym.n_type() & N_TYPE) != N_SECT )
			continue;
		
		// 'L' labels do not denote atom breaks
		if ( symbolName[0] == 'L' ) {
			// <rdar://problem/9218847> Formalize data in code with L$start$ labels
			if ( strncmp(symbolName, "L$start$", 8) == 0 ) 
				_hasDataInCodeLabels = true;
			continue;
		}
		// how many def syms in each section
		if ( sym.n_sect() > _machOSectionsCount )
			throw "bad n_sect in symbol table";
			
		_symbolsInSections++;
	}
}

template <typename A>
int Parser<A>::sectionIndexSorter(void* extra, const void* l, const void* r)
{
	Parser<A>* parser = (Parser<A>*)extra;
	const uint32_t* left = (uint32_t*)l;
	const uint32_t* right = (uint32_t*)r;
	const macho_section<P>* leftSect =	parser->machOSectionFromSectionIndex(*left);
	const macho_section<P>* rightSect = parser->machOSectionFromSectionIndex(*right);
	
	// can't just return difference because 64-bit diff does not fit in 32-bit return type
	int64_t result = leftSect->addr() - rightSect->addr();
	if ( result == 0 ) {
		// two sections with same start address
		// one with zero size goes first
		bool leftEmpty = ( leftSect->size() == 0 );
		bool rightEmpty = ( rightSect->size() == 0 );
		if ( leftEmpty != rightEmpty ) {
			return ( rightEmpty ? 1 : -1 );
		}
		if ( !leftEmpty && !rightEmpty )
			throwf("overlapping sections");
		// both empty, so chose file order
		return ( rightSect - leftSect );
	}
	else if ( result < 0 )
		return -1;
	else
		return 1;
}

template <typename A>
void Parser<A>::makeSortedSectionsArray(uint32_t array[])
{
	const bool log = false;
	
	if ( log ) {
		fprintf(stderr, "unsorted sections:\n");
		for(unsigned int i=0; i < _machOSectionsCount; ++i ) 
			fprintf(stderr, "0x%08llX %s %s\n", _sectionsStart[i].addr(), _sectionsStart[i].segname(), _sectionsStart[i].sectname());
 	}
	
	// sort by symbol table address
	for (uint32_t i=0; i < _machOSectionsCount; ++i)
		array[i] = i;
	::qsort_r_local(array, _machOSectionsCount, sizeof(uint32_t), this, &sectionIndexSorter);

	if ( log ) {
		fprintf(stderr, "sorted sections:\n");
		for(unsigned int i=0; i < _machOSectionsCount; ++i ) 
			fprintf(stderr, "0x%08llX %s %s\n", _sectionsStart[array[i]].addr(), _sectionsStart[array[i]].segname(), _sectionsStart[array[i]].sectname());
	}
}



template <typename A>
int Parser<A>::symbolIndexSorter(void* extra, const void* l, const void* r)
{
	ParserAndSectionsArray* extraInfo = (ParserAndSectionsArray*)extra;
	Parser<A>* parser = extraInfo->parser;
	const uint32_t* sortedSectionsArray = extraInfo->sortedSectionsArray;
	const uint32_t* left = (uint32_t*)l;
	const uint32_t* right = (uint32_t*)r;
	const macho_nlist<P>& leftSym =	parser->symbolFromIndex(*left);
	const macho_nlist<P>& rightSym = parser->symbolFromIndex(*right);
	// can't just return difference because 64-bit diff does not fit in 32-bit return type
	int64_t result = leftSym.n_value() - rightSym.n_value();
	if ( result == 0 ) {
		// two symbols with same address
		// if in different sections, sort earlier section first
		if ( leftSym.n_sect() != rightSym.n_sect() ) {
			for (uint32_t i=0; i < parser->machOSectionCount(); ++i) {
				if ( sortedSectionsArray[i]+1 == leftSym.n_sect() )
					return -1;
				if ( sortedSectionsArray[i]+1 == rightSym.n_sect() )
					return 1;
			}
		}
		// two symbols in same section, means one is an alias
		// if only one is global, make the other an alias (sort first)
		if ( (leftSym.n_type() & N_EXT) != (rightSym.n_type() & N_EXT) ) {
			if ( (rightSym.n_type() & N_EXT) != 0 )
				return -1;
			else
				return 1;
		}
		// if both are global, make alphabetically last one be the alias
		return ( strcmp(parser->nameFromSymbol(rightSym), parser->nameFromSymbol(leftSym)) );
	}
	else if ( result < 0 )
		return -1;
	else
		return 1;
}


template <typename A>
void Parser<A>::makeSortedSymbolsArray(uint32_t array[], const uint32_t sectionArray[])
{
	const bool log = false;
	
	uint32_t* p = array;
	for (uint32_t i=0; i < this->_symbolCount; ++i) {
		const macho_nlist<P>& sym =	symbolFromIndex(i);
		// ignore stabs
		if ( (sym.n_type() & N_STAB) != 0 )
			continue;
		
		// only look at definitions
		if ( (sym.n_type() & N_TYPE) != N_SECT )
			continue;
		
		// 'L' labels do not denote atom breaks
		const char* symbolName = this->nameFromSymbol(sym);
		if ( symbolName[0] == 'L' )
			continue;

		// how many def syms in each section
		if ( sym.n_sect() > _machOSectionsCount )
			throw "bad n_sect in symbol table";
			
		// append to array
		*p++ = i;
	}
	assert(p == &array[_symbolsInSections] && "second pass over symbol table yield a different number of symbols");
	
	// sort by symbol table address
	ParserAndSectionsArray extra = { this, sectionArray };
	::qsort_r_local(array, _symbolsInSections, sizeof(uint32_t), &extra, &symbolIndexSorter);
	
	// look for two symbols at same address
	_overlappingSymbols = false;
	for (unsigned int i=1; i < _symbolsInSections; ++i) {
		if ( symbolFromIndex(array[i-1]).n_value() == symbolFromIndex(array[i]).n_value() ) {
			//fprintf(stderr, "overlapping symbols at 0x%08llX\n", symbolFromIndex(array[i-1]).n_value());
			_overlappingSymbols = true;
		}
	}

	if ( log ) {
		fprintf(stderr, "sorted symbols:\n");
		for(unsigned int i=0; i < _symbolsInSections; ++i ) 
			fprintf(stderr, "0x%09llX symIndex=%d sectNum=%2d, %s\n", symbolFromIndex(array[i]).n_value(), array[i], symbolFromIndex(array[i]).n_sect(), nameFromSymbol(symbolFromIndex(array[i])) );
	}
}


template <typename A>
void Parser<A>::makeSections()
{
	// classify each section by type
	// compute how many Section objects will be needed and total size for all
	unsigned int totalSectionsSize = 0;
	uint8_t machOSectsStorage[sizeof(MachOSectionAndSectionClass<P>)*(_machOSectionsCount+2)]; // also room for tentative-defs and absolute symbols
	// allocate raw storage for all section objects on stack
	MachOSectionAndSectionClass<P>* machOSects = (MachOSectionAndSectionClass<P>*)machOSectsStorage;
	unsigned int count = 0;
	for (uint32_t i=0; i < _machOSectionsCount; ++i) {
		const macho_section<P>* sect = &_sectionsStart[i];
		if ( (sect->flags() & S_ATTR_DEBUG) != 0 ) {
			if ( strcmp(sect->segname(), "__DWARF") == 0 ) {
				// note that .o file has dwarf
				_file->_debugInfoKind = ld::relocatable::File::kDebugInfoDwarf;
				// save off iteresting dwarf sections
				if ( strcmp(sect->sectname(), "__debug_info") == 0 )
					_file->_dwarfDebugInfoSect = sect;
				else if ( strcmp(sect->sectname(), "__debug_abbrev") == 0 )
					_file->_dwarfDebugAbbrevSect = sect;
				else if ( strcmp(sect->sectname(), "__debug_line") == 0 )
					_file->_dwarfDebugLineSect = sect;
				else if ( strcmp(sect->sectname(), "__debug_str") == 0 )
					_file->_dwarfDebugStringSect = sect;
				// linker does not propagate dwarf sections to output file
				continue;
			}
			else if ( strcmp(sect->segname(), "__LD") == 0 ) {
				if ( strncmp(sect->sectname(), "__compact_unwind", 16) == 0 ) {
					machOSects[count].sect = sect;
					totalSectionsSize += sizeof(CUSection<A>);
					machOSects[count++].type = sectionTypeCompactUnwind;
					continue;
				}
			}
		}
		// ignore empty __OBJC sections
		if ( (sect->size() == 0) && (strcmp(sect->segname(), "__OBJC") == 0) )
			continue;
		// objc image info section is really attributes and not content
		if ( ((strcmp(sect->sectname(), "__image_info") == 0) && (strcmp(sect->segname(), "__OBJC") == 0))
			|| ((strncmp(sect->sectname(), "__objc_imageinfo", 16) == 0) && (strcmp(sect->segname(), "__DATA") == 0)) ) {
			//	struct objc_image_info  {
			//		uint32_t	version;	// initially 0
			//		uint32_t	flags;
			//	};
			// #define OBJC_IMAGE_SUPPORTS_GC   2
			// #define OBJC_IMAGE_GC_ONLY       4
			//
			const uint32_t* contents = (uint32_t*)(_file->fileContent()+sect->offset());
			if ( (sect->size() >= 8) && (contents[0] == 0) ) {
				uint32_t flags = E::get32(contents[1]);
				if ( (flags & 4) == 4 )
					_file->_objConstraint = ld::File::objcConstraintGC;
				else if ( (flags & 2) == 2 )
					_file->_objConstraint = ld::File::objcConstraintRetainReleaseOrGC;
				else
					_file->_objConstraint = ld::File::objcConstraintRetainRelease;
				if ( sect->size() > 8 ) {
					warning("section %s/%s has unexpectedly large size %llu in %s", 
							sect->segname(), Section<A>::makeSectionName(sect), sect->size(), _file->path());
				}
			}
			else {
				warning("can't parse %s/%s section in %s", sect->segname(), Section<A>::makeSectionName(sect), _file->path());
			}
			continue;
		}
		machOSects[count].sect = sect;
		switch ( sect->flags() & SECTION_TYPE ) {
			case S_SYMBOL_STUBS:
				if ( _stubsSectionNum == 0 ) {
					_stubsSectionNum = i+1;
					_stubsMachOSection = sect;
				}
				else
					assert(1 && "multiple S_SYMBOL_STUBS sections");
			case S_LAZY_SYMBOL_POINTERS:
				break;
			case S_4BYTE_LITERALS:
				totalSectionsSize += sizeof(Literal4Section<A>);
				machOSects[count++].type = sectionTypeLiteral4;
				break;
			case S_8BYTE_LITERALS:
				totalSectionsSize += sizeof(Literal8Section<A>);
				machOSects[count++].type = sectionTypeLiteral8;
				break;
			case S_16BYTE_LITERALS:
				totalSectionsSize += sizeof(Literal16Section<A>);
				machOSects[count++].type = sectionTypeLiteral16;
				break;
			case S_NON_LAZY_SYMBOL_POINTERS:
				totalSectionsSize += sizeof(NonLazyPointerSection<A>);
				machOSects[count++].type = sectionTypeNonLazy;
				break;
			case S_LITERAL_POINTERS:
				if ( (strcmp(sect->segname(), "__OBJC") == 0) && (strcmp(sect->sectname(), "__cls_refs") == 0) ) {
					totalSectionsSize += sizeof(Objc1ClassReferences<A>);
					machOSects[count++].type = sectionTypeObjC1ClassRefs;
				}
				else {
					totalSectionsSize += sizeof(PointerToCStringSection<A>);
					machOSects[count++].type = sectionTypeCStringPointer;
				}
				break;
			case S_CSTRING_LITERALS:
				totalSectionsSize += sizeof(CStringSection<A>);
				machOSects[count++].type = sectionTypeCString;
				break;
			case S_MOD_INIT_FUNC_POINTERS:
			case S_MOD_TERM_FUNC_POINTERS:
			case S_THREAD_LOCAL_INIT_FUNCTION_POINTERS:
			case S_INTERPOSING:
			case S_ZEROFILL:
			case S_REGULAR:
			case S_COALESCED:
			case S_THREAD_LOCAL_REGULAR:
			case S_THREAD_LOCAL_ZEROFILL:
				if ( (strcmp(sect->segname(), "__TEXT") == 0) && (strcmp(sect->sectname(), "__eh_frame") == 0) ) {
					totalSectionsSize += sizeof(CFISection<A>);
					machOSects[count++].type = sectionTypeCFI;
				}
				else if ( (strcmp(sect->segname(), "__DATA") == 0) && (strcmp(sect->sectname(), "__cfstring") == 0) ) {
					totalSectionsSize += sizeof(CFStringSection<A>);
					machOSects[count++].type = sectionTypeCFString;
				}
				else if ( (strcmp(sect->segname(), "__TEXT") == 0) && (strcmp(sect->sectname(), "__ustring") == 0) ) {
					totalSectionsSize += sizeof(UTF16StringSection<A>);
					machOSects[count++].type = sectionTypeUTF16Strings;
				}
				else if ( (strcmp(sect->segname(), "__DATA") == 0) && (strncmp(sect->sectname(), "__objc_classrefs", 16) == 0) ) {
					totalSectionsSize += sizeof(ObjC2ClassRefsSection<A>);
					machOSects[count++].type = sectionTypeObjC2ClassRefs;
				}
				else if ( (strcmp(sect->segname(), "__DATA") == 0) && (strcmp(sect->sectname(), "__objc_catlist") == 0) ) {
					totalSectionsSize += sizeof(ObjC2CategoryListSection<A>);
					machOSects[count++].type = typeObjC2CategoryList;
				}
				else if ( _AppleObjc && (strcmp(sect->segname(), "__OBJC") == 0) && (strcmp(sect->sectname(), "__class") == 0) ) {
					totalSectionsSize += sizeof(ObjC1ClassSection<A>);
					machOSects[count++].type = sectionTypeObjC1Classes;
				}
				else {
					totalSectionsSize += sizeof(SymboledSection<A>);
					machOSects[count++].type = sectionTypeSymboled;
				}
				break;
			case S_THREAD_LOCAL_VARIABLES:
				totalSectionsSize += sizeof(TLVDefsSection<A>);
				machOSects[count++].type = sectionTypeTLVDefs;
				break;
			case S_THREAD_LOCAL_VARIABLE_POINTERS:
			default:
				throwf("unknown section type %d", sect->flags() & SECTION_TYPE);
		}
	}
	
	// sort by address (mach-o object files don't aways have sections sorted)
	::qsort(machOSects, count, sizeof(MachOSectionAndSectionClass<P>), MachOSectionAndSectionClass<P>::sorter);
		
	// we will synthesize a dummy Section<A> object for tentative definitions
	if ( _tentativeDefinitionCount > 0 ) {
		totalSectionsSize += sizeof(TentativeDefinitionSection<A>);
		machOSects[count++].type = sectionTypeTentativeDefinitions;
	}
	
	// we will synthesize a dummy Section<A> object for Absolute symbols
	if ( _absoluteSymbolCount > 0 ) {
		totalSectionsSize += sizeof(AbsoluteSymbolSection<A>);
		machOSects[count++].type = sectionTypeAbsoluteSymbols;
	}

	// allocate one block for all Section objects as well as pointers to each
	uint8_t* space = new uint8_t[totalSectionsSize+count*sizeof(Section<A>*)];
	_file->_sectionsArray = (Section<A>**)space;
	_file->_sectionsArrayCount = count;
	Section<A>** objects = _file->_sectionsArray;
	space += count*sizeof(Section<A>*);
	for (uint32_t i=0; i < count; ++i) {
		switch ( machOSects[i].type ) {
			case sectionTypeIgnore:
				break;
			case sectionTypeLiteral4:
				*objects++ = new (space) Literal4Section<A>(*this, *_file, machOSects[i].sect);
				space += sizeof(Literal4Section<A>);
				break;
			case sectionTypeLiteral8:
				*objects++ = new (space) Literal8Section<A>(*this, *_file, machOSects[i].sect);
				space += sizeof(Literal8Section<A>);
				break;
			case sectionTypeLiteral16:
				*objects++ = new (space) Literal16Section<A>(*this, *_file, machOSects[i].sect);
				space += sizeof(Literal16Section<A>);
				break;
			case sectionTypeNonLazy:
				*objects++ = new (space) NonLazyPointerSection<A>(*this, *_file, machOSects[i].sect);
				space += sizeof(NonLazyPointerSection<A>);
				break;
			case sectionTypeCFI:
				_EHFrameSection = new (space) CFISection<A>(*this, *_file, machOSects[i].sect);
				*objects++ = _EHFrameSection;
				space += sizeof(CFISection<A>);
				break;
			case sectionTypeCString:
				*objects++ = new (space) CStringSection<A>(*this, *_file, machOSects[i].sect);
				space += sizeof(CStringSection<A>);
				break;
			case sectionTypeCStringPointer:
				*objects++ = new (space) PointerToCStringSection<A>(*this, *_file, machOSects[i].sect);
				space += sizeof(PointerToCStringSection<A>);
				break;
			case sectionTypeObjC1ClassRefs:
				*objects++ = new (space) Objc1ClassReferences<A>(*this, *_file, machOSects[i].sect);
				space += sizeof(Objc1ClassReferences<A>);
				break;
			case sectionTypeUTF16Strings:
				*objects++ = new (space) UTF16StringSection<A>(*this, *_file, machOSects[i].sect);
				space += sizeof(UTF16StringSection<A>);
				break;
			case sectionTypeCFString:
				*objects++ = new (space) CFStringSection<A>(*this, *_file, machOSects[i].sect);
				space += sizeof(CFStringSection<A>);
				break;
			case sectionTypeObjC2ClassRefs:
				*objects++ = new (space) ObjC2ClassRefsSection<A>(*this, *_file, machOSects[i].sect);
				space += sizeof(ObjC2ClassRefsSection<A>);
				break;
			case typeObjC2CategoryList:
				*objects++ = new (space) ObjC2CategoryListSection<A>(*this, *_file, machOSects[i].sect);
				space += sizeof(ObjC2CategoryListSection<A>);
				break;
			case sectionTypeObjC1Classes: 
				*objects++ = new (space) ObjC1ClassSection<A>(*this, *_file, machOSects[i].sect);
				space += sizeof(ObjC1ClassSection<A>);
				break;
			case sectionTypeSymboled:
				*objects++ = new (space) SymboledSection<A>(*this, *_file, machOSects[i].sect);
				space += sizeof(SymboledSection<A>);
				break;
			case sectionTypeTLVDefs:
				*objects++ = new (space) TLVDefsSection<A>(*this, *_file, machOSects[i].sect);
				space += sizeof(TLVDefsSection<A>);
				break;
			case sectionTypeCompactUnwind:
				_compactUnwindSection = new (space) CUSection<A>(*this, *_file, machOSects[i].sect);
				*objects++ = _compactUnwindSection;
				space += sizeof(CUSection<A>);
				break;
			case sectionTypeTentativeDefinitions:
				*objects++ = new (space) TentativeDefinitionSection<A>(*this, *_file);
				space += sizeof(TentativeDefinitionSection<A>);
				break;
			case sectionTypeAbsoluteSymbols:
				_absoluteSection = new (space) AbsoluteSymbolSection<A>(*this, *_file);
				*objects++ = _absoluteSection;
				space += sizeof(AbsoluteSymbolSection<A>);
				break;
			default:
				throw "internal error uknown SectionType";
		}
	}
}


template <typename A>
Section<A>* Parser<A>::sectionForAddress(typename A::P::uint_t addr)
{
	for (uint32_t i=0; i < _file->_sectionsArrayCount; ++i ) {
		const macho_section<typename A::P>* sect = _file->_sectionsArray[i]->machoSection();
		// TentativeDefinitionSection and AbsoluteSymbolSection have no mach-o section
		if ( sect != NULL ) {
			if ( (sect->addr() <= addr) && (addr < (sect->addr()+sect->size())) ) {
				return _file->_sectionsArray[i];
			}
		}
	}
	// not strictly in any section
	// may be in a zero length section
	for (uint32_t i=0; i < _file->_sectionsArrayCount; ++i ) {
		const macho_section<typename A::P>* sect = _file->_sectionsArray[i]->machoSection();
		// TentativeDefinitionSection and AbsoluteSymbolSection have no mach-o section
		if ( sect != NULL ) {
			if ( (sect->addr() == addr) && (sect->size() == 0) ) {
				return _file->_sectionsArray[i];
			}
		}
	}
	
	throwf("sectionForAddress(0x%llX) address not in any section", (uint64_t)addr);
}

template <typename A>
Section<A>* Parser<A>::sectionForNum(unsigned int num)
{
	for (uint32_t i=0; i < _file->_sectionsArrayCount; ++i ) {
		const macho_section<typename A::P>* sect = _file->_sectionsArray[i]->machoSection();
		// TentativeDefinitionSection and AbsoluteSymbolSection have no mach-o section
		if ( sect != NULL ) {
			if ( num == (unsigned int)((sect - _sectionsStart)+1) )
				return _file->_sectionsArray[i];
		}
	}
	throwf("sectionForNum(%u) section number not for any section", num);
}

template <typename A>
Atom<A>* Parser<A>::findAtomByAddress(pint_t addr)
{
	Section<A>* section = this->sectionForAddress(addr);
	return section->findAtomByAddress(addr);
}

template <typename A>
Atom<A>* Parser<A>::findAtomByAddressOrNullIfStub(pint_t addr)
{
	if ( hasStubsSection() && (_stubsMachOSection->addr() <= addr) && (addr < (_stubsMachOSection->addr()+_stubsMachOSection->size())) ) 
		return NULL;
	return findAtomByAddress(addr);
}

template <typename A>
Atom<A>* Parser<A>::findAtomByAddressOrLocalTargetOfStub(pint_t addr, uint32_t* offsetInAtom)
{
	if ( hasStubsSection() && (_stubsMachOSection->addr() <= addr) && (addr < (_stubsMachOSection->addr()+_stubsMachOSection->size())) ) {
		// target is a stub, remove indirection
		uint32_t symbolIndex = this->symbolIndexFromIndirectSectionAddress(addr, _stubsMachOSection);
		assert(symbolIndex != INDIRECT_SYMBOL_LOCAL);
		const macho_nlist<P>& sym = this->symbolFromIndex(symbolIndex);
		// can't be to external weak symbol
		assert( (this->combineFromSymbol(sym) != ld::Atom::combineByName) || (this->scopeFromSymbol(sym) != ld::Atom::scopeGlobal) );
		*offsetInAtom = 0;
		return this->findAtomByName(this->nameFromSymbol(sym));
	}
	Atom<A>* target = this->findAtomByAddress(addr);
	*offsetInAtom = addr - target->_objAddress;
	return target;
}

template <typename A>
Atom<A>* Parser<A>::findAtomByName(const char* name)
{
	uint8_t* p = _file->_atomsArray;
	for(int i=_file->_atomsArrayCount; i > 0; --i) {
		Atom<A>* atom = (Atom<A>*)p;
		if ( strcmp(name, atom->name()) == 0 )
			return atom;
		p += sizeof(Atom<A>);
	}
	return NULL;
}

template <typename A>
void Parser<A>::findTargetFromAddress(pint_t addr, TargetDesc& target)
{
	if ( hasStubsSection() && (_stubsMachOSection->addr() <= addr) && (addr < (_stubsMachOSection->addr()+_stubsMachOSection->size())) ) {
		// target is a stub, remove indirection
		uint32_t symbolIndex = this->symbolIndexFromIndirectSectionAddress(addr, _stubsMachOSection);
		assert(symbolIndex != INDIRECT_SYMBOL_LOCAL);
		const macho_nlist<P>& sym = this->symbolFromIndex(symbolIndex);
		target.atom = NULL;
		target.name = this->nameFromSymbol(sym);
		target.weakImport = this->weakImportFromSymbol(sym);
		target.addend = 0;
		return;
	}
	Section<A>* section = this->sectionForAddress(addr);
	target.atom = section->findAtomByAddress(addr);
	target.addend = addr - target.atom->_objAddress;
	target.weakImport = false;
	target.name = NULL;
}

template <typename A>
void Parser<A>::findTargetFromAddress(pint_t baseAddr, pint_t addr, TargetDesc& target)
{
	findTargetFromAddress(baseAddr, target);
	target.addend = addr - target.atom->_objAddress;
}

template <typename A>
void Parser<A>::findTargetFromAddressAndSectionNum(pint_t addr, unsigned int sectNum, TargetDesc& target)
{
	if ( sectNum == R_ABS ) {
		// target is absolute symbol that corresponds to addr
		if ( _absoluteSection != NULL ) {
			target.atom = _absoluteSection->findAbsAtomForValue(addr);
			if ( target.atom != NULL ) {
				target.name = NULL;
				target.weakImport = false;
				target.addend = 0;
				return;
			}
		}
		throwf("R_ABS reloc but no absolute symbol at target address");
	}

	if ( hasStubsSection() && (stubsSectionNum() == sectNum) ) {
		// target is a stub, remove indirection
		uint32_t symbolIndex = this->symbolIndexFromIndirectSectionAddress(addr, _stubsMachOSection);
		assert(symbolIndex != INDIRECT_SYMBOL_LOCAL);
		const macho_nlist<P>& sym = this->symbolFromIndex(symbolIndex);
		// use direct reference when stub is to a static function
		if ( ((sym.n_type() & N_TYPE) == N_SECT) && (((sym.n_type() & N_EXT) == 0) || (this->nameFromSymbol(sym)[0] == 'L')) ) {
			this->findTargetFromAddressAndSectionNum(sym.n_value(), sym.n_sect(), target);
		}
		else {
			target.atom = NULL;
			target.name = this->nameFromSymbol(sym);
			target.weakImport = this->weakImportFromSymbol(sym);
			target.addend = 0;
		}
		return;
	}
	Section<A>* section = this->sectionForNum(sectNum);
	target.atom = section->findAtomByAddress(addr);
	if ( target.atom == NULL ) {
		typedef typename A::P::sint_t sint_t;
		sint_t a = (sint_t)addr;
		sint_t sectStart = (sint_t)(section->machoSection()->addr());
		sint_t sectEnd  = sectStart + section->machoSection()->size();
		if ( a < sectStart ) {
			// target address is before start of section, so must be negative addend
			target.atom = section->findAtomByAddress(sectStart);
			target.addend = a - sectStart;
			target.weakImport = false;
			target.name = NULL;
			return;
		}
		else if ( a >= sectEnd ) {
			target.atom = section->findAtomByAddress(sectEnd-1);
			target.addend = a - sectEnd;
			target.weakImport = false;
			target.name = NULL;
			return;
		}
	}
	assert(target.atom != NULL);
	target.addend = addr - target.atom->_objAddress;
	target.weakImport = false;
	target.name = NULL;
}

template <typename A>
void Parser<A>::addDtraceExtraInfos(const SourceLocation& src, const char* providerName)
{
	// for every ___dtrace_stability$* and ___dtrace_typedefs$* undefine with
	// a matching provider name, add a by-name kDtraceTypeReference at probe site
 	const char* dollar = strchr(providerName, '$');
	if ( dollar != NULL ) {
		int providerNameLen = dollar-providerName+1;
		for ( std::vector<const char*>::iterator it = _dtraceProviderInfo.begin(); it != _dtraceProviderInfo.end(); ++it) {
			const char* typeDollar = strchr(*it, '$');
			if ( typeDollar != NULL ) {
				if ( strncmp(typeDollar+1, providerName, providerNameLen) == 0 ) {
					addFixup(src, ld::Fixup::k1of1, ld::Fixup::kindDtraceExtra,false, *it);
				}
			}
		}
	}
}

template <typename A>
const char* Parser<A>::scanSymbolTableForAddress(uint64_t addr)
{
	uint64_t closestSymAddr = 0;
	const char* closestSymName = NULL;
	for (uint32_t i=0; i < this->_symbolCount; ++i) {
		const macho_nlist<P>& sym =	symbolFromIndex(i);
		// ignore stabs
		if ( (sym.n_type() & N_STAB) != 0 )
			continue;
		
		// only look at definitions
		if ( (sym.n_type() & N_TYPE) != N_SECT )
			continue;

		// return with exact match
		if ( sym.n_value() == addr )
			return nameFromSymbol(sym);
		
		// record closest seen so far
		if ( (sym.n_value() < addr) && ((sym.n_value() > closestSymAddr) || (closestSymName == NULL)) )
			closestSymName = nameFromSymbol(sym);
	}

	return (closestSymName != NULL) ? closestSymName : "unknown";
}


template <typename A>
void Parser<A>::addFixups(const SourceLocation& src, ld::Fixup::Kind setKind, const TargetDesc& target)
{
	// some fixup pairs can be combined
	ld::Fixup::Cluster cl = ld::Fixup::k1of3;
	ld::Fixup::Kind firstKind = ld::Fixup::kindSetTargetAddress;
	bool combined = false;
	if ( target.addend == 0 ) {
		cl = ld::Fixup::k1of1;
		combined = true;
		switch ( setKind ) {
			case ld::Fixup::kindStoreLittleEndian32:
				firstKind = ld::Fixup::kindStoreTargetAddressLittleEndian32;
				break;
			case ld::Fixup::kindStoreLittleEndian64:
				firstKind = ld::Fixup::kindStoreTargetAddressLittleEndian64;
				break;
			case ld::Fixup::kindStoreBigEndian32:
				firstKind = ld::Fixup::kindStoreTargetAddressBigEndian32;
				break;
			case ld::Fixup::kindStoreBigEndian64:
				firstKind = ld::Fixup::kindStoreTargetAddressBigEndian64;
				break;
			case ld::Fixup::kindStoreX86BranchPCRel32:
				firstKind = ld::Fixup::kindStoreTargetAddressX86BranchPCRel32;
				break;
			case ld::Fixup::kindStoreX86PCRel32:
				firstKind = ld::Fixup::kindStoreTargetAddressX86PCRel32;
				break;
			case ld::Fixup::kindStoreX86PCRel32GOTLoad:
				firstKind = ld::Fixup::kindStoreTargetAddressX86PCRel32GOTLoad;
				break;
			case ld::Fixup::kindStoreX86PCRel32TLVLoad:
				firstKind = ld::Fixup::kindStoreTargetAddressX86PCRel32TLVLoad;
				break;
			case ld::Fixup::kindStoreX86Abs32TLVLoad:
				firstKind = ld::Fixup::kindStoreTargetAddressX86Abs32TLVLoad;
				break;
			case ld::Fixup::kindStoreARMBranch24:
				firstKind = ld::Fixup::kindStoreTargetAddressARMBranch24;
				break;
			case ld::Fixup::kindStoreThumbBranch22:
				firstKind = ld::Fixup::kindStoreTargetAddressThumbBranch22;
				break;
			default:
				combined = false;
				cl = ld::Fixup::k1of2;
				break;
		}
	}

	if ( target.atom != NULL ) {
		if ( target.atom->scope() == ld::Atom::scopeTranslationUnit ) {
			addFixup(src, cl, firstKind, target.atom);
		}
		else if ( (target.atom->combine() == ld::Atom::combineByNameAndContent) || (target.atom->combine() == ld::Atom::combineByNameAndReferences) ) {
			addFixup(src, cl, firstKind, ld::Fixup::bindingByContentBound, target.atom);
		}
		else if ( (src.atom->section().type() == ld::Section::typeCFString) && (src.offsetInAtom != 0) ) {
			// backing string in CFStrings should always be direct
			addFixup(src, cl, firstKind, target.atom);
		}
		else {
			// change direct fixup to by-name fixup
			addFixup(src, cl, firstKind, false, target.atom->name());
		}
	}
	else {
		addFixup(src, cl, firstKind, target.weakImport, target.name);
	}
	if ( target.addend == 0 ) {
		if ( ! combined )
			addFixup(src, ld::Fixup::k2of2, setKind);
	}
	else {
		addFixup(src, ld::Fixup::k2of3, ld::Fixup::kindAddAddend, target.addend);
		addFixup(src, ld::Fixup::k3of3, setKind);
	}
}

template <typename A>
void Parser<A>::addFixups(const SourceLocation& src, ld::Fixup::Kind kind, const TargetDesc& target, const TargetDesc& picBase)
{
	ld::Fixup::Cluster cl = (target.addend == 0) ? ld::Fixup::k1of4 : ld::Fixup::k1of5;
	if ( target.atom != NULL ) {
		if ( target.atom->scope() == ld::Atom::scopeTranslationUnit ) {
			addFixup(src, cl, ld::Fixup::kindSetTargetAddress, target.atom);
		}
		else if ( (target.atom->combine() == ld::Atom::combineByNameAndContent) || (target.atom->combine() == ld::Atom::combineByNameAndReferences) ) {
			addFixup(src, cl, ld::Fixup::kindSetTargetAddress, ld::Fixup::bindingByContentBound, target.atom);
		}
		else {
			addFixup(src, cl, ld::Fixup::kindSetTargetAddress, false, target.atom->name());
		}
	}
	else {
		addFixup(src, cl, ld::Fixup::kindSetTargetAddress, target.weakImport, target.name);
	}
	if ( target.addend == 0 ) {
		assert(picBase.atom != NULL);
		addFixup(src, ld::Fixup::k2of4, ld::Fixup::kindSubtractTargetAddress, picBase.atom);
		addFixup(src, ld::Fixup::k3of4, ld::Fixup::kindSubtractAddend, picBase.addend);
		addFixup(src, ld::Fixup::k4of4, kind);
	}
	else {
		addFixup(src, ld::Fixup::k2of5, ld::Fixup::kindAddAddend, target.addend);
		addFixup(src, ld::Fixup::k3of5, ld::Fixup::kindSubtractTargetAddress, picBase.atom);
		addFixup(src, ld::Fixup::k4of5, ld::Fixup::kindSubtractAddend, picBase.addend);
		addFixup(src, ld::Fixup::k5of5, kind);
	}
}



template <typename A>
uint32_t TentativeDefinitionSection<A>::computeAtomCount(class Parser<A>& parser, 
															struct Parser<A>::LabelAndCFIBreakIterator& it, 
															const struct Parser<A>::CFI_CU_InfoArrays&)
{
	return parser.tentativeDefinitionCount();
}

template <typename A>
uint32_t TentativeDefinitionSection<A>::appendAtoms(class Parser<A>& parser, uint8_t* p, 
														struct Parser<A>::LabelAndCFIBreakIterator& it, 
														const struct Parser<A>::CFI_CU_InfoArrays&)
{
	this->_beginAtoms = (Atom<A>*)p;
	uint32_t count = 0;
	for (uint32_t i=parser.undefinedStartIndex(); i < parser.undefinedEndIndex(); ++i) {
		const macho_nlist<P>& sym =	parser.symbolFromIndex(i);
		if ( ((sym.n_type() & N_TYPE) == N_UNDF) && (sym.n_value() != 0) ) {
			uint64_t size = sym.n_value();
			uint8_t alignP2 = GET_COMM_ALIGN(sym.n_desc());
			if ( alignP2 == 0 ) {
				// common symbols align to their size
				// that is, a 4-byte common aligns to 4-bytes
				// if this size is not a power of two, 
				// then round up to the next power of two
				alignP2 = 63 - (uint8_t)__builtin_clzll(size);
				if ( size != (1ULL << alignP2) )
					++alignP2;
			}
			// limit alignment of extremely large commons to 2^15 bytes (8-page)
			if ( alignP2 > 15 )
				alignP2 = 15;
			Atom<A>* allocatedSpace = (Atom<A>*)p;
			new (allocatedSpace) Atom<A>(*this, parser.nameFromSymbol(sym), (pint_t)ULLONG_MAX, size,
										ld::Atom::definitionTentative,  ld::Atom::combineByName, 
										parser.scopeFromSymbol(sym), ld::Atom::typeZeroFill, ld::Atom::symbolTableIn, 
										parser.dontDeadStripFromSymbol(sym), false, false, ld::Atom::Alignment(alignP2) );
			p += sizeof(Atom<A>);
			++count;
		}
	}
	this->_endAtoms = (Atom<A>*)p;
	return count;
}


template <typename A>
uint32_t AbsoluteSymbolSection<A>::computeAtomCount(class Parser<A>& parser, 
															struct Parser<A>::LabelAndCFIBreakIterator& it, 
															const struct Parser<A>::CFI_CU_InfoArrays&)
{
	return parser.absoluteSymbolCount();
}

template <typename A>
uint32_t AbsoluteSymbolSection<A>::appendAtoms(class Parser<A>& parser, uint8_t* p, 
														struct Parser<A>::LabelAndCFIBreakIterator& it, 
														const struct Parser<A>::CFI_CU_InfoArrays&)
{
	this->_beginAtoms = (Atom<A>*)p;
	uint32_t count = 0;
	for (uint32_t i=0; i < parser.symbolCount(); ++i) {
		const macho_nlist<P>& sym =	parser.symbolFromIndex(i);
		if ( (sym.n_type() & N_TYPE) != N_ABS )
			continue;
		const char* absName = parser.nameFromSymbol(sym);
		// ignore .objc_class_name_* symbols 
		if ( strncmp(absName, ".objc_class_name_", 17) == 0 )
			continue;
		// ignore .objc_class_name_* symbols 
		if ( strncmp(absName, ".objc_category_name_", 20) == 0 )
			continue;
		// ignore empty *.eh symbols
		if ( strcmp(&absName[strlen(absName)-3], ".eh") == 0 )
			continue;

		Atom<A>* allocatedSpace = (Atom<A>*)p;
		new (allocatedSpace) Atom<A>(*this, parser, sym, 0);
		p += sizeof(Atom<A>);
		++count;
	}
	this->_endAtoms = (Atom<A>*)p;
	return count;
}

template <typename A>
Atom<A>* AbsoluteSymbolSection<A>::findAbsAtomForValue(typename A::P::uint_t value)
{
	Atom<A>* end = this->_endAtoms;
	for(Atom<A>* p = this->_beginAtoms; p < end; ++p) {
		if ( p->_objAddress == value )	
			return p;
	}
	return NULL;
}


template <typename A>
uint32_t Parser<A>::indirectSymbol(uint32_t indirectIndex)
{
	if ( indirectIndex >= _indirectTableCount )
		throw "indirect symbol index out of range";
	return E::get32(_indirectTable[indirectIndex]);
}

template <typename A>
const macho_nlist<typename A::P>& Parser<A>::symbolFromIndex(uint32_t index)
{
	if ( index > _symbolCount )
		throw "symbol index out of range";
	return _symbols[index];
}

template <typename A>
const macho_section<typename A::P>*	Parser<A>::machOSectionFromSectionIndex(uint32_t index)
{
	if ( index >= _machOSectionsCount )
		throw "section index out of range";
	return &_sectionsStart[index];
}

template <typename A>
uint32_t Parser<A>::symbolIndexFromIndirectSectionAddress(pint_t addr, const macho_section<P>* sect)
{
	uint32_t elementSize = 0;
	switch ( sect->flags() & SECTION_TYPE ) {
		case S_SYMBOL_STUBS:
			elementSize = sect->reserved2();
			break;
		case S_LAZY_SYMBOL_POINTERS:
		case S_NON_LAZY_SYMBOL_POINTERS:
			elementSize = sizeof(pint_t);
			break;
		default:
			throw "section does not use inirect symbol table";
	}	
	uint32_t indexInSection = (addr - sect->addr()) / elementSize;
	uint32_t indexIntoIndirectTable = sect->reserved1() + indexInSection;
	return this->indirectSymbol(indexIntoIndirectTable);
}



template <typename A>
const char* Parser<A>::nameFromSymbol(const macho_nlist<P>& sym)
{
	return &_strings[sym.n_strx()];
}

template <typename A>
ld::Atom::Scope Parser<A>::scopeFromSymbol(const macho_nlist<P>& sym)
{
	if ( (sym.n_type() & N_EXT) == 0 )
		return ld::Atom::scopeTranslationUnit;
	else if ( (sym.n_type() & N_PEXT) != 0 )
		return ld::Atom::scopeLinkageUnit;
	else if ( this->nameFromSymbol(sym)[0] == 'l' ) // since all 'l' symbols will be remove, don't make them global
		return ld::Atom::scopeLinkageUnit;
	else
		return ld::Atom::scopeGlobal;
}

template <typename A>
ld::Atom::Definition Parser<A>::definitionFromSymbol(const macho_nlist<P>& sym)
{
	switch ( sym.n_type() & N_TYPE ) {
		case N_ABS:
			return ld::Atom::definitionAbsolute;
		case N_SECT:
			return ld::Atom::definitionRegular;
		case N_UNDF:
			if ( sym.n_value() != 0 ) 
				return ld::Atom::definitionTentative;
	}
	throw "definitionFromSymbol() bad symbol";
}

template <typename A>
ld::Atom::Combine Parser<A>::combineFromSymbol(const macho_nlist<P>& sym)
{
	if ( sym.n_desc() & N_WEAK_DEF ) 
		return ld::Atom::combineByName;
	else
		return ld::Atom::combineNever;
}


template <typename A>
ld::Atom::SymbolTableInclusion Parser<A>::inclusionFromSymbol(const macho_nlist<P>& sym)
{
	const char* symbolName = nameFromSymbol(sym);
	// labels beginning with 'l' (lowercase ell) are automatically removed in final linked images <rdar://problem/4571042>
	// labels beginning with 'L' should have been stripped by the assembler, so are stripped now
	if ( sym.n_desc() & REFERENCED_DYNAMICALLY ) 
		return ld::Atom::symbolTableInAndNeverStrip;
	else if ( symbolName[0] == 'l' )
		return ld::Atom::symbolTableNotInFinalLinkedImages;
	else if ( symbolName[0] == 'L' )
		return ld::Atom::symbolTableNotIn;
	else
		return ld::Atom::symbolTableIn;
}

template <typename A>
bool Parser<A>::dontDeadStripFromSymbol(const macho_nlist<P>& sym)
{
	return ( (sym.n_desc() & (N_NO_DEAD_STRIP|REFERENCED_DYNAMICALLY)) != 0 );
}

template <typename A>
bool Parser<A>::isThumbFromSymbol(const macho_nlist<P>& sym)
{
	return ( sym.n_desc() & N_ARM_THUMB_DEF );
}

template <typename A>
bool Parser<A>::weakImportFromSymbol(const macho_nlist<P>& sym)
{
	return ( ((sym.n_type() & N_TYPE) == N_UNDF) && ((sym.n_desc() & N_WEAK_REF) != 0) );
}

template <typename A>
bool Parser<A>::resolverFromSymbol(const macho_nlist<P>& sym)
{
	return ( sym.n_desc() & N_SYMBOL_RESOLVER );
}


/* Skip over a LEB128 value (signed or unsigned).  */
static void
skip_leb128 (const uint8_t ** offset, const uint8_t * end)
{
  while (*offset != end && **offset >= 0x80)
    (*offset)++;
  if (*offset != end)
    (*offset)++;
}

/* Read a ULEB128 into a 64-bit word.  Return (uint64_t)-1 on overflow
   or error.  On overflow, skip past the rest of the uleb128.  */
static uint64_t
read_uleb128 (const uint8_t ** offset, const uint8_t * end)
{
  uint64_t result = 0;
  int bit = 0;

  do  {
    uint64_t b;

    if (*offset == end)
      return (uint64_t) -1;

    b = **offset & 0x7f;

    if (bit >= 64 || b << bit >> bit != b)
      result = (uint64_t) -1;
    else
      result |= b << bit, bit += 7;
  } while (*(*offset)++ >= 0x80);
  return result;
}


/* Skip over a DWARF attribute of form FORM.  */
template <typename A>
bool Parser<A>::skip_form(const uint8_t ** offset, const uint8_t * end, uint64_t form,
							uint8_t addr_size, bool dwarf64)
{
  int64_t sz=0;

  switch (form)
    {
    case DW_FORM_addr:
      sz = addr_size;
      break;

    case DW_FORM_block2:
      if (end - *offset < 2)
	return false;
      sz = 2 + A::P::E::get16(*(uint16_t*)offset);
      break;

    case DW_FORM_block4:
      if (end - *offset < 4)
	return false;
      sz = 2 + A::P::E::get32(*(uint32_t*)offset);
      break;

    case DW_FORM_data2:
    case DW_FORM_ref2:
      sz = 2;
      break;

    case DW_FORM_data4:
    case DW_FORM_ref4:
      sz = 4;
      break;

    case DW_FORM_data8:
    case DW_FORM_ref8:
      sz = 8;
      break;

    case DW_FORM_string:
      while (*offset != end && **offset)
	++*offset;
    case DW_FORM_data1:
    case DW_FORM_flag:
    case DW_FORM_ref1:
      sz = 1;
      break;

    case DW_FORM_block:
      sz = read_uleb128 (offset, end);
      break;

    case DW_FORM_block1:
      if (*offset == end)
	return false;
      sz = 1 + **offset;
      break;

    case DW_FORM_sdata:
    case DW_FORM_udata:
    case DW_FORM_ref_udata:
      skip_leb128 (offset, end);
      return true;

    case DW_FORM_strp:
    case DW_FORM_ref_addr:
      sz = 4;
      break;

    default:
      return false;
    }
  if (end - *offset < sz)
    return false;
  *offset += sz;
  return true;
}


template <typename A>
const char* Parser<A>::getDwarfString(uint64_t form, const uint8_t* p)
{
	if ( form == DW_FORM_string )
		return (const char*)p;
	else if ( form == DW_FORM_strp ) {
		uint32_t offset = E::get32(*((uint32_t*)p));
		const char* dwarfStrings = (char*)_file->fileContent() + _file->_dwarfDebugStringSect->offset();
		if ( offset > _file->_dwarfDebugStringSect->size() ) {
			warning("unknown dwarf DW_FORM_strp (offset=0x%08X) is too big in %s\n", offset, this->_path);
			return NULL;
		}
		return &dwarfStrings[offset];
	}
	warning("unknown dwarf string encoding (form=%lld) in %s\n", form, this->_path);
	return NULL;
}


template <typename A>
struct AtomAndLineInfo {
	Atom<A>*			atom;
	ld::Atom::LineInfo	info;
};


// <rdar://problem/5591394> Add support to ld64 for N_FUN stabs when used for symbolic constants
// Returns whether a stabStr belonging to an N_FUN stab represents a
// symbolic constant rather than a function
template <typename A>
bool Parser<A>::isConstFunStabs(const char *stabStr)
{
	const char* colon;
	// N_FUN can be used for both constants and for functions. In case it's a constant,
	// the format of the stabs string is "symname:c=<value>;"
	// ':' cannot appear in the symbol name, except if it's an Objective-C method
	// (in which case the symbol name starts with + or -, and then it's definitely
	//  not a constant)
	return (stabStr != NULL) && (stabStr[0] != '+') && (stabStr[0] != '-')
			&& ((colon = strchr(stabStr, ':')) != NULL)
			&& (colon[1] == 'c') && (colon[2] == '=');
}


template <typename A>
void Parser<A>::parseDebugInfo()
{
	// check for dwarf __debug_info section
	if ( _file->_dwarfDebugInfoSect == NULL ) {
		// if no DWARF debug info, look for stabs
		this->parseStabs();
		return;
	}
	if ( _file->_dwarfDebugInfoSect->size() == 0 )
		return;
		
	uint64_t stmtList;
	const char* tuDir;
	const char* tuName;
	if ( !read_comp_unit(&tuName, &tuDir, &stmtList) ) {
		// if can't parse dwarf, warn and give up
		_file->_dwarfTranslationUnitPath = NULL;
		warning("can't parse dwarf compilation unit info in %s", _path);
		_file->_debugInfoKind = ld::relocatable::File::kDebugInfoNone;
		return;
	}
	if ( (tuName != NULL) && (tuName[1] == '/') ) {
		_file->_dwarfTranslationUnitPath = tuName;
	}
	else if ( (tuDir != NULL) && (tuName != NULL) ) {
		asprintf((char**)&(_file->_dwarfTranslationUnitPath), "%s/%s", tuDir, tuName);
	}
	else if ( tuDir == NULL ) {
		_file->_dwarfTranslationUnitPath = tuName;
	}
	else {
		_file->_dwarfTranslationUnitPath = NULL;
	}
	
	// add line number info to atoms from dwarf
	std::vector<AtomAndLineInfo<A> > entries;
	entries.reserve(64);
	if ( _file->_debugInfoKind == ld::relocatable::File::kDebugInfoDwarf ) {
		// file with just data will have no __debug_line info
		if ( (_file->_dwarfDebugLineSect != NULL) && (_file->_dwarfDebugLineSect->size() != 0) ) {
			// validate stmt_list
			if ( (stmtList != (uint64_t)-1) && (stmtList < _file->_dwarfDebugLineSect->size()) ) {
				const uint8_t* debug_line = (uint8_t*)_file->fileContent() + _file->_dwarfDebugLineSect->offset();
				struct line_reader_data* lines = line_open(&debug_line[stmtList],
														_file->_dwarfDebugLineSect->size() - stmtList, E::little_endian);
				struct line_info result;
				Atom<A>* curAtom = NULL;
				uint32_t curAtomOffset = 0;
				uint32_t curAtomAddress = 0;
				uint32_t curAtomSize = 0;
				std::map<uint32_t,const char*>	dwarfIndexToFile;
				if ( lines != NULL ) {
					while ( line_next(lines, &result, line_stop_pc) ) {
						//fprintf(stderr, "curAtom=%p, result.pc=0x%llX, result.line=%llu, result.end_of_sequence=%d,"
						//				  " curAtomAddress=0x%X, curAtomSize=0x%X\n",
						//		curAtom, result.pc, result.line, result.end_of_sequence, curAtomAddress, curAtomSize);
						// work around weird debug line table compiler generates if no functions in __text section
						if ( (curAtom == NULL) && (result.pc == 0) && result.end_of_sequence && (result.file == 1))
							continue;
						// for performance, see if in next pc is in current atom
						if ( (curAtom != NULL) && (curAtomAddress <= result.pc) && (result.pc < (curAtomAddress+curAtomSize)) ) {
							curAtomOffset = result.pc - curAtomAddress;
						}
						// or pc at end of current atom
						else if ( result.end_of_sequence && (curAtom != NULL) && (result.pc == (curAtomAddress+curAtomSize)) ) {
							curAtomOffset = result.pc - curAtomAddress;
						}
						// or only one function that is a one line function
						else if ( result.end_of_sequence && (curAtom == NULL) && (this->findAtomByAddress(0) != NULL) && (result.pc == this->findAtomByAddress(0)->size()) ) {
							curAtom			= this->findAtomByAddress(0);
							curAtomOffset	= result.pc - curAtom->objectAddress();
							curAtomAddress	= curAtom->objectAddress();
							curAtomSize		= curAtom->size();
						}
						else {
							// do slow look up of atom by address
							try {
								curAtom = this->findAtomByAddress(result.pc);
							}
							catch (...) {
								// in case of bug in debug info, don't abort link, just limp on
								curAtom = NULL;
							}
							if ( curAtom == NULL )
								break; // file has line info but no functions
							if ( result.end_of_sequence && (curAtomAddress+curAtomSize < result.pc) ) {	
								// a one line function can be returned by line_next() as one entry with pc at end of blob
								// look for alt atom starting at end of previous atom
								uint32_t previousEnd = curAtomAddress+curAtomSize;
								Atom<A>* alt = this->findAtomByAddressOrNullIfStub(previousEnd);
								if ( alt == NULL )
									continue; // ignore spurious debug info for stubs
								if ( result.pc <= alt->objectAddress() + alt->size() ) {
									curAtom			= alt;
									curAtomOffset	= result.pc - alt->objectAddress();
									curAtomAddress	= alt->objectAddress();
									curAtomSize		= alt->size();
								}
								else {
									curAtomOffset	= result.pc - curAtom->objectAddress();
									curAtomAddress	= curAtom->objectAddress();
									curAtomSize		= curAtom->size();
								}
							}
							else {
								curAtomOffset	= result.pc - curAtom->objectAddress();
								curAtomAddress	= curAtom->objectAddress();
								curAtomSize		= curAtom->size();
							}
						}
						const char* filename;
						std::map<uint32_t,const char*>::iterator pos = dwarfIndexToFile.find(result.file);
						if ( pos == dwarfIndexToFile.end() ) {
							filename = line_file(lines, result.file);
							dwarfIndexToFile[result.file] = filename;
						}
						else {
							filename = pos->second;
						}
						// only record for ~8000 line info records per function
						if ( curAtom->roomForMoreLineInfoCount() ) {
							AtomAndLineInfo<A> entry;
							entry.atom = curAtom;
							entry.info.atomOffset = curAtomOffset;
							entry.info.fileName = filename;
							entry.info.lineNumber = result.line;
							//fprintf(stderr, "addr=0x%08llX, line=%lld, file=%s, atom=%s, atom.size=0x%X, end=%d\n", 
							//		result.pc, result.line, filename, curAtom->name(), curAtomSize, result.end_of_sequence);
							entries.push_back(entry);
							curAtom->incrementLineInfoCount();
						}
						if ( result.end_of_sequence ) {
							curAtom = NULL;
						}
					}
					line_free(lines);
				}
			}
		}
	}
		
	// assign line info start offset for each atom
	uint8_t* p = _file->_atomsArray;
	uint32_t liOffset = 0;
	for(int i=_file->_atomsArrayCount; i > 0; --i) {
		Atom<A>* atom = (Atom<A>*)p;
		atom->_lineInfoStartIndex = liOffset;
		liOffset += atom->_lineInfoCount;
		atom->_lineInfoCount = 0;
		p += sizeof(Atom<A>);
	}
	assert(liOffset == entries.size());
	_file->_lineInfos.reserve(liOffset);

	// copy each line info for each atom 
	for (typename std::vector<AtomAndLineInfo<A> >::iterator it = entries.begin(); it != entries.end(); ++it) {
		uint32_t slot = it->atom->_lineInfoStartIndex + it->atom->_lineInfoCount;
		_file->_lineInfos[slot] = it->info;
		it->atom->_lineInfoCount++;
	}
	
	// done with temp vector
	entries.clear();
}

template <typename A>
void Parser<A>::parseStabs()
{
	// scan symbol table for stabs entries
	Atom<A>* currentAtom = NULL;
	pint_t currentAtomAddress = 0;
	enum { start, inBeginEnd, inFun } state = start;
	for (uint32_t symbolIndex = 0; symbolIndex < _symbolCount; ++symbolIndex ) {
		const macho_nlist<P>& sym = this->symbolFromIndex(symbolIndex);
		bool useStab = true;
		uint8_t type = sym.n_type();
		const char* symString = (sym.n_strx() != 0) ? this->nameFromSymbol(sym) : NULL;
		if ( (type & N_STAB) != 0 ) {
			_file->_debugInfoKind =  (_hasUUID ? ld::relocatable::File::kDebugInfoStabsUUID : ld::relocatable::File::kDebugInfoStabs);
			ld::relocatable::File::Stab stab;
			stab.atom	= NULL;
			stab.type	= type;
			stab.other	= sym.n_sect();
			stab.desc	= sym.n_desc();
			stab.value	= sym.n_value();
			stab.string = NULL;
			switch (state) {
				case start:
					switch (type) {
						case N_BNSYM:
							// beginning of function block
							state = inBeginEnd;
							// fall into case to lookup atom by addresss
						case N_LCSYM:
						case N_STSYM:
							currentAtomAddress = sym.n_value();
							currentAtom = this->findAtomByAddress(currentAtomAddress);
							if ( currentAtom != NULL ) {
								stab.atom = currentAtom;
								stab.string = symString;
							}
							else {
								fprintf(stderr, "can't find atom for stabs BNSYM at %08llX in %s",
									(uint64_t)sym.n_value(), _path);
							}
							break;
						case N_SO:
						case N_OSO:
						case N_OPT:
						case N_LSYM:
						case N_RSYM:
						case N_PSYM:
							// not associated with an atom, just copy
							stab.string = symString;
							break;
						case N_GSYM:
						{
							// n_value field is NOT atom address ;-(
							// need to find atom by name match
							const char* colon = strchr(symString, ':');
							if ( colon != NULL ) {
								// build underscore leading name
								int nameLen = colon - symString;
								char symName[nameLen+2];
								strlcpy(&symName[1], symString, nameLen+1);
								symName[0] = '_';
								symName[nameLen+1] = '\0';
								currentAtom = this->findAtomByName(symName);
								if ( currentAtom != NULL ) {
									stab.atom = currentAtom;
									stab.string = symString;
								}
							}
							else {
								// might be a debug-note without trailing :G()
								currentAtom = this->findAtomByName(symString);
								if ( currentAtom != NULL ) {
									stab.atom = currentAtom;
									stab.string = symString;
								}
							}
							if ( stab.atom == NULL ) {
								// ld_classic added bogus GSYM stabs for old style dtrace probes
								if ( (strncmp(symString, "__dtrace_probe$", 15) != 0) )
									warning("can't find atom for N_GSYM stabs %s in %s", symString, _path);
								useStab = false;
							}
							break;
						}
						case N_FUN:
							if ( isConstFunStabs(symString) ) {
								// constant not associated with a function
								stab.string = symString;
							}
							else {
								// old style stabs without BNSYM
								state = inFun;
								currentAtomAddress = sym.n_value();
								currentAtom = this->findAtomByAddress(currentAtomAddress);
								if ( currentAtom != NULL ) {
									stab.atom = currentAtom;
									stab.string = symString;
								}
								else {
									warning("can't find atom for stabs FUN at %08llX in %s",
										(uint64_t)currentAtomAddress, _path);
								}
							}
							break;
						case N_SOL:
						case N_SLINE:
							stab.string = symString;
							// old stabs
							break;
						case N_BINCL:
						case N_EINCL:
						case N_EXCL:
							stab.string = symString;
							// -gfull built .o file
							break;
						default:
							warning("unknown stabs type 0x%X in %s", type, _path);
					}
					break;
				case inBeginEnd:
					stab.atom = currentAtom;
					switch (type) {
						case N_ENSYM:
							state = start;
							currentAtom = NULL;
							break;
						case N_LCSYM:
						case N_STSYM:
						{
							Atom<A>* nestedAtom = this->findAtomByAddress(sym.n_value());
							if ( nestedAtom != NULL ) {
								stab.atom = nestedAtom;
								stab.string = symString;
							}
							else {
								warning("can't find atom for stabs 0x%X at %08llX in %s",
									type, (uint64_t)sym.n_value(), _path);
							}
							break;
						}
						case N_LBRAC:
						case N_RBRAC:
						case N_SLINE:
							// adjust value to be offset in atom
							stab.value -= currentAtomAddress;
						default:
							stab.string = symString;
							break;
					}
					break;
				case inFun:
					switch (type) {
						case N_FUN:
							if ( isConstFunStabs(symString) ) {
								stab.atom = currentAtom;
								stab.string = symString;
							}
							else {
								if ( sym.n_sect() != 0 ) {
									// found another start stab, must be really old stabs...
									currentAtomAddress = sym.n_value();
									currentAtom = this->findAtomByAddress(currentAtomAddress);
									if ( currentAtom != NULL ) {
										stab.atom = currentAtom;
										stab.string = symString;
									}
									else {
										warning("can't find atom for stabs FUN at %08llX in %s",
											(uint64_t)currentAtomAddress, _path);
									}
								}
								else {
									// found ending stab, switch back to start state
									stab.string = symString;
									stab.atom = currentAtom;
									state = start;
									currentAtom = NULL;
								}
							}
							break;
						case N_LBRAC:
						case N_RBRAC:
						case N_SLINE:
							// adjust value to be offset in atom
							stab.value -= currentAtomAddress;
							stab.atom = currentAtom;
							break;
						case N_SO:
							stab.string = symString;
							state = start;
							break;
						default:
							stab.atom = currentAtom;
							stab.string = symString;
							break;
					}
					break;
			}
			// add to list of stabs for this .o file
			if ( useStab )
				_file->_stabs.push_back(stab);
		}
	}
}



// Look at the compilation unit DIE and determine
// its NAME, compilation directory (in COMP_DIR) and its
// line number information offset (in STMT_LIST).  NAME and COMP_DIR
// may be NULL (especially COMP_DIR) if they are not in the .o file;
// STMT_LIST will be (uint64_t) -1.
//
// At present this assumes that there's only one compilation unit DIE.
//
template <typename A>
bool Parser<A>::read_comp_unit(const char ** name, const char ** comp_dir,
							uint64_t *stmt_list)
{
	const uint8_t * debug_info;
	const uint8_t * debug_abbrev;
	const uint8_t * di;
	const uint8_t * da;
	const uint8_t * end;
	const uint8_t * enda;
	uint64_t sz;
	uint16_t vers;
	uint64_t abbrev_base;
	uint64_t abbrev;
	uint8_t address_size;
	bool dwarf64;

	*name = NULL;
	*comp_dir = NULL;
	*stmt_list = (uint64_t) -1;

	if ( (_file->_dwarfDebugInfoSect == NULL) || (_file->_dwarfDebugAbbrevSect == NULL) )
		return false;

	debug_info = (uint8_t*)_file->fileContent() + _file->_dwarfDebugInfoSect->offset();
	debug_abbrev = (uint8_t*)_file->fileContent() + _file->_dwarfDebugAbbrevSect->offset();
	di = debug_info;

	if (_file->_dwarfDebugInfoSect->size() < 12)
		/* Too small to be a real debug_info section.  */
		return false;
	sz = A::P::E::get32(*(uint32_t*)di);
	di += 4;
	dwarf64 = sz == 0xffffffff;
	if (dwarf64)
		sz = A::P::E::get64(*(uint64_t*)di), di += 8;
	else if (sz > 0xffffff00)
		/* Unknown dwarf format.  */
		return false;

	/* Verify claimed size.  */
	if (sz + (di - debug_info) > _file->_dwarfDebugInfoSect->size() || sz <= (dwarf64 ? 23 : 11))
		return false;

	vers = A::P::E::get16(*(uint16_t*)di);
	if (vers < 2 || vers > 3)
	/* DWARF version wrong for this code.
	   Chances are we could continue anyway, but we don't know for sure.  */
		return false;
	di += 2;

	/* Find the debug_abbrev section.  */
	abbrev_base = dwarf64 ? A::P::E::get64(*(uint64_t*)di) : A::P::E::get32(*(uint32_t*)di);
	di += dwarf64 ? 8 : 4;

	if (abbrev_base > _file->_dwarfDebugAbbrevSect->size())
		return false;
	da = debug_abbrev + abbrev_base;
	enda = debug_abbrev + _file->_dwarfDebugAbbrevSect->size();

	address_size = *di++;

	/* Find the abbrev number we're looking for.  */
	end = di + sz;
	abbrev = read_uleb128 (&di, end);
	if (abbrev == (uint64_t) -1)
		return false;

	/* Skip through the debug_abbrev section looking for that abbrev.  */
	for (;;)
	{
		uint64_t this_abbrev = read_uleb128 (&da, enda);
		uint64_t attr;

		if (this_abbrev == abbrev)
			/* This is almost always taken.  */
			break;
		skip_leb128 (&da, enda); /* Skip the tag.  */
		if (da == enda)
			return false;
		da++;  /* Skip the DW_CHILDREN_* value.  */

		do {
			attr = read_uleb128 (&da, enda);
			skip_leb128 (&da, enda);
		} while (attr != 0 && attr != (uint64_t) -1);
		if (attr != 0)
			return false;
	}

	/* Check that the abbrev is one for a DW_TAG_compile_unit.  */
	if (read_uleb128 (&da, enda) != DW_TAG_compile_unit)
	return false;
	if (da == enda)
	return false;
	da++;  /* Skip the DW_CHILDREN_* value.  */

	/* Now, go through the DIE looking for DW_AT_name,
	 DW_AT_comp_dir, and DW_AT_stmt_list.  */
	for (;;)
	{
		uint64_t attr = read_uleb128 (&da, enda);
		uint64_t form = read_uleb128 (&da, enda);

		if (attr == (uint64_t) -1)
			return false;
		else if (attr == 0)
			return true;

		if (form == DW_FORM_indirect)
			form = read_uleb128 (&di, end);

		if (attr == DW_AT_name)
			*name = getDwarfString(form, di);
		else if (attr == DW_AT_comp_dir)
			*comp_dir = getDwarfString(form, di);
		else if (attr == DW_AT_stmt_list && form == DW_FORM_data4)
			*stmt_list = A::P::E::get32(*(uint32_t*)di);
		else if (attr == DW_AT_stmt_list && form == DW_FORM_data8)
			*stmt_list = A::P::E::get64(*(uint64_t*)di);
		if (! skip_form (&di, end, form, address_size, dwarf64))
			return false;
	}
}



template <typename A>
File<A>::~File()
{
	free(_sectionsArray);
	free(_atomsArray);
}

template <typename A>
const char* File<A>::translationUnitSource() const
{
	return _dwarfTranslationUnitPath;
}

	

template <typename A>
bool File<A>::forEachAtom(ld::File::AtomHandler& handler) const
{
	handler.doFile(*this);
	uint8_t* p = _atomsArray;
	for(int i=_atomsArrayCount; i > 0; --i) {
		handler.doAtom(*((Atom<A>*)p));
		p += sizeof(Atom<A>);
	}
	return (_atomsArrayCount != 0);
}

template <typename A>
const char* Section<A>::makeSegmentName(const macho_section<typename A::P>* sect)
{
	// mach-o section record only has room for 16-byte seg/sect names
	// so a 16-byte name has no trailing zero
	const char* name = sect->segname();
	if ( strlen(name) < 16 ) 
		return name;
	char* tmp = new char[17];
	strlcpy(tmp, name, 17);
	return tmp;
}

template <typename A>
const char* Section<A>::makeSectionName(const macho_section<typename A::P>* sect)
{
	const char* name = sect->sectname();
	if ( strlen(name) < 16 ) 
		return name;
		
	// special case common long section names so we don't have to malloc
	if ( strncmp(sect->sectname(), "__objc_classrefs", 16) == 0 )
		return "__objc_classrefs";
	if ( strncmp(sect->sectname(), "__objc_classlist", 16) == 0 )
		return "__objc_classlist";
	if ( strncmp(sect->sectname(), "__objc_nlclslist", 16) == 0 )
		return "__objc_nlclslist";
	if ( strncmp(sect->sectname(), "__objc_nlcatlist", 16) == 0 )
		return "__objc_nlcatlist";
	if ( strncmp(sect->sectname(), "__objc_protolist", 16) == 0 )
		return "__objc_protolist";
	if ( strncmp(sect->sectname(), "__objc_protorefs", 16) == 0 )
		return "__objc_protorefs";
	if ( strncmp(sect->sectname(), "__objc_superrefs", 16) == 0 )
		return "__objc_superrefs";
	if ( strncmp(sect->sectname(), "__objc_imageinfo", 16) == 0 )
		return "__objc_imageinfo";
	if ( strncmp(sect->sectname(), "__objc_stringobj", 16) == 0 )
		return "__objc_stringobj";
	if ( strncmp(sect->sectname(), "__gcc_except_tab", 16) == 0 )
		return "__gcc_except_tab";

	char* tmp = new char[17];
	strlcpy(tmp, name, 17);
	return tmp;
}

template <typename A>
bool Section<A>::readable(const macho_section<typename A::P>* sect)
{
	return true;
}

template <typename A>
bool Section<A>::writable(const macho_section<typename A::P>* sect)
{
	// mach-o .o files do not contain segment permissions
	// we just know TEXT is special
	return ( strcmp(sect->segname(), "__TEXT") != 0 );
}

template <typename A>
bool Section<A>::exectuable(const macho_section<typename A::P>* sect)
{
	// mach-o .o files do not contain segment permissions
	// we just know TEXT is special
	return ( strcmp(sect->segname(), "__TEXT") == 0 );
}


template <typename A>
ld::Section::Type Section<A>::sectionType(const macho_section<typename A::P>* sect)
{
	switch ( sect->flags() & SECTION_TYPE ) {
		case S_ZEROFILL:
			return ld::Section::typeZeroFill;
		case S_CSTRING_LITERALS:
			if ( (strcmp(sect->sectname(), "__cstring") == 0) && (strcmp(sect->segname(), "__TEXT") == 0) )
				return ld::Section::typeCString;
			else
				return ld::Section::typeNonStdCString;
		case S_4BYTE_LITERALS:
			return ld::Section::typeLiteral4;
		case S_8BYTE_LITERALS:
			return ld::Section::typeLiteral8;
		case S_LITERAL_POINTERS:
			return ld::Section::typeCStringPointer;
		case S_NON_LAZY_SYMBOL_POINTERS:
			return ld::Section::typeNonLazyPointer;
		case S_LAZY_SYMBOL_POINTERS:
			return ld::Section::typeLazyPointer;
		case S_SYMBOL_STUBS:
			return ld::Section::typeStub;
		case S_MOD_INIT_FUNC_POINTERS:
			return ld::Section::typeInitializerPointers;
		case S_MOD_TERM_FUNC_POINTERS:
			return ld::Section::typeTerminatorPointers;
		case S_INTERPOSING:
			return ld::Section::typeUnclassified;
		case S_16BYTE_LITERALS:
			return ld::Section::typeLiteral16;
		case S_REGULAR:
		case S_COALESCED:
			if ( sect->flags() & S_ATTR_PURE_INSTRUCTIONS ) {
				return ld::Section::typeCode;
			}
			else if ( strcmp(sect->segname(), "__TEXT") == 0 ) {
				if ( strcmp(sect->sectname(), "__eh_frame") == 0 ) 
					return ld::Section::typeCFI;
				else if ( strcmp(sect->sectname(), "__ustring") == 0 )
					return ld::Section::typeUTF16Strings;
				else if ( strcmp(sect->sectname(), "__textcoal_nt") == 0 )
					return ld::Section::typeCode;
				else if ( strcmp(sect->sectname(), "__StaticInit") == 0 )
					return ld::Section::typeCode;
				else if ( strcmp(sect->sectname(), "__constructor") == 0 )
					return ld::Section::typeInitializerPointers;
			}
			else if ( strcmp(sect->segname(), "__DATA") == 0 ) {
				if ( strcmp(sect->sectname(), "__cfstring") == 0 ) 
					return ld::Section::typeCFString;
				else if ( strcmp(sect->sectname(), "__dyld") == 0 )
					return ld::Section::typeDyldInfo;
				else if ( strcmp(sect->sectname(), "__program_vars") == 0 )
					return ld::Section::typeDyldInfo;
				else if ( strncmp(sect->sectname(), "__objc_classrefs", 16) == 0 )
					return ld::Section::typeObjCClassRefs;
				else if ( strcmp(sect->sectname(), "__objc_catlist") == 0 )
					return ld::Section::typeObjC2CategoryList;
			}
			else if ( strcmp(sect->segname(), "__OBJC") == 0 ) {
				if ( strcmp(sect->sectname(), "__class") == 0 ) 
					return ld::Section::typeObjC1Classes;
			}
			break;
		case S_THREAD_LOCAL_REGULAR:
			return ld::Section::typeTLVInitialValues;
		case S_THREAD_LOCAL_ZEROFILL:
			return ld::Section::typeTLVZeroFill;
		case S_THREAD_LOCAL_VARIABLES:
			return ld::Section::typeTLVDefs;
		case S_THREAD_LOCAL_INIT_FUNCTION_POINTERS:
			return ld::Section::typeTLVInitializerPointers;
	}
	return ld::Section::typeUnclassified;
}


template <typename A>
Atom<A>* Section<A>::findContentAtomByAddress(pint_t addr, class Atom<A>* start, class Atom<A>* end)
{
	// do a binary search of atom array
	uint32_t atomCount = end - start;
	Atom<A>* base = start;
	for (uint32_t n = atomCount; n > 0; n /= 2) {
		Atom<A>* pivot = &base[n/2];
		pint_t atomStartAddr = pivot->_objAddress;
		pint_t atomEndAddr = atomStartAddr + pivot->_size;
		if ( atomStartAddr <= addr ) {
			// address in normal atom
			if (addr < atomEndAddr)
				return pivot;
			// address in "end" label (but not in alias)
			if ( (pivot->_size == 0) && (addr == atomEndAddr) && !pivot->isAlias() )
				return pivot;
		}
		if ( addr >= atomEndAddr ) {
			// key > pivot 
			// move base to atom after pivot
			base = &pivot[1];
			--n; 
		}
		else {
			// key < pivot 
			// keep same base
		}
	}
	return NULL;
}

template <typename A>
ld::Atom::Alignment Section<A>::alignmentForAddress(pint_t addr) 
{ 
	const uint32_t sectionAlignment = this->_machOSection->align();
	return ld::Atom::Alignment(sectionAlignment, (addr % (1 << sectionAlignment)));
}

template <typename A>
uint32_t Section<A>::sectionNum(class Parser<A>& parser) const	
{ 
	if ( _machOSection == NULL )
		return 0;
	else
		return 1 + (this->_machOSection - parser.firstMachOSection());
}

// arm does not have zero cost exceptions
template <> uint32_t CFISection<arm>::cfiCount() { return 0; }

template <typename A>
uint32_t CFISection<A>::cfiCount()
{
	// create ObjectAddressSpace object for use by libunwind
	OAS oas(*this, (uint8_t*)this->file().fileContent()+this->_machOSection->offset());
	return libunwind::CFI_Parser<OAS>::getCFICount(oas, 
										this->_machOSection->addr(), this->_machOSection->size());
}

template <typename A>
void CFISection<A>::warnFunc(void* ref, uint64_t funcAddr, const char* msg)
{
	Parser<A>* parser = (Parser<A>*)ref;
	if ( ! parser->convertUnwindInfo() ) 
		return;
	if ( funcAddr != CFI_INVALID_ADDRESS ) {
		// atoms are not constructed yet, so scan symbol table for labels
		const char* name = parser->scanSymbolTableForAddress(funcAddr);
		warning("could not create compact unwind for %s: %s", name, msg);
	}
	else {
		warning("could not create compact unwind: %s", msg);
	}
}

template <>
bool CFISection<x86_64>::needsRelocating()
{
	return true;
}

template <typename A>
bool CFISection<A>::needsRelocating()
{
	return false;
}

template <>
void CFISection<x86_64>::cfiParse(class Parser<x86_64>& parser, uint8_t* buffer, 
									libunwind::CFI_Atom_Info<CFISection<x86_64>::OAS> cfiArray[], 
									uint32_t count)
{
	// copy __eh_frame data to buffer
	memcpy(buffer, file().fileContent() + this->_machOSection->offset(), this->_machOSection->size());

	// and apply relocations
	const macho_relocation_info<P>* relocs = (macho_relocation_info<P>*)(file().fileContent() + this->_machOSection->reloff());
	const macho_relocation_info<P>* relocsEnd = &relocs[this->_machOSection->nreloc()];
	for (const macho_relocation_info<P>* reloc = relocs; reloc < relocsEnd; ++reloc) {
		uint64_t value = 0;
		switch ( reloc->r_type() ) {
			case X86_64_RELOC_SUBTRACTOR:	
				value =  0 - parser.symbolFromIndex(reloc->r_symbolnum()).n_value();
				++reloc;
				if ( reloc->r_extern() )
					value += parser.symbolFromIndex(reloc->r_symbolnum()).n_value();
				break;
			case X86_64_RELOC_UNSIGNED:
				value = parser.symbolFromIndex(reloc->r_symbolnum()).n_value();
				break;
			case X86_64_RELOC_GOT:
				// this is used for the reference to the personality function in CIEs
				// store the symbol number of the personality function for later use as a Fixup
				value = reloc->r_symbolnum();
				break;
			default:
				fprintf(stderr, "CFISection::cfiParse() unexpected relocation type at r_address=0x%08X\n", reloc->r_address());
				break;
		}
		uint64_t*	p64;
		uint32_t*	p32;
		switch ( reloc->r_length() ) {
			case 3:
				p64 = (uint64_t*)&buffer[reloc->r_address()];
				E::set64(*p64, value + E::get64(*p64));
				break;
			case 2:
				p32 = (uint32_t*)&buffer[reloc->r_address()];
				E::set32(*p32, value + E::get32(*p32));
				break;
			default:
				fprintf(stderr, "CFISection::cfiParse() unexpected relocation size at r_address=0x%08X\n", reloc->r_address());
				break;
		}
	}
	
	
	// create ObjectAddressSpace object for use by libunwind
	OAS oas(*this, buffer);
	
	// use libuwind to parse __eh_frame data into array of CFI_Atom_Info
	const char* msg;
	msg = libunwind::DwarfInstructions<OAS, libunwind::Registers_x86_64>::parseCFIs(
							oas, this->_machOSection->addr(), this->_machOSection->size(), 
							cfiArray, count, (void*)&parser, warnFunc);
	if ( msg != NULL ) 
		throwf("malformed __eh_frame section: %s", msg);
}

template <>
void CFISection<x86>::cfiParse(class Parser<x86>& parser, uint8_t* buffer, 
									libunwind::CFI_Atom_Info<CFISection<x86>::OAS> cfiArray[], 
									uint32_t count)
{
	// create ObjectAddressSpace object for use by libunwind
	OAS oas(*this, (uint8_t*)this->file().fileContent()+this->_machOSection->offset());
	
	// use libuwind to parse __eh_frame data into array of CFI_Atom_Info
	const char* msg;
	msg = libunwind::DwarfInstructions<OAS, libunwind::Registers_x86>::parseCFIs(
							oas, this->_machOSection->addr(), this->_machOSection->size(), 
							cfiArray, count, (void*)&parser, warnFunc);
	if ( msg != NULL ) 
		throwf("malformed __eh_frame section: %s", msg);
}




template <>
void CFISection<arm>::cfiParse(class Parser<arm>& parser, uint8_t* buffer, 
									libunwind::CFI_Atom_Info<CFISection<arm>::OAS> cfiArray[], 
									uint32_t count)
{
	// arm does not use zero cost exceptions
	assert(count == 0);
}



template <typename A>
uint32_t CFISection<A>::computeAtomCount(class Parser<A>& parser, 
											struct Parser<A>::LabelAndCFIBreakIterator& it, 
											const struct Parser<A>::CFI_CU_InfoArrays& cfis)
{
	return cfis.cfiCount;
}



template <typename A>
uint32_t CFISection<A>::appendAtoms(class Parser<A>& parser, uint8_t* p, 
									struct Parser<A>::LabelAndCFIBreakIterator& it, 
									const struct Parser<A>::CFI_CU_InfoArrays& cfis)
{
	this->_beginAtoms = (Atom<A>*)p;
	// walk CFI_Atom_Info array and create atom for each entry
	const CFI_Atom_Info* start = &cfis.cfiArray[0];
	const CFI_Atom_Info* end   = &cfis.cfiArray[cfis.cfiCount];
	for(const CFI_Atom_Info* a=start; a < end; ++a) {
		Atom<A>* space = (Atom<A>*)p;
		new (space) Atom<A>(*this, (a->isCIE ? "CIE" : "FDE"), a->address, a->size, 
										ld::Atom::definitionRegular, ld::Atom::combineNever, ld::Atom::scopeTranslationUnit,
										ld::Atom::typeCFI, ld::Atom::symbolTableNotInFinalLinkedImages, 
										false, false, false, ld::Atom::Alignment(0));
		p += sizeof(Atom<A>);
	}
	this->_endAtoms = (Atom<A>*)p;
	return cfis.cfiCount;
}


template <> bool CFISection<x86_64>::bigEndian() { return false; }
template <> bool CFISection<x86>::bigEndian() { return false; }
template <> bool CFISection<arm>::bigEndian() { return false; }


template <>
void CFISection<x86_64>::addCiePersonalityFixups(class Parser<x86_64>& parser, const CFI_Atom_Info* cieInfo)
{
	uint8_t personalityEncoding = cieInfo->u.cieInfo.personality.encodingOfTargetAddress;
	if ( personalityEncoding == 0x9B ) {
		// compiler always produces X86_64_RELOC_GOT with addend of 4 to personality function
		// CFISection<x86_64>::cfiParse() set targetAddress to be symbolIndex + 4 + addressInCIE
		uint32_t symbolIndex = cieInfo->u.cieInfo.personality.targetAddress - 4 
									- cieInfo->address - cieInfo->u.cieInfo.personality.offsetInCFI;
		const macho_nlist<P>& sym = parser.symbolFromIndex(symbolIndex);
		const char* personalityName = parser.nameFromSymbol(sym);

		Atom<x86_64>* cieAtom = this->findAtomByAddress(cieInfo->address);
		Parser<x86_64>::SourceLocation src(cieAtom, cieInfo->u.cieInfo.personality.offsetInCFI);
		parser.addFixup(src, ld::Fixup::k1of3, ld::Fixup::kindSetTargetAddress, false, personalityName);
		parser.addFixup(src, ld::Fixup::k2of3, ld::Fixup::kindAddAddend, 4);
		parser.addFixup(src, ld::Fixup::k3of3, ld::Fixup::kindStoreX86PCRel32GOT);
	}
	else if ( personalityEncoding != 0 ) {
		throwf("unsupported address encoding (%02X) of personality function in CIE", 
				personalityEncoding);
	}
}

template <>
void CFISection<x86>::addCiePersonalityFixups(class Parser<x86>& parser, const CFI_Atom_Info* cieInfo)
{
	uint8_t personalityEncoding = cieInfo->u.cieInfo.personality.encodingOfTargetAddress;
	if ( (personalityEncoding == 0x9B) || (personalityEncoding == 0x90) ) {
		uint32_t offsetInCFI = cieInfo->u.cieInfo.personality.offsetInCFI;
		uint32_t nlpAddr = cieInfo->u.cieInfo.personality.targetAddress;
		Atom<x86>* cieAtom = this->findAtomByAddress(cieInfo->address);
		Atom<x86>* nlpAtom = parser.findAtomByAddress(nlpAddr);
		assert(nlpAtom->contentType() == ld::Atom::typeNonLazyPointer);
		Parser<x86>::SourceLocation src(cieAtom, cieInfo->u.cieInfo.personality.offsetInCFI);

		parser.addFixup(src, ld::Fixup::k1of4, ld::Fixup::kindSetTargetAddress, ld::Fixup::bindingByContentBound, nlpAtom);
		parser.addFixup(src, ld::Fixup::k2of4, ld::Fixup::kindSubtractTargetAddress, cieAtom);
		parser.addFixup(src, ld::Fixup::k3of4, ld::Fixup::kindSubtractAddend, offsetInCFI);
		parser.addFixup(src, ld::Fixup::k4of4, ld::Fixup::kindStoreLittleEndian32);
	}
	else if ( personalityEncoding != 0 ) {
		throwf("unsupported address encoding (%02X) of personality function in CIE", personalityEncoding);
	}
}


template <typename A>
void CFISection<A>::addCiePersonalityFixups(class Parser<A>& parser, const CFI_Atom_Info* cieInfo)
{
	// FIX ME
	assert(0);
}

template <typename A>
void CFISection<A>::makeFixups(class Parser<A>& parser, const struct Parser<A>::CFI_CU_InfoArrays& cfis)
{
	ld::Fixup::Kind store32 = bigEndian() ? ld::Fixup::kindStoreBigEndian32 : ld::Fixup::kindStoreLittleEndian32;
	ld::Fixup::Kind store64 = bigEndian() ? ld::Fixup::kindStoreBigEndian64 : ld::Fixup::kindStoreLittleEndian64;

	// add all references for FDEs, including implicit group references
	const CFI_Atom_Info* end = &cfis.cfiArray[cfis.cfiCount];
	for(const CFI_Atom_Info* p = &cfis.cfiArray[0]; p < end; ++p) {
		if ( p->isCIE ) {
			// add reference to personality function if used
			if ( p->u.cieInfo.personality.targetAddress != CFI_INVALID_ADDRESS ) {
				this->addCiePersonalityFixups(parser, p);
			}
		}
		else {
			// find FDE Atom
			Atom<A>* fdeAtom = this->findAtomByAddress(p->address);
			// find function Atom
			Atom<A>* functionAtom = parser.findAtomByAddress(p->u.fdeInfo.function.targetAddress);
			// find CIE Atom
			Atom<A>* cieAtom = this->findAtomByAddress(p->u.fdeInfo.cie.targetAddress);
			// find LSDA Atom
			Atom<A>* lsdaAtom = NULL;
			if ( p->u.fdeInfo.lsda.targetAddress != CFI_INVALID_ADDRESS ) {
				lsdaAtom = parser.findAtomByAddress(p->u.fdeInfo.lsda.targetAddress);
			}
			// add reference from FDE to CIE (always 32-bit pc-rel)
			typename Parser<A>::SourceLocation fdeToCieSrc(fdeAtom, p->u.fdeInfo.cie.offsetInCFI);
			parser.addFixup(fdeToCieSrc, ld::Fixup::k1of4, ld::Fixup::kindSetTargetAddress, fdeAtom);
			parser.addFixup(fdeToCieSrc, ld::Fixup::k2of4, ld::Fixup::kindAddAddend, p->u.fdeInfo.cie.offsetInCFI);
			parser.addFixup(fdeToCieSrc, ld::Fixup::k3of4, ld::Fixup::kindSubtractTargetAddress, cieAtom);
			parser.addFixup(fdeToCieSrc, ld::Fixup::k4of4, store32, cieAtom);

			// add reference from FDE to function
			typename Parser<A>::SourceLocation fdeToFuncSrc(fdeAtom, p->u.fdeInfo.function.offsetInCFI);
			switch (p->u.fdeInfo.function.encodingOfTargetAddress) {
				case DW_EH_PE_pcrel|DW_EH_PE_ptr:
					if ( sizeof(typename A::P::uint_t) == 8 ) {
						parser.addFixup(fdeToFuncSrc, ld::Fixup::k1of4, ld::Fixup::kindSetTargetAddress, functionAtom);
						parser.addFixup(fdeToFuncSrc, ld::Fixup::k2of4, ld::Fixup::kindSubtractTargetAddress, fdeAtom);
						parser.addFixup(fdeToFuncSrc, ld::Fixup::k3of4, ld::Fixup::kindSubtractAddend, p->u.fdeInfo.function.offsetInCFI);
						parser.addFixup(fdeToFuncSrc, ld::Fixup::k4of4, store64);
						break;
					}
					// else fall into 32-bit case
				case DW_EH_PE_pcrel|DW_EH_PE_sdata4:
					parser.addFixup(fdeToFuncSrc, ld::Fixup::k1of4, ld::Fixup::kindSetTargetAddress, functionAtom);
					parser.addFixup(fdeToFuncSrc, ld::Fixup::k2of4, ld::Fixup::kindSubtractTargetAddress, fdeAtom);
					parser.addFixup(fdeToFuncSrc, ld::Fixup::k3of4, ld::Fixup::kindSubtractAddend, p->u.fdeInfo.function.offsetInCFI);
					parser.addFixup(fdeToFuncSrc, ld::Fixup::k4of4, store32);
					break;
				default:
					throw "unsupported encoding in FDE of pointer to function";
			}
		
			// add reference from FDE to LSDA
			typename Parser<A>::SourceLocation fdeToLsdaSrc(fdeAtom,  p->u.fdeInfo.lsda.offsetInCFI);
			if ( lsdaAtom != NULL ) {
				switch (p->u.fdeInfo.lsda.encodingOfTargetAddress) {
					case DW_EH_PE_pcrel|DW_EH_PE_ptr:
						if ( sizeof(typename A::P::uint_t) == 8 ) {
							parser.addFixup(fdeToLsdaSrc, ld::Fixup::k1of4, ld::Fixup::kindSetTargetAddress, lsdaAtom);
							parser.addFixup(fdeToLsdaSrc, ld::Fixup::k2of4, ld::Fixup::kindSubtractTargetAddress, fdeAtom);
							parser.addFixup(fdeToLsdaSrc, ld::Fixup::k3of4, ld::Fixup::kindSubtractAddend, p->u.fdeInfo.lsda.offsetInCFI);
							parser.addFixup(fdeToLsdaSrc, ld::Fixup::k4of4, store64);
							break;
						}
						// else fall into 32-bit case
					case DW_EH_PE_pcrel|DW_EH_PE_sdata4:
						parser.addFixup(fdeToLsdaSrc, ld::Fixup::k1of4, ld::Fixup::kindSetTargetAddress, lsdaAtom);
						parser.addFixup(fdeToLsdaSrc, ld::Fixup::k2of4, ld::Fixup::kindSubtractTargetAddress, fdeAtom);
						parser.addFixup(fdeToLsdaSrc, ld::Fixup::k3of4, ld::Fixup::kindSubtractAddend, p->u.fdeInfo.lsda.offsetInCFI);
						parser.addFixup(fdeToLsdaSrc, ld::Fixup::k4of4, store32);
					break;
					default:
						throw "unsupported encoding in FDE of pointer to LSDA";
				}
			}
			
			// FDE is in group lead by function atom
			typename Parser<A>::SourceLocation fdeSrc(functionAtom,0);
			parser.addFixup(fdeSrc, ld::Fixup::k1of1, ld::Fixup::kindNoneGroupSubordinateFDE, fdeAtom);
			
			// LSDA is in group lead by function atom
			if ( lsdaAtom != NULL ) {
				parser.addFixup(fdeSrc, ld::Fixup::k1of1, ld::Fixup::kindNoneGroupSubordinateLSDA, lsdaAtom);
			}
		}
	}
}




template <typename A>
const void*	 CFISection<A>::OAS::mappedAddress(pint_t addr)
{
	if ( (_ehFrameStartAddr <= addr) && (addr < _ehFrameEndAddr) )
		return &_ehFrameContent[addr-_ehFrameStartAddr];
	else {
		// requested bytes are not in __eh_frame section
		// this can occur when examining the instruction bytes in the __text
		File<A>& file = _ehFrameSection.file();
		for (uint32_t i=0; i < file._sectionsArrayCount; ++i ) {
			const macho_section<typename A::P>* sect = file._sectionsArray[i]->machoSection();
			// TentativeDefinitionSection and AbsoluteSymbolSection have no mach-o section
			if ( sect != NULL ) {
				if ( (sect->addr() <= addr) && (addr < (sect->addr()+sect->size())) ) {
					return file.fileContent() + sect->offset() + addr - sect->addr();
				}
			}
		}
		throwf("__eh_frame parsing problem.  Can't find target of reference to address 0x%08llX", (uint64_t)addr);
	}
}
		

template <typename A>
uint64_t CFISection<A>::OAS::getULEB128(pint_t& logicalAddr, pint_t end)
{
	uintptr_t size = (end - logicalAddr);
	libunwind::LocalAddressSpace::pint_t laddr = (libunwind::LocalAddressSpace::pint_t)mappedAddress(logicalAddr);
	libunwind::LocalAddressSpace::pint_t sladdr = laddr;
	uint64_t result = libunwind::LocalAddressSpace::getULEB128(laddr, laddr+size);
	logicalAddr += (laddr-sladdr);
	return result;
}

template <typename A>
int64_t CFISection<A>::OAS::getSLEB128(pint_t& logicalAddr, pint_t end)
{
	uintptr_t size = (end - logicalAddr);
	libunwind::LocalAddressSpace::pint_t laddr = (libunwind::LocalAddressSpace::pint_t)mappedAddress(logicalAddr);
	libunwind::LocalAddressSpace::pint_t sladdr = laddr;
	int64_t result = libunwind::LocalAddressSpace::getSLEB128(laddr, laddr+size);
	logicalAddr += (laddr-sladdr);
	return result;
}

template <typename A>
typename A::P::uint_t CFISection<A>::OAS::getEncodedP(pint_t& addr, pint_t end, uint8_t encoding)
{
	pint_t startAddr = addr;
	pint_t p = addr;
	pint_t result;

	// first get value
	switch (encoding & 0x0F) {
		case DW_EH_PE_ptr:
			result = getP(addr);
			p += sizeof(pint_t);
			addr = (pint_t)p;
			break;
		case DW_EH_PE_uleb128:
			result = getULEB128(addr, end);
			break;
		case DW_EH_PE_udata2:
			result = get16(addr);
			p += 2;
			addr = (pint_t)p;
			break;
		case DW_EH_PE_udata4:
			result = get32(addr);
			p += 4;
			addr = (pint_t)p;
			break;
		case DW_EH_PE_udata8:
			result = get64(addr);
			p += 8;
			addr = (pint_t)p;
			break;
		case DW_EH_PE_sleb128:
			result = getSLEB128(addr, end);
			break;
		case DW_EH_PE_sdata2:
			result = (int16_t)get16(addr);
			p += 2;
			addr = (pint_t)p;
			break;
		case DW_EH_PE_sdata4:
			result = (int32_t)get32(addr);
			p += 4;
			addr = (pint_t)p;
			break;
		case DW_EH_PE_sdata8:
			result = get64(addr);
			p += 8;
			addr = (pint_t)p;
			break;
		default:
			throwf("ObjectFileAddressSpace<A>::getEncodedP() encoding 0x%08X not supported", encoding);
	}
	
	// then add relative offset
	switch ( encoding & 0x70 ) {
		case DW_EH_PE_absptr:
			// do nothing
			break;
		case DW_EH_PE_pcrel:
			result += startAddr;
			break;
		case DW_EH_PE_textrel:
			throw "DW_EH_PE_textrel pointer encoding not supported";
			break;
		case DW_EH_PE_datarel:
			throw "DW_EH_PE_datarel pointer encoding not supported";
			break;
		case DW_EH_PE_funcrel:
			throw "DW_EH_PE_funcrel pointer encoding not supported";
			break;
		case DW_EH_PE_aligned:
			throw "DW_EH_PE_aligned pointer encoding not supported";
			break;
		default:
			throwf("ObjectFileAddressSpace<A>::getEncodedP() encoding 0x%08X not supported", encoding);
			break;
	}

//  Note: DW_EH_PE_indirect is only used in CIEs to refernce the personality pointer
//  When parsing .o files that pointer contains zero, so we don't to return that.
//  Instead we skip the dereference and return the address of the pointer.
//	if ( encoding & DW_EH_PE_indirect )
//		result = getP(result);
	
	return result;
}

template <>
const char* CUSection<x86_64>::personalityName(class Parser<x86_64>& parser, const macho_relocation_info<x86_64::P>* reloc)
{
	assert(reloc->r_extern() && "reloc not extern on personality column in __compact_unwind section");
	assert((reloc->r_type() == X86_64_RELOC_UNSIGNED) && "wrong reloc type on personality column in __compact_unwind section");
	const macho_nlist<P>& sym = parser.symbolFromIndex(reloc->r_symbolnum());
	return parser.nameFromSymbol(sym);
}

template <>
const char* CUSection<x86>::personalityName(class Parser<x86>& parser, const macho_relocation_info<x86::P>* reloc)
{
	assert(reloc->r_extern() && "reloc not extern on personality column in __compact_unwind section");
	assert((reloc->r_type() == GENERIC_RELOC_VANILLA) && "wrong reloc type on personality column in __compact_unwind section");
	const macho_nlist<P>& sym = parser.symbolFromIndex(reloc->r_symbolnum());
	return parser.nameFromSymbol(sym);
}

template <typename A>
const char* CUSection<A>::personalityName(class Parser<A>& parser, const macho_relocation_info<P>* reloc)
{
	return NULL;
}


template <typename A>
int CUSection<A>::infoSorter(const void* l, const void* r)
{
	// sort references by symbol index, then address 
	const Info* left = (Info*)l;
	const Info* right = (Info*)r;
	if ( left->functionSymbolIndex == right->functionSymbolIndex )
		return (left->functionStartAddress - right->functionStartAddress);
	else
		return (left->functionSymbolIndex - right->functionSymbolIndex);
}

template <typename A>
void CUSection<A>::parse(class Parser<A>& parser, uint32_t cnt, Info array[])
{
	// walk section content and copy to Info array
	const macho_compact_unwind_entry<P>* const entries = (macho_compact_unwind_entry<P>*)(this->file().fileContent() + this->_machOSection->offset());
	for (uint32_t i=0; i < cnt; ++i) {
		Info* info = &array[i];
		const macho_compact_unwind_entry<P>* entry = &entries[i];
		info->functionStartAddress	= entry->codeStart();
		info->functionSymbolIndex   = 0xFFFFFFFF;
		info->rangeLength			= entry->codeLen();
		info->compactUnwindInfo		= entry->compactUnwindInfo();
		info->personality			= NULL;
		info->lsdaAddress			= entry->lsda();
		info->function				= NULL;
		info->lsda					= NULL;
		if ( (info->compactUnwindInfo & UNWIND_PERSONALITY_MASK) != 0 )
			warning("no bits should be set in UNWIND_PERSONALITY_MASK of compact unwind encoding in __LD,__compact_unwind section");
		if ( info->lsdaAddress != 0 ) {
			info->compactUnwindInfo |= UNWIND_HAS_LSDA;
		}
	}
	
	// scan relocs, local relocs are useless - ignore them
	// extern relocs are needed for personality references (possibly for function/lsda refs??)
	const macho_relocation_info<P>* relocs = (macho_relocation_info<P>*)(this->file().fileContent() + this->_machOSection->reloff());
	const macho_relocation_info<P>* relocsEnd = &relocs[this->_machOSection->nreloc()];
	for (const macho_relocation_info<P>* reloc = relocs; reloc < relocsEnd; ++reloc) {
		if ( reloc->r_extern() ) {
			// only expect external relocs on some colummns
			if ( (reloc->r_address() % sizeof(macho_compact_unwind_entry<P>)) == macho_compact_unwind_entry<P>::personalityFieldOffset() ) {
				uint32_t entryIndex = reloc->r_address() / sizeof(macho_compact_unwind_entry<P>);
				array[entryIndex].personality = this->personalityName(parser, reloc);
			}
			else if ( (reloc->r_address() % sizeof(macho_compact_unwind_entry<P>)) == macho_compact_unwind_entry<P>::lsdaFieldOffset() ) {
				uint32_t entryIndex = reloc->r_address() / sizeof(macho_compact_unwind_entry<P>);
				const macho_nlist<P>& lsdaSym = parser.symbolFromIndex(reloc->r_symbolnum());
				if ( (lsdaSym.n_type() & N_TYPE) == N_SECT ) 
					array[entryIndex].lsdaAddress = lsdaSym.n_value();
				else
					warning("unexpected extern relocation to lsda in __compact_unwind section");
			}
			else if ( (reloc->r_address() % sizeof(macho_compact_unwind_entry<P>)) == macho_compact_unwind_entry<P>::codeStartFieldOffset() ) {
				uint32_t entryIndex = reloc->r_address() / sizeof(macho_compact_unwind_entry<P>);
				array[entryIndex].functionSymbolIndex = reloc->r_symbolnum();
			}
			else {
				warning("unexpected extern relocation in __compact_unwind section");
			}
		}
	}
	
	// sort array by function start address so unwind infos will be contiguous for a given function
	::qsort(array, cnt, sizeof(Info), infoSorter);
}

template <typename A>
uint32_t CUSection<A>::count()
{
	const macho_section<P>*	machoSect =	this->machoSection();
	if ( (machoSect->size() % sizeof(macho_compact_unwind_entry<P>)) != 0 )
		throw "malformed __LD,__compact_unwind section, bad length";
		
	return machoSect->size() / sizeof(macho_compact_unwind_entry<P>);
}

template <typename A>
void CUSection<A>::makeFixups(class Parser<A>& parser, const struct Parser<A>::CFI_CU_InfoArrays& cus)
{
	Info* const arrayStart = cus.cuArray;
	Info* const arrayEnd = &cus.cuArray[cus.cuCount];
	for (Info* info=arrayStart; info < arrayEnd; ++info) {
		// if external reloc was used, real address is symbol n_value + addend
		if ( info->functionSymbolIndex != 0xFFFFFFFF )
			info->functionStartAddress += parser.symbolFromIndex(info->functionSymbolIndex).n_value();
		// find function atom from address
		info->function = parser.findAtomByAddress(info->functionStartAddress);	
		// find lsda atom from address
		if ( info->lsdaAddress != 0 ) {
			info->lsda = parser.findAtomByAddress(info->lsdaAddress);		
			// add lsda subordinate
			typename Parser<A>::SourceLocation src(info->function, info->functionStartAddress - info->function->objectAddress());
			parser.addFixup(src, ld::Fixup::k1of1, ld::Fixup::kindNoneGroupSubordinateLSDA, info->lsda);
		}
		if ( info->personality != NULL ) {
			// add personality subordinate
			typename Parser<A>::SourceLocation src(info->function, info->functionStartAddress - info->function->objectAddress());
			parser.addFixup(src, ld::Fixup::k1of1, ld::Fixup::kindNoneGroupSubordinatePersonality, false, info->personality);
		}
	}
	
}

template <typename A>
SymboledSection<A>::SymboledSection(Parser<A>& parser, File<A>& f, const macho_section<typename A::P>* s)
	: Section<A>(f, s), _type(ld::Atom::typeUnclassified) 
{
	switch ( s->flags() & SECTION_TYPE ) {
		case S_ZEROFILL:
			_type = ld::Atom::typeZeroFill;
			break;
		case S_MOD_INIT_FUNC_POINTERS:
			_type = ld::Atom::typeInitializerPointers;
			break;
		case S_MOD_TERM_FUNC_POINTERS:
			_type = ld::Atom::typeTerminatorPointers;
			break;
		case S_THREAD_LOCAL_VARIABLES:
			_type = ld::Atom::typeTLV;
			break;
		case S_THREAD_LOCAL_ZEROFILL:
			_type = ld::Atom::typeTLVZeroFill;
			break;
		case S_THREAD_LOCAL_REGULAR:
			_type = ld::Atom::typeTLVInitialValue;
			break;
		case S_THREAD_LOCAL_INIT_FUNCTION_POINTERS:
			_type = ld::Atom::typeTLVInitializerPointers;
			break;
		case S_REGULAR:
			if ( strncmp(s->sectname(), "__gcc_except_tab", 16) == 0 )
				_type = ld::Atom::typeLSDA;
			else if ( this->type() == ld::Section::typeInitializerPointers )
				_type = ld::Atom::typeInitializerPointers;
			break;
	}
}


template <typename A>
bool SymboledSection<A>::dontDeadStrip() 
{
	switch ( _type ) {
		case ld::Atom::typeInitializerPointers:
		case ld::Atom::typeTerminatorPointers:
			return true;
		default:
			// model an object file without MH_SUBSECTIONS_VIA_SYMBOLS as one in which nothing can be dead stripped
			if ( ! this->_file.canScatterAtoms() )
				return true;
			// call inherited
			return Section<A>::dontDeadStrip();
	}
	return false;
}


template <typename A>
uint32_t SymboledSection<A>::computeAtomCount(class Parser<A>& parser, 
												struct Parser<A>::LabelAndCFIBreakIterator& it, 
												const struct Parser<A>::CFI_CU_InfoArrays&)
{
	const pint_t startAddr = this->_machOSection->addr();
	const pint_t endAddr = startAddr + this->_machOSection->size();
	const uint32_t sectNum = this->sectionNum(parser);

	uint32_t count = 0;
	pint_t	addr;
	pint_t	size;
	const macho_nlist<P>* sym;
	while ( it.next(parser, sectNum, startAddr, endAddr, &addr, &size, &sym) ) {
		++count;
	}
	//fprintf(stderr, "computeAtomCount(%s,%s) => %d\n", this->segmentName(), this->sectionName(), count);
	return count;
}

template <typename A>
uint32_t SymboledSection<A>::appendAtoms(class Parser<A>& parser, uint8_t* p, 
											struct Parser<A>::LabelAndCFIBreakIterator& it, 
											const struct Parser<A>::CFI_CU_InfoArrays&)
{
	this->_beginAtoms = (Atom<A>*)p;

	//fprintf(stderr, "SymboledSection::appendAtoms() in section %s\n", this->_machOSection->sectname());
	const pint_t startAddr = this->_machOSection->addr();
	const pint_t endAddr = startAddr + this->_machOSection->size();
	const uint32_t sectNum = this->sectionNum(parser);

	uint32_t count = 0;
	pint_t	addr;
	pint_t	size;
	const macho_nlist<P>* label;
	while ( it.next(parser, sectNum, startAddr, endAddr, &addr, &size, &label) ) {
		Atom<A>* allocatedSpace = (Atom<A>*)p;
		// is break because of label or CFI?
		if ( label != NULL ) {
			// The size is computed based on the address of the next label (or the end of the section for the last label)
			// If there are two labels at the same address, we want them one to be an alias of the other.
			// If the label is at the end of a section, it is has zero size, but is not an alias
			const bool isAlias = ( (size == 0) && (addr <  endAddr) );
			new (allocatedSpace) Atom<A>(*this, parser, *label, size, isAlias);
			if ( isAlias )
				this->_hasAliases = true;
		}
		else {
			ld::Atom::SymbolTableInclusion inclusion = ld::Atom::symbolTableNotIn;
			ld::Atom::ContentType ctype = this->contentType();
			if ( ctype == ld::Atom::typeLSDA )
				inclusion = ld::Atom::symbolTableInWithRandomAutoStripLabel;
			new (allocatedSpace) Atom<A>(*this, "anon", addr, size, ld::Atom::definitionRegular, ld::Atom::combineNever, 
										ld::Atom::scopeTranslationUnit, ctype, inclusion, 
										this->dontDeadStrip(), false, false, this->alignmentForAddress(addr));
		}
		p += sizeof(Atom<A>);
		++count;
	}

	this->_endAtoms = (Atom<A>*)p;
	return count;
}


template <typename A>
uint32_t ImplicitSizeSection<A>::computeAtomCount(class Parser<A>& parser, 
													struct Parser<A>::LabelAndCFIBreakIterator& it, 
													const struct Parser<A>::CFI_CU_InfoArrays&)
{
	uint32_t count = 0;
	const macho_section<P>* sect = this->machoSection();
	const pint_t startAddr = sect->addr();
	const pint_t endAddr = startAddr + sect->size();
	for (pint_t addr = startAddr; addr < endAddr; addr += elementSizeAtAddress(addr) ) {
		if ( useElementAt(parser, it, addr) ) 
			++count;
	}
	if ( it.fileHasOverlappingSymbols && (sect->size() != 0) && (this->combine(parser, startAddr) == ld::Atom::combineByNameAndContent) ) {
		// if there are multiple labels in this section for the same address, then clone them into multi atoms
		pint_t  prevSymbolAddr = (pint_t)(-1);
		uint8_t prevSymbolSectNum = 0;
		for(uint32_t i=0; i < it.sortedSymbolCount; ++i) {
			const macho_nlist<P>& sym = parser.symbolFromIndex(it.sortedSymbolIndexes[i]);
			const pint_t symbolAddr = sym.n_value();
			const pint_t symbolSectNum = sym.n_sect();
			if ( (symbolAddr == prevSymbolAddr) && (prevSymbolSectNum == symbolSectNum) && (symbolSectNum == this->sectionNum(parser)) ) { 
				++count;
			}
			prevSymbolAddr = symbolAddr;
			prevSymbolSectNum = symbolSectNum;
		}
	}
	return count;
}

template <typename A>
uint32_t ImplicitSizeSection<A>::appendAtoms(class Parser<A>& parser, uint8_t* p, 
											struct Parser<A>::LabelAndCFIBreakIterator& it, 
											const struct Parser<A>::CFI_CU_InfoArrays&)
{
	this->_beginAtoms = (Atom<A>*)p;
	
	const macho_section<P>* sect = this->machoSection();
	const pint_t startAddr = sect->addr();
	const pint_t endAddr = startAddr + sect->size();
	const uint32_t sectNum = this->sectionNum(parser);
	//fprintf(stderr, "ImplicitSizeSection::appendAtoms() in section %s\n", sect->sectname());
	uint32_t count = 0;
	pint_t	foundAddr;
	pint_t	size;
	const macho_nlist<P>* foundLabel;
	Atom<A>* allocatedSpace;
	while ( it.next(parser, sectNum, startAddr, endAddr, &foundAddr, &size, &foundLabel) ) {
		if ( foundLabel != NULL ) {
			pint_t labeledAtomSize = this->elementSizeAtAddress(foundAddr);
			allocatedSpace = (Atom<A>*)p;
			if ( this->ignoreLabel(parser.nameFromSymbol(*foundLabel)) ) {
				//fprintf(stderr, "  0x%08llX make annon\n", (uint64_t)foundAddr);
				new (allocatedSpace) Atom<A>(*this, this->unlabeledAtomName(parser, foundAddr), foundAddr, 
											this->elementSizeAtAddress(foundAddr), this->definition(), 
											this->combine(parser, foundAddr), this->scopeAtAddress(parser, foundAddr), 
											this->contentType(), this->symbolTableInclusion(), 
											this->dontDeadStrip(), false, false, this->alignmentForAddress(foundAddr));
			}
			else {
				// make named atom for label
				//fprintf(stderr, "  0x%08llX make labeled\n", (uint64_t)foundAddr);
				new (allocatedSpace) Atom<A>(*this, parser, *foundLabel, labeledAtomSize);
			}
			++count;
			p += sizeof(Atom<A>);
			foundAddr += labeledAtomSize;
			size -= labeledAtomSize;
		}
		// some number of anonymous atoms
		for (pint_t addr = foundAddr; addr < (foundAddr+size); addr += elementSizeAtAddress(addr) ) {
			// make anon atoms for area before label
			if ( this->useElementAt(parser, it, addr) ) {
				//fprintf(stderr, "  0x%08llX make annon\n", (uint64_t)addr);
				allocatedSpace = (Atom<A>*)p;
				new (allocatedSpace) Atom<A>(*this, this->unlabeledAtomName(parser, addr), addr, this->elementSizeAtAddress(addr), 
											this->definition(), this->combine(parser, addr), this->scopeAtAddress(parser, addr), 
											this->contentType(), this->symbolTableInclusion(), 
											this->dontDeadStrip(), false, false, this->alignmentForAddress(addr));
				++count;
				p += sizeof(Atom<A>);
			}
		}
	}

	this->_endAtoms = (Atom<A>*)p;

	return count;
}


template <typename A>
unsigned long Literal4Section<A>::contentHash(const class Atom<A>* atom, const ld::IndirectBindingTable& ind) const
{
	const uint32_t* literalContent = (uint32_t*)atom->contentPointer();
	return *literalContent;
}

template <typename A>
bool Literal4Section<A>::canCoalesceWith(const class Atom<A>* atom, const ld::Atom& rhs, 
													const ld::IndirectBindingTable& ind) const
{
	assert(this->type() == rhs.section().type());
	const uint32_t* literalContent = (uint32_t*)atom->contentPointer();
	
	const Atom<A>* rhsAtom = dynamic_cast<const Atom<A>*>(&rhs);
	assert(rhsAtom != NULL);
	if ( rhsAtom != NULL ) {
		const uint32_t* rhsLiteralContent = (uint32_t*)rhsAtom->contentPointer();
		return (*literalContent == *rhsLiteralContent);
	}
	return false;
}


template <typename A>
unsigned long Literal8Section<A>::contentHash(const class Atom<A>* atom, const ld::IndirectBindingTable& ind) const
{
#if __LP64__
	const uint64_t* literalContent = (uint64_t*)atom->contentPointer();
	return *literalContent;
#else
	unsigned long hash = 5381;
	const uint8_t* byteContent = atom->contentPointer();
	for (int i=0; i < 8; ++i) {
		hash = hash * 33 + byteContent[i];
	}
	return hash;
#endif
}

template <typename A>
bool Literal8Section<A>::canCoalesceWith(const class Atom<A>* atom, const ld::Atom& rhs, 
													const ld::IndirectBindingTable& ind) const
{
	if ( rhs.section().type() != ld::Section::typeLiteral8 )
		return false;
	assert(this->type() == rhs.section().type());
	const uint64_t* literalContent = (uint64_t*)atom->contentPointer();
	
	const Atom<A>* rhsAtom = dynamic_cast<const Atom<A>*>(&rhs);
	assert(rhsAtom != NULL);
	if ( rhsAtom != NULL ) {
		const uint64_t* rhsLiteralContent = (uint64_t*)rhsAtom->contentPointer();
		return (*literalContent == *rhsLiteralContent);
	}
	return false;
}


template <typename A>
unsigned long Literal16Section<A>::contentHash(const class Atom<A>* atom, const ld::IndirectBindingTable& ind) const
{
	unsigned long hash = 5381;
	const uint8_t* byteContent = atom->contentPointer();
	for (int i=0; i < 16; ++i) {
		hash = hash * 33 + byteContent[i];
	}
	return hash;
}

template <typename A>
bool Literal16Section<A>::canCoalesceWith(const class Atom<A>* atom, const ld::Atom& rhs, 
													const ld::IndirectBindingTable& ind) const
{
	if ( rhs.section().type() != ld::Section::typeLiteral16 )
		return false;
	assert(this->type() == rhs.section().type());
	const uint64_t* literalContent = (uint64_t*)atom->contentPointer();
	
	const Atom<A>* rhsAtom = dynamic_cast<const Atom<A>*>(&rhs);
	assert(rhsAtom != NULL);
	if ( rhsAtom != NULL ) {
		const uint64_t* rhsLiteralContent = (uint64_t*)rhsAtom->contentPointer();
		return ((literalContent[0] == rhsLiteralContent[0]) && (literalContent[1] == rhsLiteralContent[1]));
	}
	return false;
}



template <typename A>
typename A::P::uint_t CStringSection<A>::elementSizeAtAddress(pint_t addr)
{
	const macho_section<P>* sect = this->machoSection();
	const char* stringContent = (char*)(this->file().fileContent() + sect->offset() + addr - sect->addr());
	return strlen(stringContent) + 1;
}

template <typename A>
bool CStringSection<A>::useElementAt(Parser<A>& parser, struct Parser<A>::LabelAndCFIBreakIterator& it, pint_t addr)
{
	return true;
}

template <typename A>
bool CStringSection<A>::ignoreLabel(const char* label)  
{ 
	return (label[0] == 'L') || (label[0] == 'l'); 
}

template <typename A>
Atom<A>* CStringSection<A>::findAtomByAddress(pint_t addr)
{
	Atom<A>* result = this->findContentAtomByAddress(addr, this->_beginAtoms, this->_endAtoms);
	return result;
}

template <typename A>
unsigned long CStringSection<A>::contentHash(const class Atom<A>* atom, const ld::IndirectBindingTable& ind) const
{
	unsigned long hash = 5381;
	const char* stringContent = (char*)atom->contentPointer();
	for (const char* s = stringContent; *s != '\0'; ++s) {
		hash = hash * 33 + *s;
	}
	return hash;
}


template <typename A>
bool CStringSection<A>::canCoalesceWith(const class Atom<A>* atom, const ld::Atom& rhs, 
													const ld::IndirectBindingTable& ind) const
{
	if ( rhs.section().type() != ld::Section::typeCString )
		return false;
	assert(this->type() == rhs.section().type());
	assert(strcmp(this->sectionName(), rhs.section().sectionName())== 0);
	assert(strcmp(this->segmentName(), rhs.section().segmentName())== 0);
	const char* stringContent = (char*)atom->contentPointer();
	
	const Atom<A>* rhsAtom = dynamic_cast<const Atom<A>*>(&rhs);
	assert(rhsAtom != NULL);
	if ( rhsAtom != NULL ) {
		if ( atom->_size != rhsAtom->_size )
			return false;
		const char* rhsStringContent = (char*)rhsAtom->contentPointer();
		return (strcmp(stringContent, rhsStringContent) == 0);
	}
	return false;
}


template <>
ld::Fixup::Kind NonLazyPointerSection<x86>::fixupKind()
{
	return ld::Fixup::kindStoreLittleEndian32;
}

template <>
ld::Fixup::Kind NonLazyPointerSection<arm>::fixupKind()
{
	return ld::Fixup::kindStoreLittleEndian32;
}


template <>
void NonLazyPointerSection<x86_64>::makeFixups(class Parser<x86_64>& parser, const struct Parser<x86_64>::CFI_CU_InfoArrays&)
{
	assert(0 && "x86_64 should not have non-lazy-pointer sections in .o files");
}

template <typename A>
void NonLazyPointerSection<A>::makeFixups(class Parser<A>& parser, const struct Parser<A>::CFI_CU_InfoArrays&)
{
	// add references for each NLP atom based on indirect symbol table
	const macho_section<P>* sect = this->machoSection();
	const pint_t endAddr = sect->addr() + sect->size();
	for( pint_t addr = sect->addr(); addr < endAddr; addr += sizeof(pint_t)) {
		typename Parser<A>::SourceLocation	src;
		typename Parser<A>::TargetDesc		target;
		src.atom = this->findAtomByAddress(addr);
		src.offsetInAtom = 0;
		uint32_t symIndex = parser.symbolIndexFromIndirectSectionAddress(addr, sect);
		target.atom = NULL;
		target.name = NULL;
		target.weakImport = false;
		target.addend = 0;
		if ( symIndex == INDIRECT_SYMBOL_LOCAL ) {
			// use direct reference for local symbols
			const pint_t* nlpContent = (pint_t*)(this->file().fileContent() + sect->offset() + addr - sect->addr());
			pint_t targetAddr = P::getP(*nlpContent);
			target.atom = parser.findAtomByAddress(targetAddr);
			target.weakImport = false;
			target.addend = (targetAddr - target.atom->objectAddress());
			// <rdar://problem/8385011> if pointer to thumb function, mask of thumb bit (not an addend of +1)
			if ( target.atom->isThumb() )
				target.addend &= (-2); 
			assert(src.atom->combine() == ld::Atom::combineNever);
		}
		else {
			const macho_nlist<P>& sym = parser.symbolFromIndex(symIndex);
			// use direct reference for local symbols
			if ( ((sym.n_type() & N_TYPE) == N_SECT) && ((sym.n_type() & N_EXT) == 0) ) {
				parser.findTargetFromAddressAndSectionNum(sym.n_value(), sym.n_sect(), target);
				assert(src.atom->combine() == ld::Atom::combineNever);
			}
			else {
				target.name = parser.nameFromSymbol(sym);
				target.weakImport = parser.weakImportFromSymbol(sym);
				assert(src.atom->combine() == ld::Atom::combineByNameAndReferences);
			}
		}
		parser.addFixups(src, this->fixupKind(), target);
	}
}

template <typename A>
ld::Atom::Combine NonLazyPointerSection<A>::combine(Parser<A>& parser, pint_t addr)
{
	const macho_section<P>* sect = this->machoSection();
	uint32_t symIndex = parser.symbolIndexFromIndirectSectionAddress(addr, sect);
	if ( symIndex == INDIRECT_SYMBOL_LOCAL)
		return ld::Atom::combineNever;
		
	// don't coalesce non-lazy-pointers to local symbols
	const macho_nlist<P>& sym = parser.symbolFromIndex(symIndex);
	if ( ((sym.n_type() & N_TYPE) == N_SECT) && ((sym.n_type() & N_EXT) == 0) ) 
		return ld::Atom::combineNever;
	
	return ld::Atom::combineByNameAndReferences;
}

template <typename A>
const char* NonLazyPointerSection<A>::targetName(const class Atom<A>* atom, const ld::IndirectBindingTable& ind) 
{
	assert(atom->combine() == ld::Atom::combineByNameAndReferences);
	assert(atom->fixupCount() == 1);
	ld::Fixup::iterator fit = atom->fixupsBegin();
	const char* name = NULL;
	switch ( fit->binding ) {
		case ld::Fixup::bindingByNameUnbound:
			name = fit->u.name;
			break;
		case ld::Fixup::bindingByContentBound:
			name = fit->u.target->name();
			break;
		case ld::Fixup::bindingsIndirectlyBound:
			name = ind.indirectName(fit->u.bindingIndex);
			break;
		default:
			assert(0);
	}
	assert(name != NULL);
	return name;
}

template <typename A>
unsigned long NonLazyPointerSection<A>::contentHash(const class Atom<A>* atom, const ld::IndirectBindingTable& ind) const
{
	assert(atom->combine() == ld::Atom::combineByNameAndReferences);
	unsigned long hash = 9508;
	for (const char* s = this->targetName(atom, ind); *s != '\0'; ++s) {
		hash = hash * 33 + *s;
	}
	return hash;
}

template <typename A>
bool NonLazyPointerSection<A>::canCoalesceWith(const class Atom<A>* atom, const ld::Atom& rhs, 
													const ld::IndirectBindingTable& indirectBindingTable) const
{
	if ( rhs.section().type() != ld::Section::typeNonLazyPointer )
		return false;
	assert(this->type() == rhs.section().type());
	// there can be many non-lazy pointer in different section names
	// we only want to coalesce in same section name
	if ( *this != rhs.section() )
		return false;
	const Atom<A>* rhsAtom = dynamic_cast<const Atom<A>*>(&rhs);
	assert(rhsAtom !=  NULL);
	const char* thisName = this->targetName(atom, indirectBindingTable);
	const char* rhsName = this->targetName(rhsAtom, indirectBindingTable);
	return (strcmp(thisName, rhsName) == 0);
}

template <typename A>
ld::Atom::Scope NonLazyPointerSection<A>::scopeAtAddress(Parser<A>& parser, pint_t addr)
{ 
	const macho_section<P>* sect = this->machoSection();
	uint32_t symIndex = parser.symbolIndexFromIndirectSectionAddress(addr, sect);
	if ( symIndex == INDIRECT_SYMBOL_LOCAL)
		return ld::Atom::scopeTranslationUnit;
	else
		return ld::Atom::scopeLinkageUnit; 
}


template <typename A>
const uint8_t* CFStringSection<A>::targetContent(const class Atom<A>* atom, const ld::IndirectBindingTable& ind,
													ContentType* ct, unsigned int* count)
{
	*ct = contentUnknown;
	for (ld::Fixup::iterator fit=atom->fixupsBegin(), end=atom->fixupsEnd(); fit != end; ++fit) {
		const ld::Atom* targetAtom = NULL;
		switch ( fit->binding ) {
			case ld::Fixup::bindingByNameUnbound:
				// ignore reference to ___CFConstantStringClassReference
				// we are just looking for reference to backing string data
				assert(fit->offsetInAtom == 0);
				assert(strcmp(fit->u.name, "___CFConstantStringClassReference") == 0);
				break;
			case ld::Fixup::bindingDirectlyBound:
			case ld::Fixup::bindingByContentBound:
				targetAtom = fit->u.target;
				break;
			case ld::Fixup::bindingsIndirectlyBound:
				targetAtom = ind.indirectAtom(fit->u.bindingIndex);
				break;
			default:
				assert(0 && "bad binding type");
		}
		assert(targetAtom != NULL);
		const Atom<A>* target = dynamic_cast<const Atom<A>*>(targetAtom);
		if ( targetAtom->section().type() == ld::Section::typeCString ) {
			*ct = contentUTF8;
			*count = targetAtom->size();
		}
		else if ( targetAtom->section().type() == ld::Section::typeUTF16Strings ) {
			*ct = contentUTF16;
			*count = (targetAtom->size()+1)/2; // round up incase of buggy compiler that has only one trailing zero byte
		}
		assert(target !=  NULL);
		return target->contentPointer();
	}
	assert(0);
	return NULL;
}

template <typename A>
unsigned long CFStringSection<A>::contentHash(const class Atom<A>* atom, const ld::IndirectBindingTable& ind) const
{
	// base hash of CFString on hash of cstring it wraps
	ContentType cType;
	unsigned long hash;
	unsigned int charCount;
	const uint8_t* content = this->targetContent(atom, ind, &cType, &charCount);
	switch ( cType ) {
		case contentUTF8:
			hash = 9408;
			for (const char* s = (char*)content; *s != '\0'; ++s) {
				hash = hash * 33 + *s;
			}
			return hash;
		case contentUTF16:
			hash = 407955;
			--charCount; // don't add last 0x0000 to hash because some buggy compilers only have trailing single byte
			for (const uint16_t* s = (uint16_t*)content; charCount > 0; ++s, --charCount) {
				hash = hash * 1025 + *s;
			}
			return hash;
		case contentUnknown:
			return 0;
	}
	return 0;
}


template <typename A>
bool CFStringSection<A>::canCoalesceWith(const class Atom<A>* atom, const ld::Atom& rhs, 
													const ld::IndirectBindingTable& indirectBindingTable) const
{
	if ( atom == &rhs )
		return true;
	if ( rhs.section().type() != ld::Section::typeCFString)
		return false;
	assert(this->type() == rhs.section().type());
	assert(strcmp(this->sectionName(), "__cfstring") == 0);
	
	ContentType thisType;
	unsigned int charCount;
	const uint8_t* cstringContent = this->targetContent(atom, indirectBindingTable, &thisType, &charCount);
	ContentType rhsType;
	const Atom<A>* rhsAtom = dynamic_cast<const Atom<A>*>(&rhs);
	assert(rhsAtom !=  NULL);
	unsigned int rhsCharCount;
	const uint8_t* rhsStringContent = this->targetContent(rhsAtom, indirectBindingTable, &rhsType, &rhsCharCount);

	if ( thisType != rhsType )
		return false;

	// no need to compare content of pointers are already the same
	if ( cstringContent == rhsStringContent ) 
		return true;
	
	// no need to compare content if size is different
	if ( charCount != rhsCharCount )
		return false;

	switch ( thisType ) {
		case contentUTF8:
			return (strcmp((char*)cstringContent, (char*)rhsStringContent) == 0);
		case contentUTF16:
			{
				const uint16_t* cstringContent16 = (uint16_t*)cstringContent;
				const uint16_t* rhsStringContent16 = (uint16_t*)rhsStringContent;
				for (unsigned int i = 0; i < charCount; ++i) {
					if ( cstringContent16[i] != rhsStringContent16[i] )
						return false;
				}
				return true;
			}
		case contentUnknown:
			return false;
	}
	return false;
}


template <typename A>
typename A::P::uint_t ObjC1ClassSection<A>::elementSizeAtAddress(pint_t addr)
{
	// nominal size for each class is 48 bytes, but sometimes the compiler
	// over aligns and there is padding after class data
	const macho_section<P>* sct = this->machoSection();
	uint32_t align = 1 << sct->align();
	uint32_t size = ((12 * sizeof(pint_t)) + align-1) & (-align);
	return size;
}

template <typename A>
const char* ObjC1ClassSection<A>::unlabeledAtomName(Parser<A>& parser, pint_t addr)
{
	// 8-bytes into class object is pointer to class name
	const macho_section<P>* sct = this->machoSection();
	uint32_t classObjcFileOffset = sct->offset() - sct->addr() + addr;
	const uint8_t* mappedFileContent = this->file().fileContent();
	pint_t nameAddr = P::getP(*((pint_t*)(mappedFileContent+classObjcFileOffset+2*sizeof(pint_t))));
	
	// find section containing string address to get string bytes
	const macho_section<P>* const sections = parser.firstMachOSection();
	const uint32_t sectionCount = parser.machOSectionCount();
	for (uint32_t i=0; i < sectionCount; ++i) {
		const macho_section<P>* aSect = &sections[i];
		if ( (aSect->addr() <= nameAddr) && (nameAddr < (aSect->addr()+aSect->size())) ) {
			assert((aSect->flags() & SECTION_TYPE) == S_CSTRING_LITERALS);
			uint32_t nameFileOffset = aSect->offset() - aSect->addr() + nameAddr;
			const char* name = (char*)mappedFileContent + nameFileOffset;
			// spin through symbol table to find absolute symbol corresponding to this class
			for (uint32_t s=0; s < parser.symbolCount(); ++s) {
				const macho_nlist<P>& sym =	parser.symbolFromIndex(s);
				if ( (sym.n_type() & N_TYPE) != N_ABS )
					continue;
				const char* absName = parser.nameFromSymbol(sym);
				if ( strncmp(absName, ".objc_class_name_", 17) == 0 ) {
					if ( strcmp(&absName[17], name) == 0 )
						return absName;
				}
			}
			assert(0 && "obj class name not found in symbol table");
		}
	}
	assert(0 && "obj class name not found");
	return "unknown objc class";
}


template <typename A>
const char* ObjC2ClassRefsSection<A>::targetClassName(const class Atom<A>* atom, const ld::IndirectBindingTable& ind) const
{
	assert(atom->fixupCount() == 1);
	ld::Fixup::iterator fit = atom->fixupsBegin();
	const char* className = NULL;
	switch ( fit->binding ) {
		case ld::Fixup::bindingByNameUnbound:
			className = fit->u.name;
			break;
		case ld::Fixup::bindingDirectlyBound:
		case ld::Fixup::bindingByContentBound:
			className = fit->u.target->name();
			break;
		case ld::Fixup::bindingsIndirectlyBound:
			className = ind.indirectName(fit->u.bindingIndex);
			break;
		default:
			assert(0 && "unsupported binding in objc2 class ref section");
	}
	assert(className != NULL);
	return className;
}


template <typename A>
unsigned long ObjC2ClassRefsSection<A>::contentHash(const class Atom<A>* atom, const ld::IndirectBindingTable& ind) const
{
	unsigned long hash = 978;
	for (const char* s = targetClassName(atom, ind); *s != '\0'; ++s) {
		hash = hash * 33 + *s;
	}
	return hash;
}

template <typename A>
bool ObjC2ClassRefsSection<A>::canCoalesceWith(const class Atom<A>* atom, const ld::Atom& rhs, 
													const ld::IndirectBindingTable& indirectBindingTable) const
{
	assert(this->type() == rhs.section().type());
	const Atom<A>* rhsAtom = dynamic_cast<const Atom<A>*>(&rhs);
	assert(rhsAtom !=  NULL);
	const char* thisClassName = targetClassName(atom, indirectBindingTable);
	const char* rhsClassName = targetClassName(rhsAtom, indirectBindingTable);
	return (strcmp(thisClassName, rhsClassName) == 0);
}


template <typename A>
const char* Objc1ClassReferences<A>::targetCString(const class Atom<A>* atom, const ld::IndirectBindingTable& ind) const
{
	assert(atom->fixupCount() == 2);
	ld::Fixup::iterator fit = atom->fixupsBegin();
	if ( fit->kind == ld::Fixup::kindSetTargetAddress )
		++fit;
	const ld::Atom* targetAtom = NULL;
	switch ( fit->binding ) {
		case ld::Fixup::bindingByContentBound:
			targetAtom = fit->u.target;
			break;
		case ld::Fixup::bindingsIndirectlyBound:
			targetAtom = ind.indirectAtom(fit->u.bindingIndex);
			if ( targetAtom == NULL ) {
				fprintf(stderr, "missing target named %s\n", ind.indirectName(fit->u.bindingIndex));
			}
			break;
		default:
			assert(0);
	}
	assert(targetAtom != NULL);
	const Atom<A>* target = dynamic_cast<const Atom<A>*>(targetAtom);
	assert(target !=  NULL);
	return (char*)target->contentPointer();
}


template <typename A>
const char* PointerToCStringSection<A>::targetCString(const class Atom<A>* atom, const ld::IndirectBindingTable& ind) const
{
	assert(atom->fixupCount() == 1);
	ld::Fixup::iterator fit = atom->fixupsBegin();
	const ld::Atom* targetAtom = NULL;
	switch ( fit->binding ) {
		case ld::Fixup::bindingByContentBound:
			targetAtom = fit->u.target;
			break;
		case ld::Fixup::bindingsIndirectlyBound:
			targetAtom = ind.indirectAtom(fit->u.bindingIndex);
			break;
		default:
			assert(0);
	}
	assert(targetAtom != NULL);
	const Atom<A>* target = dynamic_cast<const Atom<A>*>(targetAtom);
	assert(target !=  NULL);
	return (char*)target->contentPointer();
}

template <typename A>
unsigned long PointerToCStringSection<A>::contentHash(const class Atom<A>* atom, 
													const ld::IndirectBindingTable& indirectBindingTable) const
{
	// make hash from section name and target cstring name
	unsigned long hash = 123;
	for (const char* s = this->sectionName(); *s != '\0'; ++s) {
		hash = hash * 33 + *s;
	}
	for (const char* s = this->targetCString(atom, indirectBindingTable); *s != '\0'; ++s) {
		hash = hash * 33 + *s;
	}
	return hash;
}

template <typename A>
bool PointerToCStringSection<A>::canCoalesceWith(const class Atom<A>* atom, const ld::Atom& rhs, 
													const ld::IndirectBindingTable& indirectBindingTable) const
{
	assert(this->type() == rhs.section().type());
	// there can be pointers-to-cstrings in different section names
	// we only want to coalesce in same section name
	if ( *this != rhs.section() )
		return false;
	
	// get string content for this 
	const char* cstringContent = this->targetCString(atom, indirectBindingTable);
	const Atom<A>* rhsAtom = dynamic_cast<const Atom<A>*>(&rhs);
	assert(rhsAtom !=  NULL);
	const char* rhsCstringContent = this->targetCString(rhsAtom, indirectBindingTable);

	assert(cstringContent != NULL);
	assert(rhsCstringContent != NULL);
	return (strcmp(cstringContent, rhsCstringContent) == 0);
}



template <typename A>
unsigned long UTF16StringSection<A>::contentHash(const class Atom<A>* atom, const ld::IndirectBindingTable& ind) const
{
	unsigned long hash = 5381;
	const uint16_t* stringContent = (uint16_t*)atom->contentPointer();
	// some buggy compilers end utf16 data with single byte, so don't use last word in hash computation
	unsigned int count = (atom->size()/2) - 1;
	for (const uint16_t* s = stringContent; count > 0; ++s, --count) {
		hash = hash * 33 + *s;
	}
	return hash;
}

template <typename A>
bool UTF16StringSection<A>::canCoalesceWith(const class Atom<A>* atom, const ld::Atom& rhs, 
													const ld::IndirectBindingTable& ind) const
{
	if ( rhs.section().type() != ld::Section::typeUTF16Strings )
		return false;
	assert(0);
	return false;
}







template <>
uint32_t Section<x86_64>::x86_64PcRelOffset(uint8_t r_type)
{
	switch ( r_type ) {
		case X86_64_RELOC_SIGNED:
			return 4;
		case X86_64_RELOC_SIGNED_1:
			return 5;
		case X86_64_RELOC_SIGNED_2:
			return 6;
		case X86_64_RELOC_SIGNED_4:
			return 8;
	}
	return 0;
}


template <>
bool Section<x86_64>::addRelocFixup(class Parser<x86_64>& parser, const macho_relocation_info<P>* reloc)
{
	const macho_section<P>* sect = this->machoSection();
	uint64_t srcAddr = sect->addr() + reloc->r_address();
	Parser<x86_64>::SourceLocation	src;
	Parser<x86_64>::TargetDesc		target;
	Parser<x86_64>::TargetDesc		toTarget;
	src.atom = this->findAtomByAddress(srcAddr);
	src.offsetInAtom = srcAddr - src.atom->_objAddress;
	const uint8_t* fixUpPtr = file().fileContent() + sect->offset() + reloc->r_address();
	uint64_t contentValue = 0;
	const macho_relocation_info<x86_64::P>* nextReloc = &reloc[1];
	bool result = false;
	bool useDirectBinding;
	switch ( reloc->r_length() ) {
		case 0:
			contentValue = *fixUpPtr;
			break;
		case 1:
			contentValue = (int64_t)(int16_t)E::get16(*((uint16_t*)fixUpPtr));
			break;
		case 2:
			contentValue = (int64_t)(int32_t)E::get32(*((uint32_t*)fixUpPtr));
			break;
		case 3:
			contentValue = E::get64(*((uint64_t*)fixUpPtr));
			break;
	}
	target.atom = NULL;
	target.name = NULL;
	target.weakImport = false;
	target.addend = 0;
	if ( reloc->r_extern() ) {
		const macho_nlist<P>& sym = parser.symbolFromIndex(reloc->r_symbolnum());
		// use direct reference for local symbols
		if ( ((sym.n_type() & N_TYPE) == N_SECT) && (((sym.n_type() & N_EXT) == 0) || (parser.nameFromSymbol(sym)[0] == 'L')) ) {
			parser.findTargetFromAddressAndSectionNum(sym.n_value(), sym.n_sect(), target);
			target.addend += contentValue;
		}
		else {
			target.name = parser.nameFromSymbol(sym);
			target.weakImport = parser.weakImportFromSymbol(sym);
			target.addend = contentValue;
		}
		// cfstrings should always use direct reference to backing store
		if ( (this->type() == ld::Section::typeCFString) && (src.offsetInAtom != 0) ) {
			parser.findTargetFromAddressAndSectionNum(sym.n_value(), sym.n_sect(), target);
			target.addend = contentValue;
		}
	}
	else {
		if ( reloc->r_pcrel()  )
			contentValue += srcAddr + x86_64PcRelOffset(reloc->r_type());
		parser.findTargetFromAddressAndSectionNum(contentValue, reloc->r_symbolnum(), target);
	}
	switch ( reloc->r_type() ) {
		case X86_64_RELOC_UNSIGNED:
			if ( reloc->r_pcrel() )
				throw "pcrel and X86_64_RELOC_UNSIGNED not supported";
			switch ( reloc->r_length() ) {
				case 0:
				case 1:
					throw "length < 2 and X86_64_RELOC_UNSIGNED not supported";
				case 2:
					parser.addFixups(src, ld::Fixup::kindStoreLittleEndian32, target);
					break;
				case 3:
					parser.addFixups(src, ld::Fixup::kindStoreLittleEndian64, target);
					break;
			}
			break;
		case X86_64_RELOC_SIGNED:
		case X86_64_RELOC_SIGNED_1:
		case X86_64_RELOC_SIGNED_2:
		case X86_64_RELOC_SIGNED_4:
			if ( ! reloc->r_pcrel() )
				throw "not pcrel and X86_64_RELOC_SIGNED* not supported";
			if ( reloc->r_length() != 2 ) 
				throw "length != 2 and X86_64_RELOC_SIGNED* not supported";
			switch ( reloc->r_type() ) {
				case X86_64_RELOC_SIGNED:
					parser.addFixups(src, ld::Fixup::kindStoreX86PCRel32, target);
					break;
				case X86_64_RELOC_SIGNED_1:
					if ( reloc->r_extern() )
						target.addend += 1;
					parser.addFixups(src, ld::Fixup::kindStoreX86PCRel32_1, target);
					break;	
				case X86_64_RELOC_SIGNED_2:
					if ( reloc->r_extern() )
						target.addend += 2;
					parser.addFixups(src, ld::Fixup::kindStoreX86PCRel32_2, target);
					break;	
				case X86_64_RELOC_SIGNED_4:
					if ( reloc->r_extern() )
						target.addend += 4;
					parser.addFixups(src, ld::Fixup::kindStoreX86PCRel32_4, target);
					break;
			}
			break;
		case X86_64_RELOC_BRANCH:
			if ( ! reloc->r_pcrel() )
				throw "not pcrel and X86_64_RELOC_BRANCH not supported";
			switch ( reloc->r_length() ) {
				case 2:
					if ( (target.name != NULL) && (strncmp(target.name, "___dtrace_probe$", 16) == 0) ) {
						parser.addFixup(src, ld::Fixup::k1of1, ld::Fixup::kindStoreX86DtraceCallSiteNop, false, target.name);
						parser.addDtraceExtraInfos(src, &target.name[16]);
					}
					else if ( (target.name != NULL) && (strncmp(target.name, "___dtrace_isenabled$", 20) == 0) ) {
						parser.addFixup(src, ld::Fixup::k1of1, ld::Fixup::kindStoreX86DtraceIsEnableSiteClear, false, target.name);
						parser.addDtraceExtraInfos(src, &target.name[20]);
					}
					else {
						parser.addFixups(src, ld::Fixup::kindStoreX86BranchPCRel32, target);
					}
					break;
				case 0:
					parser.addFixups(src, ld::Fixup::kindStoreX86BranchPCRel8, target);
					break;
				default:
					throwf("length=%d and X86_64_RELOC_BRANCH not supported", reloc->r_length());
			}
			break;
		case X86_64_RELOC_GOT:
			if ( ! reloc->r_extern() ) 
				throw "not extern and X86_64_RELOC_GOT not supported";
			if ( ! reloc->r_pcrel() )
				throw "not pcrel and X86_64_RELOC_GOT not supported";
			if ( reloc->r_length() != 2 ) 
				throw "length != 2 and X86_64_RELOC_GOT not supported";
			parser.addFixups(src, ld::Fixup::kindStoreX86PCRel32GOT, target);
			break;
		case X86_64_RELOC_GOT_LOAD:
			if ( ! reloc->r_extern() ) 
				throw "not extern and X86_64_RELOC_GOT_LOAD not supported";
			if ( ! reloc->r_pcrel() )
				throw "not pcrel and X86_64_RELOC_GOT_LOAD not supported";
			if ( reloc->r_length() != 2 ) 
				throw "length != 2 and X86_64_RELOC_GOT_LOAD not supported";
			parser.addFixups(src, ld::Fixup::kindStoreX86PCRel32GOTLoad, target);
			break;
		case X86_64_RELOC_SUBTRACTOR:
			if ( reloc->r_pcrel() )
				throw "X86_64_RELOC_SUBTRACTOR cannot be pc-relative";
			if ( reloc->r_length() < 2 )
				throw "X86_64_RELOC_SUBTRACTOR must have r_length of 2 or 3";
			if ( !reloc->r_extern() )
				throw "X86_64_RELOC_SUBTRACTOR must have r_extern=1";
			if ( nextReloc->r_type() != X86_64_RELOC_UNSIGNED )
				throw "X86_64_RELOC_SUBTRACTOR must be followed by X86_64_RELOC_UNSIGNED";
			result = true;
			if ( nextReloc->r_pcrel() )
				throw "X86_64_RELOC_UNSIGNED following a X86_64_RELOC_SUBTRACTOR cannot be pc-relative";
			if ( nextReloc->r_length() != reloc->r_length() )
				throw "X86_64_RELOC_UNSIGNED following a X86_64_RELOC_SUBTRACTOR must have same r_length";
			if ( nextReloc->r_extern() ) {
				const macho_nlist<P>& sym = parser.symbolFromIndex(nextReloc->r_symbolnum());
				// use direct reference for local symbols
				if ( ((sym.n_type() & N_TYPE) == N_SECT) && (((sym.n_type() & N_EXT) == 0) || (parser.nameFromSymbol(sym)[0] == 'L')) ) {
					parser.findTargetFromAddressAndSectionNum(sym.n_value(), sym.n_sect(), toTarget);
					toTarget.addend = contentValue;
					useDirectBinding = true;
				}
				else {
					toTarget.name = parser.nameFromSymbol(sym);
					toTarget.weakImport = parser.weakImportFromSymbol(sym);
					toTarget.addend = contentValue;
					useDirectBinding = false;
				}
			}
			else {
				parser.findTargetFromAddressAndSectionNum(contentValue, nextReloc->r_symbolnum(), toTarget);
				useDirectBinding = (toTarget.atom->scope() == ld::Atom::scopeTranslationUnit);
			}
			if ( useDirectBinding )
				parser.addFixup(src, ld::Fixup::k1of4, ld::Fixup::kindSetTargetAddress, toTarget.atom);
			else
				parser.addFixup(src, ld::Fixup::k1of4, ld::Fixup::kindSetTargetAddress, toTarget.weakImport, toTarget.name);
			parser.addFixup(src, ld::Fixup::k2of4, ld::Fixup::kindAddAddend, toTarget.addend);
			if ( target.atom == NULL )
				parser.addFixup(src, ld::Fixup::k3of4, ld::Fixup::kindSubtractTargetAddress, false, target.name);
			else
				parser.addFixup(src, ld::Fixup::k3of4, ld::Fixup::kindSubtractTargetAddress, target.atom);
			if ( reloc->r_length() == 2 )
				parser.addFixup(src, ld::Fixup::k4of4, ld::Fixup::kindStoreLittleEndian32);
			else
				parser.addFixup(src, ld::Fixup::k4of4, ld::Fixup::kindStoreLittleEndian64);
			break;
		case X86_64_RELOC_TLV:
			if ( ! reloc->r_extern() ) 
				throw "not extern and X86_64_RELOC_TLV not supported";
			if ( ! reloc->r_pcrel() )
				throw "not pcrel and X86_64_RELOC_TLV not supported";
			if ( reloc->r_length() != 2 ) 
				throw "length != 2 and X86_64_RELOC_TLV not supported";
			parser.addFixups(src, ld::Fixup::kindStoreX86PCRel32TLVLoad, target);
			break;
		default:
			throwf("unknown relocation type %d", reloc->r_type());
	}
	return result;
}



template <>
bool Section<x86>::addRelocFixup(class Parser<x86>& parser, const macho_relocation_info<P>* reloc)
{
	const macho_section<P>* sect = this->machoSection();
	uint32_t srcAddr;
	const uint8_t* fixUpPtr;
	uint32_t contentValue = 0;
	ld::Fixup::Kind kind = ld::Fixup::kindNone;
	Parser<x86>::SourceLocation	src;
	Parser<x86>::TargetDesc		target;

	if ( (reloc->r_address() & R_SCATTERED) == 0 ) {
		srcAddr = sect->addr() + reloc->r_address();
		src.atom = this->findAtomByAddress(srcAddr);
		src.offsetInAtom = srcAddr - src.atom->_objAddress;
		fixUpPtr = file().fileContent() + sect->offset() + reloc->r_address();
		switch ( reloc->r_type() ) {
		case GENERIC_RELOC_VANILLA:
			switch ( reloc->r_length() ) {
				case 0:
					contentValue = (int32_t)(int8_t)*fixUpPtr;
					if ( reloc->r_pcrel() ) {
						kind = ld::Fixup::kindStoreX86BranchPCRel8;
						contentValue += srcAddr + sizeof(uint8_t);
					}
					else
						throw "r_length=0 and r_pcrel=0 not supported";
					break;
				case 1:
					contentValue = (int32_t)(int16_t)E::get16(*((uint16_t*)fixUpPtr));
					if ( reloc->r_pcrel() ) {
						kind = ld::Fixup::kindStoreX86PCRel16;
						contentValue += srcAddr + sizeof(uint16_t);
					}
					else
						kind = ld::Fixup::kindStoreLittleEndian16;
					break;
				case 2:
					contentValue = E::get32(*((uint32_t*)fixUpPtr));
					if ( reloc->r_pcrel() ) {
						kind = ld::Fixup::kindStoreX86BranchPCRel32;
						contentValue += srcAddr + sizeof(uint32_t);
					}
					else
						kind = ld::Fixup::kindStoreLittleEndian32;
					break;
				case 3:
					throw "r_length=3 not supported";
			}
			if ( reloc->r_extern() ) {
				target.atom = NULL;
				const macho_nlist<P>& targetSymbol = parser.symbolFromIndex(reloc->r_symbolnum());
				target.name = parser.nameFromSymbol(targetSymbol);
				target.weakImport = parser.weakImportFromSymbol(targetSymbol);
				target.addend = (int32_t)contentValue;
			}
			else {
				parser.findTargetFromAddressAndSectionNum(contentValue, reloc->r_symbolnum(), target);
			}
			if ( (kind == ld::Fixup::kindStoreX86BranchPCRel32) && (target.name != NULL) ) {
				if ( strncmp(target.name, "___dtrace_probe$", 16) == 0 ) {
					parser.addFixup(src, ld::Fixup::k1of1, ld::Fixup::kindStoreX86DtraceCallSiteNop, false, target.name);
					parser.addDtraceExtraInfos(src, &target.name[16]);
					return false;
				}
				else if ( strncmp(target.name, "___dtrace_isenabled$", 20) == 0 ) {
					parser.addFixup(src, ld::Fixup::k1of1, ld::Fixup::kindStoreX86DtraceIsEnableSiteClear, false, target.name);
					parser.addDtraceExtraInfos(src, &target.name[20]);
					return false;
				}
			}
			parser.addFixups(src, kind, target);
			return false;
			break;
		case GENERIC_RLEOC_TLV:
			{
				if ( !reloc->r_extern() )
					throw "r_extern=0 and r_type=GENERIC_RLEOC_TLV not supported";
				if ( reloc->r_length() != 2 )
					throw "r_length!=2 and r_type=GENERIC_RLEOC_TLV not supported";
				const macho_nlist<P>& sym = parser.symbolFromIndex(reloc->r_symbolnum());
				// use direct reference for local symbols
				if ( ((sym.n_type() & N_TYPE) == N_SECT) && ((sym.n_type() & N_EXT) == 0) ) {
					parser.findTargetFromAddressAndSectionNum(sym.n_value(), sym.n_sect(), target);
				}
				else {
					target.atom = NULL;
					target.name = parser.nameFromSymbol(sym);
					target.weakImport = parser.weakImportFromSymbol(sym);
				}			
				target.addend = (int64_t)(int32_t)E::get32(*((uint32_t*)fixUpPtr));
				if ( reloc->r_pcrel() ) {
					parser.addFixups(src, ld::Fixup::kindStoreX86PCRel32TLVLoad, target);
				}
				else {
					parser.addFixups(src, ld::Fixup::kindStoreX86Abs32TLVLoad, target);
				}
				return false;
			}
			break;
		default:
			throwf("unsupported i386 relocation type (%d)", reloc->r_type());
		}
	}
	else {
		// scattered relocation
		const macho_scattered_relocation_info<P>* sreloc = (macho_scattered_relocation_info<P>*)reloc;
		srcAddr = sect->addr() + sreloc->r_address();
		src.atom = this->findAtomByAddress(srcAddr);
		assert(src.atom != NULL);
		src.offsetInAtom = srcAddr - src.atom->_objAddress;
		fixUpPtr = file().fileContent() + sect->offset() + sreloc->r_address();
		uint32_t relocValue = sreloc->r_value();
		bool result = false;
		// file format allows pair to be scattered or not
		const macho_scattered_relocation_info<P>* nextSReloc = &sreloc[1];
		const macho_relocation_info<P>* nextReloc = &reloc[1];
		bool nextRelocIsPair = false;
		uint32_t nextRelocAddress = 0;
		uint32_t nextRelocValue = 0;
 		if ( (nextReloc->r_address() & R_SCATTERED) == 0 ) {
			if ( nextReloc->r_type() == GENERIC_RELOC_PAIR ) {
				nextRelocIsPair = true;
				nextRelocAddress = nextReloc->r_address();
				result = true;  // iterator should skip next reloc, since we've consumed it here
			}
		}
		else {
			if ( nextSReloc->r_type() == GENERIC_RELOC_PAIR ) {
				nextRelocIsPair = true;
				nextRelocAddress = nextSReloc->r_address();
				nextRelocValue = nextSReloc->r_value();
			}
		}
		switch (sreloc->r_type()) {
			case GENERIC_RELOC_VANILLA:
				// with a scattered relocation we get both the target (sreloc->r_value()) and the target+offset (*fixUpPtr)
				target.atom = parser.findAtomByAddress(relocValue);
				if ( sreloc->r_pcrel() ) {
					switch ( sreloc->r_length() ) {
						case 0:
							contentValue = srcAddr + 1 + *fixUpPtr;
							target.addend = (int32_t)contentValue - (int32_t)relocValue;
							parser.addFixups(src, ld::Fixup::kindStoreX86PCRel8, target);
							break;
						case 1:
							contentValue = srcAddr + 2 + LittleEndian::get16(*((uint16_t*)fixUpPtr));
							target.addend = (int32_t)contentValue - (int32_t)relocValue;
							parser.addFixups(src, ld::Fixup::kindStoreX86PCRel16, target);
							break;
						case 2:
							contentValue = srcAddr + 4 + LittleEndian::get32(*((uint32_t*)fixUpPtr));
							target.addend = (int32_t)contentValue - (int32_t)relocValue;
							parser.addFixups(src, ld::Fixup::kindStoreX86PCRel32, target);
							break;
						case 3:
							throw "unsupported r_length=3 for scattered pc-rel vanilla reloc";
							break;
					}
				}
				else {
					if ( sreloc->r_length() != 2 )
						throwf("unsupported r_length=%d for scattered vanilla reloc", sreloc->r_length());
					contentValue = LittleEndian::get32(*((uint32_t*)fixUpPtr));
					target.addend = (int32_t)contentValue - (int32_t)(target.atom->objectAddress());
					parser.addFixups(src, ld::Fixup::kindStoreLittleEndian32, target);
				}
				break;
			case GENERIC_RELOC_SECTDIFF:
			case GENERIC_RELOC_LOCAL_SECTDIFF:
				{
					if ( !nextRelocIsPair ) 
						throw "GENERIC_RELOC_SECTDIFF missing following pair";
					switch ( sreloc->r_length() ) {
						case 0:
						case 3:
							throw "bad length for GENERIC_RELOC_SECTDIFF";
						case 1:
							contentValue = (int32_t)(int16_t)LittleEndian::get16(*((uint16_t*)fixUpPtr));
							kind = ld::Fixup::kindStoreLittleEndian16;
							break;
						case 2:
							contentValue = LittleEndian::get32(*((uint32_t*)fixUpPtr));
							kind = ld::Fixup::kindStoreLittleEndian32;
							break;
					}
					Atom<x86>* fromAtom  = parser.findAtomByAddress(nextRelocValue);
					uint32_t offsetInFrom = nextRelocValue - fromAtom->_objAddress;
					parser.findTargetFromAddress(sreloc->r_value(), target);
					// check for addend encoded in the section content
					int64_t addend = (int32_t)contentValue - (int32_t)(sreloc->r_value() - nextRelocValue);
					if ( addend < 0 ) {
						// switch binding base on coalescing
						if ( target.atom == NULL ) {
							parser.addFixup(src, ld::Fixup::k1of5, ld::Fixup::kindSetTargetAddress, false, target.name);
						}
						else if ( target.atom->scope() == ld::Atom::scopeTranslationUnit ) {
							parser.addFixup(src, ld::Fixup::k1of5, ld::Fixup::kindSetTargetAddress, target.atom);
						}
						else if ( (target.atom->combine() == ld::Atom::combineByNameAndContent) || (target.atom->combine() == ld::Atom::combineByNameAndReferences) ) {
							parser.addFixup(src, ld::Fixup::k1of5, ld::Fixup::kindSetTargetAddress, ld::Fixup::bindingByContentBound, target.atom);
						}
						else {
							parser.addFixup(src, ld::Fixup::k1of5, ld::Fixup::kindSetTargetAddress, false, target.atom->name());
						}
						parser.addFixup(src, ld::Fixup::k2of5, ld::Fixup::kindAddAddend, target.addend);
						parser.addFixup(src, ld::Fixup::k3of5, ld::Fixup::kindSubtractTargetAddress, fromAtom);
						parser.addFixup(src, ld::Fixup::k4of5, ld::Fixup::kindSubtractAddend, offsetInFrom-addend);
						parser.addFixup(src, ld::Fixup::k5of5, kind);
					}
					else {
						// switch binding base on coalescing
						if ( target.atom == NULL ) {
							parser.addFixup(src, ld::Fixup::k1of5, ld::Fixup::kindSetTargetAddress, false, target.name);
						}
						else if ( target.atom->scope() == ld::Atom::scopeTranslationUnit ) {
							parser.addFixup(src, ld::Fixup::k1of5, ld::Fixup::kindSetTargetAddress, target.atom);
						}
						else if ( (target.atom->combine() == ld::Atom::combineByNameAndContent) || (target.atom->combine() == ld::Atom::combineByNameAndReferences) ) {
							parser.addFixup(src, ld::Fixup::k1of5, ld::Fixup::kindSetTargetAddress, ld::Fixup::bindingByContentBound, target.atom);
						}
						else {
							parser.addFixup(src, ld::Fixup::k1of5, ld::Fixup::kindSetTargetAddress, false, target.atom->name());
						}
						parser.addFixup(src, ld::Fixup::k2of5, ld::Fixup::kindAddAddend, target.addend+addend);
						parser.addFixup(src, ld::Fixup::k3of5, ld::Fixup::kindSubtractTargetAddress, fromAtom);
						parser.addFixup(src, ld::Fixup::k4of5, ld::Fixup::kindSubtractAddend, offsetInFrom);
						parser.addFixup(src, ld::Fixup::k5of5, kind);
					}
				}
				break;
		}
		return result;
	}
}


	


#if SUPPORT_ARCH_arm_any
template <>
bool Section<arm>::addRelocFixup(class Parser<arm>& parser, const macho_relocation_info<P>* reloc)
{
	const macho_section<P>* sect = this->machoSection();
	bool result = false;
	uint32_t srcAddr;
	uint32_t dstAddr;
	uint32_t* fixUpPtr;
	int32_t displacement = 0;
	uint32_t instruction = 0;
	pint_t contentValue = 0;
	Parser<arm>::SourceLocation	src;
	Parser<arm>::TargetDesc		target;
	const macho_relocation_info<P>* nextReloc;
	
	if ( (reloc->r_address() & R_SCATTERED) == 0 ) {
		bool externSymbolIsThumbDef = false;
		srcAddr = sect->addr() + reloc->r_address();
		src.atom = this->findAtomByAddress(srcAddr);
		src.offsetInAtom = srcAddr - src.atom->_objAddress;
		fixUpPtr = (uint32_t*)(file().fileContent() + sect->offset() + reloc->r_address());
		if ( reloc->r_type() != ARM_RELOC_PAIR )
			instruction = LittleEndian::get32(*fixUpPtr);
		if ( reloc->r_extern() ) {
			const macho_nlist<P>& targetSymbol = parser.symbolFromIndex(reloc->r_symbolnum());
			// use direct reference for local symbols
			if ( ((targetSymbol.n_type() & N_TYPE) == N_SECT) && (((targetSymbol.n_type() & N_EXT) == 0) || (parser.nameFromSymbol(targetSymbol)[0] == 'L')) ) {
				parser.findTargetFromAddressAndSectionNum(targetSymbol.n_value(), targetSymbol.n_sect(), target);
			}
			else {
				target.atom = NULL;
				target.name = parser.nameFromSymbol(targetSymbol);
				target.weakImport = parser.weakImportFromSymbol(targetSymbol);
				if ( ((targetSymbol.n_type() & N_TYPE) == N_SECT) &&  (targetSymbol.n_desc() & N_ARM_THUMB_DEF) )
					externSymbolIsThumbDef = true;
			}
		}
		switch ( reloc->r_type() ) {
			case ARM_RELOC_BR24:
				// Sign-extend displacement
				displacement = (instruction & 0x00FFFFFF) << 2;
				if ( (displacement & 0x02000000) != 0 )
					displacement |= 0xFC000000;
				// The pc added will be +8 from the pc
				displacement += 8;
				// If this is BLX add H << 1
				if ((instruction & 0xFE000000) == 0xFA000000)
					displacement += ((instruction & 0x01000000) >> 23);
				if ( reloc->r_extern() ) {
					target.addend = srcAddr + displacement;
					if ( externSymbolIsThumbDef )
						target.addend &= -2; // remove thumb bit
				}
				else {
					dstAddr = srcAddr + displacement;
					parser.findTargetFromAddressAndSectionNum(dstAddr, reloc->r_symbolnum(), target);
				}
				// special case "calls" for dtrace 
				if ( (target.name != NULL) && (strncmp(target.name, "___dtrace_probe$", 16) == 0) ) {
					parser.addFixup(src, ld::Fixup::k1of1,
															ld::Fixup::kindStoreARMDtraceCallSiteNop, false, target.name);
					parser.addDtraceExtraInfos(src, &target.name[16]);
				}
				else if ( (target.name != NULL) && (strncmp(target.name, "___dtrace_isenabled$", 20) == 0) ) {
					parser.addFixup(src, ld::Fixup::k1of1, 
															ld::Fixup::kindStoreARMDtraceIsEnableSiteClear, false, target.name);
					parser.addDtraceExtraInfos(src, &target.name[20]);
				}
				else {
					parser.addFixups(src, ld::Fixup::kindStoreARMBranch24, target);
				}
				break;
			case ARM_THUMB_RELOC_BR22:
				// thumb2 added two more bits to displacement, complicating the displacement decoding
				{
					uint32_t s = (instruction >> 10) & 0x1;
					uint32_t j1 = (instruction >> 29) & 0x1;
					uint32_t j2 = (instruction >> 27) & 0x1;
					uint32_t imm10 = instruction & 0x3FF;
					uint32_t imm11 = (instruction >> 16) & 0x7FF;
					uint32_t i1 = (j1 == s);
					uint32_t i2 = (j2 == s);
					uint32_t dis = (s << 24) | (i1 << 23) | (i2 << 22) | (imm10 << 12) | (imm11 << 1);
					int32_t sdis = dis;
					if ( s )
						sdis |= 0xFE000000;
					displacement = sdis;
				}
				// The pc added will be +4 from the pc
				displacement += 4;
				// If the instruction was blx, force the low 2 bits to be clear
				dstAddr = srcAddr + displacement;
				if ((instruction & 0xF8000000) == 0xE8000000)
					dstAddr &= 0xFFFFFFFC;

				if ( reloc->r_extern() ) {
					target.addend = dstAddr;
				}
				else {
					parser.findTargetFromAddressAndSectionNum(dstAddr, reloc->r_symbolnum(), target);
				}
				// special case "calls" for dtrace 
				if ( (target.name != NULL) && (strncmp(target.name, "___dtrace_probe$", 16) == 0) ) {
					parser.addFixup(src, ld::Fixup::k1of1,
															ld::Fixup::kindStoreThumbDtraceCallSiteNop, false, target.name);
					parser.addDtraceExtraInfos(src, &target.name[16]);
				}
				else if ( (target.name != NULL) && (strncmp(target.name, "___dtrace_isenabled$", 20) == 0) ) {
					parser.addFixup(src, ld::Fixup::k1of1, 
															ld::Fixup::kindStoreThumbDtraceIsEnableSiteClear, false, target.name);
					parser.addDtraceExtraInfos(src, &target.name[20]);
				}
				else {
					parser.addFixups(src, ld::Fixup::kindStoreThumbBranch22, target);
				}
				break;
			case ARM_RELOC_VANILLA:
				if ( reloc->r_length() != 2 )
					throw "bad length for ARM_RELOC_VANILLA";
				contentValue = LittleEndian::get32(*fixUpPtr);
				if ( reloc->r_extern() ) {
					target.addend = (int32_t)contentValue;
					if ( externSymbolIsThumbDef )
						target.addend &= -2; // remove thumb bit
				}
				else {
					parser.findTargetFromAddressAndSectionNum(contentValue, reloc->r_symbolnum(), target);
					// possible non-extern relocation turned into by-name ref because target is a weak-def
					if ( target.atom != NULL ) {
						if ( target.atom->isThumb() )
							target.addend &= -2; // remove thumb bit
						// if reference to LSDA, add group subordinate fixup
						if ( target.atom->contentType() == ld::Atom::typeLSDA ) {
							Parser<arm>::SourceLocation	src2;
							src2.atom = src.atom;
							src2.offsetInAtom = 0;
							parser.addFixup(src2, ld::Fixup::k1of1, ld::Fixup::kindNoneGroupSubordinateLSDA, target.atom);
						}
					}
				}
				parser.addFixups(src, ld::Fixup::kindStoreLittleEndian32, target);
				break;
			case ARM_THUMB_32BIT_BRANCH:
				// silently ignore old unnecessary reloc
				break;
			case ARM_RELOC_HALF:
				nextReloc = &reloc[1];
				if ( nextReloc->r_type() == ARM_RELOC_PAIR ) {
					uint32_t instruction16;
					uint32_t other16 = (nextReloc->r_address() & 0xFFFF);
					bool isThumb;
					if ( reloc->r_length() & 2 ) {
						isThumb = true;
						uint32_t i =    ((instruction & 0x00000400) >> 10);
						uint32_t imm4 =  (instruction & 0x0000000F);
						uint32_t imm3 = ((instruction & 0x70000000) >> 28);
						uint32_t imm8 = ((instruction & 0x00FF0000) >> 16);
						instruction16 = (imm4 << 12) | (i << 11) | (imm3 << 8) | imm8;
					}
					else {
						isThumb = false;
						uint32_t imm4 = ((instruction & 0x000F0000) >> 16);
						uint32_t imm12 = (instruction & 0x00000FFF);
						instruction16 = (imm4 << 12) | imm12;
					}
					if ( reloc->r_length() & 1 ) {
						// high 16
						dstAddr = ((instruction16 << 16) | other16);
                        if ( reloc->r_extern() ) {
                            target.addend = dstAddr;
							if ( externSymbolIsThumbDef )
								target.addend &= -2; // remove thumb bit
						}
                        else {
                            parser.findTargetFromAddress(dstAddr, target);
                            if ( target.atom->isThumb() )
                                target.addend &= (-2); // remove thumb bit
                        }
						parser.addFixups(src, (isThumb ? ld::Fixup::kindStoreThumbHigh16 : ld::Fixup::kindStoreARMHigh16), target);
					}
					else {
						// low 16
						dstAddr = (other16 << 16) | instruction16;
                        if ( reloc->r_extern() ) {
                            target.addend = dstAddr;
							if ( externSymbolIsThumbDef )
								target.addend &= -2; // remove thumb bit
                        }
                        else {
                            parser.findTargetFromAddress(dstAddr, target);
                            if ( target.atom->isThumb() )
                                target.addend &= (-2); // remove thumb bit
                        }
						parser.addFixups(src, (isThumb ? ld::Fixup::kindStoreThumbLow16 : ld::Fixup::kindStoreARMLow16), target);
					}
					result = true;
				}
				else
					throw "for ARM_RELOC_HALF, next reloc is not ARM_RELOC_PAIR";
				break;
			default:
				throwf("unknown relocation type %d", reloc->r_type());
				break;
		}
	}
	else {
		const macho_scattered_relocation_info<P>* sreloc = (macho_scattered_relocation_info<P>*)reloc;
		// file format allows pair to be scattered or not
		const macho_scattered_relocation_info<P>* nextSReloc = &sreloc[1];
		nextReloc = &reloc[1];
		srcAddr = sect->addr() + sreloc->r_address();
		dstAddr = sreloc->r_value();
		fixUpPtr = (uint32_t*)(file().fileContent() + sect->offset() + sreloc->r_address());
		instruction = LittleEndian::get32(*fixUpPtr);
		src.atom = this->findAtomByAddress(srcAddr);
		src.offsetInAtom = srcAddr - src.atom->_objAddress;
		bool nextRelocIsPair = false;
		uint32_t nextRelocAddress = 0;
		uint32_t nextRelocValue = 0;
		if ( (nextReloc->r_address() & R_SCATTERED) == 0 ) {
			if ( nextReloc->r_type() == ARM_RELOC_PAIR ) {
				nextRelocIsPair = true;
				nextRelocAddress = nextReloc->r_address();
				result = true;
			}
		}
		else {
			if ( nextSReloc->r_type() == ARM_RELOC_PAIR ) {
				nextRelocIsPair = true;
				nextRelocAddress = nextSReloc->r_address();
				nextRelocValue = nextSReloc->r_value();
				result = true;
			}
		}
		switch ( sreloc->r_type() ) {
			case ARM_RELOC_VANILLA:
				// with a scattered relocation we get both the target (sreloc->r_value()) and the target+offset (*fixUpPtr)
				if ( sreloc->r_length() != 2 )
					throw "bad length for ARM_RELOC_VANILLA";
				target.atom = parser.findAtomByAddress(sreloc->r_value());
				contentValue = LittleEndian::get32(*fixUpPtr);
				target.addend = contentValue - target.atom->_objAddress;
				if ( target.atom->isThumb() )
					target.addend &= -2; // remove thumb bit
				parser.addFixups(src, ld::Fixup::kindStoreLittleEndian32, target);
				break;
			case ARM_RELOC_BR24:
				// Sign-extend displacement
				displacement = (instruction & 0x00FFFFFF) << 2;
				if ( (displacement & 0x02000000) != 0 )
					displacement |= 0xFC000000;
				// The pc added will be +8 from the pc
				displacement += 8;
				// If this is BLX add H << 1
				if ((instruction & 0xFE000000) == 0xFA000000)
					displacement += ((instruction & 0x01000000) >> 23);
				target.atom = parser.findAtomByAddress(sreloc->r_value());
				target.addend = (int64_t)(srcAddr + displacement) - (int64_t)(target.atom->_objAddress);
				parser.addFixups(src, ld::Fixup::kindStoreARMBranch24, target);
				break;
			case ARM_THUMB_RELOC_BR22:
				// thumb2 added two more bits to displacement, complicating the displacement decoding
				{
					uint32_t s = (instruction >> 10) & 0x1;
					uint32_t j1 = (instruction >> 29) & 0x1;
					uint32_t j2 = (instruction >> 27) & 0x1;
					uint32_t imm10 = instruction & 0x3FF;
					uint32_t imm11 = (instruction >> 16) & 0x7FF;
					uint32_t i1 = (j1 == s);
					uint32_t i2 = (j2 == s);
					uint32_t dis = (s << 24) | (i1 << 23) | (i2 << 22) | (imm10 << 12) | (imm11 << 1);
					int32_t sdis = dis;
					if ( s )
						sdis |= 0xFE000000;
					displacement = sdis;
				}
				// The pc added will be +4 from the pc
				displacement += 4;
				dstAddr = srcAddr+displacement;
				// If the instruction was blx, force the low 2 bits to be clear
				if ((instruction & 0xF8000000) == 0xE8000000)
					dstAddr &= 0xFFFFFFFC;
				target.atom = parser.findAtomByAddress(sreloc->r_value());
				target.addend = dstAddr - target.atom->_objAddress;
				parser.addFixups(src, ld::Fixup::kindStoreThumbBranch22, target);
				break;
			case ARM_RELOC_SECTDIFF:
			case ARM_RELOC_LOCAL_SECTDIFF:
				{
					if ( ! nextRelocIsPair ) 
						throw "ARM_RELOC_SECTDIFF missing following pair";
					if ( sreloc->r_length() != 2 )
						throw "bad length for ARM_RELOC_SECTDIFF";
					contentValue = LittleEndian::get32(*fixUpPtr);
					Atom<arm>* fromAtom  = parser.findAtomByAddress(nextRelocValue);
					uint32_t offsetInFrom = nextRelocValue - fromAtom->_objAddress;
					uint32_t offsetInTarget;
					Atom<arm>* targetAtom = parser.findAtomByAddressOrLocalTargetOfStub(sreloc->r_value(), &offsetInTarget);
					// check for addend encoded in the section content
                    int64_t addend = (int32_t)contentValue - (int32_t)(sreloc->r_value() - nextRelocValue);
					if ( targetAtom->isThumb() )
						addend &= -2; // remove thumb bit
					// if reference to LSDA, add group subordinate fixup
					if ( targetAtom->contentType() == ld::Atom::typeLSDA ) {
						Parser<arm>::SourceLocation	src2;
						src2.atom = src.atom;
						src2.offsetInAtom = 0;
						parser.addFixup(src2, ld::Fixup::k1of1, ld::Fixup::kindNoneGroupSubordinateLSDA, targetAtom);
					}
					if ( addend < 0 ) { 
						// switch binding base on coalescing
						if ( targetAtom->scope() == ld::Atom::scopeTranslationUnit ) {
							parser.addFixup(src, ld::Fixup::k1of5, ld::Fixup::kindSetTargetAddress, targetAtom);
						}
						else if ( (targetAtom->combine() == ld::Atom::combineByNameAndContent) || (targetAtom->combine() == ld::Atom::combineByNameAndReferences) ) {
							parser.addFixup(src, ld::Fixup::k1of5, ld::Fixup::kindSetTargetAddress, ld::Fixup::bindingByContentBound, targetAtom);
						}
						else {
							parser.addFixup(src, ld::Fixup::k1of5, ld::Fixup::kindSetTargetAddress, false, targetAtom->name());
						}
						parser.addFixup(src, ld::Fixup::k2of5, ld::Fixup::kindAddAddend, offsetInTarget);
						parser.addFixup(src, ld::Fixup::k3of5, ld::Fixup::kindSubtractTargetAddress, fromAtom);
						parser.addFixup(src, ld::Fixup::k4of5, ld::Fixup::kindSubtractAddend, offsetInFrom-addend);
						parser.addFixup(src, ld::Fixup::k5of5, ld::Fixup::kindStoreLittleEndian32);
					}
					else {
						if ( targetAtom->scope() == ld::Atom::scopeTranslationUnit ) {
							parser.addFixup(src, ld::Fixup::k1of5, ld::Fixup::kindSetTargetAddress, targetAtom);
						}
						else if ( (targetAtom->combine() == ld::Atom::combineByNameAndContent) || (targetAtom->combine() == ld::Atom::combineByNameAndReferences) ) {
							parser.addFixup(src, ld::Fixup::k1of5, ld::Fixup::kindSetTargetAddress, ld::Fixup::bindingByContentBound, targetAtom);
						}
						else {
							parser.addFixup(src, ld::Fixup::k1of5, ld::Fixup::kindSetTargetAddress, false, targetAtom->name());
						}
						parser.addFixup(src, ld::Fixup::k2of5, ld::Fixup::kindAddAddend, (uint32_t)(offsetInTarget+addend));
						parser.addFixup(src, ld::Fixup::k3of5, ld::Fixup::kindSubtractTargetAddress, fromAtom);
						parser.addFixup(src, ld::Fixup::k4of5, ld::Fixup::kindSubtractAddend, offsetInFrom);
						parser.addFixup(src, ld::Fixup::k5of5, ld::Fixup::kindStoreLittleEndian32);
					}
				}
				break;
			case ARM_RELOC_HALF_SECTDIFF:
				if ( nextRelocIsPair ) {
					instruction = LittleEndian::get32(*fixUpPtr);
					Atom<arm>* fromAtom  = parser.findAtomByAddress(nextRelocValue);
					uint32_t offsetInFrom = nextRelocValue - fromAtom->_objAddress;
					Atom<arm>* targetAtom  = parser.findAtomByAddress(sreloc->r_value());
					uint32_t offsetInTarget = sreloc->r_value() - targetAtom->_objAddress;
					uint32_t instruction16;
					uint32_t other16 = (nextRelocAddress & 0xFFFF);
					bool isThumb;
					if ( sreloc->r_length() & 2 ) {
						isThumb = true;
						uint32_t i =    ((instruction & 0x00000400) >> 10);
						uint32_t imm4 =  (instruction & 0x0000000F);
						uint32_t imm3 = ((instruction & 0x70000000) >> 28);
						uint32_t imm8 = ((instruction & 0x00FF0000) >> 16);
						instruction16 = (imm4 << 12) | (i << 11) | (imm3 << 8) | imm8;
					}
					else {
						isThumb = false;
						uint32_t imm4 = ((instruction & 0x000F0000) >> 16);
						uint32_t imm12 = (instruction & 0x00000FFF);
						instruction16 = (imm4 << 12) | imm12;
					}
					if ( sreloc->r_length() & 1 )
						dstAddr = ((instruction16 << 16) | other16);
					else 
						dstAddr = (other16 << 16) | instruction16;
					if ( targetAtom->isThumb() )
						dstAddr &= (-2); // remove thumb bit
                    int32_t addend = dstAddr - (sreloc->r_value() - nextRelocValue);
					if ( targetAtom->scope() == ld::Atom::scopeTranslationUnit ) {
						parser.addFixup(src, ld::Fixup::k1of5, ld::Fixup::kindSetTargetAddress, targetAtom);
					}
					else if ( (targetAtom->combine() == ld::Atom::combineByNameAndContent) || (targetAtom->combine() == ld::Atom::combineByNameAndReferences) ) {
						parser.addFixup(src, ld::Fixup::k1of5, ld::Fixup::kindSetTargetAddress, ld::Fixup::bindingByContentBound, targetAtom);
					}
					else {
						parser.addFixup(src, ld::Fixup::k1of5, ld::Fixup::kindSetTargetAddress, false, targetAtom->name());
					}
					parser.addFixup(src, ld::Fixup::k2of5, ld::Fixup::kindAddAddend, (uint32_t)offsetInTarget+addend);
					parser.addFixup(src, ld::Fixup::k3of5, ld::Fixup::kindSubtractTargetAddress, fromAtom);
					parser.addFixup(src, ld::Fixup::k4of5, ld::Fixup::kindSubtractAddend, offsetInFrom);
					if ( sreloc->r_length() & 1 ) {
						// high 16
						parser.addFixup(src, ld::Fixup::k5of5, (isThumb ? ld::Fixup::kindStoreThumbHigh16 : ld::Fixup::kindStoreARMHigh16));
					}
					else {
						// low 16
						parser.addFixup(src, ld::Fixup::k5of5, (isThumb ? ld::Fixup::kindStoreThumbLow16 : ld::Fixup::kindStoreARMLow16));
					}
					result = true;
				}
				else
					throw "ARM_RELOC_HALF_SECTDIFF reloc missing following pair";
				break;
			case ARM_RELOC_HALF:
				if ( nextRelocIsPair ) {
					instruction = LittleEndian::get32(*fixUpPtr);
					Atom<arm>* targetAtom  = parser.findAtomByAddress(sreloc->r_value());
					uint32_t instruction16;
					uint32_t other16 = (nextRelocAddress & 0xFFFF);
					bool isThumb;
					if ( sreloc->r_length() & 2 ) {
						isThumb = true;
						uint32_t i =    ((instruction & 0x00000400) >> 10);
						uint32_t imm4 =  (instruction & 0x0000000F);
						uint32_t imm3 = ((instruction & 0x70000000) >> 28);
						uint32_t imm8 = ((instruction & 0x00FF0000) >> 16);
						instruction16 = (imm4 << 12) | (i << 11) | (imm3 << 8) | imm8;
					}
					else {
						isThumb = false;
						uint32_t imm4 = ((instruction & 0x000F0000) >> 16);
						uint32_t imm12 = (instruction & 0x00000FFF);
						instruction16 = (imm4 << 12) | imm12;
					}
					if ( sreloc->r_length() & 1 )
						dstAddr = ((instruction16 << 16) | other16);
					else 
						dstAddr = (other16 << 16) | instruction16;
					if ( targetAtom->scope() == ld::Atom::scopeTranslationUnit ) {
						parser.addFixup(src, ld::Fixup::k1of3, ld::Fixup::kindSetTargetAddress, targetAtom);
					}
					else if ( (targetAtom->combine() == ld::Atom::combineByNameAndContent) || (targetAtom->combine() == ld::Atom::combineByNameAndReferences) ) {
						parser.addFixup(src, ld::Fixup::k1of3, ld::Fixup::kindSetTargetAddress, ld::Fixup::bindingByContentBound, targetAtom);
					}
					else {
						parser.addFixup(src, ld::Fixup::k1of3, ld::Fixup::kindSetTargetAddress, false, targetAtom->name());
					}
					parser.addFixup(src, ld::Fixup::k2of3, ld::Fixup::kindAddAddend, dstAddr - targetAtom->_objAddress);
					if ( sreloc->r_length() & 1 ) {
						// high 16
						parser.addFixup(src, ld::Fixup::k3of3, (isThumb ? ld::Fixup::kindStoreThumbHigh16 : ld::Fixup::kindStoreARMHigh16));
					}
					else {
						// low 16
						parser.addFixup(src, ld::Fixup::k3of3, (isThumb ? ld::Fixup::kindStoreThumbLow16 : ld::Fixup::kindStoreARMLow16));
					}
					result = true;
				}
				else
					throw "scattered ARM_RELOC_HALF reloc missing following pair";
				break;
			default:
				throwf("unknown ARM scattered relocation type %d", sreloc->r_type());
		}
	}
	return result;
}
#endif





template <typename A>
bool ObjC1ClassSection<A>::addRelocFixup(class Parser<A>& parser, const macho_relocation_info<P>* reloc)
{
	// inherited
	FixedSizeSection<A>::addRelocFixup(parser, reloc);
	
	assert(0 && "needs template specialization");
	return false;
}

template <>
bool ObjC1ClassSection<x86>::addRelocFixup(class Parser<x86>& parser, const macho_relocation_info<x86::P>* reloc)
{
	// if this is the reloc for the super class name string, add implicit reference to super class
	if ( ((reloc->r_address() & R_SCATTERED) == 0) && (reloc->r_type() == GENERIC_RELOC_VANILLA) ) {
		assert( reloc->r_length() == 2 );
		assert( ! reloc->r_pcrel() );
		
		const macho_section<P>* sect = this->machoSection();
		Parser<x86>::SourceLocation	src;
		uint32_t srcAddr = sect->addr() + reloc->r_address();
		src.atom = this->findAtomByAddress(srcAddr);
		src.offsetInAtom = srcAddr - src.atom->objectAddress();
		if ( src.offsetInAtom == 4 ) {
			Parser<x86>::TargetDesc		stringTarget;
			const uint8_t* fixUpPtr = file().fileContent() + sect->offset() + reloc->r_address();
			uint32_t contentValue = LittleEndian::get32(*((uint32_t*)fixUpPtr));
			parser.findTargetFromAddressAndSectionNum(contentValue, reloc->r_symbolnum(), stringTarget);
			
			assert(stringTarget.atom != NULL);
			assert(stringTarget.atom->contentType() == ld::Atom::typeCString);
			const char* superClassBaseName = (char*)stringTarget.atom->rawContentPointer();
			char* superClassName = new char[strlen(superClassBaseName) + 20];
			strcpy(superClassName, ".objc_class_name_");
			strcat(superClassName, superClassBaseName);
			
			parser.addFixup(src, ld::Fixup::k1of1, ld::Fixup::kindSetTargetAddress, false, superClassName);
		}
	}
	// inherited
	return FixedSizeSection<x86>::addRelocFixup(parser, reloc);
}



template <typename A>
bool Objc1ClassReferences<A>::addRelocFixup(class Parser<A>& parser, const macho_relocation_info<P>* reloc)
{
	// inherited
	PointerToCStringSection<A>::addRelocFixup(parser, reloc);
	
	assert(0 && "needs template specialization");
	return false;
}



template <>
bool Objc1ClassReferences<x86>::addRelocFixup(class Parser<x86>& parser, const macho_relocation_info<x86::P>* reloc)
{
	// add implict class refs, fixups not usable yet, so look at relocations
	assert( (reloc->r_address() & R_SCATTERED) == 0 );
	assert( reloc->r_type() == GENERIC_RELOC_VANILLA );
	assert( reloc->r_length() == 2 );
	assert( ! reloc->r_pcrel() );
	
	const macho_section<P>* sect = this->machoSection();
	Parser<x86>::SourceLocation	src;
	uint32_t srcAddr = sect->addr() + reloc->r_address();
	src.atom = this->findAtomByAddress(srcAddr);
	src.offsetInAtom = srcAddr - src.atom->objectAddress();
	Parser<x86>::TargetDesc		stringTarget;
	const uint8_t* fixUpPtr = file().fileContent() + sect->offset() + reloc->r_address();
	uint32_t contentValue = LittleEndian::get32(*((uint32_t*)fixUpPtr));
	parser.findTargetFromAddressAndSectionNum(contentValue, reloc->r_symbolnum(), stringTarget);
	
	assert(stringTarget.atom != NULL);
	assert(stringTarget.atom->contentType() == ld::Atom::typeCString);
	const char* baseClassName = (char*)stringTarget.atom->rawContentPointer();
	char* objcClassName = new char[strlen(baseClassName) + 20];
	strcpy(objcClassName, ".objc_class_name_");
	strcat(objcClassName, baseClassName);

	parser.addFixup(src, ld::Fixup::k1of1, ld::Fixup::kindSetTargetAddress, false, objcClassName);

	// inherited
	return PointerToCStringSection<x86>::addRelocFixup(parser, reloc);
}


template <typename A>
void Section<A>::makeFixups(class Parser<A>& parser, const struct Parser<A>::CFI_CU_InfoArrays&)
{
	const macho_section<P>* sect = this->machoSection();
	const macho_relocation_info<P>* relocs = (macho_relocation_info<P>*)(file().fileContent() + sect->reloff());
	const uint32_t relocCount = sect->nreloc();
	for (uint32_t r = 0; r < relocCount; ++r) {
		try {
			if ( this->addRelocFixup(parser, &relocs[r]) )
				++r; // skip next
		}
		catch (const char* msg) {
			throwf("in section %s,%s reloc %u: %s", sect->segname(), Section<A>::makeSectionName(sect), r, msg);
		}
	}
	
	// add follow-on fixups if .o file is missing .subsections_via_symbols
	if ( this->addFollowOnFixups() ) {
		Atom<A>* end = &_endAtoms[-1];
		for(Atom<A>* p = _beginAtoms; p < end; ++p) {
			typename Parser<A>::SourceLocation src(p, 0);
			Atom<A>* nextAtom = &p[1];
			parser.addFixup(src, ld::Fixup::k1of1, ld::Fixup::kindNoneFollowOn, nextAtom);
		}
	}
	else if ( this->type() == ld::Section::typeCode ) {
		// if FDE broke text not at a symbol, use followOn to keep code together
		Atom<A>* end = &_endAtoms[-1];
		for(Atom<A>* p = _beginAtoms; p < end; ++p) {
			typename Parser<A>::SourceLocation src(p, 0);
			Atom<A>* nextAtom = &p[1];
			if ( (p->symbolTableInclusion() == ld::Atom::symbolTableIn) && (nextAtom->symbolTableInclusion() == ld::Atom::symbolTableNotIn) ) {
				parser.addFixup(src, ld::Fixup::k1of1, ld::Fixup::kindNoneFollowOn, nextAtom);
			}
		}
	}
	
	// <rdar://problem/9218847> track data-in-code
	if ( parser.hasDataInCodeLabels() && (this->type() == ld::Section::typeCode) ) {
		for (uint32_t i=0; i < parser.symbolCount(); ++i) {
			const macho_nlist<P>& sym =	parser.symbolFromIndex(i);
			// ignore stabs
			if ( (sym.n_type() & N_STAB) != 0 )
				continue;
			// ignore non-definitions
			if ( (sym.n_type() & N_TYPE) != N_SECT )
				continue;

			// 'L' labels do not denote atom breaks
			const char* symbolName = parser.nameFromSymbol(sym);
			if ( symbolName[0] == 'L' ) {
				if ( strncmp(symbolName, "L$start$", 8) == 0 ) {
					ld::Fixup::Kind kind = ld::Fixup::kindNone;
					if ( strncmp(&symbolName[8], "data$", 5) == 0 )
						kind = ld::Fixup::kindDataInCodeStartData;
					else if ( strncmp(&symbolName[8], "code$", 5) == 0 )
						kind = ld::Fixup::kindDataInCodeEnd;
					else if ( strncmp(&symbolName[8], "jt8$", 4) == 0 )
						kind = ld::Fixup::kindDataInCodeStartJT8;
					else if ( strncmp(&symbolName[8], "jt16$", 4) == 0 )
						kind = ld::Fixup::kindDataInCodeStartJT16;
					else if ( strncmp(&symbolName[8], "jt32$", 4) == 0 )
						kind = ld::Fixup::kindDataInCodeStartJT32;
					else if ( strncmp(&symbolName[8], "jta32$", 4) == 0 )
						kind = ld::Fixup::kindDataInCodeStartJTA32;
					else 
						warning("unknown L$start$ label %s in file %s", symbolName, this->file().path());
					if ( kind != ld::Fixup::kindNone ) {
						Atom<A>* inAtom = parser.findAtomByAddress(sym.n_value());
						typename Parser<A>::SourceLocation src(inAtom, sym.n_value() - inAtom->objectAddress());
						parser.addFixup(src, ld::Fixup::k1of1, kind);
					}
				}
			}
		}
	}
	
	// <rdar://problem/11150575> Handle LC_DATA_IN_CODE in object files
	if ( this->type() == ld::Section::typeCode ) {
		const pint_t startAddr = this->_machOSection->addr();
		const pint_t endAddr = startAddr + this->_machOSection->size();
		for ( const macho_data_in_code_entry<P>* p = parser.dataInCodeStart(); p != parser.dataInCodeEnd(); ++p ) {
			if ( (p->offset() >= startAddr) && (p->offset() < endAddr) ) {
				ld::Fixup::Kind kind = ld::Fixup::kindNone;
				switch ( p->kind() ) {
					case DICE_KIND_DATA:
						kind = ld::Fixup::kindDataInCodeStartData;
						break;
					case DICE_KIND_JUMP_TABLE8:
						kind = ld::Fixup::kindDataInCodeStartJT8;
						break;
					case DICE_KIND_JUMP_TABLE16:
						kind = ld::Fixup::kindDataInCodeStartJT16;
						break;
					case DICE_KIND_JUMP_TABLE32:
						kind = ld::Fixup::kindDataInCodeStartJT32;
						break;
					case DICE_KIND_ABS_JUMP_TABLE32:
						kind = ld::Fixup::kindDataInCodeStartJTA32;
						break;
					default:
						kind = ld::Fixup::kindDataInCodeStartData;
						warning("uknown LC_DATA_IN_CODE kind (%d) at offset 0x%08X", p->kind(), p->offset());
						break;
				}
				Atom<A>* inAtom = parser.findAtomByAddress(p->offset());
				typename Parser<A>::SourceLocation srcStart(inAtom, p->offset() - inAtom->objectAddress());
				parser.addFixup(srcStart, ld::Fixup::k1of1, kind);
				typename Parser<A>::SourceLocation srcEnd(inAtom, p->offset() + p->length() - inAtom->objectAddress());
				parser.addFixup(srcEnd, ld::Fixup::k1of1, ld::Fixup::kindDataInCodeEnd);
			}
		}
	}
	
	
	// add follow-on fixups for aliases
	if ( _hasAliases ) {
		for(Atom<A>* p = _beginAtoms; p < _endAtoms; ++p) {
			if ( p->isAlias() && ! this->addFollowOnFixups() ) {
				Atom<A>* targetOfAlias = &p[1];
				assert(p < &_endAtoms[-1]);
				assert(p->_objAddress == targetOfAlias->_objAddress);
				typename Parser<A>::SourceLocation src(p, 0);
				parser.addFixup(src, ld::Fixup::k1of1, ld::Fixup::kindNoneFollowOn, targetOfAlias);
			}
		}
	}
}



//
// main function used by linker to instantiate ld::Files
//
ld::relocatable::File* parse(const uint8_t* fileContent, uint64_t fileLength, 
							 const char* path, time_t modTime, ld::File::Ordinal ordinal, const ParserOptions& opts)
{
	switch ( opts.architecture ) {
#if SUPPORT_ARCH_x86_64
		case CPU_TYPE_X86_64:
			if ( mach_o::relocatable::Parser<x86_64>::validFile(fileContent) )
				return mach_o::relocatable::Parser<x86_64>::parse(fileContent, fileLength, path, modTime, ordinal, opts);
			break;
#endif
#if SUPPORT_ARCH_i386
		case CPU_TYPE_I386:
			if ( mach_o::relocatable::Parser<x86>::validFile(fileContent) )
				return mach_o::relocatable::Parser<x86>::parse(fileContent, fileLength, path, modTime, ordinal, opts);
			break;
#endif
#if SUPPORT_ARCH_arm_any
		case CPU_TYPE_ARM:
			if ( mach_o::relocatable::Parser<arm>::validFile(fileContent, opts.objSubtypeMustMatch, opts.subType) )
				return mach_o::relocatable::Parser<arm>::parse(fileContent, fileLength, path, modTime, ordinal, opts);
			break;
#endif
	}
	return NULL;
}

//
// used by archive reader to validate member object file
//
bool isObjectFile(const uint8_t* fileContent, uint64_t fileLength, const ParserOptions& opts)
{
	switch ( opts.architecture ) {
		case CPU_TYPE_X86_64:
			return ( mach_o::relocatable::Parser<x86_64>::validFile(fileContent) );
		case CPU_TYPE_I386:
			return ( mach_o::relocatable::Parser<x86>::validFile(fileContent) );
		case CPU_TYPE_ARM:
			return ( mach_o::relocatable::Parser<arm>::validFile(fileContent, opts.objSubtypeMustMatch, opts.subType) );
	}
	return false;
}

//
// used by linker to infer architecture when no -arch is on command line
//
bool isObjectFile(const uint8_t* fileContent, cpu_type_t* result, cpu_subtype_t* subResult)
{
	if ( mach_o::relocatable::Parser<x86_64>::validFile(fileContent) ) {
		*result = CPU_TYPE_X86_64;
		*subResult = CPU_SUBTYPE_X86_64_ALL;
		return true;
	}
	if ( mach_o::relocatable::Parser<x86>::validFile(fileContent) ) {
		*result = CPU_TYPE_I386;
		*subResult = CPU_SUBTYPE_X86_ALL;
		return true;
	}
	if ( mach_o::relocatable::Parser<arm>::validFile(fileContent, false, 0) ) {
		*result = CPU_TYPE_ARM;
		const macho_header<Pointer32<LittleEndian> >* header = (const macho_header<Pointer32<LittleEndian> >*)fileContent;
		*subResult = header->cpusubtype();
		return true;
	}
	return false;
}					

//
// used by linker is error messages to describe bad .o file
//
const char* archName(const uint8_t* fileContent)
{
	if ( mach_o::relocatable::Parser<x86_64>::validFile(fileContent) ) {
		return mach_o::relocatable::Parser<x86_64>::fileKind(fileContent);
	}
	if ( mach_o::relocatable::Parser<x86>::validFile(fileContent) ) {
		return mach_o::relocatable::Parser<x86>::fileKind(fileContent);
	}
	if ( mach_o::relocatable::Parser<arm>::validFile(fileContent, false, 0) ) {
		return mach_o::relocatable::Parser<arm>::fileKind(fileContent);
	}
	return NULL;
}

//
// Used by archive reader when -ObjC option is specified
//	
bool hasObjC2Categories(const uint8_t* fileContent)
{
	if ( mach_o::relocatable::Parser<x86_64>::validFile(fileContent) ) {
		return mach_o::relocatable::Parser<x86_64>::hasObjC2Categories(fileContent);
	}
	else if ( mach_o::relocatable::Parser<arm>::validFile(fileContent, false, 0) ) {
		return mach_o::relocatable::Parser<arm>::hasObjC2Categories(fileContent);
	}
	else if ( mach_o::relocatable::Parser<x86>::validFile(fileContent, false, 0) ) {
		return mach_o::relocatable::Parser<x86>::hasObjC2Categories(fileContent);
	}
	return false;
}				

//
// Used by archive reader when -ObjC option is specified
//	
bool hasObjC1Categories(const uint8_t* fileContent)
{
	if ( mach_o::relocatable::Parser<x86>::validFile(fileContent, false, 0) ) {
		return mach_o::relocatable::Parser<x86>::hasObjC1Categories(fileContent);
	}
	return false;
}



} // namespace relocatable
} // namespace mach_o


