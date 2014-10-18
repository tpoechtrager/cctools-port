/* -*- mode: C++; c-basic-offset: 4; tab-width: 4 -*-
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


#include <sys/types.h>
#include <sys/stat.h>
#include <mach/vm_prot.h>
#include <sys/sysctl.h>
#include <mach-o/dyld.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <spawn.h>
#include <cxxabi.h>
#include <Availability.h>

#include <vector>

#include "Options.h"
#include "Architectures.hpp"
#include "MachOFileAbstraction.hpp"
#include "Snapshot.h"

// upward dependency on lto::version()
namespace lto {
	extern const char* version();
}

// magic to place command line in crash reports
const int crashreporterBufferSize = 2000;
static char crashreporterBuffer[crashreporterBufferSize];
#if defined(__has_include) && __has_include(<CrashReporterClient.h>)
#define HAVE_CRASHREPORTER_HEADER 1
#else
#define HAVE_CRASHREPORTER_HEADER 0
#endif
#if __MAC_OS_X_VERSION_MIN_REQUIRED >= 1070 && HAVE_CRASHREPORTER_HEADER
	#include <CrashReporterClient.h>
	// hack until ld does not need to build on 10.6 anymore
    struct crashreporter_annotations_t gCRAnnotations 
        __attribute__((section("__DATA," CRASHREPORTER_ANNOTATIONS_SECTION))) 
        = { CRASHREPORTER_ANNOTATIONS_VERSION, 0, 0, 0, 0, 0, 0 };
#else
	extern "C" char* __crashreporter_info__;
	__attribute__((used)) 
	char* __crashreporter_info__ = crashreporterBuffer;
#endif


static bool			sEmitWarnings = true;
static bool			sFatalWarnings = false;
static const char*	sWarningsSideFilePath = NULL;
static FILE*		sWarningsSideFile = NULL;
static int			sWarningsCount = 0;

void warning(const char* format, ...)
{
	++sWarningsCount;
	if ( sEmitWarnings ) {
		va_list	list;
		if ( sWarningsSideFilePath != NULL ) {
			if ( sWarningsSideFile == NULL )
				sWarningsSideFile = fopen(sWarningsSideFilePath, "a");
		}
		va_start(list, format);
		fprintf(stderr, "ld: warning: ");
		vfprintf(stderr, format, list);
		fprintf(stderr, "\n");
		if ( sWarningsSideFile != NULL ) {
			fprintf(sWarningsSideFile, "ld: warning: ");
			vfprintf(sWarningsSideFile, format, list);
			fprintf(sWarningsSideFile, "\n");
			fflush(sWarningsSideFile);
		}
		va_end(list);
	}
}

void throwf(const char* format, ...)
{
	va_list	list;
	char*	p;
	va_start(list, format);
	vasprintf(&p, format, list);
	va_end(list);

	const char*	t = p;
	throw t;
}

bool Options::FileInfo::checkFileExists(const char *p)
{
	struct stat statBuffer;
	if (p == NULL) p = path;
	if ( stat(p, &statBuffer) == 0 ) {
		if (p != path) path = strdup(p);
		fileLen = statBuffer.st_size;
		modTime = statBuffer.st_mtime;
		return true;
	}
    return false;
}

Options::Options(int argc, const char* argv[])
	: fOutputFile("a.out"), fArchitecture(0), fSubArchitecture(0), fArchitectureName("unknown"), fOutputKind(kDynamicExecutable), 
	  fHasPreferredSubType(false), fArchSupportsThumb2(false), fPrebind(false), fBindAtLoad(false), fKeepPrivateExterns(false),
	  fNeedsModuleTable(false), fIgnoreOtherArchFiles(false), fErrorOnOtherArchFiles(false), fForceSubtypeAll(false), 
	  fInterposeMode(kInterposeNone), fDeadStrip(false), fNameSpace(kTwoLevelNameSpace),
	  fDylibCompatVersion(0), fDylibCurrentVersion(0), fDylibInstallName(NULL), fFinalName(NULL), fEntryName("start"), 
	  fBaseAddress(0), fMaxAddress(0x7FFFFFFFFFFFFFFFLL), 
	  fBaseWritableAddress(0), fSplitSegs(false),
	  fExportMode(kExportDefault), fLibrarySearchMode(kSearchDylibAndArchiveInEachDir),
	  fUndefinedTreatment(kUndefinedError), fMessagesPrefixedWithArchitecture(true), 
	  fWeakReferenceMismatchTreatment(kWeakReferenceMismatchNonWeak),
	  fClientName(NULL),
	  fUmbrellaName(NULL), fInitFunctionName(NULL), fDotOutputFile(NULL), fExecutablePath(NULL),
	  fBundleLoader(NULL), fDtraceScriptName(NULL), fSegAddrTablePath(NULL), fMapPath(NULL), 
	  fDyldInstallPath("/usr/lib/dyld"), fTempLtoObjectPath(NULL), fOverridePathlibLTO(NULL), 
	  fZeroPageSize(ULLONG_MAX), fStackSize(0), fStackAddr(0), fSourceVersion(0), fSDKVersion(0), fExecutableStack(false), 
	  fNonExecutableHeap(false), fDisableNonExecutableHeap(false),
	  fMinimumHeaderPad(32), fSegmentAlignment(4096), 
	  fCommonsMode(kCommonsIgnoreDylibs),  fUUIDMode(kUUIDContent), fLocalSymbolHandling(kLocalSymbolsAll), fWarnCommons(false), 
	  fVerbose(false), fKeepRelocations(false), fWarnStabs(false),
	  fTraceDylibSearching(false), fPause(false), fStatistics(false), fPrintOptions(false),
	  fSharedRegionEligible(false), fPrintOrderFileStatistics(false),  
	  fReadOnlyx86Stubs(false), fPositionIndependentExecutable(false), fPIEOnCommandLine(false),
	  fDisablePositionIndependentExecutable(false), fMaxMinimumHeaderPad(false),
	  fDeadStripDylibs(false),  fAllowTextRelocs(false), fWarnTextRelocs(false), fKextsUseStubs(false),
	  fUsingLazyDylibLinking(false), fEncryptable(true), 
	  fOrderData(true), fMarkDeadStrippableDylib(false),
	  fMakeCompressedDyldInfo(true), fMakeCompressedDyldInfoForceOff(false), fNoEHLabels(false),
	  fAllowCpuSubtypeMismatches(false), fUseSimplifiedDylibReExports(false),
	  fObjCABIVersion2Override(false), fObjCABIVersion1Override(false), fCanUseUpwardDylib(false),
	  fFullyLoadArchives(false), fLoadAllObjcObjectsFromArchives(false), fFlatNamespace(false),
	  fLinkingMainExecutable(false), fForFinalLinkedImage(false), fForStatic(false),
	  fForDyld(false), fMakeTentativeDefinitionsReal(false), fWhyLoad(false), fRootSafe(false),
	  fSetuidSafe(false), fImplicitlyLinkPublicDylibs(true), fAddCompactUnwindEncoding(true),
	  fWarnCompactUnwind(false), fRemoveDwarfUnwindIfCompactExists(false),
	  fAutoOrderInitializers(true), fOptimizeZeroFill(true), fMergeZeroFill(false), fLogObjectFiles(false),
	  fLogAllFiles(false), fTraceDylibs(false), fTraceIndirectDylibs(false), fTraceArchives(false),
	  fOutputSlidable(false), fWarnWeakExports(false), 
	  fObjcGcCompaction(false), fObjCGc(false), fObjCGcOnly(false), 
	  fDemangle(false), fTLVSupport(false), 
	  fVersionLoadCommand(false), fVersionLoadCommandForcedOn(false), 
	  fVersionLoadCommandForcedOff(false), fFunctionStartsLoadCommand(false),
	  fFunctionStartsForcedOn(false), fFunctionStartsForcedOff(false),
	  fDataInCodeInfoLoadCommand(false), fDataInCodeInfoLoadCommandForcedOn(false), fDataInCodeInfoLoadCommandForcedOff(false),
	  fCanReExportSymbols(false), fObjcCategoryMerging(true), fPageAlignDataAtoms(false), 
	  fNeedsThreadLoadCommand(false), fEntryPointLoadCommand(false),
	  fEntryPointLoadCommandForceOn(false), fEntryPointLoadCommandForceOff(false),
	  fSourceVersionLoadCommand(false), 
	  fSourceVersionLoadCommandForceOn(false), fSourceVersionLoadCommandForceOff(false), 
	  fDependentDRInfo(false), fDependentDRInfoForcedOn(false), fDependentDRInfoForcedOff(false),
	  fDebugInfoStripping(kDebugInfoMinimal), fTraceOutputFile(NULL), 
	  fMacVersionMin(ld::macVersionUnset), fIOSVersionMin(ld::iOSVersionUnset), 
	  fSaveTempFiles(false), fSnapshotRequested(false), fPipelineFifo(NULL)
{
	this->checkForClassic(argc, argv);
	this->parsePreCommandLineEnvironmentSettings();
	this->parse(argc, argv);
	this->parsePostCommandLineEnvironmentSettings();
	this->reconfigureDefaults();
	this->checkIllegalOptionCombinations();
}

Options::~Options()
{
}

bool Options::errorBecauseOfWarnings() const
{
	return (sFatalWarnings && (sWarningsCount > 0));
}


const char*	Options::installPath() const
{
	if ( fDylibInstallName != NULL )
		return fDylibInstallName;
	else if ( fFinalName != NULL )
		return fFinalName;
	else
		return fOutputFile;
}


bool Options::interposable(const char* name) const
{
	switch ( fInterposeMode ) {
		case kInterposeNone:
			return false;
		case kInterposeAllExternal:
			return true;
		case kInterposeSome:
			return fInterposeList.contains(name);
	}
	throw "internal error";
}


bool Options::printWhyLive(const char* symbolName) const
{
	return ( fWhyLive.find(symbolName) != fWhyLive.end() );
}


const char*	Options::dotOutputFile()
{
	return fDotOutputFile;
}


bool Options::hasWildCardExportRestrictList() const
{
	// has -exported_symbols_list which contains some wildcards
	return ((fExportMode == kExportSome) && fExportSymbols.hasWildCards());
}

bool Options::hasWeakBitTweaks() const
{
	// has -exported_symbols_list which contains some wildcards
	return (!fForceWeakSymbols.empty() || !fForceNotWeakSymbols.empty());
}

bool Options::allGlobalsAreDeadStripRoots() const
{
	// -exported_symbols_list means globals are not exported by default
	if ( fExportMode == kExportSome )
		return false;
	//
	switch ( fOutputKind ) {
		case Options::kDynamicExecutable:
		case Options::kStaticExecutable:
		case Options::kPreload:
			// by default unused globals in a main executable are stripped
			return false;
		case Options::kDynamicLibrary:
		case Options::kDynamicBundle:
		case Options::kObjectFile:
		case Options::kDyld:
		case Options::kKextBundle:
			return true;
	}
	return false;
}


bool Options::keepRelocations()
{
	return fKeepRelocations;
}

bool Options::warnStabs()
{
	return fWarnStabs;
}

const char* Options::executablePath()
{
	return fExecutablePath;
}


uint32_t Options::initialSegProtection(const char* segName) const
{
	for(std::vector<Options::SegmentProtect>::const_iterator it = fCustomSegmentProtections.begin(); it != fCustomSegmentProtections.end(); ++it) {
		if ( strcmp(it->name, segName) == 0 ) {
			return it->init;
		}
	}
	if ( strcmp(segName, "__PAGEZERO") == 0 ) {
		return 0;
	}
	else if ( strcmp(segName, "__TEXT") == 0 ) {
		return VM_PROT_READ | VM_PROT_EXECUTE;
	}
	else if ( strcmp(segName, "__LINKEDIT") == 0 ) {
		return VM_PROT_READ;
	}
	
	// all others default to read-write
	return VM_PROT_READ | VM_PROT_WRITE;
}

uint32_t Options::maxSegProtection(const char* segName) const
{
	// iPhoneOS always uses same protection for max and initial
	// <rdar://problem/11663436> simulator apps need to use MacOSX max-prot
	if ( (fIOSVersionMin != ld::iOSVersionUnset) && (fArchitecture != CPU_TYPE_I386) )
		return initialSegProtection(segName);

	for(std::vector<Options::SegmentProtect>::const_iterator it = fCustomSegmentProtections.begin(); it != fCustomSegmentProtections.end(); ++it) {
		if ( strcmp(it->name, segName) == 0 ) {
			return it->max;
		}
	}
	if ( strcmp(segName, "__PAGEZERO") == 0 ) {
		return 0;
	}
	// all others default to all
	return VM_PROT_READ | VM_PROT_WRITE | VM_PROT_EXECUTE;
}
	
uint64_t Options::segPageSize(const char* segName) const
{
	for(std::vector<SegmentSize>::const_iterator it=fCustomSegmentSizes.begin(); it != fCustomSegmentSizes.end(); ++it) {
		if ( strcmp(it->name, segName) == 0 )
			return it->size;
	}
	return fSegmentAlignment;
}

uint64_t Options::customSegmentAddress(const char* segName) const
{
	for(std::vector<SegmentStart>::const_iterator it=fCustomSegmentAddresses.begin(); it != fCustomSegmentAddresses.end(); ++it) {
		if ( strcmp(it->name, segName) == 0 )
			return it->address;
	}
	// if custom stack in use, model as segment with custom address
	if ( (fStackSize != 0) && (strcmp("__UNIXSTACK", segName) == 0) )
		return fStackAddr - fStackSize;
	return 0;
}

bool Options::hasCustomSegmentAddress(const char* segName) const
{
	for(std::vector<SegmentStart>::const_iterator it=fCustomSegmentAddresses.begin(); it != fCustomSegmentAddresses.end(); ++it) {
		if ( strcmp(it->name, segName) == 0 )
			return true;
	}
	// if custom stack in use, model as segment with custom address
	if ( (fStackSize != 0) && (strcmp("__UNIXSTACK", segName) == 0) )
		return true;
	return false;
}

bool Options::hasCustomSectionAlignment(const char* segName, const char* sectName) const
{
	for (std::vector<SectionAlignment>::const_iterator it =	fSectionAlignments.begin(); it != fSectionAlignments.end(); ++it) {
		if ( (strcmp(it->segmentName, segName) == 0) && (strcmp(it->sectionName, sectName) == 0) )
			return true;
	}
	return false;
}

uint8_t Options::customSectionAlignment(const char* segName, const char* sectName) const
{
	for (std::vector<SectionAlignment>::const_iterator it =	fSectionAlignments.begin(); it != fSectionAlignments.end(); ++it) {
		if ( (strcmp(it->segmentName, segName) == 0) && (strcmp(it->sectionName, sectName) == 0) )
			return it->alignment;
	}
	return 0;
}


bool Options::hasExportedSymbolOrder()
{
	return (fExportSymbolsOrder.size() > 0);
}

bool Options::exportedSymbolOrder(const char* sym, unsigned int* order) const
{
	NameToOrder::const_iterator pos = fExportSymbolsOrder.find(sym);
	if ( pos != fExportSymbolsOrder.end() ) {
		*order = pos->second;
		return true;
	}
	else {
		*order = 0xFFFFFFFF;
		return false;
	}
}

void Options::loadSymbolOrderFile(const char* fileOfExports, NameToOrder& orderMapping)
{
	// read in whole file
	int fd = ::open(fileOfExports, O_RDONLY, 0);
	if ( fd == -1 )
		throwf("can't open -exported_symbols_order file: %s", fileOfExports);
	struct stat stat_buf;
	::fstat(fd, &stat_buf);
	char* p = (char*)malloc(stat_buf.st_size);
	if ( p == NULL )
		throwf("can't process -exported_symbols_order file: %s", fileOfExports);

	if ( read(fd, p, stat_buf.st_size) != stat_buf.st_size )
		throwf("can't read -exported_symbols_order file: %s", fileOfExports);

	::close(fd);

	// parse into symbols and add to hash_set
	unsigned int count = 0;
	char * const end = &p[stat_buf.st_size];
	enum { lineStart, inSymbol, inComment } state = lineStart;
	char* symbolStart = NULL;
	for (char* s = p; s < end; ++s ) {
		switch ( state ) {
		case lineStart:
			if ( *s =='#' ) {
				state = inComment;
			}
			else if ( !isspace(*s) ) {
				state = inSymbol;
				symbolStart = s;
			}
			break;
		case inSymbol:
			if ( (*s == '\n') || (*s == '\r') ) {
				*s = '\0';
				// removing any trailing spaces
				char* last = s-1;
				while ( isspace(*last) ) {
					*last = '\0';
					--last;
				}
				orderMapping[symbolStart] = ++count;
				symbolStart = NULL;
				state = lineStart;
			}
			break;
		case inComment:
			if ( (*s == '\n') || (*s == '\r') )
				state = lineStart;
			break;
		}
	}
	if ( state == inSymbol ) {
		warning("missing line-end at end of file \"%s\"", fileOfExports);
		int len = end-symbolStart+1;
		char* temp = new char[len];
		strlcpy(temp, symbolStart, len);

		// remove any trailing spaces
		char* last = &temp[len-2];
		while ( isspace(*last) ) {
			*last = '\0';
			--last;
		}
		orderMapping[temp] = ++count;
	}

	// Note: we do not free() the malloc buffer, because the strings are used by the export-set hash table
}

bool Options::forceWeak(const char* symbolName) const
{
	return fForceWeakSymbols.contains(symbolName);
}

bool Options::forceNotWeak(const char* symbolName) const
{
	return fForceNotWeakSymbols.contains(symbolName);
}

bool Options::forceWeakNonWildCard(const char* symbolName) const
{
	return fForceWeakSymbols.containsNonWildcard(symbolName);
}

bool Options::forceNotWeakNonWildcard(const char* symbolName) const
{
	return fForceNotWeakSymbols.containsNonWildcard(symbolName);
}


bool Options::shouldExport(const char* symbolName) const
{
	switch (fExportMode) {
		case kExportSome:
			return fExportSymbols.contains(symbolName);
		case kDontExportSome:
			return ! fDontExportSymbols.contains(symbolName);
		case kExportDefault:
			return true;
	}
	throw "internal error";
}

bool Options::shouldReExport(const char* symbolName) const
{
	return fReExportSymbols.contains(symbolName);
}

bool Options::keepLocalSymbol(const char* symbolName) const
{
	switch (fLocalSymbolHandling) {
		case kLocalSymbolsAll:
			return true;
		case kLocalSymbolsNone:
			return false;
		case kLocalSymbolsSelectiveInclude:
			return fLocalSymbolsIncluded.contains(symbolName);
		case kLocalSymbolsSelectiveExclude:
			return ! fLocalSymbolsExcluded.contains(symbolName);
	}
	throw "internal error";
}

void Options::setArchitecture(cpu_type_t type, cpu_subtype_t subtype)
{
	for (const ArchInfo* t=archInfoArray; t->archName != NULL; ++t) {
		if ( (type == t->cpuType) && (subtype == t->cpuSubType) ) {
			fArchitecture = type;
			fSubArchitecture = subtype;
			fArchitectureName = t->archName;
			fHasPreferredSubType = t->isSubType;
			fArchSupportsThumb2 = t->supportsThumb2;
			switch ( type ) {
				case CPU_TYPE_I386:
				case CPU_TYPE_X86_64:
					if ( (fMacVersionMin == ld::macVersionUnset) && (fIOSVersionMin == ld::iOSVersionUnset) && (fOutputKind != Options::kObjectFile) ) {
				#ifdef DEFAULT_MACOSX_MIN_VERSION
						warning("-macosx_version_min not specified, assuming " DEFAULT_MACOSX_MIN_VERSION);
						setMacOSXVersionMin(DEFAULT_MACOSX_MIN_VERSION);
				#else
						warning("-macosx_version_min not specified, assuming 10.6");
						fMacVersionMin = ld::mac10_6;
				#endif		
					}
					if ( !fMakeCompressedDyldInfo && minOS(ld::mac10_6, ld::iOS_3_1) && !fMakeCompressedDyldInfoForceOff )
						fMakeCompressedDyldInfo = true;
					break;
				case CPU_TYPE_ARM:
					if ( (fMacVersionMin == ld::macVersionUnset) && (fIOSVersionMin == ld::iOSVersionUnset) && (fOutputKind != Options::kObjectFile) ) {
				#if defined(DEFAULT_IPHONEOS_MIN_VERSION)
						warning("-ios_version_min not specified, assuming " DEFAULT_IPHONEOS_MIN_VERSION);
						setIOSVersionMin(DEFAULT_IPHONEOS_MIN_VERSION);
				#elif defined(DEFAULT_MACOSX_MIN_VERSION)
						warning("-macosx_version_min not specified, assuming " DEFAULT_MACOSX_MIN_VERSION);
						setMacOSXVersionMin(DEFAULT_MACOSX_MIN_VERSION);
				#else
						warning("-macosx_version_min not specified, assuming 10.6");
						fMacVersionMin = ld::mac10_6;
				#endif
					}
					if ( !fMakeCompressedDyldInfo && minOS(ld::mac10_6, ld::iOS_3_1) && !fMakeCompressedDyldInfoForceOff )
						fMakeCompressedDyldInfo = true;
					break;
			}
			fLinkSnapshot.recordArch(fArchitectureName);
			return;
		}
	}
	fArchitectureName = "unknown architecture";
}

void Options::parseArch(const char* arch)
{
	if ( arch == NULL )
		throw "-arch must be followed by an architecture string";
	for (const ArchInfo* t=archInfoArray; t->archName != NULL; ++t) {
		if ( strcmp(t->archName,arch) == 0 ) {
			fArchitectureName = arch;
			fArchitecture = t->cpuType;
			fSubArchitecture = t->cpuSubType;
			fHasPreferredSubType = t->isSubType;
			fArchSupportsThumb2 = t->supportsThumb2;
			return;
		}
	}
	throwf("unknown/unsupported architecture name for: -arch %s", arch);
}

bool Options::checkForFile(const char* format, const char* dir, const char* rootName, FileInfo& result) const
{
	char possiblePath[strlen(dir)+strlen(rootName)+strlen(format)+8];
	sprintf(possiblePath, format,  dir, rootName);
	bool found = result.checkFileExists(possiblePath);
	if ( fTraceDylibSearching )
		printf("[Logging for XBS]%sfound library: '%s'\n", (found ? " " : " not "), possiblePath);
	return found;
}


Options::FileInfo Options::findLibrary(const char* rootName, bool dylibsOnly)
{
	FileInfo result;
	const int rootNameLen = strlen(rootName);
	// if rootName ends in .o there is no .a vs .dylib choice
	if ( (rootNameLen > 3) && (strcmp(&rootName[rootNameLen-2], ".o") == 0) ) {
		for (std::vector<const char*>::iterator it = fLibrarySearchPaths.begin();
			 it != fLibrarySearchPaths.end();
			 it++) {
			const char* dir = *it;
			if ( checkForFile("%s/%s", dir, rootName, result) )
				return result;
		}
	}
	else {
		bool lookForDylibs = ( fOutputKind != Options::kDyld);
		switch ( fLibrarySearchMode ) {
		case kSearchAllDirsForDylibsThenAllDirsForArchives:
				// first look in all directories for just for dylibs
				if ( lookForDylibs ) {
					for (std::vector<const char*>::iterator it = fLibrarySearchPaths.begin();
						 it != fLibrarySearchPaths.end();
						 it++) {
						const char* dir = *it;
						if ( checkForFile("%s/lib%s.dylib", dir, rootName, result) )
							return result;
					}
					for (std::vector<const char*>::iterator it = fLibrarySearchPaths.begin();
						 it != fLibrarySearchPaths.end();
						 it++) {
						const char* dir = *it;
						if ( checkForFile("%s/lib%s.so", dir, rootName, result) )
							return result;
					}
				}
				// next look in all directories for just for archives
				if ( !dylibsOnly ) {
					for (std::vector<const char*>::iterator it = fLibrarySearchPaths.begin();
						 it != fLibrarySearchPaths.end();
						 it++) {
						const char* dir = *it;
						if ( checkForFile("%s/lib%s.a", dir, rootName, result) )
							return result;
					}
				}
				break;

			case kSearchDylibAndArchiveInEachDir:
				// look in each directory for just for a dylib then for an archive
				for (std::vector<const char*>::iterator it = fLibrarySearchPaths.begin();
					 it != fLibrarySearchPaths.end();
					 it++) {
					const char* dir = *it;
					if ( lookForDylibs && checkForFile("%s/lib%s.dylib", dir, rootName, result) )
						return result;
					if ( lookForDylibs && checkForFile("%s/lib%s.so", dir, rootName, result) )
						return result;
					if ( !dylibsOnly && checkForFile("%s/lib%s.a", dir, rootName, result) )
						return result;
				}
				break;
		}
	}
	throwf("library not found for -l%s", rootName);
}

Options::FileInfo Options::findFramework(const char* frameworkName)
{
	if ( frameworkName == NULL )
		throw "-framework missing next argument";
	char temp[strlen(frameworkName)+1];
	strcpy(temp, frameworkName);
	const char* name = temp;
	const char* suffix = NULL;
	char* comma = strchr(temp, ',');
	if ( comma != NULL ) {
		*comma = '\0';
		suffix = &comma[1];
	}
	return findFramework(name, suffix);
}

Options::FileInfo Options::findFramework(const char* rootName, const char* suffix)
{
	for (std::vector<const char*>::iterator it = fFrameworkSearchPaths.begin();
		 it != fFrameworkSearchPaths.end();
		 it++) {
		// ??? Shouldn't we be using String here and just initializing it?
		// ??? Use str.c_str () to pull out the string for the stat call.
		const char* dir = *it;
		char possiblePath[PATH_MAX];
		strcpy(possiblePath, dir);
		strcat(possiblePath, "/");
		strcat(possiblePath, rootName);
		strcat(possiblePath, ".framework/");
		strcat(possiblePath, rootName);
		if ( suffix != NULL ) {
			char realPath[PATH_MAX];
			// no symlink in framework to suffix variants, so follow main symlink
			if ( realpath(possiblePath, realPath) != NULL ) {
				strcpy(possiblePath, realPath);
				strcat(possiblePath, suffix);
			}
		}
        FileInfo result;
		bool found = result.checkFileExists(possiblePath);
		if ( fTraceDylibSearching )
			printf("[Logging for XBS]%sfound framework: '%s'\n",
				   (found ? " " : " not "), possiblePath);
		if ( found ) {
			return result;
		}
	}
	// try without suffix
	if ( suffix != NULL )
		return findFramework(rootName, NULL);
	else
		throwf("framework not found %s", rootName);
}

Options::FileInfo Options::findFile(const char* path) const
{
	FileInfo result;

	// if absolute path and not a .o file, the use SDK prefix
	if ( (path[0] == '/') && (strcmp(&path[strlen(path)-2], ".o") != 0) ) {
		const int pathLen = strlen(path);
		for (std::vector<const char*>::const_iterator it = fSDKPaths.begin(); it != fSDKPaths.end(); it++) {
			// ??? Shouldn't we be using String here?
			const char* sdkPathDir = *it;
			const int sdkPathDirLen = strlen(sdkPathDir);
			char possiblePath[sdkPathDirLen+pathLen+4];
			strcpy(possiblePath, sdkPathDir);
			if ( possiblePath[sdkPathDirLen-1] == '/' )
				possiblePath[sdkPathDirLen-1] = '\0';
			strcat(possiblePath, path);
			if ( result.checkFileExists(possiblePath) ) {
				return result;
			}
		}
	}
	// try raw path
	if ( result.checkFileExists(path) ) {
		return result;
	}

	// try @executable_path substitution
	if ( (strncmp(path, "@executable_path/", 17) == 0) && (fExecutablePath != NULL) ) {
		char newPath[strlen(fExecutablePath) + strlen(path)];
		strcpy(newPath, fExecutablePath);
		char* addPoint = strrchr(newPath,'/');
		if ( addPoint != NULL )
			strcpy(&addPoint[1], &path[17]);
		else
			strcpy(newPath, &path[17]);
		if ( result.checkFileExists(newPath) ) {
			return result;
		}
	}

	// not found
	throwf("file not found: %s", path);
}

Options::FileInfo Options::findFileUsingPaths(const char* path) const 
{
	FileInfo result;

	const char* lastSlash = strrchr(path, '/');
	const char* leafName = (lastSlash == NULL) ? path : &lastSlash[1];

	// Is this in a framework?
	// /path/Foo.framework/Foo							==> true (Foo)
	// /path/Foo.framework/Frameworks/Bar.framework/Bar ==> true (Bar)
	// /path/Foo.framework/Resources/Bar				==> false
	bool isFramework = false;
	if ( lastSlash != NULL ) {
		char frameworkDir[strlen(leafName) + 20];
		strcpy(frameworkDir, "/");
		strcat(frameworkDir, leafName);
		strcat(frameworkDir, ".framework/");
		if ( strstr(path, frameworkDir) != NULL )
			isFramework = true;
	}
	
	// These are abbreviated versions of the routines findFramework and findLibrary above
	// because we already know the final name of the file that we're looking for and so
	// don't need to try variations, just paths. We do need to add the additional bits
	// onto the framework path though.
	if ( isFramework ) {
		for (std::vector<const char*>::const_iterator it = fFrameworkSearchPaths.begin();
			 it != fFrameworkSearchPaths.end();
			 it++) {
			const char* dir = *it;
			char possiblePath[PATH_MAX];
			strcpy(possiblePath, dir);
			strcat(possiblePath, "/");
			strcat(possiblePath, leafName);
			strcat(possiblePath, ".framework");

			//fprintf(stderr,"Finding Framework: %s/%s, leafName=%s\n", possiblePath, leafName, leafName);
			if ( checkForFile("%s/%s", possiblePath, leafName, result) )
				return result;
		}
	} 
	else {
		// if this is a .dylib inside a framework, do not search -L paths
		// <rdar://problem/5427952> ld64's re-export cycle detection logic prevents use of X11 libGL on Leopard 
		int leafLen = strlen(leafName);
		bool embeddedDylib = ( (leafLen > 6) 
					&& (strcmp(&leafName[leafLen-6], ".dylib") == 0) 
					&& (strstr(path, ".framework/") != NULL) );
		if ( !embeddedDylib ) {
			for (std::vector<const char*>::const_iterator it = fLibrarySearchPaths.begin();
				 it != fLibrarySearchPaths.end();
				 it++) {
				const char* dir = *it;
				//fprintf(stderr,"Finding Library: %s/%s\n", dir, leafName);
				if ( checkForFile("%s/%s", dir, leafName, result) )
					return result;
			}
		}
	}

	// If we didn't find it fall back to findFile.
	return findFile(path);
}
 

void Options::parseSegAddrTable(const char* segAddrPath, const char* installPth)
{
	FILE* file = fopen(segAddrPath, "r");
	if ( file == NULL ) {
		warning("-seg_addr_table file cannot be read: %s", segAddrPath);
		return;
	}
	
	char path[PATH_MAX];
	uint64_t firstColumAddress = 0;
	uint64_t secondColumAddress = 0;
	bool hasSecondColumn = false;
	while ( fgets(path, PATH_MAX, file) != NULL ) {
		path[PATH_MAX-1] = '\0';
		char* eol = strchr(path, '\n');
		if ( eol != NULL )
			*eol = '\0';
		// ignore lines not starting with 0x number
		if ( (path[0] == '0') && (path[1] == 'x') ) {
			char* p;
			firstColumAddress = strtoull(path, &p, 16);
			while ( isspace(*p) )
				++p;
			// see if second column is a number
			if ( (p[0] == '0') && (p[1] == 'x') ) {
				secondColumAddress = strtoull(p, &p, 16);
				hasSecondColumn = true;
				while ( isspace(*p) )
					++p;
			}
			while ( isspace(*p) )
				++p;
			if ( p[0] == '/' ) {
				// remove any trailing whitespace
				for(char* end = eol-1; (end > p) && isspace(*end); --end)
					*end = '\0';
				// see if this line is for the dylib being linked
				if ( strcmp(p, installPth) == 0 ) {
					fBaseAddress = firstColumAddress;
					if ( hasSecondColumn ) {
						fBaseWritableAddress = secondColumAddress;
						fSplitSegs = true;
					}
					break; // out of while loop
				}
			}
		}
	}

	fclose(file);
}

void Options::loadFileList(const char* fileOfPaths, ld::File::Ordinal baseOrdinal)
{
	FILE* file;
	const char* comma = strrchr(fileOfPaths, ',');
	const char* prefix = NULL;
	if ( comma != NULL ) {
		// <rdar://problem/5907981> -filelist fails with comma in path
		file = fopen(fileOfPaths, "r");
		if ( file == NULL ) {
			prefix = comma+1;
			int realFileOfPathsLen = comma-fileOfPaths;
			char realFileOfPaths[realFileOfPathsLen+1];
			strncpy(realFileOfPaths,fileOfPaths, realFileOfPathsLen);
			realFileOfPaths[realFileOfPathsLen] = '\0';
			file = fopen(realFileOfPaths, "r");
			if ( file == NULL )
				throwf("-filelist file '%s' could not be opened, errno=%d (%s)\n", realFileOfPaths, errno, strerror(errno));
		}
	}
	else {
		file = fopen(fileOfPaths, "r");
		if ( file == NULL )
			throwf("-filelist file '%s' could not be opened, errno=%d (%s)\n", fileOfPaths, errno, strerror(errno));
	}

	char path[PATH_MAX];
	ld::File::Ordinal previousOrdinal = baseOrdinal;
	while ( fgets(path, PATH_MAX, file) != NULL ) {
		path[PATH_MAX-1] = '\0';
		char* eol = strchr(path, '\n');
		if ( eol != NULL )
			*eol = '\0';
		if ( prefix != NULL ) {
			char builtPath[strlen(prefix)+strlen(path)+2];
			strcpy(builtPath, prefix);
			strcat(builtPath, "/");
			strcat(builtPath, path);
           if (fPipelineFifo != NULL) {
			   FileInfo info = FileInfo(builtPath);
			   info.ordinal = previousOrdinal.nextFileListOrdinal();
			   previousOrdinal = info.ordinal;
			   info.fromFileList = true;
              fInputFiles.push_back(info);
           } else {
			   FileInfo info = findFile(builtPath);
			   info.ordinal = previousOrdinal.nextFileListOrdinal();
			   previousOrdinal = info.ordinal;
			   info.fromFileList = true;
			   fInputFiles.push_back(info);
           }
		}
		else {
           if (fPipelineFifo != NULL) {
			   FileInfo info = FileInfo(path);
			   info.ordinal = previousOrdinal.nextFileListOrdinal();
			   previousOrdinal = info.ordinal;
			   info.fromFileList = true;
			   fInputFiles.push_back(info);
           } else {
			   FileInfo info = findFile(path);
			   info.ordinal = previousOrdinal.nextFileListOrdinal();
			   previousOrdinal = info.ordinal;
			   info.fromFileList = true;
			   fInputFiles.push_back(info);
           }
		}
	}
	fclose(file);
}


void Options::SetWithWildcards::remove(const NameSet& toBeRemoved)
{
	for(NameSet::const_iterator it=toBeRemoved.begin(); it != toBeRemoved.end(); ++it) {
		const char* symbolName = *it;
		NameSet::iterator pos = fRegular.find(symbolName);
		if ( pos != fRegular.end() )
			fRegular.erase(pos);
	}
}

bool Options::SetWithWildcards::hasWildCards(const char* symbol) 
{
	// an exported symbol name containing *, ?, or [ requires wildcard matching
	return ( strpbrk(symbol, "*?[") != NULL );
}

void Options::SetWithWildcards::insert(const char* symbol)
{
	if ( hasWildCards(symbol) )
		fWildCard.push_back(symbol);
	else
		fRegular.insert(symbol);
}

bool Options::SetWithWildcards::contains(const char* symbol) const
{
	// first look at hash table on non-wildcard symbols
	if ( fRegular.find(symbol) != fRegular.end() )
		return true;
	// next walk list of wild card symbols looking for a match
	for(std::vector<const char*>::const_iterator it = fWildCard.begin(); it != fWildCard.end(); ++it) {
		if ( wildCardMatch(*it, symbol) )
			return true;
	}
	return false;
}

bool Options::SetWithWildcards::containsNonWildcard(const char* symbol) const
{
	// look at hash table on non-wildcard symbols
	return ( fRegular.find(symbol) != fRegular.end() );
}



bool Options::SetWithWildcards::inCharRange(const char*& p, unsigned char c) const
{
	++p; // find end
	const char* b = p;
	while ( *p != '\0' ) {
		if ( *p == ']') {
			const char* e = p;
			// found beginining [ and ending ]
			unsigned char last = '\0';
			for ( const char* s = b; s < e; ++s ) {
				if ( *s == '-' ) {
					unsigned char next = *(++s);
					if ( (last <= c) && (c <= next) )
						return true;
					++s;
				}
				else {
					if ( *s == c )
						return true;
					last = *s;
				}
			}
			return false;
		}
		++p;
	}
	return false;
}

bool Options::SetWithWildcards::wildCardMatch(const char* pattern, const char* symbol) const
{
	const char* s = symbol;
	for (const char* p = pattern; *p != '\0'; ++p) {
		switch ( *p ) {
			case '*':
				if ( p[1] == '\0' )
					return true;
				for (const char* t = s; *t != '\0'; ++t) {
					if ( wildCardMatch(&p[1], t) )
						return true;
				}
				return false;
			case '?':
				if ( *s == '\0' )
					return false;
				++s;
				break;
			case '[':
				if ( ! inCharRange(p, *s) )
					return false;
				++s;
				break;
			default:
				if ( *s != *p )
					return false;
				++s;
		}
	}
	return (*s == '\0');
}


void Options::loadExportFile(const char* fileOfExports, const char* option, SetWithWildcards& set)
{
	if ( fileOfExports == NULL )
		throwf("missing file after %s", option);
	// read in whole file
	int fd = ::open(fileOfExports, O_RDONLY, 0);
	if ( fd == -1 )
		throwf("can't open %s file: %s", option, fileOfExports);
	struct stat stat_buf;
	::fstat(fd, &stat_buf);
	char* p = (char*)malloc(stat_buf.st_size);
	if ( p == NULL )
		throwf("can't process %s file: %s", option, fileOfExports);

	if ( read(fd, p, stat_buf.st_size) != stat_buf.st_size )
		throwf("can't read %s file: %s", option, fileOfExports);

	::close(fd);

	// parse into symbols and add to hash_set
	char * const end = &p[stat_buf.st_size];
	enum { lineStart, inSymbol, inComment } state = lineStart;
	char* symbolStart = NULL;
	for (char* s = p; s < end; ++s ) {
		switch ( state ) {
		case lineStart:
			if ( *s =='#' ) {
				state = inComment;
			}
			else if ( !isspace(*s) ) {
				state = inSymbol;
				symbolStart = s;
			}
			break;
		case inSymbol:
			if ( (*s == '\n') || (*s == '\r') ) {
				*s = '\0';
				// removing any trailing spaces
				char* last = s-1;
				while ( isspace(*last) ) {
					*last = '\0';
					--last;
				}
				set.insert(symbolStart);
				symbolStart = NULL;
				state = lineStart;
			}
			break;
		case inComment:
			if ( (*s == '\n') || (*s == '\r') )
				state = lineStart;
			break;
		}
	}
	if ( state == inSymbol ) {
		warning("missing line-end at end of file \"%s\"", fileOfExports);
		int len = end-symbolStart+1;
		char* temp = new char[len];
		strlcpy(temp, symbolStart, len);

		// remove any trailing spaces
		char* last = &temp[len-2];
		while ( isspace(*last) ) {
			*last = '\0';
			--last;
		}
		set.insert(temp);
	}

	// Note: we do not free() the malloc buffer, because the strings are used by the export-set hash table
}

void Options::parseAliasFile(const char* fileOfAliases)
{
	// read in whole file
	int fd = ::open(fileOfAliases, O_RDONLY, 0);
	if ( fd == -1 )
		throwf("can't open alias file: %s", fileOfAliases);
	struct stat stat_buf;
	::fstat(fd, &stat_buf);
	char* p = (char*)malloc(stat_buf.st_size+1);
	if ( p == NULL )
		throwf("can't process alias file: %s", fileOfAliases);

	if ( read(fd, p, stat_buf.st_size) != stat_buf.st_size )
		throwf("can't read alias file: %s", fileOfAliases);
	p[stat_buf.st_size] = '\n';
	::close(fd);

	// parse into symbols and add to fAliases
	AliasPair pair;
	char * const end = &p[stat_buf.st_size+1];
	enum { lineStart, inRealName, inBetween, inAliasName, inComment } state = lineStart;
	int lineNumber = 1;
	for (char* s = p; s < end; ++s ) {
		switch ( state ) {
		case lineStart:
			if ( *s =='#' ) {
				state = inComment;
			}
			else if ( !isspace(*s) ) {
				state = inRealName;
				pair.realName = s;
			}
			break;
		case inRealName:
			if ( *s == '\n' ) {
				warning("line needs two symbols but has only one at line #%d in \"%s\"", lineNumber, fileOfAliases);
				++lineNumber;
				state = lineStart;
			}
			else if ( isspace(*s) ) {
				*s = '\0';
				state = inBetween;
			}
			break;
		case inBetween:
			if ( *s == '\n' ) {
				warning("line needs two symbols but has only one at line #%d in \"%s\"", lineNumber, fileOfAliases);
				++lineNumber;
				state = lineStart;
			}
			else if ( ! isspace(*s) ) {
				state = inAliasName;
				pair.alias = s;
			}
			break;
		case inAliasName:
			if ( *s =='#' ) {
				*s = '\0';
				// removing any trailing spaces
				char* last = s-1;
				while ( isspace(*last) ) {
					*last = '\0';
					--last;
				}
				fAliases.push_back(pair);
				state = inComment;
			}
			else if ( *s == '\n' ) {
				*s = '\0';
				// removing any trailing spaces
				char* last = s-1;
				while ( isspace(*last) ) {
					*last = '\0';
					--last;
				}
				fAliases.push_back(pair);
				state = lineStart;
			}
			break;
		case inComment:
			if ( *s == '\n' )
				state = lineStart;
			break;
		}
	}

	// Note: we do not free() the malloc buffer, because the strings therein are used by fAliases
}



void Options::setUndefinedTreatment(const char* treatment)
{
	if ( treatment == NULL )
		throw "-undefined missing [ warning | error | suppress | dynamic_lookup ]";

	if ( strcmp(treatment, "warning") == 0 )
		fUndefinedTreatment = kUndefinedWarning;
	else if ( strcmp(treatment, "error") == 0 )
		fUndefinedTreatment = kUndefinedError;
	else if ( strcmp(treatment, "suppress") == 0 )
		fUndefinedTreatment = kUndefinedSuppress;
	else if ( strcmp(treatment, "dynamic_lookup") == 0 )
		fUndefinedTreatment = kUndefinedDynamicLookup;
	else
		throw "invalid option to -undefined [ warning | error | suppress | dynamic_lookup ]";
}

Options::Treatment Options::parseTreatment(const char* treatment)
{
	if ( treatment == NULL )
		return kNULL;

	if ( strcmp(treatment, "warning") == 0 )
		return kWarning;
	else if ( strcmp(treatment, "error") == 0 )
		return kError;
	else if ( strcmp(treatment, "suppress") == 0 )
		return kSuppress;
	else
		return kInvalid;
}

void Options::setMacOSXVersionMin(const char* version)
{
	if ( version == NULL )
		throw "-macosx_version_min argument missing";

	if ( (strncmp(version, "10.", 3) == 0) && isdigit(version[3]) ) {
		unsigned int minorVersion = version[3] - '0';
		fMacVersionMin = (ld::MacVersionMin)(0x000A0000 | (minorVersion << 8));
	}
	else {
		warning("unknown option to -macosx_version_min, not 10.x");
	}
}

void Options::setIOSVersionMin(const char* version)
{
	if ( version == NULL )
		throw "-ios_version_min argument missing";
	if ( ! isdigit(version[0]) )
		throw "-ios_version_min argument is not a number";
	if ( version[1] != '.' )
		throw "-ios_version_min argument is missing period as second character";
	if ( ! isdigit(version[2]) )
		throw "-ios_version_min argument is not a number";

	unsigned int majorVersion = version[0] - '0';
	unsigned int minorVersion = version[2] - '0';
	fIOSVersionMin = (ld::IOSVersionMin)((majorVersion << 16) | (minorVersion << 8));
}

bool Options::minOS(ld::MacVersionMin requiredMacMin, ld::IOSVersionMin requirediPhoneOSMin)
{
	if ( fMacVersionMin != ld::macVersionUnset ) {
		return ( fMacVersionMin >= requiredMacMin );
	}
	else {
		return ( fIOSVersionMin >= requirediPhoneOSMin);
	}
}


void Options::setWeakReferenceMismatchTreatment(const char* treatment)
{
	if ( treatment == NULL )
		throw "-weak_reference_mismatches missing [ error | weak | non-weak ]";

	if ( strcmp(treatment, "error") == 0 )
		fWeakReferenceMismatchTreatment = kWeakReferenceMismatchError;
	else if ( strcmp(treatment, "weak") == 0 )
		fWeakReferenceMismatchTreatment = kWeakReferenceMismatchWeak;
	else if ( strcmp(treatment, "non-weak") == 0 )
		fWeakReferenceMismatchTreatment = kWeakReferenceMismatchNonWeak;
	else
		throw "invalid option to -weak_reference_mismatches [ error | weak | non-weak ]";
}

Options::CommonsMode Options::parseCommonsTreatment(const char* mode)
{
	if ( mode == NULL )
		throw "-commons missing [ ignore_dylibs | use_dylibs | error ]";

	if ( strcmp(mode, "ignore_dylibs") == 0 )
		return kCommonsIgnoreDylibs;
	else if ( strcmp(mode, "use_dylibs") == 0 )
		return kCommonsOverriddenByDylibs;
	else if ( strcmp(mode, "error") == 0 )
		return kCommonsConflictsDylibsError;
	else
		throw "invalid option to -commons [ ignore_dylibs | use_dylibs | error ]";
}

void Options::addDylibOverride(const char* paths)
{
	if ( paths == NULL )
		throw "-dylib_file must followed by two colon separated paths";
	const char* colon = strchr(paths, ':');
	if ( colon == NULL )
		throw "-dylib_file must followed by two colon separated paths";
	int len = colon-paths;
	char* target = new char[len+2];
	strncpy(target, paths, len);
	target[len] = '\0';
	DylibOverride entry;
	entry.installName = target;
	entry.useInstead = &colon[1];	
	fDylibOverrides.push_back(entry);
}

uint64_t Options::parseAddress(const char* addr)
{
	char* endptr;
	uint64_t result = strtoull(addr, &endptr, 16);
	return result;
}

uint32_t Options::parseProtection(const char* prot)
{
	uint32_t result = 0;
	for(const char* p = prot; *p != '\0'; ++p) {
		switch(tolower(*p)) {
			case 'r':
				result |= VM_PROT_READ;
				break;
			case 'w':
				result |= VM_PROT_WRITE;
				break;
			case 'x':
				result |= VM_PROT_EXECUTE;
				break;
			case '-':
				break;
			default:
				throwf("unknown -segprot lettter in %s", prot);
		}
	}
	return result;
}


//
// Parses number of form A[.B[.B[.D[.E]]]] into a uint64_t where the bits are a24.b10.c10.d10.e10
//
uint64_t Options::parseVersionNumber64(const char* versionString)
{
	uint64_t a = 0;
	uint64_t b = 0;
	uint64_t c = 0;
	uint64_t d = 0;
	uint64_t e = 0;
	char* end;
	a = strtoul(versionString, &end, 10);
	if ( *end == '.' ) {
		b = strtoul(&end[1], &end, 10);
		if ( *end == '.' ) {
			c = strtoul(&end[1], &end, 10);
			if ( *end == '.' ) {
				d = strtoul(&end[1], &end, 10);
				if ( *end == '.' ) {
					e = strtoul(&end[1], &end, 10);
				}
			}
		}
	}
	if ( (*end != '\0') || (a > 0xFFFFFF) || (b > 0x3FF) || (c > 0x3FF) || (d > 0x3FF)  || (e > 0x3FF) )
		throwf("malformed 64-bit a.b.c.d.e version number: %s", versionString);

	return (a << 40) | ( b << 30 ) | ( c << 20 ) | ( d << 10 ) | e;
}


uint32_t Options::currentVersion32() const
{
	// warn if it does not fit into 32 bit vers number
	uint32_t a = (fDylibCurrentVersion >> 40) & 0xFFFF;
	uint32_t b = (fDylibCurrentVersion >> 30) & 0xFF;
	uint32_t c = (fDylibCurrentVersion >> 20) & 0xFF;
	uint64_t rep32 = ((uint64_t)a << 40) |  ((uint64_t)b << 30) | ((uint64_t)c << 20);
	if ( rep32 != fDylibCurrentVersion ) {
		warning("truncating -current_version to fit in 32-bit space used by old mach-o format");
		a = (fDylibCurrentVersion >> 40) & 0xFFFFFF;		
		if ( a > 0xFFFF )
			a = 0xFFFF;
		b = (fDylibCurrentVersion >> 30) & 0x3FF;
		if ( b > 0xFF )
			b = 0xFF;
		c = (fDylibCurrentVersion >> 20) & 0x3FF;
		if ( c > 0xFF )
			c = 0xFF;
	}
	return (a << 16) | ( b << 8 ) | c;
}

//
// Parses number of form X[.Y[.Z]] into a uint32_t where the nibbles are xxxx.yy.zz
//
uint32_t Options::parseVersionNumber32(const char* versionString)
{
	uint32_t x = 0;
	uint32_t y = 0;
	uint32_t z = 0;
	char* end;
	x = strtoul(versionString, &end, 10);
	if ( *end == '.' ) {
		y = strtoul(&end[1], &end, 10);
		if ( *end == '.' ) {
			z = strtoul(&end[1], &end, 10);
		}
	}
	if ( (*end != '\0') || (x > 0xffff) || (y > 0xff) || (z > 0xff) )
		throwf("malformed 32-bit x.y.z version number: %s", versionString);

	return (x << 16) | ( y << 8 ) | z;
}

static const char* cstringSymbolName(const char* orderFileString)
{
	char* result;
	asprintf(&result, "cstring=%s", orderFileString);
	// convert escaped characters
	char* d = result;
	for(const char* s=result; *s != '\0'; ++s, ++d) {
		if ( *s == '\\' ) {
			++s;
			switch ( *s ) {
				case 'n':
					*d = '\n';
					break;
				case 't':
					*d = '\t';
					break;
				case 'v':
					*d = '\v';
					break;
				case 'b':
					*d = '\b';
					break;
				case 'r':
					*d = '\r';
					break;
				case 'f':
					*d = '\f';
					break;
				case 'a':
					*d = '\a';
					break;
				case '\\':
					*d = '\\';
					break;
				case '?':
					*d = '\?';
					break;
				case '\'':
					*d = '\r';
					break;
				case '\"':
					*d = '\"';
					break;
				case 'x':
					// hexadecimal value of char
					{
						++s;
						char value = 0;
						while ( isxdigit(*s) ) {
							value *= 16;
							if ( isdigit(*s) )
								value += (*s-'0');
							else
								value += ((toupper(*s)-'A') + 10);
							++s;
						}
						*d = value;
					}
					break;
				default:
					if ( isdigit(*s) ) {
						// octal value of char
						char value = 0;
						while ( isdigit(*s) ) {
							value = (value << 3) + (*s-'0');
							++s;
						}
						*d = value;
					}
			}
		}
		else {
			*d = *s;
		}
	}
	*d = '\0';
	return result;
}

void Options::parseOrderFile(const char* path, bool cstring)
{
	// order files override auto-ordering
	fAutoOrderInitializers = false;

	// read in whole file
	int fd = ::open(path, O_RDONLY, 0);
	if ( fd == -1 )
		throwf("can't open order file: %s", path);
	struct stat stat_buf;
	::fstat(fd, &stat_buf);
	char* p = (char*)malloc(stat_buf.st_size+1);
	if ( p == NULL )
		throwf("can't process order file: %s", path);
	if ( read(fd, p, stat_buf.st_size) != stat_buf.st_size )
		throwf("can't read order file: %s", path);
	::close(fd);
	p[stat_buf.st_size] = '\n';

	// parse into vector of pairs
	char * const end = &p[stat_buf.st_size+1];
	enum { lineStart, inSymbol, inComment } state = lineStart;
	char* symbolStart = NULL;
	for (char* s = p; s < end; ++s ) {
		switch ( state ) {
		case lineStart:
			if ( *s =='#' ) {
				state = inComment;
			}
			else if ( !isspace(*s) || cstring ) {
				state = inSymbol;
				symbolStart = s;
			}
			break;
		case inSymbol:
			if ( (*s == '\n') || (!cstring && (*s == '#')) ) {
				bool wasComment = (*s == '#');
				*s = '\0';
				// removing any trailing spaces
				char* last = s-1;
				while ( isspace(*last) ) {
					*last = '\0';
					--last;
				}
				// if there is an architecture prefix, only use this symbol it if matches current arch
				if ( strncmp(symbolStart, "ppc:", 4) == 0 ) {
					symbolStart = NULL;
				}
				else if ( strncmp(symbolStart, "ppc64:", 6) == 0 ) {
					symbolStart = NULL;
				}
				else if ( strncmp(symbolStart, "i386:", 5) == 0 ) {
					if ( fArchitecture == CPU_TYPE_I386 )
						symbolStart = &symbolStart[5];
					else
						symbolStart = NULL;
				}
				else if ( strncmp(symbolStart, "x86_64:", 7) == 0 ) {
					if ( fArchitecture == CPU_TYPE_X86_64 )
						symbolStart = &symbolStart[7];
					else
						symbolStart = NULL;
				}
				else if ( strncmp(symbolStart, "arm:", 4) == 0 ) {
					if ( fArchitecture == CPU_TYPE_ARM )
						symbolStart = &symbolStart[4];
					else
						symbolStart = NULL;
				}
				if ( symbolStart != NULL ) {
					char* objFileName = NULL;
					char* colon = strstr(symbolStart, ".o:");
					if ( colon != NULL ) {
						colon[2] = '\0';
						objFileName = symbolStart;
						symbolStart = &colon[3];
					}
					// trim leading spaces
					while ( isspace(*symbolStart) ) 
						++symbolStart;
					Options::OrderedSymbol pair;
					if ( cstring )
						pair.symbolName = cstringSymbolName(symbolStart);
					else
						pair.symbolName = symbolStart;
					pair.objectFileName = objFileName;
					fOrderedSymbols.push_back(pair);
				}
				symbolStart = NULL;
				if ( wasComment )
					state = inComment;
				else
					state = lineStart;
			}
			break;
		case inComment:
			if ( *s == '\n' )
				state = lineStart;
			break;
		}
	}
	// Note: we do not free() the malloc buffer, because the strings are used by the fOrderedSymbols
}

void Options::parseSectionOrderFile(const char* segment, const char* section, const char* path)
{
	if ( (strcmp(section, "__cstring") == 0) && (strcmp(segment, "__TEXT") == 0) ) {
		parseOrderFile(path, true);
	}
	else if ( (strncmp(section, "__literal",9) == 0) && (strcmp(segment, "__TEXT") == 0) ) {
		warning("sorting of __literal[4,8,16] sections not supported");
	}
	else {
		// ignore section information and append all symbol names to global order file
		parseOrderFile(path, false);
	}
}

void Options::addSection(const char* segment, const char* section, const char* path)
{
	if ( strlen(segment) > 16 )
		throw "-seccreate segment name max 16 chars";
	if ( strlen(section) > 16 ) {
		char* tmp = strdup(section);
		tmp[16] = '\0';
		warning("-seccreate section name (%s) truncated to 16 chars (%s)\n", section, tmp);
		section = tmp;
	}

	// read in whole file
	int fd = ::open(path, O_RDONLY, 0);
	if ( fd == -1 )
		throwf("can't open -sectcreate file: %s", path);
	struct stat stat_buf;
	::fstat(fd, &stat_buf);
	char* p = (char*)malloc(stat_buf.st_size);
	if ( p == NULL )
		throwf("can't process -sectcreate file: %s", path);
	if ( read(fd, p, stat_buf.st_size) != stat_buf.st_size )
		throwf("can't read -sectcreate file: %s", path);
	::close(fd);

	// record section to create
	ExtraSection info = { segment, section, path, (uint8_t*)p, static_cast<uint64_t>(stat_buf.st_size) };
	fExtraSections.push_back(info);
}

void Options::addSectionAlignment(const char* segment, const char* section, const char* alignmentStr)
{
	if ( strlen(segment) > 16 )
		throw "-sectalign segment name max 16 chars";
	if ( strlen(section) > 16 )
		throw "-sectalign section name max 16 chars";

	// argument to -sectalign is a hexadecimal number
	char* endptr;
	unsigned long value = strtoul(alignmentStr, &endptr, 16);
	if ( *endptr != '\0')
		throw "argument for -sectalign is not a hexadecimal number";
	if ( value > 0x8000 )
		throw "argument for -sectalign must be less than or equal to 0x8000";
	if ( value == 0 ) {
		warning("zero is not a valid -sectalign");
		value = 1;
	}

	// alignment is power of 2 (e.g. page alignment = 12)
	uint8_t alignment = (uint8_t)__builtin_ctz(value);
	if ( (unsigned long)(1 << alignment) != value ) {
		warning("alignment for -sectalign %s %s is not a power of two, using 0x%X",
			segment, section, 1 << alignment);
	}

	SectionAlignment info = { segment, section, alignment };
	fSectionAlignments.push_back(info);
}

void Options::addLibrary(const FileInfo& info)
{
	// if this library has already been added, don't add again (archives are automatically repeatedly searched)
	for (std::vector<Options::FileInfo>::iterator fit = fInputFiles.begin(); fit != fInputFiles.end(); fit++) {
		if ( strcmp(info.path, fit->path) == 0 ) {
			// if dylib is specified again but weak, record that it should be weak
			if ( info.options.fWeakImport )
				fit->options.fWeakImport = true;
			return;
		}
	}
	// add to list
	fInputFiles.push_back(info);
}

void Options::warnObsolete(const char* arg)
{
	warning("option %s is obsolete and being ignored", arg);
}




//
// Process all command line arguments.
//
// The only error checking done here is that each option is valid and if it has arguments
// that they too are valid.
//
// The general rule is "last option wins", i.e. if both -bundle and -dylib are specified,
// whichever was last on the command line is used.
//
// Error check for invalid combinations of options is done in checkIllegalOptionCombinations()
//
void Options::parse(int argc, const char* argv[])
{
    // Store the original args in the link snapshot.
    fLinkSnapshot.recordRawArgs(argc, argv);
    
	// pass one builds search list from -L and -F options
	this->buildSearchPaths(argc, argv);

	// reduce re-allocations
	fInputFiles.reserve(32);

	// pass two parse all other options
	for(int i=1; i < argc; ++i) {
		const char* arg = argv[i];

		if ( arg[0] == '-' ) {
            // by default, copy one arg to the snapshot link command, and do no file copying
            int snapshotArgIndex = i;
            int snapshotArgCount = -1; // -1 means compute count based on change in index
            int snapshotFileArgIndex = -1; // -1 means no data file parameter to arg

			// Since we don't care about the files passed, just the option names, we do this here.
			if (fPrintOptions)
				fprintf (stderr, "[Logging ld64 options]\t%s\n", arg);

			if ( (arg[1] == 'L') || (arg[1] == 'F') ) {
                snapshotArgCount = 0; // stripped out of link snapshot
				if (arg[2] == '\0')
					++i;
				// previously handled by buildSearchPaths()
			}
			// The one gnu style option we have to keep compatibility
			// with gcc. Might as well have the single hyphen one as well.
			else if ( (strcmp(arg, "--help") == 0)
					  || (strcmp(arg, "-help") == 0)) {
				fprintf (stdout, "ld64: For information on command line options please use 'man ld'.\n");
				exit (0);
			}
			else if ( strcmp(arg, "-arch") == 0 ) {
				parseArch(argv[++i]);
			}
			else if ( strcmp(arg, "-dynamic") == 0 ) {
				// default
			}
			else if ( strcmp(arg, "-static") == 0 ) {
				fForStatic = true;
				if ( (fOutputKind != kObjectFile) && (fOutputKind != kKextBundle) ) {
					fOutputKind = kStaticExecutable;
				}
			}
			else if ( strcmp(arg, "-dylib") == 0 ) {
				fOutputKind = kDynamicLibrary;
			}
			else if ( strcmp(arg, "-bundle") == 0 ) {
				fOutputKind = kDynamicBundle;
			}
			else if ( strcmp(arg, "-dylinker") == 0 ) {
				fOutputKind = kDyld;
			}
			else if ( strcmp(arg, "-execute") == 0 ) {
				if ( fOutputKind != kStaticExecutable )
					fOutputKind = kDynamicExecutable;
			}
			else if ( strcmp(arg, "-preload") == 0 ) {
				fOutputKind = kPreload;
			}
			else if ( strcmp(arg, "-r") == 0 ) {
				fOutputKind = kObjectFile;
			}
			else if ( strcmp(arg, "-kext") == 0 ) {
				fOutputKind = kKextBundle;
			}
			else if ( strcmp(arg, "-o") == 0 ) {
                snapshotArgCount = 0;
				fOutputFile = argv[++i];
                fLinkSnapshot.setSnapshotName(fOutputFile);
			}
			else if ( strncmp(arg, "-lazy-l", 7) == 0 ) {
                snapshotArgCount = 0;
				FileInfo info = findLibrary(&arg[7], true);
				info.options.fLazyLoad = true;
				info.ordinal = ld::File::Ordinal::makeArgOrdinal((uint16_t)i);
				addLibrary(info);
				fUsingLazyDylibLinking = true;
			}
			else if ( strcmp(arg, "-lto_library") == 0 ) {
                snapshotFileArgIndex = 1;
				fOverridePathlibLTO = argv[++i];
				if ( fOverridePathlibLTO == NULL )
					throw "missing argument to -lto_library";
			}
			else if ( (arg[1] == 'l') && (strncmp(arg,"-lazy_",6) !=0) ) {
                snapshotArgCount = 0;
				FileInfo info = findLibrary(&arg[2]);
				info.ordinal = ld::File::Ordinal::makeArgOrdinal((uint16_t)i);
				addLibrary(info);
			}
			// This causes a dylib to be weakly bound at
			// link time.  This corresponds to weak_import.
			else if ( strncmp(arg, "-weak-l", 7) == 0 ) {
                // SNAPSHOT FIXME: what should we do for link snapshots? (ignore for now)
                snapshotArgCount = 0;
				FileInfo info = findLibrary(&arg[7]);
				info.options.fWeakImport = true;
				info.ordinal = ld::File::Ordinal::makeArgOrdinal((uint16_t)i);
				addLibrary(info);
			}
			// Avoid lazy binding.
			else if ( strcmp(arg, "-bind_at_load") == 0 ) {
				fBindAtLoad = true;
			}
			else if ( strcmp(arg, "-twolevel_namespace") == 0 ) {
				fNameSpace = kTwoLevelNameSpace;
			}
			else if ( strcmp(arg, "-flat_namespace") == 0 ) {
				fNameSpace = kFlatNameSpace;
			}
			// Also sets a bit to ensure dyld causes everything
			// in the namespace to be flat.
			// ??? Deprecate
			else if ( strcmp(arg, "-force_flat_namespace") == 0 ) {
				fNameSpace = kForceFlatNameSpace;
			}
			// Similar to --whole-archive.
			else if ( strcmp(arg, "-all_load") == 0 ) {
				fFullyLoadArchives = true;
			}
			else if ( strcmp(arg, "-noall_load") == 0) {
				warnObsolete(arg);
			}
			// Similar to -all_load
			else if ( strcmp(arg, "-ObjC") == 0 ) {
				fLoadAllObjcObjectsFromArchives = true;
			}
			// Similar to -all_load, but for the following archive only.
			else if ( strcmp(arg, "-force_load") == 0 ) {
				FileInfo info = findFile(argv[++i]);
				info.options.fForceLoad = true;
				info.ordinal = ld::File::Ordinal::makeArgOrdinal((uint16_t)i);
				addLibrary(info);
			}
			// Library versioning.
			else if ( (strcmp(arg, "-dylib_compatibility_version") == 0)
					  || (strcmp(arg, "-compatibility_version") == 0)) {
				 const char* vers = argv[++i];
				 if ( vers == NULL )
					throw "-dylib_compatibility_version missing <version>";
				fDylibCompatVersion = parseVersionNumber32(vers);
			}
			else if ( (strcmp(arg, "-dylib_current_version") == 0)
					  || (strcmp(arg, "-current_version") == 0)) {
				 const char* vers = argv[++i];
				 if ( vers == NULL )
					throw "-dylib_current_version missing <version>";
				fDylibCurrentVersion = parseVersionNumber64(vers);
			}
			else if ( strcmp(arg, "-sectorder") == 0 ) {
				 if ( (argv[i+1]==NULL) || (argv[i+2]==NULL) || (argv[i+3]==NULL) )
					throw "-sectorder missing <segment> <section> <file-path>";
                snapshotFileArgIndex = 3;
				parseSectionOrderFile(argv[i+1], argv[i+2], argv[i+3]);
				i += 3;
			}
			else if ( strcmp(arg, "-order_file") == 0 ) {
                snapshotFileArgIndex = 1;
				parseOrderFile(argv[++i], false);
			}
			else if ( strcmp(arg, "-order_file_statistics") == 0 ) {
				fPrintOrderFileStatistics = true;
			}
			// ??? Deprecate segcreate.
			// -sectcreate puts whole files into a section in the output.
			else if ( (strcmp(arg, "-sectcreate") == 0) || (strcmp(arg, "-segcreate") == 0) ) {
				 if ( (argv[i+1]==NULL) || (argv[i+2]==NULL) || (argv[i+3]==NULL) )
					throw "-sectcreate missing <segment> <section> <file-path>";
                snapshotFileArgIndex = 3;
				addSection(argv[i+1], argv[i+2], argv[i+3]);
				i += 3;
			}
			// Since we have a full path in binary/library names we need to be able to override it.
			else if ( (strcmp(arg, "-dylib_install_name") == 0)
					  || (strcmp(arg, "-dylinker_install_name") == 0)
					  || (strcmp(arg, "-install_name") == 0)) {
				fDylibInstallName = argv[++i];
                if ( fDylibInstallName == NULL )
					throw "-install_name missing <path>";
			}
			// Sets the base address of the output.
			else if ( (strcmp(arg, "-seg1addr") == 0) || (strcmp(arg, "-image_base") == 0) ) {
				 const char* address = argv[++i];
				 if ( address == NULL )
					throwf("%s missing <address>", arg);
				fBaseAddress = parseAddress(address);
				uint64_t temp = ((fBaseAddress+fSegmentAlignment-1) & (-fSegmentAlignment)); 
				if ( fBaseAddress != temp ) {
					warning("-seg1addr not %lld byte aligned, rounding up", fSegmentAlignment);
					fBaseAddress = temp;
				}
			}
			else if ( strcmp(arg, "-e") == 0 ) {
				fEntryName = argv[++i];
			}
			// Same as -@ from the FSF linker.
			else if ( strcmp(arg, "-filelist") == 0 ) {
                 snapshotArgCount = 0;
				 const char* path = argv[++i];
				 if ( (path == NULL) || (path[0] == '-') )
					throw "-filelist missing <path>";
				 ld::File::Ordinal baseOrdinal = ld::File::Ordinal::makeArgOrdinal((uint16_t)i);
				 loadFileList(path, baseOrdinal);
			}
			else if ( strcmp(arg, "-keep_private_externs") == 0 ) {
				 fKeepPrivateExterns = true;
			}
			else if ( strcmp(arg, "-final_output") == 0 ) {
				fFinalName = argv[++i];
			}
			// Ensure that all calls to exported symbols go through lazy pointers.  Multi-module
			// just ensures that this happens for cross object file boundaries.
			else if ( (strcmp(arg, "-interposable") == 0) || (strcmp(arg, "-multi_module") == 0)) {
				switch ( fInterposeMode ) {
					case kInterposeNone:
					case kInterposeAllExternal:
						fInterposeMode = kInterposeAllExternal;
						break;
					case kInterposeSome:
						// do nothing, -interposable_list overrides -interposable"
						break;
				}
			}
			else if ( strcmp(arg, "-interposable_list") == 0 ) {
                snapshotFileArgIndex = 1;
				fInterposeMode = kInterposeSome;
				loadExportFile(argv[++i], "-interposable_list", fInterposeList);
			}
			// Default for -interposable/-multi_module/-single_module.
			else if ( strcmp(arg, "-single_module") == 0 ) {
				fInterposeMode = kInterposeNone;
			}
			else if ( strcmp(arg, "-exported_symbols_list") == 0 ) {
                snapshotFileArgIndex = 1;
				if ( fExportMode == kDontExportSome )
					throw "can't use -exported_symbols_list and -unexported_symbols_list";
				fExportMode = kExportSome;
				loadExportFile(argv[++i], "-exported_symbols_list", fExportSymbols);
			}
			else if ( strcmp(arg, "-unexported_symbols_list") == 0 ) {
                snapshotFileArgIndex = 1;
				if ( fExportMode == kExportSome )
					throw "can't use -unexported_symbols_list and -exported_symbols_list";
				fExportMode = kDontExportSome;
				loadExportFile(argv[++i], "-unexported_symbols_list", fDontExportSymbols);
			}
			else if ( strcmp(arg, "-exported_symbol") == 0 ) {
				if ( fExportMode == kDontExportSome )
					throw "can't use -exported_symbol and -unexported_symbols";
				fExportMode = kExportSome;
				fExportSymbols.insert(argv[++i]);
			}
			else if ( strcmp(arg, "-unexported_symbol") == 0 ) {
				if ( fExportMode == kExportSome )
					throw "can't use -unexported_symbol and -exported_symbol";
				fExportMode = kDontExportSome;
				fDontExportSymbols.insert(argv[++i]);
			}
			else if ( strcmp(arg, "-non_global_symbols_no_strip_list") == 0 ) {
                snapshotFileArgIndex = 1;
				if ( fLocalSymbolHandling == kLocalSymbolsSelectiveExclude )
					throw "can't use -non_global_symbols_no_strip_list and -non_global_symbols_strip_list";
				fLocalSymbolHandling = kLocalSymbolsSelectiveInclude;
				loadExportFile(argv[++i], "-non_global_symbols_no_strip_list", fLocalSymbolsIncluded);
			}
			else if ( strcmp(arg, "-non_global_symbols_strip_list") == 0 ) {
                snapshotFileArgIndex = 1;
				if ( fLocalSymbolHandling == kLocalSymbolsSelectiveInclude )
					throw "can't use -non_global_symbols_no_strip_list and -non_global_symbols_strip_list";
				fLocalSymbolHandling = kLocalSymbolsSelectiveExclude;
				loadExportFile(argv[++i], "-non_global_symbols_strip_list", fLocalSymbolsExcluded);
			}
			// ??? Deprecate
			else if ( strcmp(arg, "-no_arch_warnings") == 0 ) {
				 fIgnoreOtherArchFiles = true;
			}
			else if ( strcmp(arg, "-force_cpusubtype_ALL") == 0 ) {
				fForceSubtypeAll = true;
				fAllowCpuSubtypeMismatches = true;
			}
			// Similar to -weak-l but uses the absolute path name to the library.
			else if ( strcmp(arg, "-weak_library") == 0 ) {
                // SNAPSHOT FIXME: what should we do for link snapshots? (ignore for now)
                snapshotArgCount = 0;
				FileInfo info = findFile(argv[++i]);
				info.options.fWeakImport = true;
				info.ordinal = ld::File::Ordinal::makeArgOrdinal((uint16_t)i);
				addLibrary(info);
			}
			else if ( strcmp(arg, "-lazy_library") == 0 ) {
                // SNAPSHOT FIXME: what should we do for link snapshots? (ignore for now)
                snapshotArgCount = 0;
				FileInfo info = findFile(argv[++i]);
				info.options.fLazyLoad = true;
				info.ordinal = ld::File::Ordinal::makeArgOrdinal((uint16_t)i);
				addLibrary(info);
				fUsingLazyDylibLinking = true;
			}
			else if ( strcmp(arg, "-framework") == 0 ) {
                snapshotArgCount = 0;
				FileInfo info = findFramework(argv[++i]);
				info.ordinal = ld::File::Ordinal::makeArgOrdinal((uint16_t)i);
				addLibrary(info);
			}
			else if ( strcmp(arg, "-weak_framework") == 0 ) {
                // SNAPSHOT FIXME: what should we do for link snapshots? (ignore for now)
                snapshotArgCount = 0;
				FileInfo info = findFramework(argv[++i]);
				info.options.fWeakImport = true;
				info.ordinal = ld::File::Ordinal::makeArgOrdinal((uint16_t)i);
				addLibrary(info);
			}
			else if ( strcmp(arg, "-lazy_framework") == 0 ) {
                // SNAPSHOT FIXME: what should we do for link snapshots? (ignore for now)
                snapshotArgCount = 0;
				FileInfo info = findFramework(argv[++i]);
				info.options.fLazyLoad = true;
				info.ordinal = ld::File::Ordinal::makeArgOrdinal((uint16_t)i);
				addLibrary(info);
				fUsingLazyDylibLinking = true;
			}
			else if ( strcmp(arg, "-search_paths_first") == 0 ) {
				// previously handled by buildSearchPaths()
			}
			else if ( strcmp(arg, "-search_dylibs_first") == 0 ) {
				// previously handled by buildSearchPaths()
			}
			else if ( strcmp(arg, "-undefined") == 0 ) {
                setUndefinedTreatment(argv[++i]);
			}
			// Debugging output flag.
			else if ( strcmp(arg, "-arch_multiple") == 0 ) {
				 fMessagesPrefixedWithArchitecture = true;
			}
			// Specify what to do with relocations in read only
			// sections like .text.  Could be errors, warnings,
			// or suppressed.  Currently we do nothing with the
			// flag.
			else if ( strcmp(arg, "-read_only_relocs") == 0 ) {
				switch ( parseTreatment(argv[++i]) ) {
					case kNULL:
					case kInvalid:
						throw "-read_only_relocs missing [ warning | error | suppress ]";
					case kWarning:
						fWarnTextRelocs = true;
						fAllowTextRelocs = true;
						break;
					case kSuppress:
						fWarnTextRelocs = false;
						fAllowTextRelocs = true;
						break;
					case kError:
						fWarnTextRelocs = false;
						fAllowTextRelocs = false;
						break;
				}
			}
			else if ( strcmp(arg, "-sect_diff_relocs") == 0 ) {
				warnObsolete(arg);
				++i;
			}
			// Warn, error or make strong a mismatch between weak
			// and non-weak references.
			else if ( strcmp(arg, "-weak_reference_mismatches") == 0 ) {
                setWeakReferenceMismatchTreatment(argv[++i]);
			}
			// For a deployment target of 10.3 and earlier ld64 will
			// prebind an executable with 0s in all addresses that
			// are prebound.  This can then be fixed up by update_prebinding
			// later.  Prebinding is less useful on 10.4 and greater.
			else if ( strcmp(arg, "-prebind") == 0 ) {
				fPrebind = true;
			}
			else if ( strcmp(arg, "-noprebind") == 0 ) {
				warnObsolete(arg);
				fPrebind = false;
			}
			else if ( strcmp(arg, "-prebind_allow_overlap") == 0 ) {
				warnObsolete(arg);
			}
			else if ( strcmp(arg, "-prebind_all_twolevel_modules") == 0 ) {
				warnObsolete(arg);
			}
			else if ( strcmp(arg, "-noprebind_all_twolevel_modules") == 0 ) {
				warnObsolete(arg);
			}
			else if ( strcmp(arg, "-nofixprebinding") == 0 ) {
				warnObsolete(arg);
			}
			// This should probably be deprecated when we respect -L and -F
			// when searching for libraries.
			else if ( strcmp(arg, "-dylib_file") == 0 ) {
                // ignore for snapshot because a stub dylib will be created in the snapshot
                 snapshotArgCount = 0;
				 addDylibOverride(argv[++i]);
			}
			// What to expand @executable_path to if found in dependent dylibs
			else if ( strcmp(arg, "-executable_path") == 0 ) {
				 fExecutablePath = argv[++i];
				 if ( (fExecutablePath == NULL) || (fExecutablePath[0] == '-') )
					throw "-executable_path missing <path>";
				// if a directory was passed, add / to end
				// <rdar://problem/5171880> ld64 can't find @executable _path relative dylibs from our umbrella frameworks
				struct stat statBuffer;
				if ( stat(fExecutablePath, &statBuffer) == 0 ) {
					if ( (statBuffer.st_mode & S_IFMT) == S_IFDIR ) {
						char* pathWithSlash = new char[strlen(fExecutablePath)+2];
						strcpy(pathWithSlash, fExecutablePath);
						strcat(pathWithSlash, "/");
						fExecutablePath = pathWithSlash;
					}
				}
			}
			// Aligns all segments to the power of 2 boundary specified.
			else if ( strcmp(arg, "-segalign") == 0 ) {
				const char* size = argv[++i];
				if ( size == NULL )
					throw "-segalign missing <size>";
				fSegmentAlignment = parseAddress(size);
				uint8_t alignment = (uint8_t)__builtin_ctz(fSegmentAlignment);
				uint32_t p2aligned = (1 << alignment);
				if ( p2aligned != fSegmentAlignment ) {
					warning("alignment for -segalign %s is not a power of two, using 0x%X", size, p2aligned);
					fSegmentAlignment = p2aligned;
				}
			}
			// Puts a specified segment at a particular address that must
			// be a multiple of the segment alignment.
			else if ( strcmp(arg, "-segaddr") == 0 ) {
				SegmentStart seg;
				seg.name = argv[++i];
				 if ( (seg.name == NULL) || (argv[i+1] == NULL) )
					throw "-segaddr missing segName Adddress";
				seg.address = parseAddress(argv[++i]);
				uint64_t temp = ((seg.address+fSegmentAlignment-1) & (-fSegmentAlignment)); 
				if ( seg.address != temp )
					warning("-segaddr %s not %lld byte aligned", seg.name, fSegmentAlignment);
				fCustomSegmentAddresses.push_back(seg);
			}
			// ??? Deprecate when we deprecate split-seg.
			else if ( strcmp(arg, "-segs_read_only_addr") == 0 ) {
				fBaseAddress = parseAddress(argv[++i]);
			}
			// ??? Deprecate when we deprecate split-seg.
			else if ( strcmp(arg, "-segs_read_write_addr") == 0 ) {
				fBaseWritableAddress = parseAddress(argv[++i]);
				fSplitSegs = true;
			}
			// ??? Deprecate when we get rid of basing at build time.
			else if ( strcmp(arg, "-seg_addr_table") == 0 ) {
                snapshotFileArgIndex = 1;
				const char* name = argv[++i];
				if ( name == NULL )
					throw "-seg_addr_table missing argument";
				fSegAddrTablePath = name;
			}
			else if ( strcmp(arg, "-seg_addr_table_filename") == 0 ) {
				warnObsolete(arg);
				++i;
			}
			else if ( strcmp(arg, "-segprot") == 0 ) {
				SegmentProtect seg;
				seg.name = argv[++i];
				 if ( (seg.name == NULL) || (argv[i+1] == NULL) || (argv[i+2] == NULL) )
					throw "-segprot missing segName max-prot init-prot";
				seg.max = parseProtection(argv[++i]);
				seg.init = parseProtection(argv[++i]);
				fCustomSegmentProtections.push_back(seg);
			}
			else if ( strcmp(arg, "-pagezero_size") == 0 ) {
				 const char* size = argv[++i];
				 if ( size == NULL )
					throw "-pagezero_size missing <size>";
				fZeroPageSize = parseAddress(size);
				uint64_t temp = fZeroPageSize & (-4096); // page align
				if ( (fZeroPageSize != temp)  )
					warning("-pagezero_size not page aligned, rounding down");
				 fZeroPageSize = temp;
			}
			else if ( strcmp(arg, "-stack_addr") == 0 ) {
				 const char* address = argv[++i];
				 if ( address == NULL )
					throw "-stack_addr missing <address>";
				fStackAddr = parseAddress(address);
			}
			else if ( strcmp(arg, "-stack_size") == 0 ) {
				 const char* size = argv[++i];
				 if ( size == NULL )
					throw "-stack_size missing <address>";
				fStackSize = parseAddress(size);
				uint64_t temp = fStackSize & (-4096); // page align
				if ( (fStackSize != temp)  )
					warning("-stack_size not page aligned, rounding down");
			}
			else if ( strcmp(arg, "-allow_stack_execute") == 0 ) {
				fExecutableStack = true;
			}
			else if ( strcmp(arg, "-allow_heap_execute") == 0 ) {
				fDisableNonExecutableHeap = true;
			}
			else if ( strcmp(arg, "-sectalign") == 0 ) {
				 if ( (argv[i+1]==NULL) || (argv[i+2]==NULL) || (argv[i+3]==NULL) )
					throw "-sectalign missing <segment> <section> <file-path>";
				addSectionAlignment(argv[i+1], argv[i+2], argv[i+3]);
				i += 3;
			}
			else if ( strcmp(arg, "-sectorder_detail") == 0 ) {
				warnObsolete(arg);
			}
			else if ( strcmp(arg, "-sectobjectsymbols") == 0 ) {
				warnObsolete(arg);
				i += 2;
			}
			else if ( strcmp(arg, "-bundle_loader") == 0 ) {
                snapshotFileArgIndex = 1;
				fBundleLoader = argv[++i];
				if ( (fBundleLoader == NULL) || (fBundleLoader[0] == '-') )
					throw "-bundle_loader missing <path>";
				FileInfo info = findFile(fBundleLoader);
				info.ordinal = ld::File::Ordinal::makeArgOrdinal((uint16_t)i);
				info.options.fBundleLoader = true;
				fInputFiles.push_back(info);
			}
			else if ( strcmp(arg, "-private_bundle") == 0 ) {
				warnObsolete(arg);
			}
			else if ( strcmp(arg, "-twolevel_namespace_hints") == 0 ) {
				// FIX FIX
			}
			// Use this flag to set default behavior for deployement targets.
			else if ( strcmp(arg, "-macosx_version_min") == 0 ) {
				setMacOSXVersionMin(argv[++i]);
			}
			else if ( (strcmp(arg, "-ios_version_min") == 0) || (strcmp(arg, "-iphoneos_version_min") == 0) ) {
				setIOSVersionMin(argv[++i]); 
			}
			else if ( (strcmp(arg, "-aspen_version_min") == 0) ) {
                //for backward compitable with llvm-gcc.
                ++i;
			}
			else if ( strcmp(arg, "-ios_simulator_version_min") == 0 ) {
				setIOSVersionMin(argv[++i]);
			}
			else if ( strcmp(arg, "-multiply_defined") == 0 ) {
				//warnObsolete(arg);
				++i;
			}
			else if ( strcmp(arg, "-multiply_defined_unused") == 0 ) {
				warnObsolete(arg);
				++i;
			}
			else if ( strcmp(arg, "-nomultidefs") == 0 ) {
				warnObsolete(arg);
			}
			// Display each file in which the argument symbol appears and whether
			// the file defines or references it.  This option takes an argument
			// as -y<symbol> note that there is no space.
			else if ( strncmp(arg, "-y", 2) == 0 ) {
				warnObsolete("-y");
			}
			// Same output as -y, but output <arg> number of undefined symbols only.
			else if ( strcmp(arg, "-Y") == 0 ) {
				//warnObsolete(arg);
				++i;
			}
			// This option affects all objects linked into the final result.
			else if ( strcmp(arg, "-m") == 0 ) {
				warnObsolete(arg);
			}
			else if ( (strcmp(arg, "-why_load") == 0) || (strcmp(arg, "-whyload") == 0) ) {
				 fWhyLoad = true;
			}
			else if ( strcmp(arg, "-why_live") == 0 ) {
				 const char* name = argv[++i];
				if ( name == NULL )
					throw "-why_live missing symbol name argument";
				fWhyLive.insert(name);
			}
			else if ( strcmp(arg, "-u") == 0 ) {
				const char* name = argv[++i];
				if ( name == NULL )
					throw "-u missing argument";
				fInitialUndefines.push_back(name);
			}
			else if ( strcmp(arg, "-U") == 0 ) {
				const char* name = argv[++i];
				if ( name == NULL )
					throw "-U missing argument";
				fAllowedUndefined.insert(name);
			}
			else if ( strcmp(arg, "-s") == 0 ) {
				warnObsolete(arg);
				fLocalSymbolHandling = kLocalSymbolsNone;
				fDebugInfoStripping = Options::kDebugInfoNone;
			}
			else if ( strcmp(arg, "-x") == 0 ) {
				fLocalSymbolHandling = kLocalSymbolsNone;
			}
			else if ( strcmp(arg, "-S") == 0 ) {
				fDebugInfoStripping = Options::kDebugInfoNone;
			}
			else if ( strcmp(arg, "-X") == 0 ) {
				warnObsolete(arg);
			}
			else if ( strcmp(arg, "-Si") == 0 ) {
				warnObsolete(arg);
				fDebugInfoStripping = Options::kDebugInfoFull;
			}
			else if ( strcmp(arg, "-b") == 0 ) {
				warnObsolete(arg);
			}
			else if ( strcmp(arg, "-Sn") == 0 ) {
				warnObsolete(arg);
				fDebugInfoStripping = Options::kDebugInfoFull;
			}
			else if ( strcmp(arg, "-Sp") == 0 ) {
				warnObsolete(arg);
			}
			else if ( strcmp(arg, "-dead_strip") == 0 ) {
				fDeadStrip = true;
			}
			else if ( strcmp(arg, "-no_dead_strip_inits_and_terms") == 0 ) {
				fDeadStrip = true;
			}
			else if ( strcmp(arg, "-w") == 0 ) {
				// previously handled by buildSearchPaths()
			}
			else if ( strcmp(arg, "-fatal_warnings") == 0 ) {
				// previously handled by buildSearchPaths()
			}
			else if ( strcmp(arg, "-arch_errors_fatal") == 0 ) {
				fErrorOnOtherArchFiles = true;
			}
			else if ( strcmp(arg, "-M") == 0 ) {
				// FIX FIX
			}
			else if ( strcmp(arg, "-headerpad") == 0 ) {
				const char* size = argv[++i];
				if ( size == NULL )
					throw "-headerpad missing argument";
				 fMinimumHeaderPad = parseAddress(size);
			}
			else if ( strcmp(arg, "-headerpad_max_install_names") == 0 ) {
				fMaxMinimumHeaderPad = true;
			}
			else if ( strcmp(arg, "-t") == 0 ) {
				fLogAllFiles = true;
			}
			else if ( strcmp(arg, "-whatsloaded") == 0 ) {
				fLogObjectFiles = true;
			}
			else if ( strcmp(arg, "-A") == 0 ) {
				warnObsolete(arg);
				 ++i;
			}
			else if ( strcmp(arg, "-umbrella") == 0 ) {
				const char* name = argv[++i];
				if ( name == NULL )
					throw "-umbrella missing argument";
				fUmbrellaName = name;
			}
			else if ( strcmp(arg, "-allowable_client") == 0 ) {
				const char* name = argv[++i];

				if ( name == NULL )
					throw "-allowable_client missing argument";

				fAllowableClients.push_back(name);
			}
			else if ( strcmp(arg, "-client_name") == 0 ) {
				const char* name = argv[++i];

				if ( name == NULL )
					throw "-client_name missing argument";

				fClientName = name;
			}
			else if ( strcmp(arg, "-sub_umbrella") == 0 ) {
				const char* name = argv[++i];
				if ( name == NULL )
					throw "-sub_umbrella missing argument";
				 fSubUmbellas.push_back(name);
			}
			else if ( strcmp(arg, "-sub_library") == 0 ) {
				const char* name = argv[++i];
				if ( name == NULL )
					throw "-sub_library missing argument";
				 fSubLibraries.push_back(name);
			}
			else if ( strcmp(arg, "-init") == 0 ) {
				const char* name = argv[++i];
				if ( name == NULL )
					throw "-init missing argument";
				fInitFunctionName = name;
			}
			else if ( strcmp(arg, "-dot") == 0 ) {
				const char* name = argv[++i];
				if ( name == NULL )
					throw "-dot missing argument";
				fDotOutputFile = name;
			}
			else if ( strcmp(arg, "-warn_commons") == 0 ) {
				fWarnCommons = true;
			}
			else if ( strcmp(arg, "-commons") == 0 ) {
				fCommonsMode = parseCommonsTreatment(argv[++i]);
			}
			else if ( strcmp(arg, "-keep_relocs") == 0 ) {
				fKeepRelocations = true;
			}
			else if ( strcmp(arg, "-warn_stabs") == 0 ) {
				fWarnStabs = true;
			}
			else if ( strcmp(arg, "-pause") == 0 ) {
				fPause = true;
			}
			else if ( strcmp(arg, "-print_statistics") == 0 ) {
				fStatistics = true;
			}
			else if ( strcmp(arg, "-d") == 0 ) {
				fMakeTentativeDefinitionsReal = true;
			}
			else if ( strcmp(arg, "-v") == 0 ) {
				// previously handled by buildSearchPaths()
			}
			else if ( strcmp(arg, "-Z") == 0 ) {
				// previously handled by buildSearchPaths()
			}
			else if ( strcmp(arg, "-syslibroot") == 0 ) {
                snapshotArgCount = 0;
				++i;
				// previously handled by buildSearchPaths()
			}
			else if ( strcmp(arg, "-no_uuid") == 0 ) {
				fUUIDMode = kUUIDNone;
			}
			else if ( strcmp(arg, "-random_uuid") == 0 ) {
				fUUIDMode = kUUIDRandom;
			}
			else if ( strcmp(arg, "-dtrace") == 0 ) {
                snapshotFileArgIndex = 1;
				const char* name = argv[++i];
				if ( name == NULL )
					throw "-dtrace missing argument";
				fDtraceScriptName = name;
			}
			else if ( strcmp(arg, "-root_safe") == 0 ) {
				fRootSafe = true;
			}
			else if ( strcmp(arg, "-setuid_safe") == 0 ) {
				fSetuidSafe = true;
			}
			else if ( strcmp(arg, "-alias") == 0 ) {
				Options::AliasPair pair;
				pair.realName = argv[++i];
				if ( pair.realName == NULL )
					throw "missing argument to -alias";
				pair.alias = argv[++i];
				if ( pair.alias == NULL )
					throw "missing argument to -alias";
				fAliases.push_back(pair);
			}
			else if ( strcmp(arg, "-alias_list") == 0 ) {
                snapshotFileArgIndex = 1;
				parseAliasFile(argv[++i]);
			}
			// put this last so that it does not interfer with other options starting with 'i'
			else if ( strncmp(arg, "-i", 2) == 0 ) {
				const char* colon = strchr(arg, ':');
				if ( colon == NULL )
					throwf("unknown option: %s", arg);
				Options::AliasPair pair;
				char* temp = new char[colon-arg];
				strlcpy(temp, &arg[2], colon-arg-1);
				pair.realName = &colon[1];
				pair.alias = temp;
				fAliases.push_back(pair);
			}
			else if ( strcmp(arg, "-save-temps") == 0 ) {
				fSaveTempFiles = true;
			}
			else if ( strcmp(arg, "-rpath") == 0 ) {
				const char* path = argv[++i];
				if ( path == NULL )
					throw "missing argument to -rpath";
				fRPaths.push_back(path);
			}
			else if ( strcmp(arg, "-read_only_stubs") == 0 ) {
				fReadOnlyx86Stubs = true;
			}
			else if ( strcmp(arg, "-slow_stubs") == 0 ) {
				warnObsolete(arg);
			}
			else if ( strcmp(arg, "-map") == 0 ) {
				fMapPath = argv[++i];
				if ( fMapPath == NULL )
					throw "missing argument to -map";
			}
			else if ( strcmp(arg, "-pie") == 0 ) {
				fPositionIndependentExecutable = true;
				fPIEOnCommandLine = true;
			}
			else if ( strcmp(arg, "-no_pie") == 0 ) {
				fDisablePositionIndependentExecutable = true;
			}
			else if ( strncmp(arg, "-reexport-l", 11) == 0 ) {
                // SNAPSHOT FIXME: what should we do for link snapshots? (ignore for now)
                snapshotArgCount = 0;
				FileInfo info = findLibrary(&arg[11], true);
				info.options.fReExport = true;
				info.ordinal = ld::File::Ordinal::makeArgOrdinal((uint16_t)i);
				addLibrary(info);
			}
			else if ( strcmp(arg, "-reexport_library") == 0 ) {
                // SNAPSHOT FIXME: what should we do for link snapshots? (ignore for now)
                snapshotArgCount = 0;
				FileInfo info = findFile(argv[++i]);
				info.options.fReExport = true;
				info.ordinal = ld::File::Ordinal::makeArgOrdinal((uint16_t)i);
				addLibrary(info);
			}
			else if ( strcmp(arg, "-reexport_framework") == 0 ) {
                // SNAPSHOT FIXME: what should we do for link snapshots? (ignore for now)
                snapshotArgCount = 0;
				FileInfo info = findFramework(argv[++i]);
				info.options.fReExport = true;
				info.ordinal = ld::File::Ordinal::makeArgOrdinal((uint16_t)i);
				addLibrary(info);
			}
			else if ( strncmp(arg, "-upward-l", 9) == 0 ) {
                // SNAPSHOT FIXME: what should we do for link snapshots? (ignore for now)
                snapshotArgCount = 0;
				FileInfo info = findLibrary(&arg[9], true);
				info.options.fUpward = true;
				info.ordinal = ld::File::Ordinal::makeArgOrdinal((uint16_t)i);
				addLibrary(info);
			}
			else if ( strcmp(arg, "-upward_library") == 0 ) {
                // SNAPSHOT FIXME: what should we do for link snapshots? (ignore for now)
                snapshotArgCount = 0;
				FileInfo info = findFile(argv[++i]);
				info.options.fUpward = true;
				info.ordinal = ld::File::Ordinal::makeArgOrdinal((uint16_t)i);
				addLibrary(info);
			}
			else if ( strcmp(arg, "-upward_framework") == 0 ) {
                // SNAPSHOT FIXME: what should we do for link snapshots? (ignore for now)
                snapshotArgCount = 0;
				FileInfo info = findFramework(argv[++i]);
				info.options.fUpward = true;
				info.ordinal = ld::File::Ordinal::makeArgOrdinal((uint16_t)i);
				addLibrary(info);
			}
			else if ( strcmp(arg, "-dead_strip_dylibs") == 0 ) {
				fDeadStripDylibs = true;
			}
			else if ( strcmp(arg, "-no_implicit_dylibs") == 0 ) {
				fImplicitlyLinkPublicDylibs = false;
			}
			else if ( strcmp(arg, "-new_linker") == 0 ) {
				// ignore
			}
			else if ( strcmp(arg, "-no_encryption") == 0 ) {
				fEncryptable = false;
			}
			else if ( strcmp(arg, "-no_compact_unwind") == 0 ) {
				fAddCompactUnwindEncoding = false;
			}
			else if ( strcmp(arg, "-mllvm") == 0 ) {
				const char* opts = argv[++i];
				if ( opts == NULL )
					throw "missing argument to -mllvm";
				fLLVMOptions.push_back(opts);
			}
			else if ( strcmp(arg, "-no_order_inits") == 0 ) {
				fAutoOrderInitializers = false;
			}
			else if ( strcmp(arg, "-no_order_data") == 0 ) {
				fOrderData = false;
			}
			else if ( strcmp(arg, "-seg_page_size") == 0 ) {
				SegmentSize seg;
				seg.name = argv[++i];
				 if ( (seg.name == NULL) || (argv[i+1] == NULL) )
					throw "-seg_page_size missing segName Adddress";
				seg.size = parseAddress(argv[++i]);
				uint64_t temp = seg.size & (-4096); // page align
				if ( (seg.size != temp)  )
					warning("-seg_page_size %s not 4K aligned, rounding down", seg.name);
				fCustomSegmentSizes.push_back(seg);
			}
			else if ( strcmp(arg, "-mark_dead_strippable_dylib") == 0 ) {
				fMarkDeadStrippableDylib = true;
			}
			else if ( strcmp(arg, "-exported_symbols_order") == 0 ) {
                snapshotFileArgIndex = 1;
				loadSymbolOrderFile(argv[++i], fExportSymbolsOrder);
			}
			else if ( strcmp(arg, "-no_compact_linkedit") == 0 ) {
				warnObsolete("-no_compact_linkedit");
			}
			else if ( strcmp(arg, "-no_eh_labels") == 0 ) {
				fNoEHLabels = true;
			}
			else if ( strcmp(arg, "-warn_compact_unwind") == 0 ) {
				fWarnCompactUnwind = true;
			}
			else if ( strcmp(arg, "-allow_sub_type_mismatches") == 0 ) {
				fAllowCpuSubtypeMismatches = true;
			}
			else if ( strcmp(arg, "-no_zero_fill_sections") == 0 ) {
				fOptimizeZeroFill = false;
			}
			else if ( strcmp(arg, "-merge_zero_fill_sections") == 0 ) {
				fMergeZeroFill = true;
			}
			else if ( strcmp(arg, "-objc_abi_version") == 0 ) {
				const char* version = argv[++i];
				 if ( version == NULL )
					throw "-objc_abi_version missing version number";
				if ( strcmp(version, "2") == 0 ) {
					fObjCABIVersion1Override = false;
					fObjCABIVersion2Override = true;
				}
				else if ( strcmp(version, "1") == 0 ) {
					fObjCABIVersion1Override = true;
					fObjCABIVersion2Override = false;
				}
				else
					warning("ignoring unrecognized argument (%s) to -objc_abi_version", version);
			}
			else if ( strcmp(arg, "-warn_weak_exports") == 0 ) {
				fWarnWeakExports = true;
			}
			else if ( strcmp(arg, "-objc_gc_compaction") == 0 ) {
				fObjcGcCompaction = true;
			}
			else if ( strcmp(arg, "-objc_gc") == 0 ) {
				fObjCGc = true;
				if ( fObjCGcOnly ) { 
					warning("-objc_gc overriding -objc_gc_only");
					fObjCGcOnly = false;	
				}
			}
			else if ( strcmp(arg, "-objc_gc_only") == 0 ) {
				fObjCGcOnly = true;
				if ( fObjCGc ) { 
					warning("-objc_gc_only overriding -objc_gc");
					fObjCGc = false;	
				}
			}
			else if ( strcmp(arg, "-demangle") == 0 ) {
				fDemangle = true;
			}
			else if ( strcmp(arg, "-version_load_command") == 0 ) {
				fVersionLoadCommandForcedOn = true;
				fVersionLoadCommandForcedOff = false;
			}
			else if ( strcmp(arg, "-no_version_load_command") == 0 ) {
				fVersionLoadCommandForcedOff = true;
				fVersionLoadCommandForcedOn = false;
			}
			else if ( strcmp(arg, "-function_starts") == 0 ) {
				fFunctionStartsForcedOn = true;
				fFunctionStartsForcedOff = false;
			}
			else if ( strcmp(arg, "-no_function_starts") == 0 ) {
				fFunctionStartsForcedOff = true;
				fFunctionStartsForcedOn = false;
			}
			else if ( strcmp(arg, "-no_data_in_code_info") == 0 ) {
				fDataInCodeInfoLoadCommandForcedOff = true;
				fDataInCodeInfoLoadCommandForcedOn = false;
			}
			else if ( strcmp(arg, "-data_in_code_info") == 0 ) {
				fDataInCodeInfoLoadCommandForcedOn  = true;
				fDataInCodeInfoLoadCommandForcedOff = false;
			}
			else if ( strcmp(arg, "-object_path_lto") == 0 ) {
				fTempLtoObjectPath = argv[++i];
				if ( fTempLtoObjectPath == NULL )
					throw "missing argument to -object_path_lto";
			}
			else if ( strcmp(arg, "-no_objc_category_merging") == 0 ) {
				fObjcCategoryMerging = false;
			}
			else if ( strcmp(arg, "-force_symbols_weak_list") == 0 ) {
                snapshotFileArgIndex = 1;
				loadExportFile(argv[++i], "-force_symbols_weak_list", fForceWeakSymbols);
			}
			else if ( strcmp(arg, "-force_symbols_not_weak_list") == 0 ) {
                snapshotFileArgIndex = 1;
				loadExportFile(argv[++i], "-force_symbols_not_weak_list", fForceNotWeakSymbols);
			}
			else if ( strcmp(arg, "-force_symbol_weak") == 0 ) {
				const char* symbol = argv[++i];
				if ( symbol == NULL )
					throw "-force_symbol_weak missing <symbol>";
				fForceWeakSymbols.insert(symbol);
			}
			else if ( strcmp(arg, "-force_symbol_not_weak") == 0 ) {
				const char* symbol = argv[++i];
				if ( symbol == NULL )
					throw "-force_symbol_not_weak missing <symbol>";
				fForceNotWeakSymbols.insert(symbol);
			}
			else if ( strcmp(arg, "-reexported_symbols_list") == 0 ) {
                snapshotFileArgIndex = 1;
				if ( fExportMode == kExportSome )
					throw "can't use -exported_symbols_list and -reexported_symbols_list";
				loadExportFile(argv[++i], "-reexported_symbols_list", fReExportSymbols);
			}
			else if ( strcmp(arg, "-dyld_env") == 0 ) {
				const char* envarg = argv[++i];
				if ( envarg == NULL )
					throw "-dyld_env missing ENV=VALUE";
				if ( strchr(envarg, '=') == NULL )
					throw "-dyld_env missing ENV=VALUE";
				fDyldEnvironExtras.push_back(envarg);
			}
			else if ( strcmp(arg, "-page_align_data_atoms") == 0 ) {
				fPageAlignDataAtoms = true;
			} 
			else if (strcmp(arg, "-debug_snapshot") == 0) {
                fLinkSnapshot.setSnapshotMode(Snapshot::SNAPSHOT_DEBUG);
                fSnapshotRequested = true;
            }
			else if ( strcmp(arg, "-new_main") == 0 ) {
				fEntryPointLoadCommandForceOn = true;
			}
			else if ( strcmp(arg, "-no_new_main") == 0 ) {
				fEntryPointLoadCommandForceOff = true;
			}
			else if ( strcmp(arg, "-source_version") == 0 ) {
				 const char* vers = argv[++i];
				 if ( vers == NULL )
					throw "-source_version missing <version>";
				fSourceVersion = parseVersionNumber64(vers);
			}
			else if ( strcmp(arg, "-add_source_version") == 0 ) {
				fSourceVersionLoadCommandForceOn = true;
			}
			else if ( strcmp(arg, "-no_source_version") == 0 ) {
				fSourceVersionLoadCommandForceOff = true;
			}
			else if ( strcmp(arg, "-sdk_version") == 0 ) {
				 const char* vers = argv[++i];
				 if ( vers == NULL )
					throw "-sdk_version missing <version>";
				fSDKVersion = parseVersionNumber32(vers);
			}
			else if ( strcmp(arg, "-dependent_dr_info") == 0 ) {
				fDependentDRInfoForcedOn = true;
			}
			else if ( strcmp(arg, "-no_dependent_dr_info") == 0 ) {
				fDependentDRInfoForcedOff = true;
			}
			else if ( strcmp(arg, "-kexts_use_stubs") == 0 ) {
				fKextsUseStubs = true;
			}
			else {
				throwf("unknown option: %s", arg);
			}
            
            if (snapshotArgCount == -1)
                snapshotArgCount = i-snapshotArgIndex+1;
            if (snapshotArgCount > 0)
                fLinkSnapshot.addSnapshotLinkArg(snapshotArgIndex, snapshotArgCount, snapshotFileArgIndex);
		}
		else {
			FileInfo info = findFile(arg);
			info.ordinal = ld::File::Ordinal::makeArgOrdinal((uint16_t)i);
			if ( strcmp(&info.path[strlen(info.path)-2], ".a") == 0 )
				addLibrary(info);
			else
				fInputFiles.push_back(info);
		}
	}
	
	// if a -lazy option was used, implicitly link in lazydylib1.o
	if ( fUsingLazyDylibLinking ) {
		FileInfo info = findLibrary("lazydylib1.o");
		info.ordinal = ld::File::Ordinal::makeArgOrdinal((uint16_t)argc);
		addLibrary(info);
	}
    
    if (fSnapshotRequested)
        fLinkSnapshot.createSnapshot();
}



//
// -syslibroot <path> is used for SDK support.
// The rule is that all search paths (both explicit and default) are
// checked to see if they exist in the SDK.  If so, that path is
// replaced with the sdk prefixed path.  If not, that search path
// is used as is.  If multiple -syslibroot options are specified
// their directory structures are logically overlayed and files
// from sdks specified earlier on the command line used before later ones.

void Options::buildSearchPaths(int argc, const char* argv[])
{
	bool addStandardLibraryDirectories = true;
	std::vector<const char*> libraryPaths;
	std::vector<const char*> frameworkPaths;
	libraryPaths.reserve(10);
	frameworkPaths.reserve(10);
	// scan through argv looking for -L, -F, -Z, and -syslibroot options
	for(int i=0; i < argc; ++i) {
		if ( (argv[i][0] == '-') && (argv[i][1] == 'L') ) {
			const char* libSearchDir = &argv[i][2];
			// Allow either "-L{path}" or "-L {path}".
			if (argv[i][2] == '\0') {
				// -L {path}.  Make sure there is an argument following this.
				const char* path = argv[++i];
				if ( path == NULL )
					throw "-L missing argument";
				libSearchDir = path;
			}
			if ( libSearchDir[0] == '\0' ) 
				throw "-L must be immediately followed by a directory path (no space)";
			struct stat statbuf;
			if ( stat(libSearchDir, &statbuf) == 0 ) {
				if ( statbuf.st_mode & S_IFDIR )
					libraryPaths.push_back(libSearchDir);
				else
					warning("path '%s' following -L not a directory", libSearchDir);
			}
			else {
				warning("directory not found for option '-L%s'", libSearchDir);
			}
		}
		else if ( (argv[i][0] == '-') && (argv[i][1] == 'F') ) {
			const char* frameworkSearchDir = &argv[i][2];
			// Allow either "-F{path}" or "-F {path}".
			if (argv[i][2] == '\0') {
				// -F {path}.  Make sure there is an argument following this.
				const char* path = argv[++i];
				if ( path == NULL )
					throw "-F missing argument";
				frameworkSearchDir = path;
			}
			if ( frameworkSearchDir[0] == '\0' ) 
				throw "-F must be immediately followed by a directory path (no space)";
			struct stat statbuf;
			if ( stat(frameworkSearchDir, &statbuf) == 0 ) {
				if ( statbuf.st_mode & S_IFDIR )
					frameworkPaths.push_back(frameworkSearchDir);
				else
					warning("path '%s' following -F not a directory", frameworkSearchDir);
			}
			else {
				warning("directory not found for option '-F%s'", frameworkSearchDir);
			}
		}
		else if ( strcmp(argv[i], "-Z") == 0 )
			addStandardLibraryDirectories = false;
		else if ( strcmp(argv[i], "-v") == 0 ) {
			fVerbose = true;
			extern const char ldVersionString[];
			fprintf(stderr, "%s", ldVersionString);
			fprintf(stderr, "configured to support archs: %s\n", ALL_SUPPORTED_ARCHS);
			 // if only -v specified, exit cleanly
			 if ( argc == 2 ) {
#ifdef LTO_SUPPORT
				const char* ltoVers = lto::version();
				if ( ltoVers != NULL )
					fprintf(stderr, "LTO support using: %s\n", ltoVers);
#endif /* LTO_SUPPORT */
				exit(0);
			}
		}
		else if ( strcmp(argv[i], "-syslibroot") == 0 ) {
			const char* path = argv[++i];
			if ( path == NULL )
				throw "-syslibroot missing argument";
			fSDKPaths.push_back(path);
		}
		else if ( strcmp(argv[i], "-search_paths_first") == 0 ) {
			fLibrarySearchMode = kSearchDylibAndArchiveInEachDir;
		}
		else if ( strcmp(argv[i], "-search_dylibs_first") == 0 ) {
			fLibrarySearchMode = kSearchAllDirsForDylibsThenAllDirsForArchives;
		}
		else if ( strcmp(argv[i], "-w") == 0 ) {
			sEmitWarnings = false;
		}
		else if ( strcmp(argv[i], "-fatal_warnings") == 0 ) {
			sFatalWarnings = true;
		}
	}
	int standardLibraryPathsStartIndex = libraryPaths.size();
	int standardFrameworkPathsStartIndex = frameworkPaths.size();
	if ( addStandardLibraryDirectories ) {
		libraryPaths.push_back("/usr/lib");
		libraryPaths.push_back("/usr/local/lib");

		frameworkPaths.push_back("/Library/Frameworks/");
		frameworkPaths.push_back("/System/Library/Frameworks/");
		// <rdar://problem/5433882> remove /Network/Library/Frameworks from default search path
	}

	// <rdar://problem/5829579> Support for configure based hacks 
	// if last -syslibroot is /, then ignore all syslibroots
	if ( fSDKPaths.size() > 0 ) {
		if ( strcmp(fSDKPaths.back(), "/") == 0 ) {
			fSDKPaths.clear();
		}
	}

	// now merge sdk and library paths to make real search paths
	fLibrarySearchPaths.reserve(libraryPaths.size()*(fSDKPaths.size()+1));
	int libIndex = 0;
	for (std::vector<const char*>::iterator it = libraryPaths.begin(); it != libraryPaths.end(); ++it, ++libIndex) {
		const char* libDir = *it;
		bool sdkOverride = false;
		if ( libDir[0] == '/' ) {
			char betterLibDir[PATH_MAX];
			if ( strstr(libDir, "/..") != NULL ) {
				if ( realpath(libDir, betterLibDir) != NULL )
					libDir = strdup(betterLibDir);
			}
			const int libDirLen = strlen(libDir);
			for (std::vector<const char*>::iterator sdkit = fSDKPaths.begin(); sdkit != fSDKPaths.end(); sdkit++) {
				const char* sdkDir = *sdkit;
				const int sdkDirLen = strlen(sdkDir);
				char newPath[libDirLen + sdkDirLen+4];
				strcpy(newPath, sdkDir);
				if ( newPath[sdkDirLen-1] == '/' )
					newPath[sdkDirLen-1] = '\0';
				strcat(newPath, libDir);
				struct stat statBuffer;
				if ( stat(newPath, &statBuffer) == 0 ) {
					fLibrarySearchPaths.push_back(strdup(newPath));
					sdkOverride = true;
				}
			}
		}
		if ( !sdkOverride ) {
			if ( (libIndex >= standardLibraryPathsStartIndex) && (fSDKPaths.size() == 1) ) {
				// <rdar://problem/6438270> -syslibroot should skip standard search paths not in the SDK
				// if one SDK is specified and a standard library path is not in the SDK, don't use it
			}
			else {
				fLibrarySearchPaths.push_back(libDir);
			}
		}
	}

	// now merge sdk and framework paths to make real search paths
	fFrameworkSearchPaths.reserve(frameworkPaths.size()*(fSDKPaths.size()+1));
	int frameIndex = 0;
	for (std::vector<const char*>::iterator it = frameworkPaths.begin(); it != frameworkPaths.end(); ++it, ++frameIndex) {
		const char* frameworkDir = *it;
		bool sdkOverride = false;
		if ( frameworkDir[0] == '/' ) {
			char betterFrameworkDir[PATH_MAX];
			if ( strstr(frameworkDir, "/..") != NULL ) {
				if ( realpath(frameworkDir, betterFrameworkDir) != NULL )
					frameworkDir = strdup(betterFrameworkDir);
			}
			const int frameworkDirLen = strlen(frameworkDir);
			for (std::vector<const char*>::iterator sdkit = fSDKPaths.begin(); sdkit != fSDKPaths.end(); sdkit++) {
				const char* sdkDir = *sdkit;
				const int sdkDirLen = strlen(sdkDir);
				char newPath[frameworkDirLen + sdkDirLen+4];
				strcpy(newPath, sdkDir);
				if ( newPath[sdkDirLen-1] == '/' )
					newPath[sdkDirLen-1] = '\0';
				strcat(newPath, frameworkDir);
				struct stat statBuffer;
				if ( stat(newPath, &statBuffer) == 0 ) {
					fFrameworkSearchPaths.push_back(strdup(newPath));
					sdkOverride = true;
				}
			}
		}
		if ( !sdkOverride ) {
			if ( (frameIndex >= standardFrameworkPathsStartIndex) && (fSDKPaths.size() == 1) ) {
				// <rdar://problem/6438270> -syslibroot should skip standard search paths not in the SDK
				// if one SDK is specified and a standard library path is not in the SDK, don't use it
			}
			else {
				fFrameworkSearchPaths.push_back(frameworkDir);
			}
		}
	}

	if ( fVerbose ) {
		fprintf(stderr,"Library search paths:\n");
		for (std::vector<const char*>::iterator it = fLibrarySearchPaths.begin();
			 it != fLibrarySearchPaths.end();
			 it++)
			fprintf(stderr,"\t%s\n", *it);
		fprintf(stderr,"Framework search paths:\n");
		for (std::vector<const char*>::iterator it = fFrameworkSearchPaths.begin();
			 it != fFrameworkSearchPaths.end();
			 it++)
			fprintf(stderr,"\t%s\n", *it);
	}
}

