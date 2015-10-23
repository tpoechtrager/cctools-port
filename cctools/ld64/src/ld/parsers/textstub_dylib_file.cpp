/* -*- mode: C++; c-basic-offset: 4; tab-width: 4 -*-
 *
 * Copyright (c) 2015 Apple Inc. All rights reserved.
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


#include <sys/param.h>
#include <sys/mman.h>

#include <vector>
#include <memory> // ld64-port
#include <functional> // ld64-port

#include "Architectures.hpp"
#include "Bitcode.hpp" // ld64-port: "bitcode.hpp" -> "Bitcode.hpp"
#include "MachOFileAbstraction.hpp"
#include "MachOTrie.hpp"
#include "textstub_dylib_file.hpp"

namespace {

///
/// A token is a light-weight reference to the content of an nmap'ed file. It
/// doesn't own the data and it doesn't make a copy of it. The referenced data
/// is only valid as long as the file is mapped in.
///
class Token {
	const char* _p;
	size_t _size;

	int compareMemory(const char* lhs, const char* rhs, size_t size) const {
		if (size == 0)
			return 0;
		return ::memcmp(lhs, rhs, size);
	}

public:
	Token() : _p(nullptr), _size(0) {}

	Token(const char* p) : _p(p), _size(0) {
		if (p)
			_size = ::strlen(p);
	}

	Token(const char* p, size_t s) : _p(p), _size(s) {}

	const char* data() const { return _p; }

	size_t size() const { return _size; }

	std::string str() const { return std::string(_p, _size); } // ld64-port: removed std::move() [-Wpessimizing-move]

	bool empty() const { return _size == 0; }

	bool operator==(Token other) const {
		if (_size != other._size)
			return false;
		return compareMemory(_p, other._p, _size) == 0;
	}

	bool operator!=(Token other) const {
		return !(*this == other);
	}
};

///
/// Simple text-based dynamic library file tokenizer.
///
class Tokenizer {
	const char* _start;
	const char* _current;
	const char* _end;
	Token _currentToken;

	void fetchNextToken();
	void scanToNextToken();
	void skip(unsigned distance) {
		_current += distance;
		assert(_current <= _end && "Skipped past the end");
	}

	const char* skipLineBreak(const char* pos) const;
	bool isDelimiter(const char* pos) const;

public:
	Tokenizer(const char* data, uint64_t size) : _start(data), _current(data), _end(data + size) {}

	void reset() {
		_current = _start;
		fetchNextToken();
	}

	Token peek() { return _currentToken; }
	Token next() {
		Token token = peek();
		fetchNextToken();
		return token;
	}
};

const char* Tokenizer::skipLineBreak(const char* pos) const
{
	if ( pos == _end )
		return pos;

	// Carriage return.
	if ( *pos == 0x0D ) {
		// line feed.
		if ( pos + 1 != _end && *(pos + 1) == 0x0A)
			return pos + 2;
		return pos + 1;
	}

	// line feed.
	if ( *pos == 0x0A )
		return pos + 1;

	return pos;
}

void Tokenizer::scanToNextToken() {
	while (true) {
		while ( isDelimiter(_current) )
			skip(1);

		const char* i = skipLineBreak(_current);
		if ( i == _current )
			break;

		_current = i;
	}
}


bool Tokenizer::isDelimiter(const char* pos) const {
	if ( pos == _end )
		return false;
	if ( *pos == ' ' || *pos == '\t' || *pos == '\r' || *pos == '\n' || *pos == ',' || *pos == ':' || *pos == '\'' || *pos == '\"' )
		return true;
	return false;
}

void Tokenizer::fetchNextToken() {
	scanToNextToken();

	if (_current == _end) {
		_currentToken = Token();
		return;
	}

	auto start = _current;
	while ( !isDelimiter(_current) ) {
		++_current;
	}

	_currentToken = Token(start, _current - start);
}

///
/// Representation of a parsed text-based dynamic library file.
///
struct DynamicLibrary {
	Token _installName;
	uint32_t _currentVersion;
	uint32_t _compatibilityVersion;
	uint8_t _swiftVersion;
	ld::File::ObjcConstraint _objcConstraint;
	Options::Platform _platform;
	std::vector<Token> _allowedClients;
	std::vector<Token> _reexportedLibraries;
	std::vector<Token> _symbols;
	std::vector<Token> _classes;
	std::vector<Token> _ivars;
	std::vector<Token> _weakDefSymbols;
	std::vector<Token> _tlvSymbols;

	DynamicLibrary() : _currentVersion(0x10000), _compatibilityVersion(0x10000), _swiftVersion(0),
		_objcConstraint(ld::File::objcConstraintNone)  {}
};

static uint32_t parseVersionNumber32(Token token) {
	if ( token.size() >= 128 )
		throwf("malformed version number");

	char buffer[128];
	uint32_t x = 0;
	uint32_t y = 0;
	uint32_t z = 0;
	char* end;

	// Make a null-terminated string.
	::memcpy(buffer, token.data(), token.size());
	buffer[token.size()] = '\0';

	x = strtoul(buffer, &end, 10);
	if ( *end == '.' ) {
		y = strtoul(&end[1], &end, 10);
		if ( *end == '.' ) {
			z = strtoul(&end[1], &end, 10);
		}
	}
	if ( (x > 0xffff) || (y > 0xff) || (z > 0xff) )
		throwf("malformed 32-bit x.y.z version number: %s", buffer);

	return (x << 16) | ( y << 8 ) | z;
}

///
/// A simple text-based dynamic library file parser.
///
class TBDFile {
	Tokenizer _tokenizer;

	Token peek() { return _tokenizer.peek(); }
	Token next() { return _tokenizer.next(); }

	void expectToken(Token str) {
		Token token = next();
		if (token != str)
			throwf("unexpected token: %s", token.str().c_str());
	}

	bool hasOptionalToken(Token str) {
		auto token = peek();
		if ( token == str ) {
			next();
			return true;
		}
		return false;
	}


	void parseFlowSequence(std::function<void (Token)> func) {
		expectToken("[");

		while ( true ) {
			auto token = peek();
			if ( token == "]" )
				break;

			token = next();
			func(token);
		}

		expectToken("]");
	}

	void parseAllowedClients(DynamicLibrary& lib) {
		if ( !hasOptionalToken("allowed-clients") )
			return;
		parseFlowSequence([&](Token name) {
			lib._allowedClients.emplace_back(name);
		});
	}

	void parseReexportedDylibs(DynamicLibrary& lib) {
		if ( !hasOptionalToken("re-exports") )
			return;
		parseFlowSequence([&](Token name) {
			lib._reexportedLibraries.emplace_back(name);
		});
	}

	void parseSymbols(DynamicLibrary& lib) {
		if ( hasOptionalToken("symbols") ) {
			parseFlowSequence([&](Token name) {
				lib._symbols.emplace_back(name);
			});
		}

		if ( hasOptionalToken("objc-classes") ) {
			parseFlowSequence([&](Token name) {
				lib._classes.emplace_back(name);
			});
		}

		if ( hasOptionalToken("objc-ivars") ) {
			parseFlowSequence([&](Token name) {
				lib._ivars.emplace_back(name);
			});
		}

		if ( hasOptionalToken("weak-def-symbols") ) {
			parseFlowSequence([&](Token name) {
				lib._weakDefSymbols.emplace_back(name);
			});
		}

		if ( hasOptionalToken("thread-local-symbols") ) {
			parseFlowSequence([&](Token name) {
				lib._tlvSymbols.emplace_back(name);
			});
		}
	}

	bool parseArchFlowSequence(Token archName) {
		expectToken("archs");

		bool foundArch = false;
		parseFlowSequence([&](Token name) {
			if ( name == archName )
				foundArch = true;
			});

		return foundArch;
	}

	void parsePlatform(DynamicLibrary& lib) {
		expectToken("platform");

		auto token =  next();
		if (token == "macosx")
			lib._platform = Options::kPlatformOSX;
		else if (token == "ios")
			lib._platform = Options::kPlatformiOS;
		else if (token == "watchos")
			lib._platform = Options::kPlatformWatchOS;
#if SUPPORT_APPLE_TV
		else if (token == "tvos")
			lib._platform = Options::kPlatform_tvOS;
#endif
		else
			lib._platform = Options::kPlatformUnknown;
	}

	void parseInstallName(DynamicLibrary& lib) {
		expectToken("install-name");

		lib._installName = next();
		if ( lib._installName.empty() )
			throwf("no install name specified");
	}

	void parseCurrentVersion(DynamicLibrary& lib) {
		if ( !hasOptionalToken("current-version") )
			return;
		lib._currentVersion = parseVersionNumber32(next());
	}

	void parseCompatibilityVersion(DynamicLibrary& lib) {
		if ( !hasOptionalToken("compatibility-version") )
			return;
		lib._compatibilityVersion = parseVersionNumber32(next());
	}

	void parseSwiftVersion(DynamicLibrary& lib) {
		if ( !hasOptionalToken("swift-version") )
			return;
		auto token = next();
		if ( token == "1.0" )
			lib._swiftVersion = 1;
		else if ( token == "1.1" )
			lib._swiftVersion = 2;
		else if ( token == "2.0" )
			lib._swiftVersion = 3;
		else
			throwf("unsupported Swift ABI version: %s", token.str().c_str());
	}

	void parseObjCConstraint(DynamicLibrary& lib) {
		if ( !hasOptionalToken("objc-constraint") )
			return;
		auto token = next();
		if ( token == "none" )
			lib._objcConstraint = ld::File::objcConstraintNone;
		else if ( token == "retain_release" )
			lib._objcConstraint = ld::File::objcConstraintRetainRelease;
		else if ( token == "retain_release_for_simulator" )
			lib._objcConstraint = ld::File::objcConstraintRetainReleaseForSimulator;
		else if ( token == "retain_release_or_gc" )
			lib._objcConstraint = ld::File::objcConstraintRetainReleaseOrGC;
		else if ( token == "gc" )
			lib._objcConstraint = ld::File::objcConstraintGC;
		else
			throwf("unexpected token: %s", token.str().c_str());
	}
	void parseExportsBlock(DynamicLibrary& lib, Token archName) {
		if ( !hasOptionalToken("exports") )
			return;

		if ( !hasOptionalToken("-") )
			return;

		while ( true ) {
			if ( !parseArchFlowSequence(archName) ) {
				Token token;
				while ( true ) {
					token = peek();
					if ( token == "archs" || token == "..." || token.empty() )
						break;
					next();
				}
				if (token == "..." || token.empty() )
					break;

				continue;
			}

			parseAllowedClients(lib);
			parseReexportedDylibs(lib);
			parseSymbols(lib);
			if ( !hasOptionalToken("-") )
				break;
		}
	}

	void parseDocument(DynamicLibrary& lib, Token archName) {
		if ( !parseArchFlowSequence(archName) )
			throwf("invalid arch");

		parsePlatform(lib);
		parseInstallName(lib);
		parseCurrentVersion(lib);
		parseCompatibilityVersion(lib);
		parseSwiftVersion(lib);
		parseObjCConstraint(lib);
		parseExportsBlock(lib, archName);
	}

public:
	TBDFile(const char* data, uint64_t size) : _tokenizer(data, size) {}

	DynamicLibrary parseFileForArch(Token archName) {
		_tokenizer.reset();
		DynamicLibrary lib;
		expectToken("---");
		parseDocument(lib, archName);
		expectToken("...");
		return lib; // ld64-port: removed std::move() [-Wpessimizing-move]
	}

	bool validForArch(Token archName) {
		_tokenizer.reset();
		auto token = next();
		if ( token != "---" )
			return false;
		return parseArchFlowSequence(archName);
	}

	void dumpTokens() {
		_tokenizer.reset();
		Token token;
		do {
			token = next();
			printf("token: %s\n", token.str().c_str());
		} while ( !token.empty() );
	}
};

} // end anonymous namespace

namespace textstub {
namespace dylib {

// forward reference
template <typename A> class File;


//
// An ExportAtom has no content.  It exists so that the linker can track which imported
// symbols came from which dynamic libraries.
//
template <typename A>
class ExportAtom : public ld::Atom
{
public:
	ExportAtom(const File<A>& f, const char* nm, bool weakDef, bool tlv)
		: ld::Atom(f._importProxySection, ld::Atom::definitionProxy,
				   (weakDef? ld::Atom::combineByName : ld::Atom::combineNever),
				   ld::Atom::scopeLinkageUnit,
				   (tlv ? ld::Atom::typeTLV : ld::Atom::typeUnclassified),
				   symbolTableNotIn, false, false, false, ld::Atom::Alignment(0)),
				   _file(f), _name(nm) {}
	// overrides of ld::Atom
	virtual const ld::File*			file() const		{ return &_file; }
	virtual const char*				name() const		{ return _name; }
	virtual uint64_t				size() const		{ return 0; }
	virtual uint64_t				objectAddress() const { return 0; }
	virtual void					copyRawContent(uint8_t buffer[]) const { }
	virtual void					setScope(Scope)		{ }

protected:
	typedef typename A::P			P;
	typedef typename A::P::uint_t	pint_t;

	virtual							~ExportAtom() {}

	const File<A>&					_file;
	const char*						_name;
};


//
// The reader for a dylib extracts all exported symbols names from the memory-mapped
// dylib, builds a hash table, then unmaps the file.  This is an important memory
// savings for large dylibs.
//
template <typename A>
class File : public ld::dylib::File
{
public:
	static bool		validFile(const uint8_t* fileContent, bool executableOrDylib);
					File(const uint8_t* fileContent, uint64_t fileLength, const char* path,
						 time_t mTime, ld::File::Ordinal ordinal, bool linkingFlatNamespace,
						 bool hoistImplicitPublicDylibs, Options::Platform platform,
						 cpu_type_t cpuType, const char* archName, uint32_t linkMinOSVersion,
						 bool allowSimToMacOSX, bool addVers, bool buildingForSimulator,
						 bool logAllFiles, const char* installPath, bool indirectDylib);
	virtual			~File() {}

	// overrides of ld::File
	virtual bool							forEachAtom(ld::File::AtomHandler&) const;
	virtual bool							justInTimeforEachAtom(const char* name, ld::File::AtomHandler&) const;
	virtual ld::File::ObjcConstraint		objCConstraint() const		{ return _objcConstraint; }
	virtual uint8_t							swiftVersion() const		{ return _swiftVersion; }

	// overrides of ld::dylib::File
	virtual void							processIndirectLibraries(ld::dylib::File::DylibHandler*, bool);
	virtual bool							providedExportAtom() const	{ return _providedAtom; }
	virtual const char*						parentUmbrella() const { return nullptr; }
	virtual const std::vector<const char*>*	allowableClients() const	{ return _allowableClients.size() != 0 ? &_allowableClients : nullptr; }
	virtual bool							hasWeakExternals() const	{ return _hasWeakExports; }
	virtual bool							deadStrippable() const		{ return false; }
	virtual bool							hasPublicInstallName() const{ return _hasPublicInstallName; }
	virtual bool							hasWeakDefinition(const char* name) const;
	virtual bool							allSymbolsAreWeakImported() const;
	virtual bool							installPathVersionSpecific() const { return _installPathOverride; }
	// All text-based stubs are per definition AppExtensionSafe.
	virtual bool							appExtensionSafe() const	{ return true; };
	virtual ld::Bitcode*					getBitcode() const			{ return _bitcode.get(); }


protected:
	virtual void							assertNoReExportCycles(ReExportChain*) const;

private:
	typedef typename A::P					P;
	typedef typename A::P::E				E;
	typedef typename A::P::uint_t			pint_t;

	friend class ExportAtom<A>;

	struct CStringHash {
		std::size_t operator()(const char* __s) const {
			unsigned long __h = 0;
			for ( ; *__s; ++__s)
				__h = 5 * __h + *__s;
			return size_t(__h);
		};
	};
	struct AtomAndWeak { ld::Atom* atom; bool weakDef; bool tlv; };
	typedef std::unordered_map<const char*, AtomAndWeak, ld::CStringHash, ld::CStringEquals> NameToAtomMap;
	typedef std::unordered_set<const char*, CStringHash, ld::CStringEquals>  NameSet;

	struct Dependent { const char* path; File<A>* dylib; };

	virtual std::pair<bool, bool>			hasWeakDefinitionImpl(const char* name) const;
	virtual bool							containsOrReExports(const char* name, bool& weakDef, bool& tlv, uint64_t& address) const;

	void									buildExportHashTable(const DynamicLibrary &lib);
	bool									isPublicLocation(const char* pth);
	bool									wrongOS() { return _wrongOS; }
	void									addSymbol(const char* name, bool weak, bool tlv);

	const Options::Platform					_platform;
	cpu_type_t								_cpuType;
	const uint32_t							_linkMinOSVersion;
	const bool								_allowSimToMacOSXLinking;
	const bool								_addVersionLoadCommand;
	bool									_linkingFlat;
	bool									_implicitlyLinkPublicDylibs;
	ld::File::ObjcConstraint				_objcConstraint;
	uint8_t									_swiftVersion;
	ld::Section								_importProxySection;
	ld::Section								_flatDummySection;
	std::vector<Dependent>					_dependentDylibs;
	std::vector<const char*>				_allowableClients;
	mutable NameToAtomMap					_atoms;
	NameSet									_ignoreExports;
	bool									_noRexports;
	bool									_hasWeakExports;
	bool									_hasPublicInstallName;
	mutable bool							_providedAtom;
	bool									_wrongOS;
	bool									_installPathOverride;
	bool									_indirectDylibsProcessed;
	std::unique_ptr<ld::Bitcode>			_bitcode;
	static bool								_s_logHashtable;
};

template <typename A>
bool File<A>::_s_logHashtable = false;


template <typename A>
File<A>::File(const uint8_t* fileContent, uint64_t fileLength, const char* path, time_t mTime,
			  ld::File::Ordinal ord, bool linkingFlatNamespace, bool hoistImplicitPublicDylibs,
			  Options::Platform platform, cpu_type_t cpuType, const char* archName,
			  uint32_t linkMinOSVersion, bool allowSimToMacOSX, bool addVers,
			  bool buildingForSimulator, bool logAllFiles, const char* targetInstallPath,
			  bool indirectDylib)
	: ld::dylib::File(strdup(path), mTime, ord), _platform(platform), _cpuType(cpuType),
	  _linkMinOSVersion(linkMinOSVersion), _allowSimToMacOSXLinking(allowSimToMacOSX),
	  _addVersionLoadCommand(addVers), _linkingFlat(linkingFlatNamespace),
	  _implicitlyLinkPublicDylibs(hoistImplicitPublicDylibs),
	  _objcConstraint(ld::File::objcConstraintNone), _swiftVersion(0),
	  _importProxySection("__TEXT", "__import", ld::Section::typeImportProxies, true),
	  _flatDummySection("__LINKEDIT", "__flat_dummy", ld::Section::typeLinkEdit, true),
	  _noRexports(false), _hasWeakExports(false),
	  _hasPublicInstallName(false), _providedAtom(false), _wrongOS(false),
	  _installPathOverride(false), _indirectDylibsProcessed(false),
	  _bitcode(new ld::Bitcode(nullptr, 0))
{
	// write out path for -t option
	if ( logAllFiles )
		printf("%s\n", path);

	TBDFile stub((const char*)fileContent, fileLength);
	auto lib = stub.parseFileForArch(archName);

	_noRexports = lib._reexportedLibraries.empty();
	_hasWeakExports = !lib._weakDefSymbols.empty();
	_dylibInstallPath = strdup(lib._installName.str().c_str());
	_dylibCurrentVersion = lib._currentVersion;
	_dylibCompatibilityVersion = lib._compatibilityVersion;
	_swiftVersion = lib._swiftVersion;
	_objcConstraint = lib._objcConstraint;
	_hasPublicInstallName = isPublicLocation(_dylibInstallPath);

	for (auto &client : lib._allowedClients)
		_allowableClients.push_back(strdup(client.str().c_str()));

	// <rdar://problem/20659505> [TAPI] Don't hoist "public" (in /usr/lib/) dylibs that should not be directly linked
	if ( !_allowableClients.empty() )
		_hasPublicInstallName = false;

	if ( (lib._platform != platform) && (platform != Options::kPlatformUnknown) ) {
		_wrongOS = true;
		if ( _addVersionLoadCommand && !indirectDylib ) {
			if ( buildingForSimulator ) {
				if ( !_allowSimToMacOSXLinking )
					throwf("building for %s simulator, but linking against dylib built for %s (%s).",
							Options::platformName(platform), Options::platformName(lib._platform), path);
			} else {
				throwf("building for %s, but linking against dylib built for %s (%s).",
						Options::platformName(platform), Options::platformName(lib._platform), path);
			}
		}
	}

	_dependentDylibs.reserve(lib._reexportedLibraries.size());
	for ( auto& reexport : lib._reexportedLibraries ) {
		Dependent entry;
		entry.path = strdup(reexport.str().c_str());
		entry.dylib = nullptr;
		if ( (targetInstallPath == nullptr) || (strcmp(targetInstallPath, entry.path) != 0) )
			_dependentDylibs.push_back(entry);
	}

	// build hash table
	buildExportHashTable(lib);

	munmap((caddr_t)fileContent, fileLength);
}

template <typename A>
void File<A>::buildExportHashTable(const DynamicLibrary& lib) {
	if ( _s_logHashtable )
		fprintf(stderr, "ld: building hashtable from text-stub info in %s\n", this->path());

	for (auto &sym : lib._symbols)
		addSymbol(sym.str().c_str(), /*weak=*/false, /*tlv=*/false);

