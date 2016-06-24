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
#include "generic_dylib_file.hpp"
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

	std::string str() const { return std::string(_p, _size); }

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

	std::vector<std::string> parseArchFlowSequence() {
		std::vector<std::string> availabledArchitectures;
		expectToken("archs");
		parseFlowSequence([&](Token name) {
			availabledArchitectures.emplace_back(name.str());
		});
		return availabledArchitectures;
	}

	bool parseArchFlowSequence(std::string &selectedArchName) {
		auto availabledArchitectures = parseArchFlowSequence();

		for (const auto &archName : availabledArchitectures) {
			if (archName == selectedArchName)
				return true;
		}

		return false;
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

	uint32_t parseVersionNumber32(Token token) {
		if ( token.size() >= 128 )
			throwf("malformed version number");

		// Make a null-terminated string.
		char buffer[128];
		::memcpy(buffer, token.data(), token.size());
		buffer[token.size()] = '\0';

		return Options::parseVersionNumber32(buffer);
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
	void parseExportsBlock(DynamicLibrary& lib, std::string &selectedArchName) {
		if ( !hasOptionalToken("exports") )
			return;

		if ( !hasOptionalToken("-") )
			return;

		while ( true ) {
			if ( !parseArchFlowSequence(selectedArchName) ) {
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

	std::vector<std::string> getCompatibleArchList(std::string &requestedArchName) {
		if (requestedArchName == "i386")
			return {"i386"};
		else if (requestedArchName == "x86_64" || requestedArchName == "x86_64h")
			return {"x86_64", "x86_64h"};
		else if (requestedArchName == "armv7" || requestedArchName == "armv7s")
			return {"armv7", "armv7s"};
		else if (requestedArchName == "armv7k")
			return {"armv7k"};
		else if (requestedArchName == "arm64")
			return {"arm64"};
		else
			return {};
	}

	std::string parseAndSelectArchitecture(std::string &requestedArchName) {
		auto availabledArchitectures = parseArchFlowSequence();

		// First try to find an exact match (cpu type and sub-cpu type).
		if (std::find(availabledArchitectures.begin(), availabledArchitectures.end(), requestedArchName)
			!= availabledArchitectures.end())
			return requestedArchName;

		// If there is no exact match, then try to find an ABI compatible slice.
		auto compatibleArchitectures = getCompatibleArchList(requestedArchName);
		std::vector<std::string> result;
		std::sort(availabledArchitectures.begin(), availabledArchitectures.end());
		std::sort(compatibleArchitectures.begin(), compatibleArchitectures.end());
		std::set_intersection(availabledArchitectures.begin(), availabledArchitectures.end(),
							  compatibleArchitectures.begin(), compatibleArchitectures.end(),
							  std::back_inserter(result));

		if (result.empty())
			return std::string();
		else
			return result.front();
	}

	void parseDocument(DynamicLibrary& lib, std::string &requestedArchName) {
		auto selectedArchName = parseAndSelectArchitecture(requestedArchName);
		if (selectedArchName.empty())
			throwf("invalid arch");

		parsePlatform(lib);
		parseInstallName(lib);
		parseCurrentVersion(lib);
		parseCompatibilityVersion(lib);
		parseSwiftVersion(lib);
		parseObjCConstraint(lib);
		parseExportsBlock(lib, selectedArchName);
	}

public:
	TBDFile(const char* data, uint64_t size) : _tokenizer(data, size) {}

	DynamicLibrary parseFileForArch(std::string requestedArchName) {
		_tokenizer.reset();
		DynamicLibrary lib;
		expectToken("---");
		parseDocument(lib, requestedArchName);
		expectToken("...");
		return lib;
	}

	bool validForArch(std::string requestedArchName) {
		_tokenizer.reset();
		auto token = next();
		if ( token != "---" )
			return false;
		return !parseAndSelectArchitecture(requestedArchName).empty();
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

//
// The reader for a dylib extracts all exported symbols names from the memory-mapped
// dylib, builds a hash table, then unmaps the file.  This is an important memory
// savings for large dylibs.
//
template <typename A>
class File final : public generic::dylib::File<A>
{
	using Base = generic::dylib::File<A>;

public:
	static bool		validFile(const uint8_t* fileContent, bool executableOrDylib);
					File(const uint8_t* fileContent, uint64_t fileLength, const char* path,
						 time_t mTime, ld::File::Ordinal ordinal, bool linkingFlatNamespace,
						 bool hoistImplicitPublicDylibs, Options::Platform platform,
						 cpu_type_t cpuType, const char* archName, uint32_t linkMinOSVersion,
						 bool allowSimToMacOSX, bool addVers, bool buildingForSimulator,
						 bool logAllFiles, const char* installPath, bool indirectDylib);
	virtual			~File() noexcept {}

private:
	void			buildExportHashTable(const DynamicLibrary &lib);

	cpu_type_t		_cpuType;
};

template <typename A>
File<A>::File(const uint8_t* fileContent, uint64_t fileLength, const char* path, time_t mTime,
			  ld::File::Ordinal ord, bool linkingFlatNamespace, bool hoistImplicitPublicDylibs,
			  Options::Platform platform, cpu_type_t cpuType, const char* archName,
			  uint32_t linkMinOSVersion, bool allowSimToMacOSX, bool addVers,
			  bool buildingForSimulator, bool logAllFiles, const char* targetInstallPath,
			  bool indirectDylib)
	: Base(strdup(path), mTime, ord, platform, linkMinOSVersion, linkingFlatNamespace,
		   hoistImplicitPublicDylibs, allowSimToMacOSX, addVers),
	  _cpuType(cpuType)
{
	this->_bitcode = std::unique_ptr<ld::Bitcode>(new ld::Bitcode(nullptr, 0));
	// Text stubs are implicit app extension safe.
	this->_appExtensionSafe = true;

	// write out path for -t option
	if ( logAllFiles )
		printf("%s\n", path);

	TBDFile stub((const char*)fileContent, fileLength);
	auto lib = stub.parseFileForArch(archName);

	this->_noRexports = lib._reexportedLibraries.empty();
	this->_hasWeakExports = !lib._weakDefSymbols.empty();
	this->_dylibInstallPath = strdup(lib._installName.str().c_str());
	this->_dylibCurrentVersion = lib._currentVersion;
	this->_dylibCompatibilityVersion = lib._compatibilityVersion;
	this->_swiftVersion = lib._swiftVersion;
	this->_objcConstraint = lib._objcConstraint;
	this->_hasPublicInstallName = this->isPublicLocation(this->_dylibInstallPath);

	// if framework, capture framework name
	const char* lastSlash = strrchr(this->_dylibInstallPath, '/');
	if ( lastSlash != NULL ) {
		const char* leafName = lastSlash+1;
		char frname[strlen(leafName)+32];
		strcpy(frname, leafName);
		strcat(frname, ".framework/");

		if ( strstr(this->_dylibInstallPath, frname) != NULL )
			this->_frameworkName = leafName;
	}

  // TEMPORARY HACK BEGIN: Support ancient re-export command LC_SUB_FRAMEWORK.
	// <rdar://problem/23614899> [TAPI] Support LC_SUB_FRAMEWORK as re-export indicator.
	auto installName = std::string(this->_dylibInstallPath);

	// All sub-frameworks of ApplicationServices use LC_SUB_FRAMEWORK.
	if (installName.find("/System/Library/Frameworks/ApplicationServices.framework/Versions/A/Frameworks/") == 0 &&
			installName.find(".dylib") == std::string::npos) {
		this->_parentUmbrella = "ApplicationServices";
	} else if (installName.find("/System/Library/Frameworks/Carbon.framework/Versions/A/Frameworks/") == 0) {
		this->_parentUmbrella = "Carbon";
	} else if (installName.find("/System/Library/Frameworks/CoreServices.framework/Versions/A/Frameworks/") == 0 &&
					 installName.find(".dylib") == std::string::npos) {
		this->_parentUmbrella = "CoreServices";
	} else if (installName.find("/System/Library/Frameworks/Accelerate.framework/Versions/A/Frameworks/vecLib.framework/Versions/A/libLinearAlgebra.dylib") == 0 ||
					 installName.find("/System/Library/Frameworks/Accelerate.framework/Versions/A/Frameworks/vecLib.framework/Versions/A/libQuadrature.dylib") == 0 ||
					 installName.find("System/Library/Frameworks/Accelerate.framework/Versions/A/Frameworks/vecLib.framework/Versions/A/libSparseBLAS.dylib") == 0) {
		this->_parentUmbrella = "vecLib";
	} else if (installName.find("/System/Library/Frameworks/WebKit.framework/Versions/A/Frameworks/WebCore.framework/Versions/A/WebCore") == 0) {
		this->_parentUmbrella = "WebKit";
	} else if (installName.find("/usr/lib/system/") == 0 &&
			   installName != "/usr/lib/system/libkxld.dylib") {
		this->_parentUmbrella = "System";
	}
	// TEMPORARY HACK END

	for (auto &client : lib._allowedClients) {
		if ((this->_parentUmbrella != nullptr) && (client.str() != this->_parentUmbrella))
			this->_allowableClients.push_back(strdup(client.str().c_str()));
	}

	// <rdar://problem/20659505> [TAPI] Don't hoist "public" (in /usr/lib/) dylibs that should not be directly linked
	if ( !this->_allowableClients.empty() )
		this->_hasPublicInstallName = false;

	if ( (lib._platform != platform) && (platform != Options::kPlatformUnknown) ) {
		this->_wrongOS = true;
		if ( this->_addVersionLoadCommand && !indirectDylib ) {
			if ( buildingForSimulator ) {
				if ( !this->_allowSimToMacOSXLinking )
					throwf("building for %s simulator, but linking against dylib built for %s (%s).",
							Options::platformName(platform), Options::platformName(lib._platform), path);
			} else {
				throwf("building for %s, but linking against dylib built for %s (%s).",
						Options::platformName(platform), Options::platformName(lib._platform), path);
			}
		}
	}

	this->_dependentDylibs.reserve(lib._reexportedLibraries.size());
	for ( const auto& reexport : lib._reexportedLibraries ) {
		const char *path = strdup(reexport.str().c_str());
		if ( (targetInstallPath == nullptr) || (strcmp(targetInstallPath, path) != 0) )
			this->_dependentDylibs.emplace_back(path, true);
	}

	// build hash table
	buildExportHashTable(lib);

	munmap((caddr_t)fileContent, fileLength);
}

template <typename A>
void File<A>::buildExportHashTable(const DynamicLibrary& lib) {
	if (this->_s_logHashtable )
		fprintf(stderr, "ld: building hashtable from text-stub info in %s\n", this->path());

	for (auto &sym : lib._symbols)
		this->addSymbol(sym.str().c_str());

#if SUPPORT_ARCH_i386
	if (this->_platform == Options::kPlatformOSX && _cpuType == CPU_TYPE_I386) {
		for (auto &sym : lib._classes)
			this->addSymbol((".objc_class_name" + sym.str()).c_str());
	} else {
		for (auto &sym : lib._classes) {
			this->addSymbol(("_OBJC_CLASS_$" + sym.str()).c_str());
			this->addSymbol(("_OBJC_METACLASS_$" + sym.str()).c_str());
		}
	}
#else
	for (auto &sym : lib._classes) {
		this->addSymbol(("_OBJC_CLASS_$" + sym.str()).c_str());
		this->addSymbol(("_OBJC_METACLASS_$" + sym.str()).c_str());
	}
#endif

	for (auto &sym : lib._ivars)
		this->addSymbol(("_OBJC_IVAR_$" + sym.str()).c_str());

	for (auto &sym : lib._weakDefSymbols)
		this->addSymbol(sym.str().c_str(), /*weak=*/true);

	for (auto &sym : lib._tlvSymbols)
		this->addSymbol(sym.str().c_str(), /*weak=*/false, /*tlv=*/true);
}

template <typename A>
class Parser
{
public:
	using P = typename A::P;

	static bool				validFile(const uint8_t* fileContent, uint64_t fileLength,
									  const std::string &path, const char* archName);
	static ld::dylib::File*	parse(const uint8_t* fileContent, uint64_t fileLength, const char* path,
								  time_t mTime, ld::File::Ordinal ordinal, const Options& opts,
								  bool indirectDylib)
	{
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
bool Parser<A>::validFile(const uint8_t* fileContent, uint64_t fileLength, const std::string &path,
						  const char* archName)
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