// this is run before the command line is parsed
void Options::parsePreCommandLineEnvironmentSettings()
{
	if ((getenv("LD_TRACE_ARCHIVES") != NULL)
		|| (getenv("RC_TRACE_ARCHIVES") != NULL))
	    fTraceArchives = true;

	if ((getenv("LD_TRACE_DYLIBS") != NULL)
		|| (getenv("RC_TRACE_DYLIBS") != NULL)) {
	    fTraceDylibs = true;
		fTraceIndirectDylibs = true;
	}

	if (getenv("RC_TRACE_DYLIB_SEARCHING") != NULL) {
	    fTraceDylibSearching = true;
	}

	if (getenv("LD_PRINT_OPTIONS") != NULL)
		fPrintOptions = true;

	if (fTraceDylibs || fTraceArchives)
		fTraceOutputFile = getenv("LD_TRACE_FILE");

	if (getenv("LD_PRINT_ORDER_FILE_STATISTICS") != NULL)
		fPrintOrderFileStatistics = true;

	if (getenv("LD_SPLITSEGS_NEW_LIBRARIES") != NULL)
		fSplitSegs = true;
		
	if (getenv("LD_NO_ENCRYPT") != NULL)
		fEncryptable = false;
	
	if (getenv("LD_ALLOW_CPU_SUBTYPE_MISMATCHES") != NULL)
		fAllowCpuSubtypeMismatches = true;
	
	sWarningsSideFilePath = getenv("LD_WARN_FILE");
	
	const char* customDyldPath = getenv("LD_DYLD_PATH");
	if ( customDyldPath != NULL ) 
		fDyldInstallPath = customDyldPath;
    
    const char* debugArchivePath = getenv("LD_DEBUG_SNAPSHOT");
    if (debugArchivePath != NULL) {
        fLinkSnapshot.setSnapshotMode(Snapshot::SNAPSHOT_DEBUG);
        if (strlen(debugArchivePath) > 0)
            fLinkSnapshot.setSnapshotPath(debugArchivePath);
        fSnapshotRequested = true;
    }

    const char* pipeFdString = getenv("LD_PIPELINE_FIFO");
    if (pipeFdString != NULL) {
		fPipelineFifo = pipeFdString;
    }
}