#if SUPPORT_ARCH_i386
	if (_platform == Options::kPlatformOSX && _cpuType == CPU_TYPE_I386) {
		for (auto &sym : lib._classes)
			addSymbol((".objc_class_name" + sym.str()).c_str(), /*weak=*/false, /*tlv=*/false);
	} else {
		for (auto &sym : lib._classes) {
			addSymbol(("_OBJC_CLASS_$" + sym.str()).c_str(), /*weak=*/false, /*tlv=*/false);
			addSymbol(("_OBJC_METACLASS_$" + sym.str()).c_str(), /*weak=*/false, /*tlv=*/false);
		}
	}
#else
	for (auto &sym : lib._classes) {
		addSymbol(("_OBJC_CLASS_$" + sym.str()).c_str(), /*weak=*/false, /*tlv=*/false);
		addSymbol(("_OBJC_METACLASS_$" + sym.str()).c_str(), /*weak=*/false, /*tlv=*/false);
	}
#endif

	for (auto &sym : lib._ivars)
		addSymbol(("_OBJC_IVAR_$" + sym.str()).c_str(), /*weak=*/false, /*tlv=*/false);

	for (auto &sym : lib._weakDefSymbols)
		addSymbol(sym.str().c_str(), /*weak=*/true, /*tlv=*/false);

	for (auto &sym : lib._tlvSymbols)
		addSymbol(sym.str().c_str(), /*weak=*/false, /*tlv=*/true);
}


