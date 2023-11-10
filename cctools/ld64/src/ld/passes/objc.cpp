/* -*- mode: C++; c-basic-offset: 4; tab-width: 4 -*-
 *
 * Copyright (c) 2010-2020 Apple Inc. All rights reserved.
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

#include <algorithm>
#include <vector>
#include <map>
#include <set>

#include "Architectures.hpp"
#include "MachOFileAbstraction.hpp"

#include "ld.hpp"
#include "objc.h"
#include "Containers.h"

namespace ld {
namespace passes {
namespace objc {


using NameToAtom = CStringMap<const ld::Atom*>;

struct objc_image_info  {
	uint32_t	version;	// initially 0
	uint32_t	flags;
};

#define OBJC_IMAGE_SUPPORTS_GC						(1<<1)
#define OBJC_IMAGE_REQUIRES_GC						(1<<2)
#define OBJC_IMAGE_OPTIMIZED_BY_DYLD				(1<<3)
#define OBJC_IMAGE_SIGNED_CLASS_RO					(1<<4)
#define OBJC_IMAGE_IS_SIMULATED					(1<<5)
#define OBJC_IMAGE_HAS_CATEGORY_CLASS_PROPERTIES	(1<<6)



//
// This class is the 8 byte section containing ObjC flags
//
template <typename A>
class ObjCImageInfoAtom : public ld::Atom {
public:
    										ObjCImageInfoAtom(bool abi2, bool hasSignedClassROs, bool hasCategoryClassProperties, uint8_t swiftVersion, uint16_t swiftLanguageVersion);

	virtual const ld::File*					file() const					{ return NULL; }
	virtual const char*						name() const					{ return "objc image info"; }
	virtual uint64_t						size() const					{ return sizeof(objc_image_info); }
	virtual uint64_t						objectAddress() const			{ return 0; }
	virtual void							setScope(Scope)					{ }
	virtual void							copyRawContent(uint8_t buffer[]) const {
		memcpy(buffer, &_content, sizeof(objc_image_info));
	}

private:	
	objc_image_info							_content;

	static ld::Section						_s_sectionABI1;
	static ld::Section						_s_sectionABI2;
};

template <typename A> ld::Section ObjCImageInfoAtom<A>::_s_sectionABI1("__OBJC", "__image_info", ld::Section::typeUnclassified);
template <typename A> ld::Section ObjCImageInfoAtom<A>::_s_sectionABI2("__DATA", "__objc_imageinfo", ld::Section::typeUnclassified);


template <typename A>
ObjCImageInfoAtom<A>::ObjCImageInfoAtom(bool abi2, bool hasSignedClassROs, bool hasCategoryClassProperties, uint8_t swiftVersion, uint16_t swiftLanguageVersion)
	: ld::Atom(abi2 ? _s_sectionABI2 : _s_sectionABI1, ld::Atom::definitionRegular, ld::Atom::combineNever,
							ld::Atom::scopeLinkageUnit, ld::Atom::typeUnclassified, 
							symbolTableNotIn, false, false, false, ld::Atom::Alignment(2))
{
	uint32_t value = 0;

    if ( hasSignedClassROs ) {
        value |= OBJC_IMAGE_SIGNED_CLASS_RO;
    }

	if ( hasCategoryClassProperties ) {
		value |= OBJC_IMAGE_HAS_CATEGORY_CLASS_PROPERTIES;
	}

	// provide swift ABI version in final binary for runtime to inspect
	value |= (swiftVersion << 8);

	// provide swift language version in final binary for runtime to inspect
	value |= (swiftLanguageVersion << 16);

	_content.version = 0;
	A::P::E::set32(_content.flags, value);
}


//
// This class is for a new Atom which is an ObjC category name created by merging names from categories
//
class SelRefAtom : public ld::Atom {
public:
											SelRefAtom(ld::Internal& state, const ld::Atom* target, bool is64)
				: ld::Atom(_s_section, ld::Atom::definitionRegular, ld::Atom::combineNever,
							ld::Atom::scopeLinkageUnit, ld::Atom::typeNonLazyPointer,
							ld::Atom::symbolTableInWithRandomAutoStripLabel, false, false, false, (is64 ? ld::Atom::Alignment(3) : ld::Atom::Alignment(2))),
				_fixup(0, ld::Fixup::k1of1, (is64 ? ld::Fixup::kindStoreTargetAddressLittleEndian64 : ld::Fixup::kindStoreTargetAddressLittleEndian32), target),
				_target(target),
				_is64(is64)
					{  state.addAtom(*this); }

	virtual const ld::File*					file() const					{ return NULL; }
	virtual const char*						name() const					{ return _target->name(); }
	virtual uint64_t						size() const					{ return (_is64 ? 8 : 4); }
	virtual uint64_t						objectAddress() const			{ return 0; }
	virtual void							copyRawContent(uint8_t buffer[]) const { }
	virtual void							setScope(Scope)					{ }
	virtual ld::Fixup::iterator				fixupsBegin() const				{ return &_fixup; }
	virtual ld::Fixup::iterator				fixupsEnd()	const 				{ return &((ld::Fixup*)&_fixup)[1]; }

private:
	mutable ld::Fixup						_fixup;
	const ld::Atom*							_target;
	bool									_is64;
	
	static ld::Section						_s_section;
	static ld::Section						_s_sectionWeak;
};

ld::Section SelRefAtom::_s_section("__DATA", "__objc_selrefs", ld::Section::typeCStringPointer);


//
// This class is for a new Atom in the __objc_nlclslist section when a category with a +load is merged into a class
//
class NonLazyClassListAtom : public ld::Atom {
public:
											NonLazyClassListAtom(ld::Internal& state, const ld::Atom* target, bool is64)
				: ld::Atom(_s_section, ld::Atom::definitionRegular, ld::Atom::combineNever,
							ld::Atom::scopeLinkageUnit, ld::Atom::typeUnclassified,
							symbolTableNotIn, false, false, false, (is64 ? ld::Atom::Alignment(3) : ld::Atom::Alignment(2))),
				_fixup(0, ld::Fixup::k1of1, (is64 ? ld::Fixup::kindStoreTargetAddressLittleEndian64 : ld::Fixup::kindStoreTargetAddressLittleEndian32), target),
				_target(target),
				_is64(is64)
					{  }

	virtual const ld::File*					file() const					{ return NULL; }
	virtual const char*						name() const					{ return _target->name(); }
	virtual uint64_t						size() const					{ return (_is64 ? 8 : 4); }
	virtual uint64_t						objectAddress() const			{ return 0; }
	virtual void							copyRawContent(uint8_t buffer[]) const { }
	virtual void							setScope(Scope)					{ }
	virtual ld::Fixup::iterator				fixupsBegin() const				{ return &_fixup; }
	virtual ld::Fixup::iterator				fixupsEnd()	const 				{ return &((ld::Fixup*)&_fixup)[1]; }

private:
	mutable ld::Fixup						_fixup;
	const ld::Atom*							_target;
	bool									_is64;

	static ld::Section						_s_section;
	static ld::Section						_s_sectionWeak;
};

ld::Section NonLazyClassListAtom::_s_section("__DATA", "__objc_nlclslist", ld::Section::typeObjC2ClassList);


//
// This class is for a new Atom which is an ObjC category name created by merging names from categories
//
template <typename A>
class CategoryNameAtom : public ld::Atom {
public:
											CategoryNameAtom(ld::Internal& state,
															const std::vector<const ld::Atom*>* categories);

	virtual const ld::File*					file() const					{ return _file; }
	virtual const char*						name() const					{ return "objc merged category name"; }
	virtual uint64_t						size() const					{ return _categoryName.size() + 1; }
	virtual uint64_t						objectAddress() const			{ return _syntheticAddress; }
	virtual void							setScope(Scope)					{ }
	virtual void							copyRawContent(uint8_t buffer[]) const {
		strcpy((char*)buffer, _categoryName.c_str());
	}

private:
	typedef typename A::P::uint_t			pint_t;

	const ld::File*							_file;
	unsigned int							_syntheticAddress;	// since all have same name, need something else to provide a stable sort
	std::string								_categoryName;

	static unsigned int					    _s_nextSyntheticAddress;
	static ld::Section						_s_section;
};

template <typename A>
ld::Section CategoryNameAtom<A>::_s_section("__TEXT", "__objc_classname", ld::Section::typeCString);

template <typename A>
unsigned int CategoryNameAtom<A>::_s_nextSyntheticAddress = 0;

struct MethodEntryInfo
{
	const char* 	methodName;
	const ld::Atom* implAtom;
	const ld::Atom* typeAtom;
	const ld::Atom* selectorStringAtom;
	const ld::Atom* selectorRefAtom;
};

//
// This class is for a new Atom which is an ObjC method list created by merging method lists from categories
//
template <typename A>
class MethodListAtom : public ld::Atom {
public:
	enum ListFormat { threePointers, threePointersAuthImpl, threeDeltas, twoPointers };
	enum ListUse    { classMethodList, categoryMethodList, propertyMethodList };

											MethodListAtom(ld::Internal& state, const ld::Atom* baseMethodList, ListFormat kind, ListUse use, const char* className,
														   bool meta, const std::vector<const ld::Atom*>* categories, NameToAtom& selectorNameToSlot,
														   std::set<const ld::Atom*>& deadAtoms);

	virtual const ld::File*					file() const					{ return _file; }
	virtual const char*						name() const					{ return _name; }
	virtual uint64_t						size() const;
	virtual uint64_t						objectAddress() const			{ return 0; }
	virtual void							setScope(Scope)					{ }
	virtual void							copyRawContent(uint8_t buffer[]) const;
	virtual ld::Fixup::iterator				fixupsBegin() const	{ return (ld::Fixup*)&_fixups[0]; }
	virtual ld::Fixup::iterator				fixupsEnd()	const	{ return (ld::Fixup*)&_fixups[_fixups.size()]; }
		

private:	
	typedef typename A::P::uint_t			pint_t;

	void									appendMethod(uint32_t methodIndex, const MethodEntryInfo& method, ld::Internal& state, NameToAtom& selectorNameToSlot);

	const ld::File*							_file;
	const char*								_name;
	unsigned int							_methodCount;
	ListFormat								_listFormat;
	ListUse									_listUse;
	std::vector<ld::Fixup>					_fixups;
	
	static ld::Section						_s_section_ptrs;
	static ld::Section						_s_section_rel;
};

template <typename A> 
ld::Section MethodListAtom<A>::_s_section_ptrs("__DATA", "__objc_data", ld::Section::typeUnclassified);

template <typename A>
ld::Section MethodListAtom<A>::_s_section_rel("__TEXT", "__objc_methlist", ld::Section::typeUnclassified);



//
// This class is for a new Atom which is an ObjC protocol list created by merging protocol lists from categories
//
template <typename A>
class ProtocolListAtom : public ld::Atom {
public:
											ProtocolListAtom(ld::Internal& state, const ld::Atom* baseProtocolList,
															const char* className, const std::vector<const ld::Atom*>* categories,
															std::set<const ld::Atom*>& deadAtoms);

	virtual const ld::File*					file() const					{ return _file; }
	virtual const char*						name() const					{ return _name.c_str(); }
	virtual uint64_t						size() const					{ return (_protocolCount+1)*sizeof(pint_t); }
	virtual uint64_t						objectAddress() const			{ return 0; }
	virtual void							setScope(Scope)					{ }
	virtual void							copyRawContent(uint8_t buffer[]) const {
		bzero(buffer, size());
		A::P::setP(*((pint_t*)(buffer)), _protocolCount);
	}
	virtual ld::Fixup::iterator				fixupsBegin() const	{ return (ld::Fixup*)&_fixups[0]; }
	virtual ld::Fixup::iterator				fixupsEnd()	const	{ return (ld::Fixup*)&_fixups[_fixups.size()]; }

private:
	typedef typename A::P::uint_t			pint_t;

	const ld::File*							_file;
	std::string								_name;
	unsigned int							_protocolCount;
	std::vector<ld::Fixup>					_fixups;
	
	static ld::Section						_s_section;
};

template <typename A>
ld::Section ProtocolListAtom<A>::_s_section("__DATA", "__objc_const", ld::Section::typeUnclassified);

//
// This class is for a new Atom which is an ObjC property list created by merging property lists from categories
//
template <typename A>
class PropertyListAtom : public ld::Atom {
public:
	enum class PropertyKind { ClassProperties, InstanceProperties };

											PropertyListAtom(ld::Internal& state, const ld::Atom* basePropertyList,
															 const std::vector<const ld::Atom*>* categories,
															 std::set<const ld::Atom*>& deadAtoms,
															 PropertyKind kind);

	virtual const ld::File*					file() const					{ return _file; }
	virtual const char*						name() const					{ return "objc merged property list"; }
	virtual uint64_t						size() const					{ return _propertyCount*2*sizeof(pint_t) + 8; }
	virtual uint64_t						objectAddress() const			{ return _syntheticAddress; }
	virtual void							setScope(Scope)					{ }
	virtual void							copyRawContent(uint8_t buffer[]) const {
		bzero(buffer, size());
		A::P::E::set32(((uint32_t*)(buffer))[0], 2*sizeof(pint_t)); // sizeof(objc_property)
		A::P::E::set32(((uint32_t*)(buffer))[1], _propertyCount);
	}
	virtual ld::Fixup::iterator				fixupsBegin() const	{ return (ld::Fixup*)&_fixups[0]; }
	virtual ld::Fixup::iterator				fixupsEnd()	const	{ return (ld::Fixup*)&_fixups[_fixups.size()]; }

private:
	typedef typename A::P::uint_t			pint_t;

	const ld::File*							_file;
	unsigned int							_propertyCount;
	unsigned int							_syntheticAddress;	// since all have same name, need something else to provide a stable sort
	std::vector<ld::Fixup>					_fixups;

	static ld::Section						_s_section;
	static unsigned int					    _s_nextSyntheticAddress;
};

template <typename A>
ld::Section PropertyListAtom<A>::_s_section("__DATA", "__objc_const", ld::Section::typeUnclassified);

template <typename A>
unsigned int PropertyListAtom<A>::_s_nextSyntheticAddress = 0;

//
// This class is used to create an Atom that replaces an atom from a .o file that holds a class_ro_t or category_t.
// It is needed because there is no way to add Fixups to an existing atom.
//
template <typename A>
class ObjCOverlayAtom : public ld::Atom {
public:
											ObjCOverlayAtom(const ld::Atom* classROAtom);

	// overrides of ld::Atom
	virtual const ld::File*				file() const		{ return _atom->file(); }
	virtual const char*					name() const		{ return _atom->name(); }
	virtual uint64_t					size() const		{ return _atom->size(); }
	virtual uint64_t					objectAddress() const { return _atom->objectAddress(); }
	virtual void						copyRawContent(uint8_t buffer[]) const
															{ _atom->copyRawContent(buffer); }
	virtual const uint8_t*				rawContentPointer() const
															{ return _atom->rawContentPointer(); }
	virtual unsigned long				contentHash(const class ld::IndirectBindingTable& ibt) const
															{ return _atom->contentHash(ibt); }
	virtual bool						canCoalesceWith(const ld::Atom& rhs, const class ld::IndirectBindingTable& ibt) const
															{ return _atom->canCoalesceWith(rhs,ibt); }

	virtual ld::Fixup::iterator			fixupsBegin() const	{ return (ld::Fixup*)&_fixups[0]; }
	virtual ld::Fixup::iterator			fixupsEnd()	const	{ return (ld::Fixup*)&_fixups[_fixups.size()]; }

protected:
	void addFixupAtOffset(uint32_t offset, bool isAuthPtr=false);

private:
	typedef typename A::P::uint_t			pint_t;

	const ld::Atom*							_atom;
	std::vector<ld::Fixup>					_fixups;
};

template <typename A>
class ClassROOverlayAtom : public ObjCOverlayAtom<A> {
public:
										ClassROOverlayAtom(ld::Internal& state, const ld::Atom* contentAtom)
											: ObjCOverlayAtom<A>(contentAtom) { }

	void								addProtocolListFixup();
	void								addPropertyListFixup();
	void								addMethodListFixup(bool isAuthPtr);
};

template <typename A>
class CategoryOverlayAtom : public ObjCOverlayAtom<A> {
public:
										CategoryOverlayAtom(const ld::Atom* contentAtom) : ObjCOverlayAtom<A>(contentAtom) { }

	void								addNameFixup();
	void								addInstanceMethodListFixup(bool isAuthPtr);
	void								addClassMethodListFixup(bool isAuthPtr);
	void								addProtocolListFixup();
	void								addInstancePropertyListFixup();
	void								addClassPropertyListFixup();
};

template <typename A>
ObjCOverlayAtom<A>::ObjCOverlayAtom(const ld::Atom* classROAtom)
	: ld::Atom(classROAtom->section(), ld::Atom::definitionRegular, ld::Atom::combineNever,
			ld::Atom::scopeLinkageUnit, ld::Atom::typeUnclassified, 
			classROAtom->symbolTableInclusion(), false, false, false, classROAtom->alignment()),
	_atom(classROAtom)
{
	// ensure all attributes are same as original
	this->setAttributesFromAtom(*classROAtom);

	// copy fixups from orginal atom
	for (ld::Fixup::iterator fit=classROAtom->fixupsBegin(); fit != classROAtom->fixupsEnd(); ++fit) {
		ld::Fixup fixup = *fit;
		_fixups.push_back(fixup);
	}
}


//
// Base class for reading and updating existing ObjC atoms from .o files
//
template <typename A>
class ObjCData {
public:
	static const ld::Atom*	getPointerInContent(ld::Internal& state, const ld::Atom* contentAtom, unsigned int offset, uint64_t* addend=NULL, bool* isAuthPtr=NULL);
	static void				setPointerInContent(ld::Internal& state, const ld::Atom* contentAtom, 
												unsigned int offset, const ld::Atom* newAtom);
	typedef typename A::P::uint_t			pint_t;
};

template <typename A>
const ld::Atom* ObjCData<A>::getPointerInContent(ld::Internal& state, const ld::Atom* contentAtom, unsigned int offset, uint64_t* addend, bool* isAuthPtr)
{
	const ld::Atom* target = NULL;
	if ( addend != NULL )
		*addend = 0;
	if ( isAuthPtr != NULL )
		*isAuthPtr = false;
	for (ld::Fixup::iterator fit=contentAtom->fixupsBegin(); fit != contentAtom->fixupsEnd(); ++fit) {
		if ( (fit->offsetInAtom == offset) && (fit->kind != ld::Fixup::kindNoneFollowOn) && (fit->kind != ld::Fixup::kindNoneGroupSubordinate) ) {
			switch ( fit->binding ) {
				case ld::Fixup::bindingsIndirectlyBound:
					target = state.indirectBindingTable[fit->u.bindingIndex];
					break;
				case ld::Fixup::bindingDirectlyBound:
					target =  fit->u.target;
					break;
				case ld::Fixup::bindingNone:
					if ( fit->kind == ld::Fixup::kindAddAddend ) {
						if ( addend != NULL )
							*addend = fit->u.addend;
					}
#if SUPPORT_ARCH_arm64e
					else if ( fit->kind == ld::Fixup::kindSetAuthData ) {
						if ( isAuthPtr != NULL )
							*isAuthPtr = true;
					}
#endif
					break;
                 default:
                    break;   
			}
		}
	}
	return target;
}

template <typename A>
void ObjCData<A>::setPointerInContent(ld::Internal& state, const ld::Atom* contentAtom, 
														unsigned int offset, const ld::Atom* newAtom)
{
	for (ld::Fixup::iterator fit=contentAtom->fixupsBegin(); fit != contentAtom->fixupsEnd(); ++fit) {
		if ( fit->offsetInAtom == offset ) {
			switch ( fit->binding ) {
				case ld::Fixup::bindingsIndirectlyBound:
					state.indirectBindingTable[fit->u.bindingIndex] = newAtom;
					return;
				case ld::Fixup::bindingDirectlyBound:
					fit->u.target = newAtom;
					return;
                default:
                     break;    
			}
		}
	}
	assert(0 && "could not update method list");
}



#define GET_FIELD(state, classAtom, field) \
	ObjCData<A>::getPointerInContent(state, classAtom, offsetof(Content, field))
#define GET_FIELD_AUTH(state, classAtom, field, auth) \
	ObjCData<A>::getPointerInContent(state, classAtom, offsetof(Content, field), nullptr, auth)

#define SET_FIELD(state, classAtom, field, valueAtom) \
	ObjCData<A>::setPointerInContent(state, classAtom, offsetof(Content, field), valueAtom)

#define GET_RO_FIELD(state, classAtom, field) \
	ObjCData<A>::getPointerInContent(state, getROData(state, classAtom), offsetof(ROContent, field))
#define GET_RO_FIELD_AUTH(state, classAtom, field, auth) \
	ObjCData<A>::getPointerInContent(state, getROData(state, classAtom), offsetof(ROContent, field), nullptr, auth)
#define SET_RO_FIELD(state, classROAtom, field, valueAtom) \
	ObjCData<A>::setPointerInContent(state, getROData(state, classAtom), offsetof(ROContent, field), valueAtom)

//
// Helper class for reading and updating existing ObjC category atoms from .o files
//
template <typename A>
class Category : public ObjCData<A> {
public:
	// Getters
	static const ld::Atom*	getName(ld::Internal& state, const ld::Atom* contentAtom);
	static const ld::Atom*	getClass(ld::Internal& state, const ld::Atom* contentAtom, uint64_t& addend);
	static const ld::Atom*	getInstanceMethods(ld::Internal& state, const ld::Atom* contentAtom, bool* isAuthPtr=nullptr);
	static const ld::Atom*	getClassMethods(ld::Internal& state, const ld::Atom* contentAtom, bool* isAuthPtr=nullptr);
	static const ld::Atom*	getProtocols(ld::Internal& state, const ld::Atom* contentAtom);
	static const ld::Atom*	getInstanceProperties(ld::Internal& state, const ld::Atom* contentAtom);
	static const ld::Atom*	getClassProperties(ld::Internal& state, const ld::Atom* contentAtom);
	static bool				usesRelMethodLists(ld::Internal& state, const ld::Atom* contentAtom);
	// Setters
	static const ld::Atom*	setName(ld::Internal& state, const ld::Atom* categoryAtom,
									const ld::Atom* categoryNameAtom, std::set<const ld::Atom*>& deadAtoms);
	static void				setInstanceMethods(ld::Internal& state, const ld::Atom*& categoryAtom, const ld::Atom* methodListAtom,
												bool usesAuthPtrs, bool& categoryIsNowOverlay, std::set<const ld::Atom*>& deadAtoms);
	static void				setClassMethods(ld::Internal& state, const ld::Atom*& categoryAtom, const ld::Atom* methodListAtom,
												bool usesAuthPtrs, bool& categoryIsNowOverlay, std::set<const ld::Atom*>& deadAtoms);
	static void 			setInstanceProperties(ld::Internal& state, const ld::Atom*& categoryAtom, const ld::Atom* propertyListAtom,
												  bool& categoryIsNowOverlay, std::set<const ld::Atom*>& deadAtoms);
	static void				setClassProperties(ld::Internal& state, const ld::Atom*& categoryAtom, const ld::Atom* propertyListAtom,
											   bool& categoryIsNowOverlay, std::set<const ld::Atom*>& deadAtoms);
	static void				setProtocols(ld::Internal& state, const ld::Atom*& categoryAtom,
										 const ld::Atom* protocolListAtom, bool& categoryIsNowOverlay,
										 std::set<const ld::Atom*>& deadAtoms);
	static uint32_t         size() { return 6*sizeof(pint_t); }

	static bool				hasCategoryClassPropertiesField(const ld::Atom* categoryAtom);

private:	
	typedef typename A::P::uint_t			pint_t;

	friend class CategoryOverlayAtom<A>;

	struct Content {
		pint_t name;
		pint_t cls;
		pint_t instanceMethods;
		pint_t classMethods;
		pint_t protocols;
		pint_t instanceProperties;
		// Fields below this point are not always present on disk.
		pint_t classProperties;
	};
};

template <typename A>
class MethodList : public ObjCData<A> {
public:
	static uint32_t	count(ld::Internal& state, const ld::Atom* methodListAtom) {
		const uint32_t* methodListData = (uint32_t*)(methodListAtom->rawContentPointer());
		return A::P::E::get32(methodListData[1]); // method_list_t.count
	}

	static uint32_t	elementSize(ld::Internal& state, const ld::Atom* methodListAtom) {
		const uint32_t* methodListData = (uint32_t*)(methodListAtom->rawContentPointer());
		return (methodListData[0] & 0x7fffffff); // method_list_t.size
	}

	static bool usesRelativeMethodList(ld::Internal& state, const ld::Atom* methodListAtom) {
		const uint32_t* methodListData = (uint32_t*)(methodListAtom->rawContentPointer());
		return (methodListData[0] & 0x80000000); // method_list_t.size
	}
};


template <typename A>
const ld::Atom*	Category<A>::getName(ld::Internal& state, const ld::Atom* contentAtom)
{
	return GET_FIELD(state, contentAtom, name);
}

template <typename A>
const ld::Atom*	Category<A>::getClass(ld::Internal& state, const ld::Atom* contentAtom, uint64_t& addend)
{
	return ObjCData<A>::getPointerInContent(state, contentAtom, offsetof(Content, cls), &addend); // category_t.cls
}

template <typename A>
const ld::Atom*	Category<A>::getInstanceMethods(ld::Internal& state, const ld::Atom* contentAtom, bool* isAuthPtr)
{
	return GET_FIELD_AUTH(state, contentAtom, instanceMethods, isAuthPtr);
}

template <typename A>
const ld::Atom*	Category<A>::getClassMethods(ld::Internal& state, const ld::Atom* contentAtom, bool* isAuthPtr)
{
	return GET_FIELD_AUTH(state, contentAtom, classMethods, isAuthPtr);
}

template <typename A>
bool Category<A>::usesRelMethodLists(ld::Internal& state, const ld::Atom* categoryAtom)
{
	const ld::Atom*	instanceMethodList = Category<A>::getInstanceMethods(state, categoryAtom);
	if ( instanceMethodList != nullptr )
		return MethodList<A>::usesRelativeMethodList(state, instanceMethodList);
	const ld::Atom* classeMethodList = Category<A>::getClassMethods(state, categoryAtom);
	if ( classeMethodList != nullptr )
		return MethodList<A>::usesRelativeMethodList(state, classeMethodList);
	return false;
}


template <typename A>
const ld::Atom*	Category<A>::getProtocols(ld::Internal& state, const ld::Atom* contentAtom)
{
	return GET_FIELD(state, contentAtom, protocols);
}

template <typename A>
const ld::Atom*	Category<A>::getInstanceProperties(ld::Internal& state, const ld::Atom* contentAtom)
{
	return GET_FIELD(state, contentAtom, instanceProperties);
}

template <typename A>
const ld::Atom*	Category<A>::getClassProperties(ld::Internal& state, const ld::Atom* contentAtom)
{
	// Only specially-marked files have this field.
	if ( hasCategoryClassPropertiesField(contentAtom) )
		return GET_FIELD(state, contentAtom, classProperties);
	return NULL;
}

template <typename A>
bool Category<A>::hasCategoryClassPropertiesField(const ld::Atom* contentAtom)
{
	// Only specially-marked files have this field.
	if ( const ld::relocatable::File* objFile = dynamic_cast<const ld::relocatable::File*>(contentAtom->file()) ) {
		return objFile->objcHasCategoryClassPropertiesField();
	}
	return false;
}



template <typename A>
void Category<A>::setInstanceMethods(ld::Internal& state, const ld::Atom*& categoryAtom, const ld::Atom* methodListAtom, bool useAuthPtrs, bool& categoryIsNowOverlay, std::set<const ld::Atom*>& deadAtoms)
{
	// if the base class does not already have a method list, we need to create an overlay
	bool needAuthPtrToMethodList = useAuthPtrs && (strcmp(methodListAtom->section().sectionName(), "__objc_methlist") == 0);
	bool isAuthPtr;
	if ( (getInstanceMethods(state, categoryAtom, &isAuthPtr) == NULL) || (isAuthPtr != needAuthPtrToMethodList) ) {
		deadAtoms.insert(categoryAtom);
		CategoryOverlayAtom<A>* overlay = new CategoryOverlayAtom<A>(categoryAtom);
		overlay->addInstanceMethodListFixup(needAuthPtrToMethodList);
		// Add the overlay after the fixup so that addAtom() can see if we need to move it to __AUTH
		state.addAtom(*overlay);
		categoryAtom = overlay;
		categoryIsNowOverlay = true;
	}
	SET_FIELD(state, categoryAtom, instanceMethods, methodListAtom);
}

template <typename A>
void Category<A>::setClassMethods(ld::Internal& state, const ld::Atom*& categoryAtom, const ld::Atom* methodListAtom, bool useAuthPtrs, bool& categoryIsNowOverlay, std::set<const ld::Atom*>& deadAtoms)
{
	// if the base class does not already have a method list, we need to create an overlay
	bool needAuthPtrToMethodList = useAuthPtrs && (strcmp(methodListAtom->section().sectionName(), "__objc_methlist") == 0);
	bool isAuthPtr;
	if ( (getClassMethods(state, categoryAtom, &isAuthPtr) == NULL) || (isAuthPtr != needAuthPtrToMethodList) ) {
		if ( categoryIsNowOverlay ) {
			((CategoryOverlayAtom<A>*)(categoryAtom))->addClassMethodListFixup(needAuthPtrToMethodList);
		}
		else {
			deadAtoms.insert(categoryAtom);
			CategoryOverlayAtom<A>* overlay = new CategoryOverlayAtom<A>(categoryAtom);
			overlay->addClassMethodListFixup(needAuthPtrToMethodList);
			// Add the overlay after the fixup so that addAtom() can see if we need to move it to __AUTH
			state.addAtom(*overlay);
			categoryAtom = overlay;
			categoryIsNowOverlay = true;
		}
	}
	SET_FIELD(state, categoryAtom, classMethods, methodListAtom);
}

template <typename A>
void Category<A>::setProtocols(ld::Internal& state, const ld::Atom*& categoryAtom,
							   const ld::Atom* protocolListAtom, bool& categoryIsNowOverlay,
							   std::set<const ld::Atom*>& deadAtoms)
{
	// if the base category does not already have a protocol list, we need to create an overlay
	if ( getProtocols(state, categoryAtom) == NULL ) {
		if ( categoryIsNowOverlay ) {
			((CategoryOverlayAtom<A>*)(categoryAtom))->addProtocolListFixup();
		}
		else {
			deadAtoms.insert(categoryAtom);
			CategoryOverlayAtom<A>* overlay = new CategoryOverlayAtom<A>(categoryAtom);
			overlay->addProtocolListFixup();
			// Add the overlay after the fixup so that addAtom() can see if we need to move it to __AUTH
			state.addAtom(*overlay);
			categoryAtom = overlay;
			categoryIsNowOverlay = true;
		}
	}
	SET_FIELD(state, categoryAtom, protocols, protocolListAtom);
}

template <typename A>
void Category<A>::setInstanceProperties(ld::Internal& state, const ld::Atom*& categoryAtom, const ld::Atom* methodListAtom,
										bool& categoryIsNowOverlay, std::set<const ld::Atom*>& deadAtoms)
{
	// if the base category does not already have a property list, we need to create an overlay
	if ( getInstanceProperties(state, categoryAtom) == NULL ) {
		if ( categoryIsNowOverlay ) {
			((CategoryOverlayAtom<A>*)(categoryAtom))->addInstancePropertyListFixup();
		}
		else {
			deadAtoms.insert(categoryAtom);
			CategoryOverlayAtom<A>* overlay = new CategoryOverlayAtom<A>(categoryAtom);
			overlay->addInstancePropertyListFixup();
			// Add the overlay after the fixup so that addAtom() can see if we need to move it to __AUTH
			state.addAtom(*overlay);
			categoryAtom = overlay;
			categoryIsNowOverlay = true;
		}
	}
	SET_FIELD(state, categoryAtom, instanceProperties, methodListAtom);
}

template <typename A>
void Category<A>::setClassProperties(ld::Internal& state, const ld::Atom*& categoryAtom, const ld::Atom* methodListAtom,
									 bool& categoryIsNowOverlay, std::set<const ld::Atom*>& deadAtoms)
{
	// if the base category does not already have a property list, we need to create an overlay
	if ( getClassProperties(state, categoryAtom) == NULL ) {
		if ( categoryIsNowOverlay ) {
			((CategoryOverlayAtom<A>*)(categoryAtom))->addClassPropertyListFixup();
		}
		else {
			deadAtoms.insert(categoryAtom);
			CategoryOverlayAtom<A>* overlay = new CategoryOverlayAtom<A>(categoryAtom);
			overlay->addClassPropertyListFixup();
			// Add the overlay after the fixup so that addAtom() can see if we need to move it to __AUTH
			state.addAtom(*overlay);
			categoryAtom = overlay;
			categoryIsNowOverlay = true;
		}
	}
	SET_FIELD(state, categoryAtom, classProperties, methodListAtom);
}



template <typename A>
class ProtocolList : public ObjCData<A> {
public:
	static uint32_t	count(ld::Internal& state, const ld::Atom* protocolListAtom)  {
		pint_t* protocolListData = (pint_t*)(protocolListAtom->rawContentPointer());
		return A::P::getP(*protocolListData); // protocol_list_t.count
	}
private:
	typedef typename A::P::uint_t	pint_t;
};

template <typename A>
class PropertyList : public ObjCData<A> {
public:
	static uint32_t	count(ld::Internal& state, const ld::Atom* protocolListAtom)  {
		uint32_t* protocolListData = (uint32_t*)(protocolListAtom->rawContentPointer());
		return A::P::E::get32(protocolListData[1]); // property_list_t.count
	}
private:
	typedef typename A::P::uint_t	pint_t;
};



//
// Helper class for reading and updating existing ObjC class atoms from .o files
//
template <typename A>
class Class : public ObjCData<A> {
public:
	static const ld::Atom*	getMetaClass(ld::Internal& state, const ld::Atom* classAtom);
	static const ld::Atom*	getName(ld::Internal& state, const ld::Atom* classAtom);
	static const ld::Atom*	getInstanceMethodList(ld::Internal& state, const ld::Atom* classAtom, bool* isAuthPtr=nullptr);
	static const ld::Atom*	getInstanceProtocolList(ld::Internal& state, const ld::Atom* classAtom);
	static const ld::Atom*	getInstancePropertyList(ld::Internal& state, const ld::Atom* classAtom);
	static const ld::Atom*	getClassMethodList(ld::Internal& state, const ld::Atom* classAtom);
	static const ld::Atom*	getClassPropertyList(ld::Internal& state, const ld::Atom* classAtom);
	static bool				usesRelMethodLists(ld::Internal& state, const ld::Atom* classAtom);
	static void				setInstanceMethodList(ld::Internal& state, const ld::Atom* classAtom,
												const ld::Atom* methodListAtom, bool useAuthPtrs, std::set<const ld::Atom*>& deadAtoms);
	static void				setInstanceProtocolList(ld::Internal& state, const ld::Atom* classAtom,
												const ld::Atom* protocolListAtom, std::set<const ld::Atom*>& deadAtoms);
	static void        		setInstancePropertyList(ld::Internal& state, const ld::Atom* classAtom,
												const ld::Atom* propertyListAtom, std::set<const ld::Atom*>& deadAtoms);
	static void  			setClassMethodList(ld::Internal& state, const ld::Atom* classAtom,
												const ld::Atom* methodListAtom, bool useAuthPtrs, std::set<const ld::Atom*>& deadAtoms);
	static void				setClassProtocolList(ld::Internal& state, const ld::Atom* classAtom,
												const ld::Atom* protocolListAtom, std::set<const ld::Atom*>& deadAtoms);
	static void				setClassPropertyList(ld::Internal& state, const ld::Atom* classAtom,
												const ld::Atom* propertyListAtom, std::set<const ld::Atom*>& deadAtoms);
	static uint32_t         size() { return sizeof(Content); }

private:
	friend class ClassROOverlayAtom<A>;

	typedef typename A::P::uint_t			pint_t;

	static const ld::Atom*	getROData(ld::Internal& state, const ld::Atom* classAtom);

	struct Content {
		pint_t isa;
		pint_t superclass;
		pint_t method_cache;
		pint_t vtable;
		pint_t data;
	};

	struct ROContent {
		uint32_t flags;
		uint32_t instanceStart;
		// Note there is 4-bytes of alignment padding between instanceSize 
		// and ivarLayout on 64-bit archs, but no padding on 32-bit archs.
		// This union is a way to model that.
		union {
			uint32_t instanceSize;
			pint_t pad;
		} instanceSize;
		pint_t ivarLayout;
		pint_t name;
		pint_t baseMethods;
		pint_t baseProtocols;
		pint_t ivars;
		pint_t weakIvarLayout;
		pint_t baseProperties;
	};
};

template <typename A>
const ld::Atom*	Class<A>::getMetaClass(ld::Internal& state, const ld::Atom* classAtom)
{
    const ld::Atom* metaClassAtom = GET_FIELD(state, classAtom, isa);
	if ( metaClassAtom->section().type() == ld::Section::typeCode ) {
		// this is a swift class with extra header, skip over that
		metaClassAtom = ObjCData<A>::getPointerInContent(state, classAtom, offsetof(Content, isa)+2*sizeof(pint_t));
	}
    assert(metaClassAtom != NULL);
    return metaClassAtom;
}

template <typename A>
const ld::Atom*	Class<A>::getROData(ld::Internal& state, const ld::Atom* classAtom)
{
    const ld::Atom* classROAtom = GET_FIELD(state, classAtom, data);
    assert(classROAtom != NULL);
    if ( strcmp(classROAtom->name(), "__objc_empty_cache") == 0 ) {
		// this is a swift class with extra header, skip over that
		classROAtom = ObjCData<A>::getPointerInContent(state, classAtom, offsetof(Content, data)+2*sizeof(pint_t));
    }
    return classROAtom;
}

template <typename A>
const ld::Atom*	Class<A>::getName(ld::Internal& state, const ld::Atom* classAtom)
{
	return GET_RO_FIELD(state, classAtom, name);
}

template <typename A>
const ld::Atom*	Class<A>::getInstanceMethodList(ld::Internal& state, const ld::Atom* classAtom, bool* isAuthPtr)
{
	return GET_RO_FIELD_AUTH(state, classAtom, baseMethods, isAuthPtr);
}

template <typename A>
bool Class<A>::usesRelMethodLists(ld::Internal& state, const ld::Atom* classAtom)
{
	const ld::Atom*	instanceMethodList = Class<A>::getInstanceMethodList(state, classAtom);
	if ( instanceMethodList != nullptr )
		return MethodList<A>::usesRelativeMethodList(state, instanceMethodList);
	const ld::Atom* classeMethodList = Class<A>::getClassMethodList(state, classAtom);
	if ( classeMethodList != nullptr )
		return MethodList<A>::usesRelativeMethodList(state, classeMethodList);
	return false;
}


template <typename A>
const ld::Atom*	Class<A>::getInstanceProtocolList(ld::Internal& state, const ld::Atom* classAtom)
{
	return GET_RO_FIELD(state, classAtom, baseProtocols);
}

template <typename A>
const ld::Atom*	Class<A>::getInstancePropertyList(ld::Internal& state, const ld::Atom* classAtom)
{
	return GET_RO_FIELD(state, classAtom, baseProperties);
}

template <typename A>
const ld::Atom*	Class<A>::getClassMethodList(ld::Internal& state, const ld::Atom* classAtom)
{
	return Class<A>::getInstanceMethodList(state, getMetaClass(state, classAtom));
}

template <typename A>
const ld::Atom*	Class<A>::getClassPropertyList(ld::Internal& state, const ld::Atom* classAtom)
{
    return Class<A>::getInstancePropertyList(state, getMetaClass(state, classAtom));
}

template <typename A>
void Class<A>::setInstanceMethodList(ld::Internal& state, const ld::Atom* classAtom,
									 const ld::Atom* methodListAtom, bool useAuthPtrs, std::set<const ld::Atom*>& deadAtoms)
{
	// if the base class does not already have a method list, we need to create an overlay
	bool needAuthPtrToMethodList = useAuthPtrs && (strcmp(methodListAtom->section().sectionName(), "__objc_methlist") == 0);
	bool isAuthPtr;
	if ( (getInstanceMethodList(state, classAtom, &isAuthPtr) == NULL) || (isAuthPtr != needAuthPtrToMethodList) ) {
		const ld::Atom* oldROAtom = getROData(state, classAtom);
		deadAtoms.insert(oldROAtom);
		ClassROOverlayAtom<A>* overlay = new ClassROOverlayAtom<A>(state, oldROAtom);
		//fprintf(stderr, "replace class RO atom %p with %p for method list in class atom %s\n", oldROAtom, overlay, classAtom->name());
		overlay->addMethodListFixup(needAuthPtrToMethodList);
		// Add the overlay after the fixup so that addAtom() can see if we need to move it to __AUTH
		state.addAtom(*overlay);
		SET_FIELD(state, classAtom, data, overlay);
	}
	SET_RO_FIELD(state, classAtom, baseMethods, methodListAtom);
}

template <typename A>
void Class<A>::setInstanceProtocolList(ld::Internal& state, const ld::Atom* classAtom,
									const ld::Atom* protocolListAtom, std::set<const ld::Atom*>& deadAtoms)
{
	// if the base class does not already have a protocol list, we need to create an overlay
	if ( getInstanceProtocolList(state, classAtom) == NULL ) {
		const ld::Atom* oldROAtom = getROData(state, classAtom);
		deadAtoms.insert(oldROAtom);
		ClassROOverlayAtom<A>* overlay = new ClassROOverlayAtom<A>(state, oldROAtom);
		//fprintf(stderr, "replace class RO atom %p with %p for protocol list in class atom %s\n", oldROAtom, overlay, classAtom->name());
		overlay->addProtocolListFixup();
		// Add the overlay after the fixup so that addAtom() can see if we need to move it to __AUTH
		state.addAtom(*overlay);
		SET_FIELD(state, classAtom, data, overlay);
	}
	SET_RO_FIELD(state, classAtom, baseProtocols, protocolListAtom);
}

template <typename A>
void Class<A>::setClassProtocolList(ld::Internal& state, const ld::Atom* classAtom,
									const ld::Atom* protocolListAtom, std::set<const ld::Atom*>& deadAtoms)
{
	// meta class also points to same protocol list as class
	const ld::Atom* metaClassAtom = getMetaClass(state, classAtom);
	//fprintf(stderr, "setClassProtocolList(), classAtom=%p %s, metaClass=%p %s\n", classAtom, classAtom->name(), metaClassAtom, metaClassAtom->name());
	setInstanceProtocolList(state, metaClassAtom, protocolListAtom, deadAtoms);
}



template <typename A>
void Class<A>::setInstancePropertyList(ld::Internal& state, const ld::Atom* classAtom,
										const ld::Atom* propertyListAtom, std::set<const ld::Atom*>& deadAtoms)
{
	// if the base class does not already have a property list, we need to create an overlay
	if ( getInstancePropertyList(state, classAtom) == NULL ) {
		const ld::Atom* oldROAtom = getROData(state, classAtom);
		deadAtoms.insert(oldROAtom);
		ClassROOverlayAtom<A>* overlay = new ClassROOverlayAtom<A>(state, oldROAtom);
		//fprintf(stderr, "replace class RO atom %p with %p for property list in class atom %s\n", oldROAtom, overlay, classAtom->name());
		overlay->addPropertyListFixup();
		// Add the overlay after the fixup so that addAtom() can see if we need to move it to __AUTH
		state.addAtom(*overlay);
		SET_FIELD(state, classAtom, data, overlay);
	}
	SET_RO_FIELD(state, classAtom, baseProperties, propertyListAtom);
}

template <typename A>
void Class<A>::setClassMethodList(ld::Internal& state, const ld::Atom* classAtom,
											const ld::Atom* methodListAtom, bool useAuthPtrs, std::set<const ld::Atom*>& deadAtoms)
{
	// class methods is just instance methods of metaClass
	setInstanceMethodList(state, getMetaClass(state, classAtom), methodListAtom, useAuthPtrs, deadAtoms);
}

template <typename A>
void Class<A>::setClassPropertyList(ld::Internal& state, const ld::Atom* classAtom,
											const ld::Atom* propertyListAtom, std::set<const ld::Atom*>& deadAtoms)
{
	// class properties is just instance properties of metaClass
	setInstancePropertyList(state, getMetaClass(state, classAtom), propertyListAtom, deadAtoms);
}

#undef GET_FIELD
#undef SET_FIELD
#undef GET_RO_FIELD
#undef SET_RO_FIELD


template <typename P>
ld::Fixup::Kind pointerFixupKind();

template <> 
ld::Fixup::Kind pointerFixupKind<Pointer32<BigEndian>>() { 
	return ld::Fixup::kindStoreTargetAddressBigEndian32; 
}
template <> 
ld::Fixup::Kind pointerFixupKind<Pointer64<BigEndian>>() { 
	return ld::Fixup::kindStoreTargetAddressBigEndian64; 
}
template <> 
ld::Fixup::Kind pointerFixupKind<Pointer32<LittleEndian>>() { 
	return ld::Fixup::kindStoreTargetAddressLittleEndian32; 
}
template <> 
ld::Fixup::Kind pointerFixupKind<Pointer64<LittleEndian>>() { 
	return ld::Fixup::kindStoreTargetAddressLittleEndian64; 
}

template <typename A>
void ObjCOverlayAtom<A>::addFixupAtOffset(uint32_t offset, bool isAuthPtr)
{
	// remove any fixups from original atom at this location
	_fixups.erase(std::remove_if(_fixups.begin(), _fixups.end(), [offset](const ld::Fixup& f){return f.offsetInAtom == offset;}), _fixups.end());

	if ( isAuthPtr ) {
#if SUPPORT_ARCH_arm64e
		const ld::Atom* targetAtom = this; // temporary, real target set later in setPointerInContent()
		const ld::Fixup::AuthData methodListAuthData = { 0xC310, true, ld::Fixup::AuthData::ptrauth_key_asda };
		_fixups.push_back(ld::Fixup(offset, ld::Fixup::k1of2, ld::Fixup::kindSetAuthData, methodListAuthData));
		_fixups.push_back(ld::Fixup(offset, ld::Fixup::k2of2, ld::Fixup::kindStoreTargetAddressLittleEndianAuth64, targetAtom));
#endif
	}
	else {
		const ld::Atom* targetAtom = this; // temporary, real target set later in setPointerInContent()
		_fixups.push_back(ld::Fixup(offset, ld::Fixup::k1of1, pointerFixupKind<typename A::P>(), targetAtom));
	}
}


template <typename A>
void ClassROOverlayAtom<A>::addMethodListFixup(bool isAuthPtr)
{
	this->addFixupAtOffset(offsetof(typename Class<A>::ROContent, baseMethods), isAuthPtr);
}

template <typename A>
void ClassROOverlayAtom<A>::addProtocolListFixup()
{
	this->addFixupAtOffset(offsetof(typename Class<A>::ROContent, baseProtocols));
}

template <typename A>
void ClassROOverlayAtom<A>::addPropertyListFixup()
{
	this->addFixupAtOffset(offsetof(typename Class<A>::ROContent, baseProperties));
}

template <typename A>
void CategoryOverlayAtom<A>::addNameFixup()
{
	this->addFixupAtOffset(offsetof(typename Category<A>::Content, name));
}

template <typename A>
void CategoryOverlayAtom<A>::addInstanceMethodListFixup(bool isAuthPtr)
{
	this->addFixupAtOffset(offsetof(typename Category<A>::Content, instanceMethods), isAuthPtr);
}

template <typename A>
void CategoryOverlayAtom<A>::addClassMethodListFixup(bool isAuthPtr)
{
	this->addFixupAtOffset(offsetof(typename Category<A>::Content, classMethods), isAuthPtr);
}

template <typename A>
void CategoryOverlayAtom<A>::addProtocolListFixup()
{
	this->addFixupAtOffset(offsetof(typename Category<A>::Content, protocols));
}

template <typename A>
void CategoryOverlayAtom<A>::addInstancePropertyListFixup()
{
	this->addFixupAtOffset(offsetof(typename Category<A>::Content, instanceProperties));
}

template <typename A>
void CategoryOverlayAtom<A>::addClassPropertyListFixup()
{
	this->addFixupAtOffset(offsetof(typename Category<A>::Content, classProperties));
}


//
// Encapsulates merging of ObjC categories
//
template <typename A>
class OptimizeCategories {
public:
	static void				doit(const Options& opts, ld::Internal& state, bool haveCategoriesWithoutClassPropertyStorage);
	static bool				hasName(ld::Internal& state, const std::vector<const ld::Atom*>* categories);
	static bool				hasInstanceMethods(ld::Internal& state, const std::vector<const ld::Atom*>* categories);
	static bool				hasClassMethods(ld::Internal& state, const std::vector<const ld::Atom*>* categories);
	static bool				hasProtocols(ld::Internal& state, const std::vector<const ld::Atom*>* categories);
	static bool				hasInstanceProperties(ld::Internal& state, const std::vector<const ld::Atom*>* categories);
	static bool				hasClassProperties(ld::Internal& state, const std::vector<const ld::Atom*>* categories);
	
	static unsigned int		class_ro_baseMethods_offset();
private:
	typedef typename A::P::uint_t			pint_t;

};


template <typename A>
bool OptimizeCategories<A>::hasName(ld::Internal& state, const std::vector<const ld::Atom*>* categories)
{
	for (std::vector<const ld::Atom*>::const_iterator it=categories->begin(); it != categories->end(); ++it) {
		const ld::Atom* categoryAtom = *it;
		const ld::Atom* name = Category<A>::getName(state, categoryAtom);
		if ( name != NULL )
			return true;
	}
	return false;
}


template <typename A>
bool OptimizeCategories<A>::hasInstanceMethods(ld::Internal& state, const std::vector<const ld::Atom*>* categories)
{
	if ( categories == nullptr )
		return false;
	for (std::vector<const ld::Atom*>::const_iterator it=categories->begin(); it != categories->end(); ++it) {
		const ld::Atom* categoryAtom = *it;
		const ld::Atom* methodList = Category<A>::getInstanceMethods(state, categoryAtom);
		if ( methodList != NULL ) {
			if ( MethodList<A>::count(state, methodList) > 0 )
				return true;
		}
	}
	return false;
}


template <typename A>
bool OptimizeCategories<A>::hasClassMethods(ld::Internal& state, const std::vector<const ld::Atom*>* categories)
{
	if ( categories == nullptr )
		return false;
	for (std::vector<const ld::Atom*>::const_iterator it=categories->begin(); it != categories->end(); ++it) {
		const ld::Atom* categoryAtom = *it;
		const ld::Atom* methodList = Category<A>::getClassMethods(state, categoryAtom);
		if ( methodList != NULL ) {
			if ( MethodList<A>::count(state, methodList) > 0 )
				return true;
		}
	}
	return false;
}

template <typename A>
bool OptimizeCategories<A>::hasProtocols(ld::Internal& state, const std::vector<const ld::Atom*>* categories)
{
	if ( categories == nullptr )
		return false;
	for (std::vector<const ld::Atom*>::const_iterator it=categories->begin(); it != categories->end(); ++it) {
		const ld::Atom* categoryAtom = *it;
		const ld::Atom* protocolListAtom = Category<A>::getProtocols(state, categoryAtom);
		if ( protocolListAtom != NULL ) {
			if ( ProtocolList<A>::count(state, protocolListAtom) > 0 ) {	
				return true;
			}
		}
	}
	return false;
}


template <typename A>
bool OptimizeCategories<A>::hasInstanceProperties(ld::Internal& state, const std::vector<const ld::Atom*>* categories)
{
	if ( categories == nullptr )
		return false;
	for (std::vector<const ld::Atom*>::const_iterator it=categories->begin(); it != categories->end(); ++it) {
		const ld::Atom* categoryAtom = *it;
		const ld::Atom* propertyListAtom = Category<A>::getInstanceProperties(state, categoryAtom);
		if ( propertyListAtom != NULL ) {
			if ( PropertyList<A>::count(state, propertyListAtom) > 0 )
				return true;
		}
	}
	return false;
}


template <typename A>
bool OptimizeCategories<A>::hasClassProperties(ld::Internal& state, const std::vector<const ld::Atom*>* categories)
{
	if ( categories == nullptr )
		return false;
	for (std::vector<const ld::Atom*>::const_iterator it=categories->begin(); it != categories->end(); ++it) {
		const ld::Atom* categoryAtom = *it;
		const ld::Atom* propertyListAtom = Category<A>::getClassProperties(state, categoryAtom);
		if ( propertyListAtom != NULL ) {
			if ( PropertyList<A>::count(state, propertyListAtom) > 0 )
				return true;
		}
	}
	return false;
}

static const ld::Atom* getFollowOnAtom(const ld::Internal& state, const ld::Atom* atom)
{
	for (ld::Fixup::iterator fit = atom->fixupsBegin(); fit != atom->fixupsEnd(); ++fit) {
		if ( fit->kind == ld::Fixup::kindNoneFollowOn ) {
			switch (fit->binding) {
				case ld::Fixup::bindingDirectlyBound:
					return fit->u.target;
				case ld::Fixup::bindingsIndirectlyBound:
					// <rdar://96314882> Handle indirectly bound symbol aliases
					return state.indirectBindingTable[fit->u.bindingIndex];
				default:
					throwf("Unexpected binding kind in objc atom: %s", atom->name());
			}
		}
	}
	return nullptr;
}

static const ld::Atom* fixClassAliases(const ld::Internal& state, const ld::Atom* classAtom, uint64_t& addend)
{
	if ( (addend != 0) && (classAtom->size() == addend) ) {
		// have pointer to swift class prefix on objc class
		const ld::Atom* nextAtom = getFollowOnAtom(state, classAtom);
		assert(nextAtom != nullptr);
		addend = 0;
		return nextAtom;
	}

	// not an alias (zero size) atom
	if ( (classAtom->size() != 0) || (classAtom->definition() == ld::Atom::definitionProxy) )
		return classAtom;

	// get real atom (not alias)
	const ld::Atom* nextAtom = getFollowOnAtom(state, classAtom);
	assert(nextAtom != nullptr);
	return nextAtom;
}

//
// Helper for std::remove_if
//
class OptimizedAway {
public:
	OptimizedAway(const std::set<const ld::Atom*>& oa) : _dead(oa) {}
	bool operator()(const ld::Atom* atom) const {
		return ( _dead.count(atom) != 0 );
	}
private:
	const std::set<const ld::Atom*>& _dead;
};

struct AtomSorter
{
	bool operator()(const Atom* left, const Atom* right)
	{
		// sort by file ordinal, then object address, then zero size, then symbol name
		// only file based atoms are supported (file() != NULL)
		if (left==right) return false;
		const File *leftf = left->file();
		const File *rightf = right->file();

		if (leftf == rightf) {
			if (left->objectAddress() != right->objectAddress()) {
				return left->objectAddress() < right->objectAddress();
			} else {
				// for atoms in the same file with the same address, zero sized
				// atoms must sort before nonzero sized atoms
				if ((left->size() == 0 && right->size() > 0) || (left->size() > 0 && right->size() == 0))
					return left->size() < right->size();
				return (strcmp(left->name(), right->name()) < 0);
			}
		}
		// <rdar://problem/51479025> don't crash if objc atom does not have an owning file, just sort those to end
		if ( leftf == nullptr )
			return false;
		if ( rightf == nullptr )
			return true;
		return  (leftf->ordinal() < rightf->ordinal());
	}
};

struct AtomByContentSorter
{

	bool operator()(const ld::Atom* left, const ld::Atom* right)
	{
		std::string_view lContent((char*)left->rawContentPointer(), left->size());
		std::string_view rContent((char*)right->rawContentPointer(), right->size());

		return (lContent < rContent);
	}
};

template <typename A>
static void optimizeCategories(const std::vector<const ld::Atom*>& categories,
							   ld::Internal& state,
							   const char* onClassName,
							   const typename MethodListAtom<A>::ListFormat methodListFormat,
							   NameToAtom& selectorNameToSlot,
							   std::set<const ld::Atom*>& deadAtoms,
							   std::map<const ld::Atom*, const ld::Atom*>& categoryToListElement,
							   std::map<const ld::Atom*, const ld::Atom*>& categoryToNlListElement,
							   bool usesAuthPtrs,
							   bool log) {

	// get category info
	// FIXME:  This merges categories even if -no_objc_category_merging is used.
	const ld::Atom* categoryAtom = categories.front();
	if (log) {
		const ld::Atom* categoryNameAtom = Category<A>::getName(state, categoryAtom);
		const char* catName = (char*)categoryNameAtom->rawContentPointer();
		fprintf(stderr, "updating method lists in category '%s' on '%s'\n", catName, onClassName);
		for (unsigned i = 1; i != categories.size(); ++i) {
			const ld::Atom* categoryNameAtom = Category<A>::getName(state, categories[i]);
			const char* catName = (char*)categoryNameAtom->rawContentPointer();
			fprintf(stderr, "  attaching method lists in category '%s'\n", catName);
		}
	}
	bool categoryIsNowOverlay = false;
	// if category has instance methods, replace method list format
	if ( OptimizeCategories<A>::hasInstanceMethods(state, &categories) ) {
		const ld::Atom* newInstanceMethodListAtom = new MethodListAtom<A>(state, nullptr, methodListFormat, MethodListAtom<A>::categoryMethodList,
																		  onClassName, false, &categories, selectorNameToSlot, deadAtoms);
		if ( const ld::Atom* methodListAtom = Category<A>::getInstanceMethods(state, categoryAtom) ) {
			deadAtoms.insert(methodListAtom);
		}
		Category<A>::setInstanceMethods(state, categoryAtom, newInstanceMethodListAtom, usesAuthPtrs, categoryIsNowOverlay, deadAtoms);
	}
	// if category has class methods, replace method list format
	if ( OptimizeCategories<A>::hasClassMethods(state, &categories) ) {
		const ld::Atom* newClassMethodListAtom = new MethodListAtom<A>(state, nullptr, methodListFormat, MethodListAtom<A>::categoryMethodList,
																	   onClassName, true, &categories, selectorNameToSlot, deadAtoms);
		if ( const ld::Atom* methodListAtom = Category<A>::getClassMethods(state, categoryAtom) ) {
			deadAtoms.insert(methodListAtom);
		}
		Category<A>::setClassMethods(state, categoryAtom, newClassMethodListAtom, usesAuthPtrs, categoryIsNowOverlay, deadAtoms);
	}
	// if any category adds protocols, generate new merged protocol list, and replace
	if ( OptimizeCategories<A>::hasProtocols(state, &categories) ) {
		const ProtocolListAtom<A>* newProtocolListAtom = new ProtocolListAtom<A>(state, nullptr, onClassName, &categories, deadAtoms);
		if ( const ld::Atom* protocolAtom = Category<A>::getProtocols(state, categoryAtom) ) {
			deadAtoms.insert(protocolAtom);
		}
		Category<A>::setProtocols(state, categoryAtom, newProtocolListAtom, categoryIsNowOverlay, deadAtoms);
	}
	// if any category adds instance properties, generate new merged property list, and replace
	if ( OptimizeCategories<A>::hasInstanceProperties(state, &categories) ) {
		const ld::Atom* newPropertyListAtom = new PropertyListAtom<A>(state, nullptr, &categories, deadAtoms, PropertyListAtom<A>::PropertyKind::InstanceProperties);
		if ( const ld::Atom* propertyListAtom = Category<A>::getInstanceProperties(state, categoryAtom) ) {
			deadAtoms.insert(propertyListAtom);
		}
		Category<A>::setInstanceProperties(state, categoryAtom, newPropertyListAtom, categoryIsNowOverlay, deadAtoms);
	}
	// if any category adds class properties, generate new merged property list, and replace
	if ( OptimizeCategories<A>::hasClassProperties(state, &categories) ) {
		const ld::Atom* newPropertyListAtom = new PropertyListAtom<A>(state, nullptr, &categories, deadAtoms, PropertyListAtom<A>::PropertyKind::ClassProperties);
		if ( const ld::Atom* propertyListAtom = Category<A>::getClassProperties(state, categoryAtom) ) {
			deadAtoms.insert(propertyListAtom);
		}
		Category<A>::setClassProperties(state, categoryAtom, newPropertyListAtom, categoryIsNowOverlay, deadAtoms);
	}

	// delete categories now incorporated into base class
	for (unsigned i = 1; i != categories.size(); ++i) {
		const ld::Atom* categoryAtom = categories[i];
		assert(!categoryToNlListElement.count(categoryAtom));
		deadAtoms.insert(categoryToListElement[categoryAtom]);
		deadAtoms.insert(categoryAtom);
	}
	if ( categoryIsNowOverlay ) {
		// switch list element to use new category atom
		const ld::Atom* originalCategoryAtom = categories.front();
		const ld::Atom* listElement = categoryToListElement[originalCategoryAtom];
		ld::Fixup::iterator fit = listElement->fixupsBegin();
		assert(fit->binding == ld::Fixup::bindingDirectlyBound);
		assert(fit->u.target == originalCategoryAtom);
		fit->u.target = categoryAtom;
		// if here is a non-lazy list, switch that too
		auto pos = categoryToNlListElement.find(originalCategoryAtom);
		if ( pos != categoryToNlListElement.end() ) {
			const ld::Atom* nlListElement = pos->second;
			ld::Fixup::iterator fit = nlListElement->fixupsBegin();
			assert(fit->binding == ld::Fixup::bindingDirectlyBound);
			assert(fit->u.target == originalCategoryAtom);
			fit->u.target = categoryAtom;
		}
	}
}

// Walk all fixups which point to the eligible classes/aliases.  Classes/aliases are only
// eligible for patching is all fixups are pointers, not code such as adrp/add.
static void removeIneligiblePatching(ld::Internal& state,
									std::unordered_set<const ld::Atom*>& eligibleObjects,
									bool warnOnNonInterposing)
{
	for (ld::Internal::FinalSection* sect : state.sections) {
		for (const ld::Atom* atom : sect->atoms) {
			const ld::Atom* target = NULL;
			const ld::Atom* minusTarget = NULL;
			ld::Fixup*		fixupWithStore = NULL;

			for (ld::Fixup::iterator fit = atom->fixupsBegin(), end = atom->fixupsEnd(); fit != end; ++fit) {
				if ( fit->firstInCluster() ) {
					target = NULL;
					minusTarget = NULL;
					fixupWithStore = NULL;
				}

				if ( fit->setsTarget(false) ) {
					switch ( fit->binding ) {
						case ld::Fixup::bindingNone:
						case ld::Fixup::bindingByNameUnbound:
							break;
						case ld::Fixup::bindingByContentBound:
						case ld::Fixup::bindingDirectlyBound:
							target = fit->u.target;
							break;
						case ld::Fixup::bindingsIndirectlyBound:
							target = state.indirectBindingTable[fit->u.bindingIndex];
							break;
					}
					assert(target != NULL);
				}

				if ( fit->kind == ld::Fixup::kindSubtractTargetAddress ) {
					switch ( fit->binding ) {
						case ld::Fixup::bindingNone:
						case ld::Fixup::bindingByNameUnbound:
							break;
						case ld::Fixup::bindingByContentBound:
						case ld::Fixup::bindingDirectlyBound:
							minusTarget = fit->u.target;
							break;
						case ld::Fixup::bindingsIndirectlyBound:
							minusTarget = state.indirectBindingTable[fit->u.bindingIndex];
							break;
					}
					assert(minusTarget != NULL);
				}

				if ( fit->isStore() )
					fixupWithStore = fit;

				if ( fit->lastInCluster() ) {
					if ( (minusTarget != NULL) && eligibleObjects.count(minusTarget) ) {
						// ... - &CLASS_$_Foo.  We can't support this
						eligibleObjects.erase(minusTarget);
						warning("'%s' is ineligible for dyld patching due to subtract fixup in '%s' from '%s'",
								minusTarget->name(), atom->name(), atom->safeFilePath());
					} else if ( (target != NULL) && eligibleObjects.count(target) ) {
						if ( fixupWithStore != NULL ) {
							bool supportedFixup = false;
							switch ( fixupWithStore->kind ) {
								case ld::Fixup::kindStoreTargetAddressLittleEndian32:
								case ld::Fixup::kindStoreTargetAddressLittleEndian64:
#if SUPPORT_ARCH_arm64e
								case ld::Fixup::kindStoreTargetAddressLittleEndianAuth64:
#endif
									supportedFixup = true;
									break;
								default:
									break;
							}

							if ( !supportedFixup ) {
								// Probably a reference to a class symbol in code, eg, adrp/add
								eligibleObjects.erase(target);
								if ( warnOnNonInterposing ) {
									warning("'%s' is ineligible for dyld patching due to "
											"non-interposable reference in '%s' from '%s'",
											target->name(), atom->name(), atom->safeFilePath());
								}
							}
						} else {
							// No fixup with store.  Be conservative and assume we can't handle this
							eligibleObjects.erase(target);
						}
					}
				}
			}
		}
	}
}

// dyld supports a more efficient method of patching objc classes in the shared cache.
// Instead of patching every use of a class such as CFString, dyld can patch the body of the shared cache CFString
// to point to the body of the on-disk CFString.
//
// Pointer equality is also requried.  There should only be a single definition of a given class.  As all shared cache
// pointers will be to the shared cache version of the class, references to the on-disk definition should be rewritten to
// point to the shared cache one.  In dyld this will be done with implicitly interposing the binds to the on-disk class,
// and means that we need rebases to be replaced with binds to self.
// A bind to self wll only succeed if the class is exported.  In the case of some CF constant classes, the class symbol
// is hidden, but there is an exported alias of the class.  We rewrite references from the hidden class to the exported
// alias to support this case.
// Finally, for a given class, and any aliases, the class can only be interposed if all references to the class/aliases are
// via pointers.  We can't patch direct references in code such as adrp/add in arm64
template <typename A>
void optimizeClassPatching(const Options& opts, ld::Internal& state, const std::set<const ld::Atom*>& classDefAtoms)
{
	// To support more efficient objc patching in the shared cache, objc classes should be -interposable
	if ( classDefAtoms.empty() )
		return;

	// Interposing will change the __objc_classlist entries to binds to self.  dyld only knows how to handle
	// that with chained fixups
	if ( !opts.sharedRegionEligible() || !opts.makeChainedFixups() )
		return;

	// Don't do this on old platforms
	if ( !opts.platforms().minOS(ld::version2022Fall) )
		return;

	// Work out which classes can be interposed.  All references to the classes must be via pointers
	std::unordered_set<const ld::Atom*> eligibleClasses;
	eligibleClasses.reserve(classDefAtoms.size() * 2);
	for (const ld::Atom* classAtom : classDefAtoms) {
		eligibleClasses.insert(classAtom);
		eligibleClasses.insert(Class<A>::getMetaClass(state, classAtom));
	}

	// Map from unexported class, to an alias which is exported
	std::unordered_map<const ld::Atom*, const ld::Atom*> classToExportedAlias;
	// Map from alias to class
	std::unordered_map<const ld::Atom*, const ld::Atom*> aliasToClass;
	for (ld::Internal::FinalSection* sect : state.sections) {
	   for (const ld::Atom* atom : sect->atoms) {
		   if ( !atom->isAlias() )
			   continue;
		   if ( const ld::Atom* aliasOf = getFollowOnAtom(state, atom) ) {
			   // Only track aliases of classes
			   // FIXME: Do we need to handle aliases of aliases?
			   if ( !eligibleClasses.count(aliasOf) )
				   continue;

			   eligibleClasses.insert(atom);

			   // Some classes are hidden, but an alias is exported.  Note down that exported
			   // alias if there is one.
			   if ( (atom->scope() == Atom::scopeGlobal) && (aliasOf->scope() != Atom::scopeGlobal) ) {
				   classToExportedAlias.insert({ aliasOf, atom });
			   }

			   aliasToClass[atom] = aliasOf;
		   }
	   }
	}

	// Walk all fixups which point to the eligible classes/aliases.  Classes/aliases are only
	// eligible for patching is all fixups are pointers, not code such as adrp/add.
	removeIneligiblePatching(state, eligibleClasses, false);

	// If none of the classes are eliglble, then nothing left to do
	if ( eligibleClasses.empty() )
		return;

	// Remove the class if any aliases had ineligible fixups
	for (auto& aliasAndClass : aliasToClass) {
		const ld::Atom* aliasAtom = aliasAndClass.first;
		const ld::Atom* classAtom = aliasAndClass.second;
		if ( !eligibleClasses.count(aliasAtom) || !eligibleClasses.count(classAtom) ) {
			eligibleClasses.erase(classAtom);
			classToExportedAlias.erase(classAtom);
		}
	}

	// At runtime, the class/metaclass pair are either both patched, or neither are.
	for (const ld::Atom* classAtom : classDefAtoms) {
		const ld::Atom* metaclassAtom = Class<A>::getMetaClass(state, classAtom);
		if ( !eligibleClasses.count(classAtom) )
			eligibleClasses.erase(metaclassAtom);
		else if ( !eligibleClasses.count(metaclassAtom) )
			eligibleClasses.erase(classAtom);
	}

	// If none of the classes are eliglble, then nothing left to do.  We do this check
	// again in case the above loops removed the last eligible classes
	if ( eligibleClasses.empty() )
		return;

	// Mark any remaining exported eligible classes/aliases as -interposable
	for (const ld::Atom* classAtom : eligibleClasses) {
		if ( classAtom->scope() == Atom::scopeGlobal )
			opts.addInterpose(classAtom->name());
	}

	// If we have unexported classes, rewrite references to aliases which are exported
	if ( !classToExportedAlias.empty() ) {
		for (ld::Internal::FinalSection* sect : state.sections) {
			for (const ld::Atom* atom : sect->atoms) {
				for (ld::Fixup::iterator fit = atom->fixupsBegin(), end = atom->fixupsEnd(); fit != end; ++fit) {
					const ld::Atom* fixupTarget = NULL;
					switch ( fit->binding ) {
						case ld::Fixup::bindingsIndirectlyBound:
							fixupTarget = state.indirectBindingTable[fit->u.bindingIndex];
							break;
						case ld::Fixup::bindingDirectlyBound:
							fixupTarget = fit->u.target;
							break;
						default:
							break;
					}

					if ( fixupTarget == nullptr )
						continue;

					auto it = classToExportedAlias.find(fixupTarget);
					if ( (it != classToExportedAlias.end()) && (atom != it->second) ) {
						fit->binding	= ld::Fixup::bindingDirectlyBound;
						fit->u.target 	= it->second;
					}
				}
			}
		}
	}
}

// Returns true if this is a CF singleton object.  For now the only eligible
// one of these looks like: struct { void* isa; uintp64_t other }
static bool isCFSingleton(const ld::Atom* atom, uint32_t pointerSize,
						  bool usesAuthPtrs)
{
	if ( atom->size() != (2 * pointerSize) ) {
		warning("'%s' is unsupported size for dyld patching", atom->name());
		return false;
	}

	// The first field needs to look like an ISA.  In arm64e that also means it needs
	// to be signed with pointer authentication
	bool sawSignedISA = false;
	for (ld::Fixup::iterator fit = atom->fixupsBegin(); fit != atom->fixupsEnd(); ++fit) {
		if ( fit->offsetInAtom != 0 ) {
			warning("'%s' is unsupported fixup location for dyld patching", atom->name());
			return false;
		}

#if SUPPORT_ARCH_arm64e
		if ( usesAuthPtrs ) {
			if ( fit->kind == Fixup::kindSetAuthData ) {
				// Make sure this is signed to match ISAs
				if ( fit->u.authData.discriminator != 0x6AE1 ) {
					warning("'%s' has unsupported ISA fixup for dyld patching", atom->name());
					return false;
				}
				if ( !fit->u.authData.hasAddressDiversity ) {
					warning("'%s' has unsupported ISA fixup for dyld patching", atom->name());
					return false;
				}
				if ( fit->u.authData.key != Fixup::AuthData::ptrauth_key_asda ) {
					warning("'%s' has unsupported ISA fixup for dyld patching", atom->name());
					return false;
				}
				sawSignedISA = true;
				continue;
			}

			if ( fit->kind != Fixup::kindStoreTargetAddressLittleEndianAuth64 ) {
				warning("'%s' has unsupported ISA fixup for dyld patching", atom->name());
				return false;
			}
			continue;
		}
#endif
		if ( (pointerSize == 4) && (fit->kind != Fixup::kindStoreTargetAddressLittleEndian32) ) {
			warning("'%s' has unsupported ISA fixup for dyld patching", atom->name());
			return false;
		}
		if ( (pointerSize == 8) && (fit->kind != Fixup::kindStoreTargetAddressLittleEndian64) ) {
			warning("'%s' has unsupported ISA fixup for dyld patching", atom->name());
			return false;
		}
	}

	if ( usesAuthPtrs && !sawSignedISA ) {
		warning("'%s' has unsupported ISA fixup for dyld patching", atom->name());
		return false;
	}

	return true;
}

// CF constant classes such as @{}, @[], @(YES), @(NO) don't allocate memory.  Intead, CF exports
// singleton objects everyone can reference.  CF roots are expensive as all of these uses are patched.
// This pass works out when its possible to instead patch the singleton itself, not the uses of it
template <typename A>
void optimizeSingletonPatching(const Options& opts, ld::Internal& state)
{
	const uint32_t pointerSize = (opts.architecture() & CPU_ARCH_ABI64) ? 8 : 4;
#if SUPPORT_ARCH_arm64e
	const bool usesAuthPtrs = opts.supportsAuthenticatedPointers();
#else
	const bool usesAuthPtrs = false;
#endif

	if ( !opts.sharedRegionEligible() )
		return;

	// Don't do this on old platforms
	if ( !opts.platforms().minOS(ld::version2022Fall) )
		return;

	// Find anything in the singleton sections
	std::vector<const ld::Atom*> singletonAtoms;
	ld::Internal::FinalSection* section = nullptr;
	for (ld::Internal::FinalSection* sect : state.sections) {
		if (strncmp(sect->sectionName(), "__const_cfobj2", 16) != 0)
			continue;
		section = sect;
		for (const ld::Atom* atom : sect->atoms) {
			if ( isCFSingleton(atom, pointerSize, usesAuthPtrs) )
				singletonAtoms.push_back(atom);
		}
		break;
	}

	if ( singletonAtoms.empty() )
		return;

	// Make sure all reference are interposable
	std::unordered_set<const ld::Atom*> eligibleObjects;
	eligibleObjects.insert(singletonAtoms.begin(), singletonAtoms.end());
	removeIneligiblePatching(state, eligibleObjects, true);

	// dyld uses the reserved2 field to know if the section is valid.
	// This means all atoms in the section need to be interposable, or none can
	// be, as dyld can't distinguish good from bad
	if ( singletonAtoms.size() != eligibleObjects.size() )
		return;

	// Mark any remaining exported objects as -interposable
	for (const ld::Atom* atom : singletonAtoms) {
		if ( atom->scope() == Atom::scopeGlobal )
			opts.addInterpose(atom->name());
	}

	// HACK: This field ends up being reserved2
	section->indirectSymTabElementSize = 2 * pointerSize;
}

static const std::string_view selectorRefName(const ld::Atom* selRefAtom, const ld::Internal& state)
{
	ld::Fixup::iterator fit = selRefAtom->fixupsBegin();
	const ld::Atom* targetAtom = nullptr;
	switch ( fit->binding ) {
		case ld::Fixup::bindingByContentBound:
			targetAtom = fit->u.target;
			break;
		case ld::Fixup::bindingsIndirectlyBound:
            targetAtom = state.indirectBindingTable[fit->u.bindingIndex];
			break;
		case ld::Fixup::bindingDirectlyBound:
			targetAtom = fit->u.target;
			break;
		default:
			assert(0 && "unsupported reference to selector");
	}
	assert(targetAtom != nullptr);
	assert(targetAtom->contentType() == ld::Atom::typeCString);
	return std::string_view((char*)targetAtom->rawContentPointer(), targetAtom->size());
}


template <typename A>
void OptimizeCategories<A>::doit(const Options& opts, ld::Internal& state, bool haveCategoriesWithoutClassPropertyStorage)
{
	std::set<const ld::Atom*> deadAtoms;
	static const bool log = false;
#if SUPPORT_ARCH_arm64e
	const bool usesAuthPtrs = opts.supportsAuthenticatedPointers();
#else
	const bool usesAuthPtrs = false;
#endif
	const typename MethodListAtom<A>::ListFormat methodListFormat = opts.useObjCRelativeMethodLists()
																    ? MethodListAtom<A>::threeDeltas
																    : (usesAuthPtrs ? MethodListAtom<A>::threePointersAuthImpl : MethodListAtom<A>::threePointers);

	// find all category atoms and the class they apply to
	std::map<const ld::Atom*, const ld::Atom*> categoryToClassAtoms;
	std::map<const ld::Atom*, const ld::Atom*> categoryToListElement;
	std::map<const ld::Atom*, const ld::Atom*> categoryToNlListElement;
	for (ld::Internal::FinalSection* sect : state.sections) {
		if ( sect->type() == ld::Section::typeObjC2CategoryList ) {
			bool isNonLazyCategory = (strcmp(sect->sectionName(), "__objc_nlcatlist") == 0);
			for (const ld::Atom* catListAtom : sect->atoms) {
				assert(catListAtom->size() == sizeof(pint_t));
				const ld::Atom* categoryAtom = ObjCData<A>::getPointerInContent(state, catListAtom, 0, nullptr);
				for (ld::Fixup::iterator fit = catListAtom->fixupsBegin(); fit != catListAtom->fixupsEnd(); ++fit) {
					if ( (fit->offsetInAtom == 0) && (fit->kind == ld::Fixup::kindAddAddend) && (fit->u.addend == sizeof(pint_t)) ) {
						// catlist points to end of category atom, could be pointing to end of swift prefix, so get next atom which is objc stuff
						const ld::Atom* betterCatAtom = getFollowOnAtom(state, categoryAtom);
						assert(categoryAtom != nullptr);
						categoryAtom = betterCatAtom;
					}
				}
				uint64_t onClassAddend;
				const ld::Atom* onClassAtom = fixClassAliases(state, Category<A>::getClass(state, categoryAtom, onClassAddend), onClassAddend);
				categoryToClassAtoms[categoryAtom] = onClassAtom;
				if ( isNonLazyCategory ) {
					categoryToNlListElement[categoryAtom] = catListAtom;
				}
				else {
					categoryToListElement[categoryAtom] = catListAtom;
				}
				if ( log ) {
					const char* className;
					if ( onClassAtom->definition() == ld::Atom::definitionProxy ) {
						className = onClassAtom->name();
					}
					else {
						const ld::Atom* classNameAtom = Class<A>::getName(state, onClassAtom);
						className = (char*)classNameAtom->rawContentPointer();
					}
					const ld::Atom* categoryNameAtom = Category<A>::getName(state, categoryAtom);
					const char* catName = (char*)categoryNameAtom->rawContentPointer();
					if (log) fprintf(stderr, "category: %p %s on %s\n", categoryAtom, catName, className);
				}
			}
		}
	}

	// find all class definition atoms
	std::set<const ld::Atom*> classDefAtoms;
	std::set<const ld::Atom*> nlClassDefAtoms;
	std::map<const ld::Atom*, unsigned> classDefToPlusLoadCount;
	for (ld::Internal::FinalSection* sect : state.sections) {
		if ( strncmp(sect->segmentName(), "__DATA", 6) != 0 )
			continue;
		if ( sect->type() == ld::Section::typeObjC2ClassList ) {
			bool isNonLazyClass = (strcmp(sect->sectionName(), "__objc_nlclslist") == 0);
			for (const ld::Atom* classListAtom : sect->atoms) {
				assert(classListAtom->size() == sizeof(pint_t));
				uint64_t classAtomAddend;
				const ld::Atom* classAtom = ObjCData<A>::getPointerInContent(state, classListAtom, 0, &classAtomAddend, nullptr);
				// if class atom has an alias, switch to real class atom
				classAtom = fixClassAliases(state, classAtom, classAtomAddend);
				classDefAtoms.insert(classAtom);
				if ( isNonLazyClass ) {
					nlClassDefAtoms.insert(classAtom);
					classDefToPlusLoadCount[classAtom] = 1;
				}
				if ( log ) {
					const ld::Atom* classNameAtom = Class<A>::getName(state, classAtom);
					const char* className = (char*)classNameAtom->rawContentPointer();
					if (log) fprintf(stderr, "class:    %p %s\n", classAtom, className);
				}
			}
		}
	}

	// build map of all categories on each class
	typedef std::map<const ld::Atom*, std::vector<const ld::Atom*>> ClassToCategories;
	ClassToCategories classDefsToCategories;
	ClassToCategories externalClassToLazyCategories;
	ClassToCategories externalClassToNonLazyCategories;
	std::set<const ld::Atom*> externalClassAtoms;
	for (const auto& mapEntry : categoryToClassAtoms) {
		const ld::Atom* categoryAtom = mapEntry.first;
		const ld::Atom* onClassAtom  = mapEntry.second;
		if ( classDefAtoms.count(onClassAtom) != 0 ) {
			if ( categoryToNlListElement.count(categoryAtom) )
				classDefToPlusLoadCount[onClassAtom] += 1;
			classDefsToCategories[onClassAtom].push_back(categoryAtom);
		}
		else if ( !haveCategoriesWithoutClassPropertyStorage ) {
			if ( categoryToNlListElement.count(categoryAtom) ) {
				std::vector<const ld::Atom*>& categories = externalClassToNonLazyCategories[onClassAtom];
				if ( categories.empty() )
					externalClassAtoms.insert(onClassAtom);
				categories.push_back(categoryAtom);
			} else {
				std::vector<const ld::Atom*>& categories = externalClassToLazyCategories[onClassAtom];
				if ( categories.empty() )
					externalClassAtoms.insert(onClassAtom);
				categories.push_back(categoryAtom);
			}
		}
	}

	for (const auto& plusLoadEntry : classDefToPlusLoadCount) {
		if ( plusLoadEntry.second < 2 )
			continue;
		const ld::Atom* onClassAtom = plusLoadEntry.first;
		const ld::Atom* onClassNameAtom = Class<A>::getName(state, onClassAtom);
		const char* onClassName = "";
		if ( onClassNameAtom != nullptr )
			onClassName = (char*)onClassNameAtom->rawContentPointer();
		if (log) fprintf(stderr,"cannot optimize method list for class '%s' because there are %u +load methods\n", onClassName, plusLoadEntry.second);
		//warning("cannot optimize method list for class '%s' because there are %u +load methods", onClassName, plusLoadEntry.second);

		// We can't attach the categories to a class defined in this dylib, but we can still optimize the categories
		std::vector<const ld::Atom*>& categories = classDefsToCategories[onClassAtom];
		if ( !haveCategoriesWithoutClassPropertyStorage ) {
			for (const ld::Atom* categoryAtom : categories) {
				if ( categoryToNlListElement.count(categoryAtom) ) {
					std::vector<const ld::Atom*>& categories = externalClassToNonLazyCategories[onClassAtom];
					if ( categories.empty() )
						externalClassAtoms.insert(onClassAtom);
					categories.push_back(categoryAtom);
				} else {
					std::vector<const ld::Atom*>& categories = externalClassToLazyCategories[onClassAtom];
					if ( categories.empty() )
						externalClassAtoms.insert(onClassAtom);
					categories.push_back(categoryAtom);
				}
			}
		}
		categories.clear();
	}

	// build initial map of all selector references
	NameToAtom selectorNameToSlot;
	for (const ld::Internal::FinalSection* sect : state.sections ) {
		if ( (sect->type() == ld::Section::typeCStringPointer) && (strcmp(sect->sectionName(), "__objc_selrefs") == 0) ) {
			for (const ld::Atom* selRefAtom : sect->atoms) {
				assert(selRefAtom->size() == sizeof(pint_t));
				const ld::Atom* selAtom = ObjCData<A>::getPointerInContent(state, selRefAtom, 0, nullptr);
				const char* selName = (char*)selAtom->rawContentPointer();
				selectorNameToSlot[selName] = selRefAtom;
			}
		}
	}

	// Note: use fixClassAliases() for categories that point to alias of class
	// Note: don't apply categories to swift classes
	// Note: what to do about old categories that don't have storage space for class properties?


	// rebuild method list of classes defined here
	if ( !classDefAtoms.empty() ) {
		// we want builds to be reproducible, so need to process classes in same order every time
		std::vector<const ld::Atom*> orderedClasses;
		for (const ld::Atom* atom : classDefAtoms)
			orderedClasses.push_back(atom);
		std::sort(orderedClasses.begin(), orderedClasses.end(), AtomSorter());

		// now walk class in order and optimize method lists
		for (const ld::Atom* classAtom : orderedClasses) {
			const ld::Atom* classNameAtom = Class<A>::getName(state, classAtom);
			const char* className = "";
			if ( classNameAtom != nullptr )
				className = (char*)classNameAtom->rawContentPointer();
			if (log) fprintf(stderr,"updating method lists in class %s\n", className);
			std::vector<const ld::Atom*>* categories = nullptr;
			if ( classDefsToCategories.count(classAtom) && opts.objcCategoryMerging() ) {
				categories = &classDefsToCategories[classAtom];
				std::sort(categories->begin(), categories->end(), AtomSorter());
			}
			bool classUsesRelMethodList = Class<A>::usesRelMethodLists(state, classAtom);
			bool needToRewriteMethodList = ( classUsesRelMethodList != opts.useObjCRelativeMethodLists() );
			
			// if any category adds instance methods, generate new merged method list, and replace
			bool categoriesHaveInstanceMethods = OptimizeCategories<A>::hasInstanceMethods(state, categories);
			if ( needToRewriteMethodList || categoriesHaveInstanceMethods ) {
				const ld::Atom* baseInstanceMethodListAtom = Class<A>::getInstanceMethodList(state, classAtom);
				if ( (baseInstanceMethodListAtom != nullptr) || categoriesHaveInstanceMethods ) {
					const ld::Atom* newInstanceMethodListAtom = new MethodListAtom<A>(state, baseInstanceMethodListAtom, methodListFormat, MethodListAtom<A>::classMethodList,
																				  className, false, categories, selectorNameToSlot, deadAtoms);
					Class<A>::setInstanceMethodList(state, classAtom, newInstanceMethodListAtom, usesAuthPtrs, deadAtoms);
				}
			}
			// if any category adds class methods, generate new merged method list, and replace
			bool categoriesHaveClassMethods = OptimizeCategories<A>::hasClassMethods(state, categories);
			if ( needToRewriteMethodList || categoriesHaveClassMethods ) {
				const ld::Atom* baseClassMethodListAtom = Class<A>::getClassMethodList(state, classAtom);
				if ( (baseClassMethodListAtom != nullptr) || categoriesHaveClassMethods ) {
					const ld::Atom* newClassMethodListAtom = new MethodListAtom<A>(state, baseClassMethodListAtom, methodListFormat, MethodListAtom<A>::classMethodList,
																				className, true, categories, selectorNameToSlot, deadAtoms);
					Class<A>::setClassMethodList(state, classAtom, newClassMethodListAtom, usesAuthPtrs, deadAtoms);
				}
			}
			if ( categories == nullptr )
				continue;
			// if any category adds protocols, generate new merged protocol list, and replace
			if ( OptimizeCategories<A>::hasProtocols(state, categories) ) {
				const ld::Atom* baseProtocolListAtom = Class<A>::getInstanceProtocolList(state, classAtom);
				const ProtocolListAtom<A>* newProtocolListAtom = new ProtocolListAtom<A>(state, baseProtocolListAtom, className, categories, deadAtoms);
				Class<A>::setInstanceProtocolList(state, classAtom, newProtocolListAtom, deadAtoms);
				Class<A>::setClassProtocolList(state, classAtom, newProtocolListAtom, deadAtoms);
			}
			// if any category adds instance properties, generate new merged property list, and replace
			if ( OptimizeCategories<A>::hasInstanceProperties(state, categories) ) {
				const ld::Atom* basePropertyListAtom = Class<A>::getInstancePropertyList(state, classAtom);
				const ld::Atom* newPropertyListAtom = new PropertyListAtom<A>(state, basePropertyListAtom, categories, deadAtoms, PropertyListAtom<A>::PropertyKind::InstanceProperties);
				Class<A>::setInstancePropertyList(state, classAtom, newPropertyListAtom, deadAtoms);
			}
			// if any category adds class properties, generate new merged property list, and replace
			if ( OptimizeCategories<A>::hasClassProperties(state, categories) ) {
				const ld::Atom* basePropertyListAtom = Class<A>::getClassPropertyList(state, classAtom);
				const ld::Atom* newPropertyListAtom = new PropertyListAtom<A>(state, basePropertyListAtom, categories, deadAtoms, PropertyListAtom<A>::PropertyKind::ClassProperties);
				Class<A>::setClassPropertyList(state, classAtom, newPropertyListAtom, deadAtoms);
			}

			// delete categories now incorporated into base class
			if ( opts.objcCategoryMerging() ) {
				for (const ld::Atom* categoryAtom : *categories) {
					if ( categoryToNlListElement.count(categoryAtom) ) {
						const ld::Atom* newListElement = new NonLazyClassListAtom(state, classAtom, (sizeof(pint_t)==8));
						state.addAtom(*newListElement);
						deadAtoms.insert(categoryToNlListElement[categoryAtom]);
					}
					deadAtoms.insert(categoryToListElement[categoryAtom]);
					deadAtoms.insert(categoryAtom);
				}
			}
		}
	}


	// rebuild/merge method list of categories on external classes
	if ( !externalClassAtoms.empty() ) {
		// we want builds to be reproducible, so need to process classes in same order every time
		std::vector<const ld::Atom*> orderedClassAtoms;
		for (const ld::Atom* atom : externalClassAtoms)
			orderedClassAtoms.push_back(atom);
		std::sort(orderedClassAtoms.begin(), orderedClassAtoms.end(), AtomSorter());

		// now walk categories on external class and rewrite method list if needed
		for (const ld::Atom* externalClassAtom : orderedClassAtoms) {

			const char* onClassName = externalClassAtom->name();
			if ( strncmp(onClassName, "_OBJC_CLASS_$_", 14) == 0 )
				onClassName = &onClassName[14];

			// Lazy categories
			auto lazyCategoriesIt = externalClassToLazyCategories.find(externalClassAtom);
			if ( lazyCategoriesIt != externalClassToLazyCategories.end() ) {
				std::vector<const ld::Atom*>& categories = lazyCategoriesIt->second;
				std::sort(categories.begin(), categories.end(), AtomSorter());

				// optimizations are to change method lists and merge categories with each other
				bool shouldOptimizeCategories = false;
				if ( opts.objcCategoryMerging() && (categories.size() > 1) )
					shouldOptimizeCategories = true;
				if ( !shouldOptimizeCategories) {
					// Check if any of the method lists need to be optimized
					for (const ld::Atom* categoryAtom : categories) {
						const ld::Atom* categoryNameAtom = Category<A>::getName(state, categoryAtom);
						const char* catName = (char*)categoryNameAtom->rawContentPointer();
						if (log) fprintf(stderr, "category: %p %s on %s\n", categoryAtom, catName, onClassName);
						if ( Category<A>::usesRelMethodLists(state, categoryAtom) != opts.useObjCRelativeMethodLists() )
							shouldOptimizeCategories = true;
					}
				}
				if ( !shouldOptimizeCategories )
					continue;

				optimizeCategories<A>(categories, state, onClassName, methodListFormat,
									  selectorNameToSlot, deadAtoms, categoryToListElement, categoryToNlListElement,
									  usesAuthPtrs, log);
			}

			// Non-lazy categories
			auto nonLazyCategoriesIt = externalClassToNonLazyCategories.find(externalClassAtom);
			if ( nonLazyCategoriesIt != externalClassToNonLazyCategories.end() ) {
				std::vector<const ld::Atom*>& categories = nonLazyCategoriesIt->second;
				std::sort(categories.begin(), categories.end(), AtomSorter());

				// Non-lazy categories need to be updated one at a time as they are converted to relative method lists, but they
				// are not merged
				std::vector<const ld::Atom*> category;
				for (const ld::Atom* categoryAtom : categories) {
					if ( Category<A>::usesRelMethodLists(state, categoryAtom) != opts.useObjCRelativeMethodLists() ) {
						category.push_back(categoryAtom);
						optimizeCategories<A>(category, state, onClassName, methodListFormat,
											  selectorNameToSlot, deadAtoms, categoryToListElement, categoryToNlListElement,
											  usesAuthPtrs, log);
						category.clear();
					}
				}
			}
		}
	}

	// remove dead atoms
	for (ld::Internal::FinalSection* sect : state.sections ) {
		sect->atoms.erase(std::remove_if(sect->atoms.begin(), sect->atoms.end(), OptimizedAway(deadAtoms)), sect->atoms.end());
	}

	// sort __selrefs section
	for (ld::Internal::FinalSection* sect : state.sections ) {
		switch ( sect->type() ) {
			case ld::Section::typeCStringPointer:
				if ( strcmp(sect->sectionName(), "__objc_selrefs") == 0 ) {
					std::sort(sect->atoms.begin(), sect->atoms.end(), [&state](const ld::Atom* lhs, const ld::Atom* rhs) {
						return (selectorRefName(lhs, state) < selectorRefName(rhs, state));
					});
				}
				break;
			case ld::Section::typeNonStdCString:
				if ( strcmp(sect->sectionName(), "__objc_methname") == 0 ) {
					auto& atoms = sect->atoms;
					// Sort selector strings alphabetically for deterministic output.
					std::sort(atoms.begin(), atoms.end(), AtomByContentSorter{});

					// Now divide the atoms into two groups, one where the length of the
					// selectors is a power-of-2. Then intertwine the groups together
					// to reduce the number of adjacent selectors with a power-of-2 length.
					// This is to potentially reduce number of hash collisions in ObjC method
					// cache, where hashes are the lower bits of selector strings.
					const auto notPowerOf2Start = std::stable_partition(atoms.begin(), atoms.end(), [](const ld::Atom* atom) {
						uint64_t atomSize = atom->size();
						uint64_t isPowerOf2 = ((atomSize-1) & atomSize) == 0;
						return isPowerOf2;
					});

					auto powerOf2It = atoms.begin() + 1;
					auto notPowerOf2It = notPowerOf2Start + 1;

					while ( powerOf2It < notPowerOf2Start && notPowerOf2It < atoms.end() ) {
						std::swap(*notPowerOf2It, *powerOf2It);
						// Increment iterators by two, so the adjacent elements don't have
						// a power-of-2 size.
						powerOf2It += 2;
						notPowerOf2It += 2;
					}
				}
				break;
			default:
				break;
		}
	}

	optimizeClassPatching<A>(opts, state, classDefAtoms);

	optimizeSingletonPatching<A>(opts, state);
}


template <typename A>
CategoryNameAtom<A>::CategoryNameAtom(ld::Internal& state, const std::vector<const ld::Atom*>* categories)
  : ld::Atom(_s_section, ld::Atom::definitionRegular, ld::Atom::combineNever,
			ld::Atom::scopeLinkageUnit, ld::Atom::typeUnclassified,
			symbolTableNotIn, false, false, false, ld::Atom::Alignment(1)), _file(categories->front()->file())
{
	for (const ld::Atom* categoryAtom : *categories) {
		const char* name = (const char*)Category<A>::getName(state, categoryAtom)->rawContentPointer();
		if (!name)
			continue;
		if (_categoryName.empty()) {
			_categoryName = name;
		} else {
			_categoryName = _categoryName + "," + name;
		}
	}
	_syntheticAddress = _s_nextSyntheticAddress++;
}


template <typename A>
static void forEachMethod(ld::Internal& state, const ld::Atom* categoryMethodListAtom,
						  void (^callback)(const MethodEntryInfo& method))
{
	const uint32_t count 			= MethodList<A>::count(state, categoryMethodListAtom);
	const uint32_t entrySize 		= MethodList<A>::elementSize(state, categoryMethodListAtom);
	const uint32_t ptrSize 			= sizeof(typename A::P::uint_t);
	const bool     isProtocolList 	= (entrySize == 2*ptrSize);
	const bool     relMethodList 	= MethodList<A>::usesRelativeMethodList(state, categoryMethodListAtom);
	MethodEntryInfo methods[count];
	bzero(methods, sizeof(methods));
	if ( entrySize == 1)
		MethodList<A>::elementSize(state, categoryMethodListAtom);

	for (ld::Fixup::iterator fit=categoryMethodListAtom->fixupsBegin(); fit != categoryMethodListAtom->fixupsEnd(); ++fit) {
		if ( relMethodList ) {
			uint32_t entryOffset = (fit->offsetInAtom - 8) % (3*sizeof(int32_t));
			uint32_t methodIndex = (fit->offsetInAtom - 8) / (3*sizeof(int32_t));
			if ( (entryOffset == 0) && (fit->clusterSize == Fixup::k1of4) ) {
				// extract method name
				assert(fit->binding == ld::Fixup::bindingsIndirectlyBound && "malformed method list");
				const ld::Atom* selRefAtom = state.indirectBindingTable[fit->u.bindingIndex];
				assert(selRefAtom->contentType() == ld::Atom::typeUnclassified && "malformed method list");
				ld::Fixup::iterator selRefFit=selRefAtom->fixupsBegin();
				assert(selRefFit->binding == ld::Fixup::bindingsIndirectlyBound && "malformed selector ref");
				const ld::Atom* methodNameAtom = state.indirectBindingTable[selRefFit->u.bindingIndex];
				assert(methodNameAtom->contentType() == ld::Atom::typeCString && "malformed method list");
				methods[methodIndex].methodName         = (char*)(methodNameAtom->rawContentPointer());
				methods[methodIndex].selectorRefAtom    = selRefAtom;
				methods[methodIndex].selectorStringAtom = methodNameAtom;
			}
			else if ( (entryOffset == 4) && (fit->clusterSize == Fixup::k1of4) ) {
				assert(fit->binding == ld::Fixup::bindingsIndirectlyBound && "malformed category method list");
				methods[methodIndex].typeAtom = state.indirectBindingTable[fit->u.bindingIndex];
				assert(methods[methodIndex].typeAtom->contentType() == ld::Atom::typeCString && "malformed category method list");
			}
			else if ( (entryOffset == 8) && (fit->clusterSize == Fixup::k1of4) ) {
				assert(fit->binding == ld::Fixup::bindingDirectlyBound && "malformed method list");
				methods[methodIndex].implAtom = fit->u.target;
			}
		}
		else {
			uint32_t entryOffset = (fit->offsetInAtom - 8) % entrySize;
			uint32_t methodIndex = (fit->offsetInAtom - 8) / entrySize;
			if ( (entryOffset == 0) && (fit->clusterSize == Fixup::k1of1) ) {
				const ld::Atom* methodNameAtom = nullptr;
				switch (fit->binding) {
					case ld::Fixup::bindingsIndirectlyBound:
						methodNameAtom = state.indirectBindingTable[fit->u.bindingIndex];
						break;
					case ld::Fixup::bindingDirectlyBound:
						methodNameAtom = fit->u.target;
						break;
					default:
						methodNameAtom = nullptr;
						break;
				}
				assert(methodNameAtom && "malformed category method list");
				assert((methodNameAtom->contentType() == ld::Atom::typeCString) || (strcmp(methodNameAtom->section().segmentName(), "__TEXT") == 0));
				methods[methodIndex].methodName         = (char*)(methodNameAtom->rawContentPointer());
				methods[methodIndex].selectorRefAtom    = nullptr;
				methods[methodIndex].selectorStringAtom = methodNameAtom;
			}
			else if ( (entryOffset == ptrSize) && (fit->clusterSize == Fixup::k1of1) ) {
				switch (fit->binding) {
					case ld::Fixup::bindingsIndirectlyBound:
						methods[methodIndex].typeAtom = state.indirectBindingTable[fit->u.bindingIndex];
						break;
					case ld::Fixup::bindingDirectlyBound:
						methods[methodIndex].typeAtom = fit->u.target;
						break;
					default:
						assert(0 && "malformed category method list");
				}
				if ( (methods[methodIndex].typeAtom->contentType() != ld::Atom::typeCString) && (strcmp(methods[methodIndex].typeAtom->section().segmentName(), "__TEXT") != 0) )
					throwf("malformed category method list (%s), type string not in __TEXT in %s", categoryMethodListAtom->name(), categoryMethodListAtom->safeFilePath());
			}
			else if ( !isProtocolList && (entryOffset == 2*ptrSize) && (fit->clusterSize == Fixup::k1of1) ) {
				switch (fit->binding) {
					case ld::Fixup::bindingsIndirectlyBound:
						methods[methodIndex].implAtom = state.indirectBindingTable[fit->u.bindingIndex];
						break;
					case ld::Fixup::bindingDirectlyBound:
						methods[methodIndex].implAtom = fit->u.target;
						break;
					default:
						assert(0 && "malformed category method list");
				}
			}
#if SUPPORT_ARCH_arm64e
			else if ( !isProtocolList && (entryOffset == 2*ptrSize) && (fit->clusterSize == Fixup::k2of2) && (fit->kind == ld::Fixup::kindStoreTargetAddressLittleEndianAuth64) ) {
				switch (fit->binding) {
					case ld::Fixup::bindingsIndirectlyBound:
						methods[methodIndex].implAtom = state.indirectBindingTable[fit->u.bindingIndex];
						break;
					case ld::Fixup::bindingDirectlyBound:
						methods[methodIndex].implAtom = fit->u.target;
						break;
					default:
						assert(0 && "malformed method list");
				}
			}
#endif
		}
	}
	for (uint32_t i=0; i < count; ++i) {
		assert(methods[i].methodName && (methods[i].implAtom || isProtocolList) && (methods[i].selectorRefAtom || methods[i].selectorStringAtom));
		callback(methods[i]);
	}
}


template <typename A> 
MethodListAtom<A>::MethodListAtom(ld::Internal& state, const ld::Atom* baseMethodList, MethodListAtom<A>::ListFormat kind, MethodListAtom<A>::ListUse use,
								  const char* className, bool meta, const std::vector<const ld::Atom*>* categories, NameToAtom& selectorNameToSlot,
								  std::set<const ld::Atom*>& deadAtoms)
  : ld::Atom((kind == threeDeltas) ? _s_section_rel : _s_section_ptrs,
			ld::Atom::definitionRegular, ld::Atom::combineNever,
			ld::Atom::scopeTranslationUnit, ld::Atom::typeUnclassified,
			symbolTableIn, false, false, false, ld::Atom::Alignment(3)), _file(NULL), _methodCount(0), _listFormat(kind), _listUse(use)
{
	static const bool log = false;
	__block CStringSet baseMethodListMethodNames;
	__block CStringSet categoryMethodNames;
	__block std::vector<const ld::Atom*> reverseMethodLists;
	if ( baseMethodList != NULL ) {
		// if base class has method list, then associate new method list with file defining class
		_file = baseMethodList->file();
		reverseMethodLists.push_back(baseMethodList);
		deadAtoms.insert(baseMethodList);
		forEachMethod<A>(state, baseMethodList,^(const MethodEntryInfo& method) {
			baseMethodListMethodNames.insert(method.methodName);
			++_methodCount;
			if (log) fprintf(stderr, "base:     '%s'\n", method.methodName);
		});
	}
	std::string name;
	if ( className == NULL )
		className = "";
	std::string suffix = "";
	if ( (categories != nullptr) && !categories->empty() ) {
		suffix = "(";
		bool needSeparator = false;
		for (const ld::Atom* aCategory : *categories) {
			const ld::Atom* categoryNameAtom = Category<A>::getName(state, aCategory);
			const char* catName = (char*)categoryNameAtom->rawContentPointer();
			if ( needSeparator )
				suffix += "|";
			suffix += catName;
			needSeparator = true;
		}
		suffix += ")";
	}
	switch ( _listUse ) {
		case classMethodList:
		case categoryMethodList:
			if ( meta )
				name = std::string("__OBJC_$_CLASS_METHODS_")    + className + suffix;
			else
				name = std::string("__OBJC_$_INSTANCE_METHODS_") + className + suffix;
			break;
		case propertyMethodList:
			if ( meta )
				name = std::string("__OBJC_$_CLASS_PROP_LIST_")    + className + suffix;
			else
				name = std::string("__OBJC_$_PROP_LIST_") + className + suffix;
			break;
	}
	_name = strdup(name.c_str());

	if ( categories != nullptr ) {
		for (const ld::Atom* aCategory : *categories) {
			const ld::Atom* methodListAtom = nullptr;
			switch (use) {
				case classMethodList:
					if ( meta )
						methodListAtom = Category<A>::getClassMethods(state, aCategory);
					else
						methodListAtom = Category<A>::getInstanceMethods(state, aCategory);
					break;
				case categoryMethodList:
					if ( meta )
						methodListAtom = Category<A>::getClassMethods(state, aCategory);
					else
						methodListAtom = Category<A>::getInstanceMethods(state, aCategory);
					break;
				case propertyMethodList:
					if ( meta )
						methodListAtom = Category<A>::getClassProperties(state, aCategory);
					else
						methodListAtom = Category<A>::getInstanceProperties(state, aCategory);
					break;
			}
			if ( methodListAtom != nullptr ) {
				forEachMethod<A>(state, methodListAtom,^(const MethodEntryInfo& method) {
					++_methodCount;
					if ( baseMethodListMethodNames.count(method.methodName) != 0 ) {
						warning("method '%s%s' in category from %s overrides method from class in %s",
							(meta ? "+" : "-"), method.methodName,
							methodListAtom->safeFilePath(), baseMethodList->safeFilePath() );
					}
					if ( categoryMethodNames.count(method.methodName) != 0 ) {
						warning("method '%s%s' in category from %s conflicts with same method from another category",
							(meta ? "+" : "-"), method.methodName,
							methodListAtom->safeFilePath());
					}
					categoryMethodNames.insert(method.methodName);
					if (log) fprintf(stderr, "category: '%s'\n", method.methodName);
				});
				reverseMethodLists.push_back(methodListAtom);
				deadAtoms.insert(methodListAtom);
				// if base class did not have method list, associate new method list with file the defined category
				if ( _file == NULL )
					_file = methodListAtom->file();
			}
		}
	}
	if (log) fprintf(stderr, "total method count in merged list %u\n\n", _methodCount);
	
	// build fixups for merged method list (in reverse order to match what objc runtime would do)
	__block uint32_t methodIndex = 0;
	while ( !reverseMethodLists.empty() ) {
		const ld::Atom* methodList = reverseMethodLists.back();
		forEachMethod<A>(state, methodList,^(const MethodEntryInfo& method) {
			appendMethod(methodIndex, method, state, selectorNameToSlot);
			++methodIndex;
		});
		reverseMethodLists.pop_back();
	}
	
	// add new method list to final sections
	state.addAtom(*this);
}


#if SUPPORT_ARCH_arm64e
static const ld::Fixup::AuthData methodImplAuthData = { 0x0000, true, ld::Fixup::AuthData::ptrauth_key_asia };
#endif

template <typename A>
void MethodListAtom<A>::appendMethod(uint32_t methodIndex, const MethodEntryInfo& method, ld::Internal& state, NameToAtom& selectorNameToSlot)
{
	int32_t entryOffset;
	const ld::Atom* selectorRefAtom = method.selectorRefAtom;
	switch ( _listFormat ) {
		case threeDeltas:
			entryOffset = 8 + methodIndex*3*sizeof(int32_t);
			if ( selectorRefAtom == nullptr ) {
				// upgrading old method list, may need to create sel-ref
				assert(method.selectorStringAtom != nullptr);
				auto pos = selectorNameToSlot.find(method.methodName);
				if ( pos == selectorNameToSlot.end() ) {
					selectorRefAtom = new SelRefAtom(state, method.selectorStringAtom, (sizeof(pint_t)==8));
					selectorNameToSlot[method.methodName] = selectorRefAtom;
				}
				else {
					selectorRefAtom = pos->second;
				}
				
			}
			// 32-delta to selector ref
			_fixups.push_back(ld::Fixup(entryOffset+0,  ld::Fixup::k1of4, ld::Fixup::kindSetTargetAddress,      ld::Fixup::bindingByContentBound, selectorRefAtom));
			_fixups.push_back(ld::Fixup(entryOffset+0,  ld::Fixup::k2of4, ld::Fixup::kindAddAddend,             -entryOffset));
			_fixups.push_back(ld::Fixup(entryOffset+0,  ld::Fixup::k3of4, ld::Fixup::kindSubtractTargetAddress, ld::Fixup::bindingDirectlyBound, this));
			_fixups.push_back(ld::Fixup(entryOffset+0,  ld::Fixup::k4of4, ld::Fixup::kindStoreLittleEndian32));

			// 32-delta to type string
			if ( method.typeAtom != nullptr ) {
				_fixups.push_back(ld::Fixup(entryOffset+4,  ld::Fixup::k1of4, ld::Fixup::kindSetTargetAddress,      ld::Fixup::bindingByContentBound, method.typeAtom));
				_fixups.push_back(ld::Fixup(entryOffset+4,  ld::Fixup::k2of4, ld::Fixup::kindAddAddend,             -(entryOffset+4)));
				_fixups.push_back(ld::Fixup(entryOffset+4,  ld::Fixup::k3of4, ld::Fixup::kindSubtractTargetAddress, ld::Fixup::bindingDirectlyBound, this));
				_fixups.push_back(ld::Fixup(entryOffset+4,  ld::Fixup::k4of4, ld::Fixup::kindStoreLittleEndian32));
			}

			// 32-delta to impl
			_fixups.push_back(ld::Fixup(entryOffset+8,  ld::Fixup::k1of4, ld::Fixup::kindSetTargetAddress,      ld::Fixup::bindingDirectlyBound, method.implAtom));
			_fixups.push_back(ld::Fixup(entryOffset+8,  ld::Fixup::k2of4, ld::Fixup::kindAddAddend,             -(entryOffset+8)));
			_fixups.push_back(ld::Fixup(entryOffset+8,  ld::Fixup::k3of4, ld::Fixup::kindSubtractTargetAddress, ld::Fixup::bindingDirectlyBound, this));
			_fixups.push_back(ld::Fixup(entryOffset+8,  ld::Fixup::k4of4, ld::Fixup::kindStoreLittleEndian32));
			break;
			
		case threePointers:
			entryOffset = 8 + methodIndex*3*sizeof(pint_t);
			_fixups.push_back(ld::Fixup(entryOffset+0, 				  ld::Fixup::k1of1, pointerFixupKind<typename A::P>(), ld::Fixup::bindingByContentBound, method.selectorStringAtom));
			// method types are optional
			if ( method.typeAtom != nullptr )
				_fixups.push_back(ld::Fixup(entryOffset+sizeof(pint_t),   ld::Fixup::k1of1, pointerFixupKind<typename A::P>(), ld::Fixup::bindingByContentBound, method.typeAtom));
			// protocol method lists have no impl pointer
			if ( method.implAtom != nullptr )
				_fixups.push_back(ld::Fixup(entryOffset+sizeof(pint_t)*2, ld::Fixup::k1of1, pointerFixupKind<typename A::P>(), ld::Fixup::bindingDirectlyBound,  method.implAtom));
			break;
		
		case threePointersAuthImpl:
			entryOffset = 8 + methodIndex*3*sizeof(pint_t);
#if SUPPORT_ARCH_arm64e
			_fixups.push_back(ld::Fixup(entryOffset+0, 				  ld::Fixup::k1of1, pointerFixupKind<typename A::P>(), ld::Fixup::bindingByContentBound, method.selectorStringAtom));
			_fixups.push_back(ld::Fixup(entryOffset+sizeof(pint_t),   ld::Fixup::k1of1, pointerFixupKind<typename A::P>(), ld::Fixup::bindingByContentBound, method.typeAtom));
			_fixups.push_back(ld::Fixup(entryOffset+sizeof(pint_t)*2, ld::Fixup::k1of2, ld::Fixup::kindSetAuthData, methodImplAuthData));
			_fixups.push_back(ld::Fixup(entryOffset+sizeof(pint_t)*2, ld::Fixup::k2of2, ld::Fixup::kindStoreTargetAddressLittleEndianAuth64,  method.implAtom));
#endif
			break;

		case twoPointers:
			entryOffset = 8 + methodIndex*2*sizeof(pint_t);
			_fixups.push_back(ld::Fixup(entryOffset+0, 				  ld::Fixup::k1of1, pointerFixupKind<typename A::P>(), ld::Fixup::bindingByContentBound, method.selectorStringAtom));
			_fixups.push_back(ld::Fixup(entryOffset+sizeof(pint_t),   ld::Fixup::k1of1, pointerFixupKind<typename A::P>(), ld::Fixup::bindingByContentBound, method.typeAtom));
			break;
	}
}

template <typename A>
uint64_t MethodListAtom<A>::size() const
{
	switch ( _listFormat ) {
		case threeDeltas:
			return 8 + _methodCount*3*sizeof(int32_t);
		case threePointers:
		case threePointersAuthImpl:
			return 8 + _methodCount*3*sizeof(pint_t);
		case twoPointers:
			return 8 + _methodCount*2*sizeof(pint_t);
	}
}

template <typename A>
void MethodListAtom<A>::copyRawContent(uint8_t buffer[]) const
{
	bzero(buffer, size());
	// set count
	A::P::E::set32(*((uint32_t*)(&buffer[4])), _methodCount);
	// set element size (high bit means entries are 32-bit relative pointers)
	switch ( _listFormat ) {
		case threeDeltas:
			A::P::E::set32(*((uint32_t*)(&buffer[0])), 0x80000000 | 3*sizeof(int32_t));
			break;
		case threePointers:
		case threePointersAuthImpl:
			A::P::E::set32(*((uint32_t*)(&buffer[0])), 3*sizeof(pint_t));
			break;
		case twoPointers:
			A::P::E::set32(*((uint32_t*)(&buffer[0])), 2*sizeof(pint_t));
			break;
	}
}

template <typename A>
ProtocolListAtom<A>::ProtocolListAtom(ld::Internal& state, const ld::Atom* baseProtocolList, const char* className,
									const std::vector<const ld::Atom*>* categories, std::set<const ld::Atom*>& deadAtoms)
  : ld::Atom(_s_section, ld::Atom::definitionRegular, ld::Atom::combineNever,
			ld::Atom::scopeLinkageUnit, ld::Atom::typeUnclassified,
			symbolTableIn, false, false, false, ld::Atom::Alignment(3)), _file(NULL), _protocolCount(0)
{
	unsigned int fixupCount = 0;
	if ( baseProtocolList != NULL ) {
		// if base class has protocol list, then associate new protocol list with file defining class
		_file = baseProtocolList->file();
		// calculate total size of merged protocol list
		_protocolCount = ProtocolList<A>::count(state, baseProtocolList);
		deadAtoms.insert(baseProtocolList);
		fixupCount = baseProtocolList->fixupsEnd() - baseProtocolList->fixupsBegin();
	}
	for (const ld::Atom* aCategoryAtom : *categories) {
		const ld::Atom* categoryProtocolListAtom = Category<A>::getProtocols(state, aCategoryAtom);
		if ( categoryProtocolListAtom != NULL ) {
			_protocolCount += ProtocolList<A>::count(state, categoryProtocolListAtom);
			fixupCount += (categoryProtocolListAtom->fixupsEnd() - categoryProtocolListAtom->fixupsBegin());
			deadAtoms.insert(categoryProtocolListAtom);
			// if base class did not have protocol list, associate new protocol list with file the defined category
			if ( _file == NULL )
				_file = categoryProtocolListAtom->file();
		}
	}
	//fprintf(stderr, "total merged protocol count=%u\n", _protocolCount);
	//fprintf(stderr, "total merged fixup count=%u\n", fixupCount);
	_name = std::string("__OBJC_CLASS_PROTOCOLS_$_") + className;
	
	// copy fixups and adjust offsets
	_fixups.reserve(fixupCount);
	uint32_t slide = 0;
	std::vector<const char*> catNames;
	for (const ld::Atom* aCategoryAtom : *categories) {
		const ld::Atom* categoryProtocolListAtom = Category<A>::getProtocols(state, aCategoryAtom);
		if ( categoryProtocolListAtom != NULL ) {
			const char* catName = (const char*)Category<A>::getName(state, aCategoryAtom)->rawContentPointer();
			if ( catName != nullptr )
				catNames.push_back(catName);
			for (ld::Fixup::iterator fit=categoryProtocolListAtom->fixupsBegin(); fit != categoryProtocolListAtom->fixupsEnd(); ++fit) {
				ld::Fixup fixup = *fit;
				fixup.offsetInAtom += slide;
				_fixups.push_back(fixup);
				//if ( fixup.binding == ld::Fixup::bindingDirectlyBound )
				//	fprintf(stderr, "offset=0x%08X, name=%s\n", fixup.offsetInAtom, fixup.u.target->name());
			}
			slide += sizeof(pint_t) * ProtocolList<A>::count(state, categoryProtocolListAtom);
		}
	}
	if ( !catNames.empty() ) {
		_name += "(";
		bool needSeparator = false;
		for (const char* catName : catNames) {
			if ( needSeparator )
				_name += "|";
			_name += catName;
			needSeparator = true;
		}
		_name += ")";
	}
	
	
	// add method list from base class last
	if ( baseProtocolList != NULL ) {
		for (ld::Fixup::iterator fit=baseProtocolList->fixupsBegin(); fit != baseProtocolList->fixupsEnd(); ++fit) {
			ld::Fixup fixup = *fit;
			fixup.offsetInAtom += slide;
			_fixups.push_back(fixup);
		}
	}
	state.addAtom(*this);
}

template <typename A>
PropertyListAtom<A>::PropertyListAtom(ld::Internal& state, const ld::Atom* basePropertyList,
				      const std::vector<const ld::Atom*>* categories, std::set<const ld::Atom*>& deadAtoms, PropertyKind kind)
  : ld::Atom(_s_section, ld::Atom::definitionRegular, ld::Atom::combineNever,
			ld::Atom::scopeLinkageUnit, ld::Atom::typeUnclassified,
			symbolTableNotIn, false, false, false, ld::Atom::Alignment(3)), _file(NULL), _propertyCount(0)
{
	unsigned int fixupCount = 0;
	if ( basePropertyList != NULL ) {
		// if base class has property list, then associate new property list with file defining class
		_file = basePropertyList->file();
		// calculate total size of merged property list
		_propertyCount = PropertyList<A>::count(state, basePropertyList);
		deadAtoms.insert(basePropertyList);
		fixupCount = basePropertyList->fixupsEnd() - basePropertyList->fixupsBegin();
	}
	for (const ld::Atom* aCategoryAtom : *categories) {
		const ld::Atom* categoryPropertyListAtom = kind == PropertyKind::ClassProperties ? Category<A>::getClassProperties(state, aCategoryAtom) : Category<A>::getInstanceProperties(state, aCategoryAtom);
		if ( categoryPropertyListAtom != NULL ) {
			_propertyCount += PropertyList<A>::count(state, categoryPropertyListAtom);
			fixupCount += (categoryPropertyListAtom->fixupsEnd() - categoryPropertyListAtom->fixupsBegin());
			deadAtoms.insert(categoryPropertyListAtom);
			// if base class did not have property list, associate new property list with file the defined category
			if ( _file == NULL )
				_file = categoryPropertyListAtom->file();
		}
	}
	//fprintf(stderr, "total merged property count=%u\n", _propertyCount);
	//fprintf(stderr, "total merged fixup count=%u\n", fixupCount);

	// copy fixups and adjust offsets
	_fixups.reserve(fixupCount);
	uint32_t slide = 0;
	for (const ld::Atom* aCategoryAtom : *categories) {
		const ld::Atom* categoryPropertyListAtom = kind == PropertyKind::ClassProperties ? Category<A>::getClassProperties(state, aCategoryAtom) : Category<A>::getInstanceProperties(state, aCategoryAtom);
		if ( categoryPropertyListAtom != NULL ) {
			for (ld::Fixup::iterator fit=categoryPropertyListAtom->fixupsBegin(); fit != categoryPropertyListAtom->fixupsEnd(); ++fit) {
				ld::Fixup fixup = *fit;
				fixup.offsetInAtom += slide;
				_fixups.push_back(fixup);
				//fprintf(stderr, "offset=0x%08X, binding=%d\n", fixup.offsetInAtom, fixup.binding);
				//if ( fixup.binding == ld::Fixup::bindingDirectlyBound )
				//	fprintf(stderr, "offset=0x%08X, name=%s\n", fixup.offsetInAtom, fixup.u.target->name());
				//else if ( fixup.binding == ld::Fixup::bindingsIndirectlyBound )
				//	fprintf(stderr, "offset=0x%08X, indirect index=%u, name=%s\n", fixup.offsetInAtom, fixup.u.bindingIndex,
				//			(char*)(state.indirectBindingTable[fixup.u.bindingIndex]->rawContentPointer()));
			}
			slide += 2*sizeof(pint_t) * PropertyList<A>::count(state, categoryPropertyListAtom);
		}
	}
	// add property list from base class last
	if ( basePropertyList != NULL ) {
		for (ld::Fixup::iterator fit=basePropertyList->fixupsBegin(); fit != basePropertyList->fixupsEnd(); ++fit) {
			ld::Fixup fixup = *fit;
			fixup.offsetInAtom += slide;
			_fixups.push_back(fixup);
		}
	}
	_syntheticAddress = _s_nextSyntheticAddress++;
	state.addAtom(*this);
}


template <typename A>
bool scanCategories(ld::Internal& state)
{
	bool warned = false;
	bool haveCategoriesWithoutClassPropertyStorage = false;
	for (std::vector<ld::Internal::FinalSection*>::iterator sit=state.sections.begin(); sit != state.sections.end(); ++sit) {
		ld::Internal::FinalSection* sect = *sit;
		if ( sect->type() == ld::Section::typeObjC2CategoryList ) {
			const char* aFileWithCategorysWithNonNullClassProperties = nullptr;
			for (std::vector<const ld::Atom*>::iterator ait=sect->atoms.begin(); ait != sect->atoms.end(); ++ait) {
				const ld::Atom* categoryListElementAtom = *ait;

				uint64_t addend;
				const ld::Atom* categoryAtom = ObjCData<A>::getPointerInContent(state, categoryListElementAtom, 0, &addend);
				
				if (Category<A>::getClassProperties(state, categoryAtom)) {
					aFileWithCategorysWithNonNullClassProperties = categoryAtom->safeFilePath();
				}

				if ( const ld::relocatable::File* objFile = dynamic_cast<const ld::relocatable::File*>(categoryAtom->file()) ) {
					if ( !objFile->objcHasCategoryClassPropertiesField() ) {
						haveCategoriesWithoutClassPropertyStorage = true;
						if ( aFileWithCategorysWithNonNullClassProperties ) {
							// Complain about mismatched category ABI.
							// These can't be combined into a single linkage unit because there is only one size indicator for all categories in the file.
							// If there is a mismatch then we don't set the HasCategoryClassProperties bit in the output file,
							// which has at runtime causes any class property metadata that was present to be ignored.
							if ( !warned ) {
								warning("Incompatible Objective-C category definitions. Some category metadata may be lost. '%s' and '%s built with different compilers",
										aFileWithCategorysWithNonNullClassProperties, categoryAtom->safeFilePath());
								warned = true;
							}
						}
					}
				}
			}
		}
	}
	return haveCategoriesWithoutClassPropertyStorage;
}


template <typename A, bool isObjC2>
void doPass(const Options& opts, ld::Internal& state)
{
	// Do nothing if the output has no ObjC content.
	if ( !state.hasObjC ) {
	 	return;
	}

	// Search for categories that have a non-null class properties field.
	// Search for categories that do not have storage for the class properties field.
	bool haveCategoriesWithoutClassPropertyStorage = scanCategories<A>(state);
	
	// optimize classes defined in this linkage unit by merging in categories also in this linkage unit
	OptimizeCategories<A>::doit(opts, state, haveCategoriesWithoutClassPropertyStorage);

	// add image info atom
	// The HasCategoryClassProperties bit is set as often as possible.
	state.addAtom(*new ObjCImageInfoAtom<A>(isObjC2, state.objcClassROPointerSigning == ClassROSigningEnabled, !haveCategoriesWithoutClassPropertyStorage, state.swiftVersion, state.swiftLanguageVersion));
}


void doPass(const Options& opts, ld::Internal& state)
{		
	switch ( opts.architecture() ) {
#if SUPPORT_ARCH_x86_64
		case CPU_TYPE_X86_64:
			doPass<x86_64, true>(opts, state);
			break;
#endif
#if SUPPORT_ARCH_i386
		case CPU_TYPE_I386:
			if (opts.objCABIVersion2POverride()) {
				doPass<x86, true>(opts, state);
			} else {
				doPass<x86, false>(opts, state);
			}
			break;
#endif
#if SUPPORT_ARCH_arm_any
		case CPU_TYPE_ARM:
			doPass<arm, true>(opts, state);
			break;
#endif
#if SUPPORT_ARCH_arm64
		case CPU_TYPE_ARM64:
#if SUPPORT_ARCH_arm64e
			if (opts.subArchitecture() == CPU_SUBTYPE_ARM64E) {
				doPass<arm64e, true>(opts, state);
				break;
			}
#endif
			doPass<arm64, true>(opts, state);
			break;
#endif
#if SUPPORT_ARCH_arm64_32
		case CPU_TYPE_ARM64_32:
			doPass<arm64_32, true>(opts, state);
			break;
#endif
#if SUPPORT_ARCH_riscv
		case CPU_TYPE_RISCV32:
			doPass<riscv32, true>(opts, state);
			break;
#endif
		default:
			assert(0 && "unknown objc arch");
	}
}


} // namespace objc
} // namespace passes 
} // namespace ld 