// this is run after the command line is parsed
void Options::parsePostCommandLineEnvironmentSettings()
{
	// when building a dynamic main executable, default any use of @executable_path to output path
	if ( fExecutablePath == NULL && (fOutputKind == kDynamicExecutable) ) {
		fExecutablePath = fOutputFile;
	}

	// allow build system to set default seg_addr_table
	if ( fSegAddrTablePath == NULL )
		fSegAddrTablePath = getenv("LD_SEG_ADDR_TABLE");

	// allow build system to turn on prebinding
	if ( !fPrebind ) {
		fPrebind = ( getenv("LD_PREBIND") != NULL );
	}
	
	// allow build system to force on dead-code-stripping
	if ( !fDeadStrip ) {
		if ( getenv("LD_DEAD_STRIP") != NULL ) {
			switch (fOutputKind) {
				case Options::kDynamicLibrary:
				case Options::kDynamicExecutable:
				case Options::kDynamicBundle:
					fDeadStrip = true;
					break;
				case Options::kPreload:
				case Options::kObjectFile:
				case Options::kDyld:
				case Options::kStaticExecutable:
				case Options::kKextBundle:
					break;
			}
		}
	}
	
	// allow build system to force on -warn_commons
	if ( getenv("LD_WARN_COMMONS") != NULL )
		fWarnCommons = true;
	
	// allow B&I to set default -source_version
	if ( fSourceVersion == 0 ) {
		const char* vers = getenv("RC_ProjectSourceVersion");
		if ( vers != NULL )
			fSourceVersion = parseVersionNumber64(vers);
	}
		
}