template <typename A>
void File<A>::addSymbol(const char* name, bool weakDef, bool tlv)
{
	// symbols that start with $ld$ are meta-data to the static linker
	// <rdar://problem/5182537> need way for ld and dyld to see different exported symbols in a dylib
	if ( strncmp(name, "$ld$", 4) == 0 ) {
		//    $ld$ <action> $ <condition> $ <symbol-name>
		const char* symAction = &name[4];
		const char* symCond = strchr(symAction, '$');
		if ( symCond != nullptr ) {
			char curOSVers[16];
			sprintf(curOSVers, "$os%d.%d$", (_linkMinOSVersion >> 16), ((_linkMinOSVersion >> 8) & 0xFF));
			if ( strncmp(symCond, curOSVers, strlen(curOSVers)) == 0 ) {
				const char* symName = strchr(&symCond[1], '$');
				if ( symName != nullptr ) {
					++symName;
					if ( strncmp(symAction, "hide$", 5) == 0 ) {
						if ( _s_logHashtable )
							fprintf(stderr, "  adding %s to ignore set for %s\n", symName, this->path());
						_ignoreExports.insert(strdup(symName));
						return;
					}
					else if ( strncmp(symAction, "add$", 4) == 0 ) {
						this->addSymbol(symName, weakDef, false);
						return;
					}
					else if ( strncmp(symAction, "install_name$", 13) == 0 ) {
						_dylibInstallPath = strdup(symName);
						_installPathOverride = true;
						return;
					}
					else if ( strncmp(symAction, "compatibility_version$", 22) == 0 ) {
						_dylibCompatibilityVersion = parseVersionNumber32(symName);
						return;
					}
					else {
						warning("bad symbol action: %s in dylib %s", name, this->path());
					}
				}
			}
		}
		else {
			warning("bad symbol condition: %s in dylib %s", name, this->path());
		}
	}

	// add symbol as possible export if we are not supposed to ignore it
	if ( _ignoreExports.count(name) == 0 ) {
		AtomAndWeak bucket;
		bucket.atom = nullptr;
		bucket.weakDef = weakDef;
		bucket.tlv = tlv;
		if ( _s_logHashtable )
			fprintf(stderr, "  adding %s to hash table for %s\n", name, this->path());
		_atoms[strdup(name)] = bucket;
	}
}


