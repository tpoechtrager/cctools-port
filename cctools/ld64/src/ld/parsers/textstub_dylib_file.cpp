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
#include <tapi/tapi.h>
#include <vector>
#include <memory> // ld64-port
#include <functional> // ld64-port

#include "Architectures.hpp"
#include "Bitcode.hpp" // ld64-port: "bitcode.hpp" -> "Bitcode.hpp"
#include "MachOFileAbstraction.hpp"
#include "MachOTrie.hpp"
#include "generic_dylib_file.hpp"
#include "textstub_dylib_file.hpp"


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
					File(const char* path, const uint8_t* fileContent, uint64_t fileLength,
						 time_t mTime, ld::File::Ordinal ordinal, bool linkingFlatNamespace,
						 bool linkingMainExecutable, bool hoistImplicitPublicDylibs,
						 Options::Platform platform, uint32_t linkMinOSVersion, bool allowWeakImports,
						 cpu_type_t cpuType, cpu_subtype_t cpuSubType, bool enforceDylibSubtypesMatch,
						 bool allowSimToMacOSX, bool addVers, bool buildingForSimulator,
						 bool logAllFiles, const char* installPath, bool indirectDylib);
	virtual			~File() noexcept {}

private:
	void			buildExportHashTable(const tapi::LinkerInterfaceFile* file);
};

static ld::File::ObjcConstraint mapObjCConstraint(tapi::ObjCConstraint constraint) {
	switch (constraint) {
	case tapi::ObjCConstraint::None:
		return ld::File::objcConstraintNone;
	case tapi::ObjCConstraint::Retain_Release:
		return ld::File::objcConstraintRetainRelease;
	case tapi::ObjCConstraint::Retain_Release_For_Simulator:
		return ld::File::objcConstraintRetainReleaseForSimulator;
	case tapi::ObjCConstraint::Retain_Release_Or_GC:
		return ld::File::objcConstraintRetainReleaseOrGC;
	case tapi::ObjCConstraint::GC:
		return ld::File::objcConstraintGC;
	}

	return ld::File::objcConstraintNone;
}

static Options::Platform mapPlatform(tapi::Platform platform) {
	switch (platform) {
	case tapi::Platform::Unknown:
		return Options::kPlatformUnknown;
	case tapi::Platform::OSX:
		return Options::kPlatformOSX;
	case tapi::Platform::iOS:
		return Options::kPlatformiOS;
	case tapi::Platform::watchOS:
		return Options::kPlatformWatchOS;
	case tapi::Platform::tvOS:
		return Options::kPlatform_tvOS;
	}

	return Options::kPlatformUnknown;
}

template <typename A>
	File<A>::File(const char* path, const uint8_t* fileContent, uint64_t fileLength,
			  time_t mTime, ld::File::Ordinal ord, bool linkingFlatNamespace,
			  bool linkingMainExecutable, bool hoistImplicitPublicDylibs, Options::Platform platform,
			  uint32_t linkMinOSVersion, bool allowWeakImports, cpu_type_t cpuType, cpu_subtype_t cpuSubType,
				bool enforceDylibSubtypesMatch, bool allowSimToMacOSX, bool addVers,
			  bool buildingForSimulator, bool logAllFiles, const char* targetInstallPath,
			  bool indirectDylib)
	: Base(strdup(path), mTime, ord, platform, linkMinOSVersion, allowWeakImports, linkingFlatNamespace,
		   hoistImplicitPublicDylibs, allowSimToMacOSX, addVers)
{
	auto matchingType = enforceDylibSubtypesMatch ?
			tapi::CpuSubTypeMatching::Exact : tapi::CpuSubTypeMatching::ABI_Compatible;

	std::string errorMessage;
	auto file = std::unique_ptr<tapi::LinkerInterfaceFile>(
		tapi::LinkerInterfaceFile::create(path, fileContent, fileLength, cpuType,
										  cpuSubType, matchingType,
										  tapi::PackedVersion32(linkMinOSVersion), errorMessage));

	if (file == nullptr)
		throw strdup(errorMessage.c_str());

	// unmap file - it is no longer needed.
	munmap((caddr_t)fileContent, fileLength);

	// write out path for -t option
	if ( logAllFiles )
		printf("%s\n", path);

	this->_bitcode = std::unique_ptr<ld::Bitcode>(new ld::Bitcode(nullptr, 0));
	this->_noRexports = !file->hasReexportedLibraries();
	this->_hasWeakExports = file->hasWeakDefinedExports();
	this->_dylibInstallPath = strdup(file->getInstallName().c_str());
	this->_installPathOverride = file->isInstallNameVersionSpecific();
	this->_dylibCurrentVersion = file->getCurrentVersion();
	this->_dylibCompatibilityVersion = file->getCompatibilityVersion();
	this->_swiftVersion = file->getSwiftVersion();
	this->_objcConstraint = mapObjCConstraint(file->getObjCConstraint());
	this->_parentUmbrella = file->getParentFrameworkName().empty() ? nullptr : strdup(file->getParentFrameworkName().c_str());
	this->_appExtensionSafe = file->isApplicationExtensionSafe();

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

	for (auto &client : file->allowableClients())
		this->_allowableClients.push_back(strdup(client.c_str()));

	// <rdar://problem/20659505> [TAPI] Don't hoist "public" (in /usr/lib/) dylibs that should not be directly linked
	this->_hasPublicInstallName = file->hasAllowableClients() ? false : this->isPublicLocation(file->getInstallName().c_str());

	for (const auto &client : file->allowableClients())
		this->_allowableClients.emplace_back(strdup(client.c_str()));

	auto dylibPlatform = mapPlatform(file->getPlatform());
	if ( (dylibPlatform != platform) && (platform != Options::kPlatformUnknown) ) {
		this->_wrongOS = true;
		if ( this->_addVersionLoadCommand && !indirectDylib ) {
			if ( buildingForSimulator ) {
				if ( !this->_allowSimToMacOSXLinking )
					throwf("building for %s simulator, but linking against dylib built for %s (%s).",
							Options::platformName(platform), Options::platformName(dylibPlatform), path);
			} else {
				throwf("building for %s, but linking against dylib built for %s (%s).",
						Options::platformName(platform), Options::platformName(dylibPlatform), path);
			}
		}
	}

	for (const auto& reexport : file->reexportedLibraries()) {
		const char *path = strdup(reexport.c_str());
		if ( (targetInstallPath == nullptr) || (strcmp(targetInstallPath, path) != 0) )
			this->_dependentDylibs.emplace_back(path, true);
	}

	for (const auto& symbol : file->ignoreExports())
		this->_ignoreExports.insert(strdup(symbol.c_str()));

	// if linking flat and this is a flat dylib, create one atom that references all imported symbols.
	if ( linkingFlatNamespace && linkingMainExecutable && (file->hasTwoLevelNamespace() == false) ) {
		std::vector<const char*> importNames;
		importNames.reserve(file->undefineds().size());
		// We do not need to strdup the name, because that will be done by the
		// ImportAtom constructor.
		for (const auto &sym : file->undefineds())
			importNames.emplace_back(sym.getName().c_str());
		this->_importAtom = new generic::dylib::ImportAtom<A>(*this, importNames);
	}

	// build hash table
	buildExportHashTable(file.get());
}