void Options::reconfigureDefaults()
{
	// sync reader options
	switch ( fOutputKind ) {
		case Options::kObjectFile:
			fForFinalLinkedImage = false;
			break;
		case Options::kDyld:
			fForDyld = true;
			fForFinalLinkedImage = true;
			fNoEHLabels = true;
			break;
		case Options::kDynamicLibrary:
		case Options::kDynamicBundle:
		case Options::kKextBundle:
			fForFinalLinkedImage = true;
			fNoEHLabels = true;
			break;
		case Options::kDynamicExecutable:
		case Options::kStaticExecutable:
		case Options::kPreload:
			fLinkingMainExecutable = true;
			fForFinalLinkedImage = true;
			fNoEHLabels = true;
			break;
	}

	// set default min OS version
	if ( (fMacVersionMin == ld::macVersionUnset)
		&& (fIOSVersionMin == ld::iOSVersionUnset) ) {
		// if neither -macosx_version_min nor -iphoneos_version_min used, try environment variables
		const char* macVers = getenv("MACOSX_DEPLOYMENT_TARGET");
		const char* iPhoneVers = getenv("IPHONEOS_DEPLOYMENT_TARGET");
		const char* iOSVers = getenv("IOS_DEPLOYMENT_TARGET");
		const char* iOSSimulatorVers = getenv("IOS_SIMULATOR_DEPLOYMENT_TARGET");
		if ( macVers != NULL ) 
			setMacOSXVersionMin(macVers);
		else if ( iPhoneVers != NULL )
			setIOSVersionMin(iPhoneVers);
		else if ( iOSVers != NULL )
			setIOSVersionMin(iOSVers);
		else if ( iOSSimulatorVers != NULL )
			setIOSVersionMin(iOSSimulatorVers);
		else {
			// if still nothing, set default based on architecture
			switch ( fArchitecture ) {
				case CPU_TYPE_I386:
				case CPU_TYPE_X86_64:
					if ( (fOutputKind != Options::kObjectFile) && (fOutputKind != Options::kPreload) ) {
			#ifdef DEFAULT_MACOSX_MIN_VERSION
						warning("-macosx_version_min not specified, assuming " DEFAULT_MACOSX_MIN_VERSION);
						setMacOSXVersionMin(DEFAULT_MACOSX_MIN_VERSION);
			#else
						warning("-macosx_version_min not specified, assuming 10.6");
						fMacVersionMin = ld::mac10_6;
			#endif		
					}
					break;
				case CPU_TYPE_ARM:
					if ( (fOutputKind != Options::kObjectFile) && (fOutputKind != Options::kPreload) ) {
			#if defined(DEFAULT_IPHONEOS_MIN_VERSION)
						warning("-ios_version_min not specified, assuming " DEFAULT_IPHONEOS_MIN_VERSION);
						setIOSVersionMin(DEFAULT_IPHONEOS_MIN_VERSION);
			#elif defined(DEFAULT_MACOSX_MIN_VERSION)
						warning("-macosx_version_min not specified, assuming " DEFAULT_MACOSX_MIN_VERSION);
						setMacOSXVersionMin(DEFAULT_MACOSX_MIN_VERSION);
			#else
						warning("-macosx_version_min not specified, assuming 10.6");
						fMacVersionMin = ld::mac10_6;
			#endif
					}
					break;
				default:
					// architecture will be infered ;ater by examining .o files
					break;
			}
		}
	}


	// adjust min based on architecture
	switch ( fArchitecture ) {
		case CPU_TYPE_I386:
			if ( (fMacVersionMin < ld::mac10_4) && (fIOSVersionMin == ld::iOSVersionUnset) ) {
				//warning("-macosx_version_min should be 10.4 or later for i386");
				fMacVersionMin = ld::mac10_4;
			}
			break;
		case CPU_TYPE_X86_64:
			if ( (fMacVersionMin < ld::mac10_4) && (fIOSVersionMin == ld::iOSVersionUnset) ) {
				//warning("-macosx_version_min should be 10.4 or later for x86_64");
				fMacVersionMin = ld::mac10_4;
			}
			break;
	}
	
	// adjust kext type based on architecture
	if ( fOutputKind == kKextBundle ) {
		switch ( fArchitecture ) {
			case CPU_TYPE_X86_64:
				// x86_64 uses new MH_KEXT_BUNDLE type
				fMakeCompressedDyldInfo = false;
				fMakeCompressedDyldInfoForceOff = true;
				fAllowTextRelocs = true;
				fUndefinedTreatment = kUndefinedDynamicLookup;
				break;
			case CPU_TYPE_ARM:
				if ( fIOSVersionMin >= ld::iOS_5_0 ) {
                    // iOS 5.0 and later use new MH_KEXT_BUNDLE type
                    fMakeCompressedDyldInfo = false;
                    fMakeCompressedDyldInfoForceOff = true;
					// kexts are PIC in iOS 6.0 and later
					fAllowTextRelocs = (fIOSVersionMin < ld::iOS_6_0);
					fKextsUseStubs = !fAllowTextRelocs;
                    fUndefinedTreatment = kUndefinedDynamicLookup;
					break;
				}
				// else use object file
			case CPU_TYPE_I386:
				// use .o files
				fOutputKind = kObjectFile;
				break;
		}
	}

	// disable implicit dylibs when targeting 10.3
	// <rdar://problem/5451987> add option to disable implicit load commands for indirectly used public dylibs
	if ( !minOS(ld::mac10_4, ld::iOS_2_0) )
		fImplicitlyLinkPublicDylibs = false;


	// allow build system to force linker to ignore -prebind
	if ( getenv("LD_FORCE_NO_PREBIND") != NULL )
		fPrebind = false;			

	// allow build system to force linker to ignore -seg_addr_table
	if ( getenv("LD_FORCE_NO_SEG_ADDR_TABLE") != NULL )
		   fSegAddrTablePath = NULL;

	// check for base address specified externally
	if ( (fSegAddrTablePath != NULL) &&  (fOutputKind == Options::kDynamicLibrary) ) {
		parseSegAddrTable(fSegAddrTablePath, this->installPath());
		// HACK to support seg_addr_table entries that are physical paths instead of install paths
		if ( fBaseAddress == 0 ) {
			if ( strcmp(this->installPath(), "/usr/lib/libstdc++.6.dylib") == 0 ) {
				parseSegAddrTable(fSegAddrTablePath, "/usr/lib/libstdc++.6.0.4.dylib");
				if ( fBaseAddress == 0 )
					parseSegAddrTable(fSegAddrTablePath, "/usr/lib/libstdc++.6.0.9.dylib");
			}
				
			else if ( strcmp(this->installPath(), "/usr/lib/libz.1.dylib") == 0 ) 
				parseSegAddrTable(fSegAddrTablePath, "/usr/lib/libz.1.2.3.dylib");
				
			else if ( strcmp(this->installPath(), "/usr/lib/libutil.dylib") == 0 ) 
				parseSegAddrTable(fSegAddrTablePath, "/usr/lib/libutil1.0.dylib");
		}		
	}
	
	// split segs only allowed for dylibs
	if ( fSplitSegs ) {
        // split seg only supported for i386, and arm.
        switch ( fArchitecture ) {
            case CPU_TYPE_I386:
                if ( fOutputKind != Options::kDynamicLibrary )
                    fSplitSegs = false;
                // make sure read and write segments are proper distance apart
                if ( fSplitSegs && (fBaseWritableAddress-fBaseAddress != 0x10000000) )
                    fBaseWritableAddress = fBaseAddress + 0x10000000;
                break;
            case CPU_TYPE_ARM:
                if ( fOutputKind != Options::kDynamicLibrary ) {
                    fSplitSegs = false;
				}
				else {
					// make sure read and write segments are proper distance apart
					if ( fSplitSegs && (fBaseWritableAddress-fBaseAddress != 0x08000000) )
						fBaseWritableAddress = fBaseAddress + 0x08000000;
				}
                break;
            default:
                fSplitSegs = false;
                fBaseAddress = 0;
                fBaseWritableAddress = 0;
		}
	}

	// set too-large size
	switch ( fArchitecture ) {
		case CPU_TYPE_I386:
			fMaxAddress = 0xFFFFFFFF;
			break;
		case CPU_TYPE_X86_64:
			break;
		case CPU_TYPE_ARM:
			switch ( fOutputKind ) {
				case Options::kDynamicExecutable:
				case Options::kDynamicLibrary:
				case Options::kDynamicBundle:
					// user land code is limited to low 1GB
					fMaxAddress = 0x2FFFFFFF;
					break;
				case Options::kStaticExecutable:
				case Options::kObjectFile:
				case Options::kDyld:
				case Options::kPreload:
				case Options::kKextBundle:
					fMaxAddress = 0xFFFFFFFF;
					break;
			}
			// range check -seg1addr for ARM
			if ( fBaseAddress > fMaxAddress ) {
				warning("ignoring -seg1addr 0x%08llX.  Address out of range.", fBaseAddress);
				fBaseAddress = 0;
			}
			break;
	}

	// <rdar://problem/6138961> -r implies no prebinding for all architectures
	if ( fOutputKind == Options::kObjectFile )
		fPrebind = false;			

	// disable prebinding depending on arch and min OS version
	if ( fPrebind ) {
		switch ( fArchitecture ) {
			case CPU_TYPE_I386:
				if ( fMacVersionMin == ld::mac10_4 ) {
					// in 10.4 only split seg dylibs are prebound
					if ( (fOutputKind != Options::kDynamicLibrary) || ! fSplitSegs )
						fPrebind = false;
				}
				else if ( fMacVersionMin >= ld::mac10_5 ) {
					// in 10.5 nothing is prebound
					fPrebind = false;
				}
				else if ( fIOSVersionMin != ld::iOSVersionUnset ) {
					// nothing in simulator is prebound
					fPrebind = false;
				}
				else {
					// in 10.3 and earlier only dylibs and main executables could be prebound
					switch ( fOutputKind ) {
						case Options::kDynamicExecutable:
						case Options::kDynamicLibrary:
							// only main executables and dylibs can be prebound
							break;
						case Options::kStaticExecutable:
						case Options::kDynamicBundle:
						case Options::kObjectFile:
						case Options::kDyld:
						case Options::kPreload:
						case Options::kKextBundle:
							// disable prebinding for everything else
							fPrebind = false;
							break;
					}
				}
				break;
			case CPU_TYPE_X86_64:
				fPrebind = false;
				break;
            case CPU_TYPE_ARM:
				switch ( fOutputKind ) {
					case Options::kDynamicExecutable:
					case Options::kDynamicLibrary:
						// only main executables and dylibs can be prebound
						break;
					case Options::kStaticExecutable:
					case Options::kDynamicBundle:
					case Options::kObjectFile:
					case Options::kDyld:
					case Options::kPreload:
					case Options::kKextBundle:
						// disable prebinding for everything else
						fPrebind = false;
						break;
				}
				break;
		}
	}

	// only prebound images can be split-seg
	if ( fSplitSegs && !fPrebind )
		fSplitSegs = false;

	// determine if info for shared region should be added
	if ( fOutputKind == Options::kDynamicLibrary ) {
		if ( minOS(ld::mac10_5, ld::iOS_3_1) )
			if ( !fPrebind )
				if ( (strncmp(this->installPath(), "/usr/lib/", 9) == 0)
					|| (strncmp(this->installPath(), "/System/Library/", 16) == 0) )
					fSharedRegionEligible = true;
	}
	else if ( fOutputKind == Options::kDyld ) {
        // <rdar://problem/10111122> Enable dyld to be put into the dyld shared cache
        fSharedRegionEligible = true;
	}
    
	// figure out if module table is needed for compatibility with old ld/dyld
	if ( fOutputKind == Options::kDynamicLibrary ) {
		switch ( fArchitecture ) {
			case CPU_TYPE_I386:
				if ( fIOSVersionMin != ld::iOSVersionUnset ) // simulator never needs modules
					break;
			case CPU_TYPE_ARM:
				if ( fPrebind )
					fNeedsModuleTable = true; // redo_prebinding requires a module table
				break;
		}
	}
	
	// <rdar://problem/5366363> -r -x implies -S
	if ( (fOutputKind == Options::kObjectFile) && (fLocalSymbolHandling == kLocalSymbolsNone) )
		fDebugInfoStripping = Options::kDebugInfoNone;			
		
	// choose how to process unwind info
	switch ( fArchitecture ) {
		case CPU_TYPE_I386:		
		case CPU_TYPE_X86_64:		
			switch ( fOutputKind ) {
				case Options::kObjectFile:
				case Options::kStaticExecutable:
				case Options::kPreload:
				case Options::kKextBundle:
					fAddCompactUnwindEncoding = false;
					break;
				case Options::kDyld:
				case Options::kDynamicLibrary:
				case Options::kDynamicBundle:
				case Options::kDynamicExecutable:
					//if ( fAddCompactUnwindEncoding && (fVersionMin >= ld::mac10_6) )
					//	fRemoveDwarfUnwindIfCompactExists = true;
					break;
			}
			break;
		case CPU_TYPE_ARM:
			fAddCompactUnwindEncoding = false;
			fRemoveDwarfUnwindIfCompactExists = false;
			break;
		case 0:
			// if -arch is missing, assume we don't want compact unwind info
			fAddCompactUnwindEncoding = false;
			break;
	}
		
	// only ARM main executables can be encrypted
	if ( fOutputKind != Options::kDynamicExecutable )
		fEncryptable = false;
	if ( fArchitecture != CPU_TYPE_ARM )
		fEncryptable = false;

	// don't move inits in dyld because dyld wants certain
	// entries point at stable locations at the start of __text
	if ( fOutputKind == Options::kDyld ) 
		fAutoOrderInitializers = false;
		
		
	// disable __data ordering for some output kinds
	switch ( fOutputKind ) {
		case Options::kObjectFile:
		case Options::kDyld:
		case Options::kStaticExecutable:
		case Options::kPreload:
		case Options::kKextBundle:
			fOrderData = false;
			break;
		case Options::kDynamicExecutable:
		case Options::kDynamicLibrary:
		case Options::kDynamicBundle:
			break;
	}
	
	// only use compressed LINKEDIT for final linked images
	switch ( fOutputKind ) {
		case Options::kDynamicExecutable:
		case Options::kDynamicLibrary:
		case Options::kDynamicBundle:
			break;
		case Options::kPreload:
		case Options::kStaticExecutable:
		case Options::kObjectFile:
		case Options::kDyld:
		case Options::kKextBundle:
			fMakeCompressedDyldInfoForceOff = true;
			break;
	}
	if ( fMakeCompressedDyldInfoForceOff ) 
		fMakeCompressedDyldInfo = false;

	
	// only use compressed LINKEDIT for:
	//			Mac OS X 10.6 or later
	//			iOS 3.1 or later
	if ( fMakeCompressedDyldInfo ) {
		if ( !minOS(ld::mac10_6, ld::iOS_3_1) )
			fMakeCompressedDyldInfo = false;
	}


	// only ARM enforces that cpu-sub-types must match
	if ( fArchitecture != CPU_TYPE_ARM )
		fAllowCpuSubtypeMismatches = true;
		
	// only final linked images can not optimize zero fill sections
	if ( fOutputKind == Options::kObjectFile )
		fOptimizeZeroFill = true;

	// all undefines in -r mode
//	if ( fOutputKind == Options::kObjectFile )
//		fUndefinedTreatment = kUndefinedSuppress;

	// only dynamic final linked images should warn about use of commmons
	if ( fWarnCommons ) {
		switch ( fOutputKind ) {
			case Options::kDynamicExecutable:
			case Options::kDynamicLibrary:
			case Options::kDynamicBundle:
				break;
			case Options::kPreload:
			case Options::kStaticExecutable:
			case Options::kObjectFile:
			case Options::kDyld:
			case Options::kKextBundle:
				fWarnCommons = false;
				break;
		}
	}
	
	// Mac OS X 10.5 and iPhoneOS 2.0 support LC_REEXPORT_DYLIB
	if ( minOS(ld::mac10_5, ld::iOS_2_0) )
		fUseSimplifiedDylibReExports = true;
	
	// Mac OS X 10.7 and iOS 4.2 support LC_LOAD_UPWARD_DYLIB
	if ( minOS(ld::mac10_7, ld::iOS_4_2) && (fOutputKind == kDynamicLibrary) )
		fCanUseUpwardDylib = true;
		
	// MacOSX 10.7 defaults to PIE
	if ( ((fArchitecture == CPU_TYPE_X86_64) || (fArchitecture == CPU_TYPE_I386))
		&& (fOutputKind == kDynamicExecutable)
		&& (fMacVersionMin >= ld::mac10_7) ) {
			fPositionIndependentExecutable = true;
	}

	// armv7 for iOS4.3 defaults to PIE
	if ( (fArchitecture == CPU_TYPE_ARM) 
		&& fArchSupportsThumb2
		&& (fOutputKind == kDynamicExecutable) 
		&& (fIOSVersionMin >= ld::iOS_4_3) ) {
			fPositionIndependentExecutable = true;
	}

	// -no_pie anywhere on command line disable PIE
	if ( fDisablePositionIndependentExecutable )
		fPositionIndependentExecutable = false;

	// set fOutputSlidable
	switch ( fOutputKind ) {
		case Options::kObjectFile:
			fOutputSlidable = false;
			break;
		case Options::kStaticExecutable:
		case Options::kDynamicExecutable:
			fOutputSlidable = fPositionIndependentExecutable;
			break;
		case Options::kPreload:
			fOutputSlidable = fPIEOnCommandLine;
			break;
		case Options::kDyld:
		case Options::kDynamicLibrary:
		case Options::kDynamicBundle:
		case Options::kKextBundle:
			fOutputSlidable = true;
			break;
	}

	// let linker know if thread local variables are supported
	if ( fMacVersionMin >= ld::mac10_7 ) {
		fTLVSupport = true;
	}
	
	// default to adding version load command for dynamic code, static code must opt-in
	switch ( fOutputKind ) {
		case Options::kObjectFile:
			fVersionLoadCommand = false;
			break;
		case Options::kStaticExecutable:
		case Options::kPreload:
		case Options::kKextBundle:
			if ( fVersionLoadCommandForcedOn )
				fVersionLoadCommand = true;
			break;
		case Options::kDynamicExecutable:
		case Options::kDyld:
		case Options::kDynamicLibrary:
		case Options::kDynamicBundle:
			if ( !fVersionLoadCommandForcedOff )
				fVersionLoadCommand = true;
			// <rdar://problem/9945513> for now, don't create version load commands for iOS simulator builds
			if ( fVersionLoadCommand && (fArchitecture == CPU_TYPE_I386) ) {
				for (std::vector<const char*>::iterator sdkit = fSDKPaths.begin(); sdkit != fSDKPaths.end(); sdkit++) {
					if ( strstr(*sdkit, "/iPhoneSimulator.platform/") != NULL )
						fVersionLoadCommand = false;
				}
			}
			break;
	}
	
	// default to adding functions start for dynamic code, static code must opt-in
	switch ( fOutputKind ) {
		case Options::kPreload:
		case Options::kStaticExecutable:
		case Options::kKextBundle:
			if ( fDataInCodeInfoLoadCommandForcedOn )
				fDataInCodeInfoLoadCommand = true;
			if ( fFunctionStartsForcedOn )
				fFunctionStartsLoadCommand = true;
			break;
		case Options::kObjectFile:
		case Options::kDynamicExecutable:
		case Options::kDyld:
		case Options::kDynamicLibrary:
		case Options::kDynamicBundle:
			if ( !fDataInCodeInfoLoadCommandForcedOff )
				fDataInCodeInfoLoadCommand = true;
			if ( !fFunctionStartsForcedOff )
				fFunctionStartsLoadCommand = true;
			break;
	}
		
	// support re-export of individual symbols in MacOSX 10.7 and iOS 4.2
	if ( (fOutputKind == kDynamicLibrary) && minOS(ld::mac10_7, ld::iOS_4_2) )
		fCanReExportSymbols = true;
	
	// ObjC optimization is only in dynamic final linked images
	switch ( fOutputKind ) {
		case Options::kObjectFile:
		case Options::kStaticExecutable:
		case Options::kPreload:
		case Options::kKextBundle:
		case Options::kDyld:
			fObjcCategoryMerging = false;
			break;
		case Options::kDynamicExecutable:
		case Options::kDynamicLibrary:
		case Options::kDynamicBundle:
			break;
	}

	// i386 main executables linked on Mac OS X 10.7 default to NX heap
	// regardless of target unless overriden with -allow_heap_execute anywhere
	// on the command line
	if ( (fArchitecture == CPU_TYPE_I386) && (fOutputKind == kDynamicExecutable) && !fDisableNonExecutableHeap)
		fNonExecutableHeap = true;
		
	// Use LC_MAIN instead of LC_UNIXTHREAD for newer OSs
	switch ( fOutputKind ) {
		case Options::kDynamicExecutable:
			if ( fEntryPointLoadCommandForceOn ) {
				fEntryPointLoadCommand = true;
				fEntryName = "_main";
			}
			else if ( fEntryPointLoadCommandForceOff ) {
				fNeedsThreadLoadCommand = true;
			}
			else {
				if ( (fIOSVersionMin != ld::iOSVersionUnset) && (fArchitecture == CPU_TYPE_I386) ) {
					// don't use LC_MAIN for simulator until min host OS is 10.8 for simulator
					fNeedsThreadLoadCommand = true;
					fEntryPointLoadCommand = false;
				}
				else if ( minOS(ld::mac10_8, ld::iOS_6_0) ) {
					fEntryPointLoadCommand = true;
					fEntryName = "_main";
				}
				else
					fNeedsThreadLoadCommand = true;
			}
			break;
		case Options::kObjectFile:
		case Options::kKextBundle:
		case Options::kDynamicLibrary:
		case Options::kDynamicBundle:
			break;
			
		case Options::kStaticExecutable:
		case Options::kPreload:
		case Options::kDyld:
			fNeedsThreadLoadCommand = true;
			break;
	}
	
	// add LC_SOURCE_VERSION
	switch ( fOutputKind ) {
		case Options::kDynamicExecutable:
		case Options::kKextBundle:
		case Options::kDynamicLibrary:
		case Options::kDynamicBundle:
		case Options::kDyld:
		case Options::kStaticExecutable:
			if ( fSourceVersionLoadCommandForceOn ) {
				fSourceVersionLoadCommand = true;
			}
			else if ( fSourceVersionLoadCommandForceOff ) {
				fSourceVersionLoadCommand = false;
			}
			else {
				if ( minOS(ld::mac10_8, ld::iOS_6_0) ) {
					fSourceVersionLoadCommand = true;
				}
				else
					fSourceVersionLoadCommand = false;
			}
			break;
		case Options::kObjectFile:
		case Options::kPreload:
			fSourceVersionLoadCommand = false;
			break;
	}
	
	
	// add LC_DYLIB_CODE_SIGN_DRS
	switch ( fOutputKind ) {
		case Options::kDynamicExecutable:
		case Options::kDynamicLibrary:
		case Options::kDynamicBundle:
			if ( fDependentDRInfoForcedOn ) {
				fDependentDRInfo = true;
			}
			else if ( fDependentDRInfoForcedOff ) {
				fDependentDRInfo = false;
			}
			else {
				if ( minOS(ld::mac10_8, ld::iOS_6_0) ) 
					fDependentDRInfo = true;
				else
					fDependentDRInfo = false;
			}
			break;
		case Options::kKextBundle:
		case Options::kDyld:
		case Options::kStaticExecutable:
		case Options::kObjectFile:
		case Options::kPreload:
			fDependentDRInfo = false;
			break;
	}
	
	// if -sdk_version not on command line, infer from -syslibroot
	if ( (fSDKVersion == 0) && (fSDKPaths.size() > 0) ) {
		const char* sdkPath = fSDKPaths.front();
		const char* end = &sdkPath[strlen(sdkPath)-1];
		while ( !isdigit(*end) && (end > sdkPath) )
			--end;
		const char* start = end-1;
		while ( (isdigit(*start) || (*start == '.')) && (start > sdkPath))
			--start;
		char sdkVersionStr[32];
		int len = end-start+1;
		if ( len > 2 ) {
			strlcpy(sdkVersionStr, start+1, len);
			fSDKVersion = parseVersionNumber32(sdkVersionStr);
		}
	}
	
	// if -sdk_version and -syslibroot not used, but targeting MacOSX, use current OS version
	if ( (fSDKVersion == 0) && (fMacVersionMin != ld::macVersionUnset) ) {
		// special case if RC_ProjectName and MACOSX_DEPLOYMENT_TARGET are both set that sdkversion=minos
		if ( getenv("RC_ProjectName") && getenv("MACOSX_DEPLOYMENT_TARGET") ) {
			fSDKVersion = fMacVersionMin;
		}
		else {
			int mib[2] = { CTL_KERN, KERN_OSRELEASE };
			char kernVersStr[100];
			size_t strlen = sizeof(kernVersStr);
			if ( sysctl(mib, 2, kernVersStr, &strlen, NULL, 0) != -1 ) {
				uint32_t kernVers = parseVersionNumber32(kernVersStr);
				int minor = (kernVers >> 16) - 4;  // kernel major version is 4 ahead of x in 10.x
				fSDKVersion = 0x000A0000 + (minor << 8);
			}
		}
	}
	
}