template <typename A>
bool File<A>::forEachAtom(ld::File::AtomHandler& handler) const
{
	handler.doFile(*this);
	return false;
}


template <typename A>
std::pair<bool, bool> File<A>::hasWeakDefinitionImpl(const char* name) const
{
	const auto pos = _atoms.find(name);
	if ( pos != _atoms.end() )
		return std::make_pair(true, pos->second.weakDef);

	// look in children that I re-export
	for (const auto &dep : _dependentDylibs) {
		auto ret = dep.dylib->hasWeakDefinitionImpl(name);
		if ( ret.first )
			return ret;
	}
	return std::make_pair(false, false);
}


template <typename A>
bool File<A>::hasWeakDefinition(const char* name) const
{
	// if supposed to ignore this export, then pretend I don't have it
	if ( _ignoreExports.count(name) != 0 )
		return false;

	return hasWeakDefinitionImpl(name).second;
}


// <rdar://problem/5529626> If only weak_import symbols are used, linker should use LD_LOAD_WEAK_DYLIB
template <typename A>
bool File<A>::allSymbolsAreWeakImported() const
{
	bool foundNonWeakImport = false;
	bool foundWeakImport = false;
	for (const auto &it : _atoms) {
		const ld::Atom* atom = it.second.atom;
		if ( atom != nullptr ) {
			if ( atom->weakImported() )
				foundWeakImport = true;
			else
				foundNonWeakImport = true;
		}
	}

	// don't automatically weak link dylib with no imports
	// so at least one weak import symbol and no non-weak-imported symbols must be found
	return foundWeakImport && !foundNonWeakImport;
}


