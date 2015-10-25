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


// already in ld::passes::stubs namespace
namespace ppc {
namespace classic {



class LazyPointerAtom : public ld::Atom {
public:
	LazyPointerAtom(ld::passes::stubs::Pass& pass, const ld::Atom& stubTo,
		bool forLazyDylib, bool for64, bool weakImport)
		: ld::Atom( forLazyDylib ? _s_sectionLazy : _s_section,
				ld::Atom::definitionRegular, ld::Atom::combineNever,
				ld::Atom::scopeTranslationUnit,
				forLazyDylib ? ld::Atom::typeLazyDylibPointer : ld::Atom::typeLazyPointer,
				symbolTableNotIn, false, false, false, for64 ? ld::Atom::Alignment(3) : ld::Atom::Alignment(2)),
			_stubTo(stubTo),
			_fixup1(0, ld::Fixup::k1of1,
			for64 ? ld::Fixup::kindStoreTargetAddressBigEndian64 : ld::Fixup::kindStoreTargetAddressBigEndian32,
			forLazyDylib ? pass.internal()->lazyBindingHelper : pass.internal()->classicBindingHelper),
			_fixup2(0, ld::Fixup::k1of1, ld::Fixup::kindLazyTarget, &stubTo),
			_for64(for64)
		{  _fixup2.weakImport = weakImport; pass.addAtom(*this);  }

	virtual const ld::File*	file() const	{ return _stubTo.file(); }
	virtual bool		translationUnitSource(const char** dir, const char** ) const
		{ return false; }
	virtual const char*	name() const		{ return _stubTo.name(); }
	virtual uint64_t	size() const		{ return _for64 ? 8 : 4; }
	virtual uint64_t	objectAddress() const	{ return 0; }
	virtual void		copyRawContent(uint8_t buffer[]) const { }
	virtual void		setScope(Scope)		{ }
	virtual ld::Fixup::iterator	fixupsBegin() const	{ return &_fixup1; }
	virtual ld::Fixup::iterator	fixupsEnd() const	{ return &((ld::Fixup*)&_fixup2)[1]; }

private:
	const ld::Atom&		_stubTo;
	mutable ld::Fixup	_fixup1;
	mutable ld::Fixup	_fixup2;
	const bool		_for64;

	static ld::Section	_s_section;
	static ld::Section	_s_sectionLazy;
};

ld::Section LazyPointerAtom::_s_section("__DATA", "__la_symbol_ptr", ld::Section::typeLazyPointer);
ld::Section LazyPointerAtom::_s_sectionLazy("__DATA", "__ld_symbol_ptr", ld::Section::typeLazyDylibPointer);



class StubPICAtom : public ld::Atom {
public:
	StubPICAtom(ld::passes::stubs::Pass& pass, const ld::Atom& stubTo,
		bool forLazyDylib, bool for64, bool weakImport)
		: ld::Atom(_s_section, ld::Atom::definitionRegular, ld::Atom::combineNever,
				ld::Atom::scopeLinkageUnit, ld::Atom::typeStub,
				symbolTableNotIn, false, false, false, ld::Atom::Alignment(2)),
			_stubTo(stubTo),
			_lazyPointer(pass, stubTo, forLazyDylib, for64, weakImport),
			_fixup1(12, ld::Fixup::k1of4, ld::Fixup::kindSetTargetAddress, &_lazyPointer),
			_fixup2(12, ld::Fixup::k2of4, ld::Fixup::kindSubtractTargetAddress, this),
			_fixup3(12, ld::Fixup::k3of4, ld::Fixup::kindSubtractAddend, 8),
			_fixup4(12, ld::Fixup::k4of4, ld::Fixup::kindStorePPCPicHigh16AddLow),
			_fixup5(20, ld::Fixup::k1of4, ld::Fixup::kindSetTargetAddress, &_lazyPointer),
			_fixup6(20, ld::Fixup::k2of4, ld::Fixup::kindSubtractTargetAddress, this),
			_fixup7(20, ld::Fixup::k3of4, ld::Fixup::kindSubtractAddend, 8),
			_fixup8(20, ld::Fixup::k4of4, for64 ? ld::Fixup::kindStorePPCPicLow14 : ld::Fixup::kindStorePPCPicLow16),
			_for64(for64)
		{ pass.addAtom(*this); }