void Options::checkIllegalOptionCombinations()
{
	// check -undefined setting
	switch ( fUndefinedTreatment ) {
		case kUndefinedError:
		case kUndefinedDynamicLookup:
			// always legal
			break;
		case kUndefinedWarning:
		case kUndefinedSuppress:
			// requires flat namespace
			if ( fNameSpace == kTwoLevelNameSpace )
				throw "can't use -undefined warning or suppress with -twolevel_namespace";
			break;
	}

	// unify -sub_umbrella with dylibs
	for (std::vector<const char*>::iterator it = fSubUmbellas.begin(); it != fSubUmbellas.end(); it++) {
		const char* subUmbrella = *it;
		bool found = false;
		for (std::vector<Options::FileInfo>::iterator fit = fInputFiles.begin(); fit != fInputFiles.end(); fit++) {
			Options::FileInfo& info = *fit;
			const char* lastSlash = strrchr(info.path, '/');
			if ( lastSlash == NULL )
				lastSlash = info.path - 1;
			if ( strcmp(&lastSlash[1], subUmbrella) == 0 ) {
				info.options.fReExport = true;
				found = true;
                fLinkSnapshot.recordSubUmbrella(info.path);
				break;
			}
		}
		if ( ! found  )
			warning("-sub_umbrella %s does not match a supplied dylib", subUmbrella);
	}

	// unify -sub_library with dylibs
	for (std::vector<const char*>::iterator it = fSubLibraries.begin(); it != fSubLibraries.end(); it++) {
		const char* subLibrary = *it;
		bool found = false;
		for (std::vector<Options::FileInfo>::iterator fit = fInputFiles.begin(); fit != fInputFiles.end(); fit++) {
			Options::FileInfo& info = *fit;
			const char* lastSlash = strrchr(info.path, '/');
			if ( lastSlash == NULL )
				lastSlash = info.path - 1;
			const char* dot = strchr(&lastSlash[1], '.');
			if ( dot == NULL )
				dot = &lastSlash[strlen(lastSlash)];
			if ( strncmp(&lastSlash[1], subLibrary, dot-lastSlash-1) == 0 ) {
				info.options.fReExport = true;
				found = true;
                fLinkSnapshot.recordSubLibrary(info.path);
				break;
			}
		}
		if ( ! found  )
			warning("-sub_library %s does not match a supplied dylib", subLibrary);
	}

	// sync reader options
	if ( fNameSpace != kTwoLevelNameSpace )
		fFlatNamespace = true;

	// check -stack_addr
	if ( fStackAddr != 0 ) {
		switch (fArchitecture) {
			case CPU_TYPE_I386:
            case CPU_TYPE_ARM:
				if ( fStackAddr > 0xFFFFFFFF )
					throw "-stack_addr must be < 4G for 32-bit processes";
				break;
			case CPU_TYPE_X86_64:
				break;
		}
		if ( (fStackAddr & -4096) != fStackAddr )
			throw "-stack_addr must be multiples of 4K";
		if ( fStackSize  == 0 )
			throw "-stack_addr must be used with -stack_size";
	}

	// check -stack_size
	if ( fStackSize != 0 ) {
		switch (fArchitecture) {
			case CPU_TYPE_I386:
				if ( fStackSize > 0xFFFFFFFF )
					throw "-stack_size must be < 4G for 32-bit processes";
				if ( fStackAddr == 0 ) {
					fStackAddr = 0xC0000000;
				}
				if ( (fStackAddr > 0xB0000000) && ((fStackAddr-fStackSize) < 0xB0000000)  )
					warning("custom stack placement overlaps and will disable shared region");
				break;
            case CPU_TYPE_ARM:
				if ( fStackSize > 0x2F000000 )
					throw "-stack_size must be < 752MB";
				if ( fStackAddr == 0 )
					fStackAddr = 0x2F000000;
                if ( fStackAddr > 0x30000000)
                    throw "-stack_addr must be < 0x30000000 for arm";
			case CPU_TYPE_X86_64:
				if ( fStackAddr == 0 ) {
					fStackAddr = 0x00007FFF5C000000LL;
				}
				break;
		}
		if ( (fStackSize & -4096) != fStackSize )
			throw "-stack_size must be multiples of 4K";
		switch ( fOutputKind ) {
			case Options::kDynamicExecutable:
			case Options::kStaticExecutable:
				// custom stack size only legal when building main executable
				break;
			case Options::kDynamicLibrary:
			case Options::kDynamicBundle:
			case Options::kObjectFile:
			case Options::kDyld:
			case Options::kPreload:
			case Options::kKextBundle:
				throw "-stack_size option can only be used when linking a main executable";
		}
		if ( fStackSize > fStackAddr )
			throwf("-stack_size (0x%08llX) must be smaller than -stack_addr (0x%08llX)", fStackSize, fStackAddr);
	}

	// check that -allow_stack_execute is only used with main executables
	if ( fExecutableStack ) {
		switch ( fOutputKind ) {
			case Options::kDynamicExecutable:
			case Options::kStaticExecutable:
				// -allow_stack_execute size only legal when building main executable
				break;
			case Options::kDynamicLibrary:
			case Options::kDynamicBundle:
			case Options::kObjectFile:
			case Options::kDyld:
			case Options::kPreload:
			case Options::kKextBundle:
				throw "-allow_stack_execute option can only be used when linking a main executable";
		}
	}

	// check that -allow_heap_execute is only used with i386 main executables
	if ( fDisableNonExecutableHeap ) {
		if ( fArchitecture != CPU_TYPE_I386 )
			throw "-allow_heap_execute option can only be used when linking for i386";
		switch ( fOutputKind ) {
			case Options::kDynamicExecutable:
				// -allow_heap_execute only legal when building main executable
				break;
			case Options::kStaticExecutable:
			case Options::kDynamicLibrary:
			case Options::kDynamicBundle:
			case Options::kObjectFile:
			case Options::kDyld:
			case Options::kPreload:
			case Options::kKextBundle:
				throw "-allow_heap_execute option can only be used when linking a main executable";
		}
	}

	// check -client_name is only used when making a bundle or main executable
	if ( fClientName != NULL ) {
		switch ( fOutputKind ) {
			case Options::kDynamicExecutable:
			case Options::kDynamicBundle:
				break;
			case Options::kStaticExecutable:
			case Options::kDynamicLibrary:
			case Options::kObjectFile:
			case Options::kDyld:
			case Options::kPreload:
			case Options::kKextBundle:
				throw "-client_name can only be used with -bundle";
		}
	}
	
	// check -init is only used when building a dylib
	if ( (fInitFunctionName != NULL) && (fOutputKind != Options::kDynamicLibrary) )
		throw "-init can only be used with -dynamiclib";

	// check -bundle_loader only used with -bundle
	if ( (fBundleLoader != NULL) && (fOutputKind != Options::kDynamicBundle) )
		throw "-bundle_loader can only be used with -bundle";

	// check -dtrace not used with -r
	if ( (fDtraceScriptName != NULL) && (fOutputKind == Options::kObjectFile) )
		throw "-dtrace can only be used when creating final linked images";

	// check -d can only be used with -r
	if ( fMakeTentativeDefinitionsReal && (fOutputKind != Options::kObjectFile) )
		throw "-d can only be used with -r";

	// check that -root_safe is not used with -r
	if ( fRootSafe && (fOutputKind == Options::kObjectFile) )
		throw "-root_safe cannot be used with -r";

	// check that -setuid_safe is not used with -r
	if ( fSetuidSafe && (fOutputKind == Options::kObjectFile) )
		throw "-setuid_safe cannot be used with -r";

	// rdar://problem/4718189 map ObjC class names to new runtime names
	bool alterObjC1ClassNamesToObjC2 = false;
	switch (fArchitecture) {
		case CPU_TYPE_I386:
			// i386 only uses new symbols when using objc2 ABI
			if ( fObjCABIVersion2Override )
				alterObjC1ClassNamesToObjC2 = true;
			break;
		case CPU_TYPE_X86_64:
		case CPU_TYPE_ARM:
			alterObjC1ClassNamesToObjC2 = true;
			break;
	}

	// make sure all required exported symbols exist
	std::vector<const char*> impliedExports;
	for (NameSet::iterator it=fExportSymbols.regularBegin(); it != fExportSymbols.regularEnd(); ++it) {
		const char* name = *it;
		const int len = strlen(name);
		if ( (strcmp(&name[len-3], ".eh") == 0) || (strncmp(name, ".objc_category_name_", 20) == 0) ) {
			// never export .eh symbols
			warning("ignoring %s in export list", name);
		}
		else if ( (fArchitecture == CPU_TYPE_I386) && !fObjCABIVersion2Override && (strncmp(name, "_OBJC_CLASS_$", 13) == 0) ) {
			warning("ignoring Objc2 Class symbol %s in i386 export list", name);
			fRemovedExports.insert(name);
		}
		else if ( alterObjC1ClassNamesToObjC2 && (strncmp(name, ".objc_class_name_", 17) == 0) ) {
			// linking ObjC2 ABI, but have ObjC1 ABI name in export list.  Change it to intended name
			fRemovedExports.insert(name);
			char* temp;
			asprintf(&temp, "_OBJC_CLASS_$_%s", &name[17]);
			impliedExports.push_back(temp);
			asprintf(&temp, "_OBJC_METACLASS_$_%s", &name[17]);
			impliedExports.push_back(temp);
		}
		else {
			fInitialUndefines.push_back(name);
		}
	}
	fExportSymbols.remove(fRemovedExports);
	for (std::vector<const char*>::iterator it=impliedExports.begin(); it != impliedExports.end(); ++it) {
		const char* name = *it;
		fExportSymbols.insert(name);
		fInitialUndefines.push_back(name);
	}

	// make sure all required re-exported symbols exist
	for (NameSet::iterator it=fReExportSymbols.regularBegin(); it != fReExportSymbols.regularEnd(); ++it) {
		fInitialUndefines.push_back(*it);
	}
	
	// make sure that -init symbol exists
	if ( fInitFunctionName != NULL )
		fInitialUndefines.push_back(fInitFunctionName);

	// make sure that entry symbol exists
	switch ( fOutputKind ) {
		case Options::kDynamicExecutable:
		case Options::kStaticExecutable:
		case Options::kDyld:
		case Options::kPreload:
			fInitialUndefines.push_back(fEntryName);
			break;
		case Options::kDynamicLibrary:
		case Options::kDynamicBundle:
		case Options::kObjectFile:
		case Options::kKextBundle:
			break;
	}

	// make sure every alias base exists
	for (std::vector<AliasPair>::iterator it=fAliases.begin(); it != fAliases.end(); ++it) {
		fInitialUndefines.push_back(it->realName);
	}

	// check custom segments
	if ( fCustomSegmentAddresses.size() != 0 ) {
		// verify no segment is in zero page
		if ( fZeroPageSize != ULLONG_MAX ) {
			for (std::vector<SegmentStart>::iterator it = fCustomSegmentAddresses.begin(); it != fCustomSegmentAddresses.end(); ++it) {
				if ( it->address < fZeroPageSize )
					throwf("-segaddr %s 0x%llX conflicts with -pagezero_size", it->name, it->address);
			}
		}
		// verify no duplicates
		for (std::vector<SegmentStart>::iterator it = fCustomSegmentAddresses.begin(); it != fCustomSegmentAddresses.end(); ++it) {
			for (std::vector<SegmentStart>::iterator it2 = fCustomSegmentAddresses.begin(); it2 != fCustomSegmentAddresses.end(); ++it2) {
				if ( (it->address == it2->address) && (it != it2) )
					throwf("duplicate -segaddr addresses for %s and %s", it->name, it2->name);
			}
			// a custom segment address of zero will disable the use of a zero page
			if ( it->address == 0 )
				fZeroPageSize = 0;
		}
	}

	if ( fZeroPageSize == ULLONG_MAX ) {
		// zero page size not specified on command line, set default
		switch (fArchitecture) {
			case CPU_TYPE_I386:
            case CPU_TYPE_ARM:
				// first 4KB for 32-bit architectures
				fZeroPageSize = 0x1000;
				break;
			case CPU_TYPE_X86_64:
				// first 4GB for x86_64 on all OS's
				fZeroPageSize = 0x100000000ULL;
				break;
			default:
				// if -arch not used, default to 4K zero-page
				fZeroPageSize = 0x1000;
		}
	}
	else {
		switch ( fOutputKind ) {
			case Options::kDynamicExecutable:
			case Options::kStaticExecutable:
				// -pagezero_size size only legal when building main executable
				break;
			case Options::kDynamicLibrary:
			case Options::kDynamicBundle:
			case Options::kObjectFile:
			case Options::kDyld:
			case Options::kPreload:
			case Options::kKextBundle:
				if ( fZeroPageSize != 0 )
					throw "-pagezero_size option can only be used when linking a main executable";
		}
	}

	// if main executable with custom base address, model zero page as custom segment
	if ( (fOutputKind == Options::kDynamicExecutable) && (fBaseAddress != 0) && (fZeroPageSize != 0) ) {
		SegmentStart seg;
		seg.name = "__PAGEZERO";
		seg.address = 0;;
		fCustomSegmentAddresses.push_back(seg);
	}

	// -dead_strip and -r are incompatible
	if ( fDeadStrip && (fOutputKind == Options::kObjectFile) )
		throw "-r and -dead_strip cannot be used together";

	// can't use -rpath unless targeting 10.5 or later
	if ( fRPaths.size() > 0 ) {
		if ( !minOS(ld::mac10_5, ld::iOS_2_0) )
			throw "-rpath can only be used when targeting Mac OS X 10.5 or later";
		switch ( fOutputKind ) {
			case Options::kDynamicExecutable:
			case Options::kDynamicLibrary:
			case Options::kDynamicBundle:
				break;
			case Options::kStaticExecutable:
			case Options::kObjectFile:
			case Options::kDyld:
			case Options::kPreload:
			case Options::kKextBundle:
				throw "-rpath can only be used when creating a dynamic final linked image";
		}
	}
	
	if ( fPositionIndependentExecutable ) {
		switch ( fOutputKind ) {
			case Options::kDynamicExecutable:
				// check -pie is only used when building a dynamic main executable for 10.5
				if ( !minOS(ld::mac10_5, ld::iOS_4_2) ) {
					if ( fIOSVersionMin == ld::iOSVersionUnset )
						throw "-pie can only be used when targeting Mac OS X 10.5 or later";
					else
						throw "-pie can only be used when targeting iOS 4.2 or later";
				}
				break;
			case Options::kStaticExecutable:
			case Options::kPreload:
				// -pie is ok with -static or -preload
				break;
			case Options::kDynamicLibrary:
			case Options::kDynamicBundle:
				warning("-pie being ignored. It is only used when linking a main executable");
				fPositionIndependentExecutable = false;
				break;
			case Options::kObjectFile:
			case Options::kDyld:
			case Options::kKextBundle:
				throw "-pie can only be used when linking a main executable";
		}
	}
	
	// check -read_only_relocs is not used with x86_64
	if ( fAllowTextRelocs ) {
		if ( (fArchitecture == CPU_TYPE_X86_64) && (fOutputKind != kKextBundle) ) {
			warning("-read_only_relocs cannot be used with x86_64");
			fAllowTextRelocs = false;
		}
	}
	
	// check -mark_auto_dead_strip is only used with dylibs
	if ( fMarkDeadStrippableDylib ) {
		if ( fOutputKind != Options::kDynamicLibrary ) {
			warning("-mark_auto_dead_strip can only be used when creating a dylib");
			fMarkDeadStrippableDylib = false;
		}
	}
	
	// -force_cpusubtype_ALL is not supported for ARM
	if ( fForceSubtypeAll ) {
		if ( fArchitecture == CPU_TYPE_ARM ) {
			warning("-force_cpusubtype_ALL will become unsupported for ARM architectures");
		}
	}
	
	// -reexported_symbols_list can only be used with -dynamiclib
	if ( !fReExportSymbols.empty() ) {
		if ( fOutputKind != Options::kDynamicLibrary )
			throw "-reexported_symbols_list can only used used when created dynamic libraries";
		if ( !minOS(ld::mac10_7, ld::iOS_4_2) )
			throw "targeted OS version does not support -reexported_symbols_list";
	}
	
	// -dyld_env can only be used with main executables
	if ( (fOutputKind != Options::kDynamicExecutable) && (fDyldEnvironExtras.size() != 0) )
		throw "-dyld_env can only used used when created main executables";
}	