template <typename A>
bool File<A>::containsOrReExports(const char* name, bool& weakDef, bool& tlv, uint64_t& addr) const
{
	if ( _ignoreExports.count(name) != 0 )
		return false;

	// check myself
	const auto pos = _atoms.find(name);
	if ( pos != _atoms.end() ) {
		weakDef = pos->second.weakDef;
		tlv = pos->second.tlv;
		addr = 0;
		return true;
	}

	// check dylibs I re-export
	for (const auto& lib : _dependentDylibs) {
		if ( !lib.dylib->implicitlyLinked() ) {
			if ( lib.dylib->containsOrReExports(name, weakDef, tlv, addr) )
				return true;
		}
	}

	return false;
}


template <typename A>
bool File<A>::justInTimeforEachAtom(const char* name, ld::File::AtomHandler& handler) const
{
	// if supposed to ignore this export, then pretend I don't have it
	if ( _ignoreExports.count(name) != 0 )
		return false;


	AtomAndWeak bucket;
	uint64_t addr;
	if ( this->containsOrReExports(name, bucket.weakDef, bucket.tlv, addr) ) {
		bucket.atom = new ExportAtom<A>(*this, name, bucket.weakDef, bucket.tlv);
		_atoms[name] = bucket;
		_providedAtom = true;
		if ( _s_logHashtable )
			fprintf(stderr, "getJustInTimeAtomsFor: %s found in %s\n", name, this->path());
		// call handler with new export atom
		handler.doAtom(*bucket.atom);
		return true;
	}

	return false;
}