template <typename A>
void File<A>::buildExportHashTable(const tapi::LinkerInterfaceFile* file) {
	if (this->_s_logHashtable )
		fprintf(stderr, "ld: building hashtable from text-stub info in %s\n", this->path());

	for (const auto &sym : file->exports()) {
		const char* name = sym.getName().c_str();
		bool weakDef = sym.isWeakDefined();
		bool tlv = sym.isThreadLocalValue();

		typename Base::AtomAndWeak bucket = { nullptr, weakDef, tlv, 0 };
		if ( this->_s_logHashtable )
			fprintf(stderr, "  adding %s to hash table for %s\n", name, this->path());
		this->_atoms[strdup(name)] = bucket;
	}
}

template <typename A>
class Parser
{
public:
	using P = typename A::P;

	static ld::dylib::File*	parse(const char* path, const uint8_t* fileContent,
								  uint64_t fileLength, time_t mTime,
								  ld::File::Ordinal ordinal, const Options& opts,
								  bool indirectDylib)
	{
		return new File<A>(path, fileContent, fileLength,mTime, ordinal,
						   opts.flatNamespace(),
						   opts.linkingMainExecutable(),
						   opts.implicitlyLinkIndirectPublicDylibs(),
						   opts.platform(),
						   opts.minOSversion(),
						   opts.allowWeakImports(),
						   opts.architecture(),
						   opts.subArchitecture(),
						   opts.enforceDylibSubtypesMatch(),
						   opts.allowSimulatorToLinkWithMacOSX(),
						   opts.addVersionLoadCommand(),
						   opts.targetIOSSimulator(),
						   opts.logAllFiles(),
						   opts.installPath(),
						   indirectDylib);
	}
};

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
		if (tapi::LinkerInterfaceFile::isSupported(path, fileContent, fileLength))
			return Parser<x86_64>::parse(path, fileContent, fileLength, modTime, ordinal, opts, indirectDylib);
#endif
#if SUPPORT_ARCH_i386
		case CPU_TYPE_I386:
		if (tapi::LinkerInterfaceFile::isSupported(path, fileContent, fileLength))
			return Parser<x86>::parse(path, fileContent, fileLength, modTime, ordinal, opts, indirectDylib);
#endif
#if SUPPORT_ARCH_arm_any
		case CPU_TYPE_ARM:
		if (tapi::LinkerInterfaceFile::isSupported(path, fileContent, fileLength))
			return Parser<arm>::parse(path, fileContent, fileLength, modTime, ordinal, opts, indirectDylib);
#endif
#if SUPPORT_ARCH_arm64
		case CPU_TYPE_ARM64:
		if (tapi::LinkerInterfaceFile::isSupported(path, fileContent, fileLength))
			return Parser<arm64>::parse(path, fileContent, fileLength, modTime, ordinal, opts, indirectDylib);
#endif
	}
	return nullptr;
}
	
	
} // namespace dylib
} // namespace textstub