void Options::checkForClassic(int argc, const char* argv[])
{
	// scan options
	bool archFound = false;
	bool staticFound = false;
	bool dtraceFound = false;
	bool kextFound = false;
	bool rFound = false;
	bool creatingMachKernel = false;
	bool newLinker = false;
	
	// build command line buffer in case ld crashes
#if __MAC_OS_X_VERSION_MIN_REQUIRED >= 1070 && HAVE_CRASHREPORTER_HEADER
	CRSetCrashLogMessage(crashreporterBuffer);
#endif
	const char* srcRoot = getenv("SRCROOT");
	if ( srcRoot != NULL ) {
		strlcpy(crashreporterBuffer, "SRCROOT=", crashreporterBufferSize);
		strlcat(crashreporterBuffer, srcRoot, crashreporterBufferSize);
		strlcat(crashreporterBuffer, "\n", crashreporterBufferSize);
	}
#ifdef LD_VERS
	strlcat(crashreporterBuffer, LD_VERS, crashreporterBufferSize);
	strlcat(crashreporterBuffer, "\n", crashreporterBufferSize);
#endif
	strlcat(crashreporterBuffer, "ld ", crashreporterBufferSize);
	for(int i=1; i < argc; ++i) {
		strlcat(crashreporterBuffer, argv[i], crashreporterBufferSize);
		strlcat(crashreporterBuffer, " ", crashreporterBufferSize);
	}

	for(int i=0; i < argc; ++i) {
		const char* arg = argv[i];
		if ( arg[0] == '-' ) {
			if ( strcmp(arg, "-arch") == 0 ) {
				parseArch(argv[++i]);
				archFound = true;
			}
			else if ( strcmp(arg, "-static") == 0 ) {
				staticFound = true;
			}
			else if ( strcmp(arg, "-kext") == 0 ) {
				kextFound = true;
			}
			else if ( strcmp(arg, "-dtrace") == 0 ) {
				dtraceFound = true;
			}
			else if ( strcmp(arg, "-r") == 0 ) {
				rFound = true;
			}
			else if ( strcmp(arg, "-new_linker") == 0 ) {
				newLinker = true;
			}
			else if ( strcmp(arg, "-classic_linker") == 0 ) {
				// ld_classic does not understand this option, so remove it
				for(int j=i; j < argc; ++j)
					argv[j] = argv[j+1];
				warning("using ld_classic");
				this->gotoClassicLinker(argc-1, argv);
			}
			else if ( strcmp(arg, "-o") == 0 ) {
				const char* outfile = argv[++i];
				if ( (outfile != NULL) && (strstr(outfile, "/mach_kernel") != NULL) )
					creatingMachKernel = true;
			}
		}
	}
}