template <typename A>
bool File<A>::isPublicLocation(const char* path)
{
	// -no_implicit_dylibs disables this optimization
	if ( ! _implicitlyLinkPublicDylibs )
		return false;

	// /usr/lib is a public location
	if ( (strncmp(path, "/usr/lib/", 9) == 0) && (strchr(&path[9], '/') == nullptr) )
		return true;

	// /System/Library/Frameworks/ is a public location
	if ( strncmp(path, "/System/Library/Frameworks/", 27) == 0 ) {
		const char* frameworkDot = strchr(&path[27], '.');
		// but only top level framework
		// /System/Library/Frameworks/Foo.framework/Versions/A/Foo                 ==> true
		// /System/Library/Frameworks/Foo.framework/Resources/libBar.dylib         ==> false
		// /System/Library/Frameworks/Foo.framework/Frameworks/Bar.framework/Bar   ==> false
		// /System/Library/Frameworks/Foo.framework/Frameworks/Xfoo.framework/XFoo ==> false
		if ( frameworkDot != nullptr ) {
			int frameworkNameLen = frameworkDot - &path[27];
			if ( strncmp(&path[strlen(path)-frameworkNameLen-1], &path[26], frameworkNameLen+1) == 0 )
				return true;
		}
	}

	return false;
}