	virtual const ld::File*	file() const	{ return _stubTo.file(); }
	virtual bool		translationUnitSource(const char** dir, const char** ) const
																			{ return false; }
	virtual const char*	name() const		{ return _stubTo.name(); }
	virtual uint64_t	size() const		{ return 32; }
	virtual uint64_t	objectAddress() const	{ return 0; }
	virtual void		copyRawContent(uint8_t buffer[]) const {
		OSWriteBigInt32(&buffer[ 0], 0, 0x7c0802a6);	// mflr r0
		OSWriteBigInt32(&buffer[ 4], 0, 0x429f0005);	// bcl 20,31,Lpicbase
		OSWriteBigInt32(&buffer[ 8], 0, 0x7d6802a6);	// Lpicbase: mflr r11
		OSWriteBigInt32(&buffer[12], 0, 0x3d6b0000);	// addis r11,r11,ha16(L_fwrite$lazy_ptr-Lpicbase)
		OSWriteBigInt32(&buffer[16], 0, 0x7c0803a6);	// mtlr r0
		if ( _for64 )
			OSWriteBigInt32(&buffer[20], 0, 0xe98b0001);// ldu r12,lo16(L_fwrite$lazy_ptr-Lpicbase)(r11)
		else
			OSWriteBigInt32(&buffer[20], 0, 0x858b0000);// lwzu r12,lo16(L_fwrite$lazy_ptr-Lpicbase)(r11)
		OSWriteBigInt32(&buffer[24], 0, 0x7d8903a6);	//  mtctr r12
		OSWriteBigInt32(&buffer[28], 0, 0x4e800420);	//  bctr
	}
	virtual void			setScope(Scope)		{ }
	virtual ld::Fixup::iterator	fixupsBegin() const	{ return &_fixup1; }
	virtual ld::Fixup::iterator	fixupsEnd() const	{ return &((ld::Fixup*)&_fixup8)[1]; }

private:
	const ld::Atom&		_stubTo;
	LazyPointerAtom		_lazyPointer;
	mutable ld::Fixup	_fixup1;
	mutable ld::Fixup	_fixup2;
	mutable ld::Fixup	_fixup3;
	mutable ld::Fixup	_fixup4;
	mutable ld::Fixup	_fixup5;
	mutable ld::Fixup	_fixup6;
	mutable ld::Fixup	_fixup7;
	mutable ld::Fixup	_fixup8;
	const bool		_for64;

	static ld::Section	_s_section;
};

ld::Section StubPICAtom::_s_section("__TEXT", "__picsymbolstub1", ld::Section::typeStub);



class StubNoPICAtom : public ld::Atom {
public:
	StubNoPICAtom(ld::passes::stubs::Pass& pass, const ld::Atom& stubTo,
		bool forLazyDylib, bool for64, bool weakImport)
			: ld::Atom(_s_section, ld::Atom::definitionRegular, ld::Atom::combineNever,
				ld::Atom::scopeLinkageUnit, ld::Atom::typeStub,
				symbolTableNotIn, false, false, false, ld::Atom::Alignment(2)),
			_stubTo(stubTo),
			_lazyPointer(pass, stubTo, forLazyDylib, for64, weakImport),
			_fixup1(0, ld::Fixup::k1of2, ld::Fixup::kindSetTargetAddress, &_lazyPointer),
			_fixup2(0, ld::Fixup::k2of2, ld::Fixup::kindStorePPCAbsHigh16AddLow),
			_fixup3(4, ld::Fixup::k1of2, ld::Fixup::kindSetTargetAddress, &_lazyPointer),
			_fixup4(4, ld::Fixup::k2of2, for64 ? ld::Fixup::kindStorePPCAbsLow14 : ld::Fixup::kindStorePPCAbsLow16),
			_for64(for64)
		{ pass.addAtom(*this); }

	virtual const ld::File*	file() const	{ return _stubTo.file(); }
	virtual bool		translationUnitSource(const char** dir, const char** ) const
		{ return false; }
	virtual const char*	name() const		{ return _stubTo.name(); }
	virtual uint64_t	size() const		{ return 16; }
	virtual uint64_t	objectAddress() const	{ return 0; }
	virtual void		copyRawContent(uint8_t buffer[]) const {
		OSWriteBigInt32(&buffer[ 0], 0, 0x3d600000);	// lis r11,ha16(L_foo$lazy_ptr)
		if ( _for64 )
			OSWriteBigInt32(&buffer[ 4], 0, 0xe98b0001);// ldu r12,lo16(L_foo$lazy_ptr)(r11)
		else
			OSWriteBigInt32(&buffer[ 4], 0, 0x858b0000);// lwzu r12,lo16(L_foo$lazy_ptr)(r11)
		OSWriteBigInt32(&buffer[ 8], 0, 0x7d8903a6);	// mtctr r12
		OSWriteBigInt32(&buffer[12], 0, 0x4e800420);	// bctr
	}
	virtual void			setScope(Scope)		{ }
	virtual ld::Fixup::iterator	fixupsBegin() const	{ return &_fixup1; }
	virtual ld::Fixup::iterator	fixupsEnd() const	{ return &((ld::Fixup*)&_fixup4)[1]; }

private:
	const ld::Atom&		_stubTo;
	LazyPointerAtom		_lazyPointer;
	mutable ld::Fixup	_fixup1;
	mutable ld::Fixup	_fixup2;
	mutable ld::Fixup	_fixup3;
	mutable ld::Fixup	_fixup4;
	const bool		_for64;

	static ld::Section	_s_section;
};

ld::Section StubNoPICAtom::_s_section("__TEXT", "__symbol_stub1", ld::Section::typeStub);


} // namespace classic
} // namespace ppc