void Options::gotoClassicLinker(int argc, const char* argv[])
{
	argv[0] = PROGRAM_PREFIX "ld_classic";
	// ld_classic does not support -iphoneos_version_min, so change
	for(int j=0; j < argc; ++j) {
		if ( (strcmp(argv[j], "-iphoneos_version_min") == 0) || (strcmp(argv[j], "-ios_version_min") == 0) ) {
			argv[j] = "-macosx_version_min";
			if ( j < argc-1 )
				argv[j+1] = "10.5";
			break;
		}
	}
	// ld classic does not understand -kext (change to -static -r)
	for(int j=0; j < argc; ++j) {
		if ( strcmp(argv[j], "-kext") == 0) 
			argv[j] = "-r";
		else if ( strcmp(argv[j], "-dynamic") == 0) 
			argv[j] = "-static";
	}
	// ld classic does not understand -demangle 
	for(int j=0; j < argc; ++j) {
		if ( strcmp(argv[j], "-demangle") == 0) 
			argv[j] = "-noprebind";
	}
	// in -v mode, print command line passed to ld_classic
	for(int i=0; i < argc; ++i) {
		if ( strcmp(argv[i], "-v") == 0 ) {
			for(int j=0; j < argc; ++j)
				printf("%s ", argv[j]);
			printf("\n");
			break;
		}
	}
	char rawPath[PATH_MAX];
	char path[PATH_MAX];
	uint32_t bufSize = PATH_MAX;
	if ( _NSGetExecutablePath(rawPath, &bufSize) != -1 ) {
		if ( realpath(rawPath, path) != NULL ) {
			char* lastSlash = strrchr(path, '/');
			if ( lastSlash != NULL ) {
				strcpy(lastSlash+1, "ld_classic");
				argv[0] = path;
				execvp(path, (char**)argv);
			}
		}
	}
	// in case of error in above, try searching for ld_classic via PATH
	execvp(argv[0], (char**)argv);
	fprintf(stderr, "can't exec ld_classic\n");
	exit(1);
}


// Note, returned string buffer is own by this function.
// It should not be freed
// It will be reused, so clients need to strdup() if they want
// to use it long term.
const char* Options::demangleSymbol(const char* sym) const
{
	// only try to demangle symbols if -demangle on command line
	if ( !fDemangle )
		return sym;

	// only try to demangle symbols that look like C++ symbols
	if ( strncmp(sym, "__Z", 3) != 0 )
		return sym;

	static size_t size = 1024;
	static char* buff = (char*)malloc(size);
	int status;
	
	char* result = abi::__cxa_demangle(&sym[1], buff, &size, &status); 
	if ( result != NULL ) {
		// if demangling successful, keep buffer for next demangle
		buff = result;
		return buff;
	}
	return sym;
}