template <typename A>
void File<A>::processIndirectLibraries(ld::dylib::File::DylibHandler* handler, bool addImplicitDylibs)
{
	// only do this once
	if ( _indirectDylibsProcessed )
		return;

	const static bool log = false;
	if ( log ) fprintf(stderr, "processIndirectLibraries(%s)\n", this->installPath());
	if ( _linkingFlat ) {
		for (auto& lib : _dependentDylibs) {
			lib.dylib = (File<A>*)handler->findDylib(lib.path, this->path());
		}
	}
	else if ( _noRexports ) {
		// MH_NO_REEXPORTED_DYLIBS bit set, then nothing to do
	}
	else {
		// two-level, might have re-exports
		for (auto& lib : _dependentDylibs) {
			if ( log )
				fprintf(stderr, "processIndirectLibraries() parent=%s, child=%s\n", this->installPath(), lib.path);
			// a LC_REEXPORT_DYLIB, LC_SUB_UMBRELLA or LC_SUB_LIBRARY says we re-export this child
			lib.dylib = (File<A>*)handler->findDylib(lib.path, this->path());
			if ( lib.dylib->hasPublicInstallName() && !lib.dylib->wrongOS() ) {
				// promote this child to be automatically added as a direct dependent if this already is
				if ( (this->explicitlyLinked() || this->implicitlyLinked()) && (strcmp(lib.path, lib.dylib->installPath()) == 0) ) {
					if ( log )
						fprintf(stderr, "processIndirectLibraries() implicitly linking %s\n", lib.dylib->installPath());
					lib.dylib->setImplicitlyLinked();
				}
				else if ( lib.dylib->explicitlyLinked() || lib.dylib->implicitlyLinked() ) {
					if ( log )
						fprintf(stderr, "processIndirectLibraries() parent is not directly linked, but child is, so no need to re-export child\n");
				} else {
					if ( log )
						fprintf(stderr, "processIndirectLibraries() parent is not directly linked, so parent=%s will re-export child=%s\n", this->installPath(), lib.path);
				}
			} else {
				// add all child's symbols to me
				if ( log )
					fprintf(stderr, "processIndirectLibraries() child is not public, so parent=%s will re-export child=%s\n", this->installPath(), lib.path);
			}
		}
	}

	// check for re-export cycles
	ReExportChain chain;
	chain.prev = nullptr;
	chain.file = this;
	this->assertNoReExportCycles(&chain);

	_indirectDylibsProcessed = true;
}

template <typename A>
void File<A>::assertNoReExportCycles(ReExportChain* prev) const
{
	// recursively check my re-exported dylibs
	ReExportChain chain;
	chain.prev = prev;
	chain.file = this;
	for (const auto& dep : _dependentDylibs) {
		ld::File* child = dep.dylib;
		// check child is not already in chain
		for (ReExportChain* p = prev; p != nullptr; p = p->prev) {
			if ( p->file == child )
				throwf("cycle in dylib re-exports with %s and %s", child->path(), this->path());
		}
		if ( dep.dylib != nullptr )
			dep.dylib->assertNoReExportCycles(&chain);
	}
}


template <typename A>
class Parser
{
public:
	typedef typename A::P	P;

	static bool				validFile(const uint8_t* fileContent, uint64_t fileLength, const std::string &path, const char* archName);
	static ld::dylib::File*	parse(const uint8_t* fileContent, uint64_t fileLength, const char* path,
								  time_t mTime, ld::File::Ordinal ordinal, const Options& opts,
								  bool indirectDylib) {
		return new File<A>(fileContent, fileLength, path, mTime, ordinal,
						   opts.flatNamespace(),
						   opts.implicitlyLinkIndirectPublicDylibs(),
						   opts.platform(),
						   opts.architecture(),
						   opts.architectureName(),
						   opts.minOSversion(),
						   opts.allowSimulatorToLinkWithMacOSX(),
						   opts.addVersionLoadCommand(),
						   opts.targetIOSSimulator(),
						   opts.logAllFiles(),
						   opts.installPath(),
						   indirectDylib);
	}
};

template <typename A>
bool Parser<A>::validFile(const uint8_t* fileContent, uint64_t fileLength, const std::string &path, const char* archName)
{
	if ( path.find(".tbd", path.size()-4) == std::string::npos )
		return false;

	TBDFile stub((const char*)fileContent, fileLength);
	if ( !stub.validForArch(archName) )
		throwf("missing required architecture %s in file %s", archName, path.c_str());

	return true;
}

//
// main function used by linker to instantiate ld::Files
//
ld::dylib::File* parse(const uint8_t* fileContent, uint64_t fileLength, const char* path,
					   time_t modTime, const Options& opts, ld::File::Ordinal ordinal,
					   bool bundleLoader, bool indirectDylib)
{
	switch ( opts.architecture() ) {
#if SUPPORT_ARCH_x86_64
		case CPU_TYPE_X86_64:
			if ( Parser<x86_64>::validFile(fileContent, fileLength, path, opts.architectureName()) )
				return Parser<x86_64>::parse(fileContent, fileLength, path, modTime, ordinal, opts, indirectDylib);
			break;
#endif
#if SUPPORT_ARCH_i386
		case CPU_TYPE_I386:
			if ( Parser<x86>::validFile(fileContent, fileLength, path, opts.architectureName()) )
				return Parser<x86>::parse(fileContent, fileLength, path, modTime, ordinal, opts, indirectDylib);
			break;
#endif
#if SUPPORT_ARCH_arm_any
		case CPU_TYPE_ARM:
			if ( Parser<arm>::validFile(fileContent, fileLength, path, opts.architectureName()) )
				return Parser<arm>::parse(fileContent, fileLength, path, modTime, ordinal, opts, indirectDylib);
			break;
#endif
#if SUPPORT_ARCH_arm64
		case CPU_TYPE_ARM64:
			if ( Parser<arm64>::validFile(fileContent, fileLength, path, opts.architectureName()) )
				return Parser<arm64>::parse(fileContent, fileLength, path, modTime, ordinal, opts, indirectDylib);
			break;
#endif
	}
	return nullptr;
}
	
	
} // namespace dylib
} // namespace textstub


