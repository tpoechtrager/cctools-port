//===- tapi/Core/TextStub_v1.cpp - Text Stub v1 -----------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// \brief Implements the text stub file (TBD v1) reader/writer.
///
//===----------------------------------------------------------------------===//

#include "tapi/Core/TextStub_v1.h"
#include "tapi/Core/InterfaceFile.h"
#include "tapi/Core/Registry.h"
#include "tapi/Core/YAML.h"
#include "tapi/Core/YAMLReaderWriter.h"
#include <set>

using namespace llvm;
using namespace llvm::yaml;
using namespace TAPI_INTERNAL;
using namespace TAPI_INTERNAL::stub::v1;
using TAPI_INTERNAL::SymbolFlags;

LLVM_YAML_IS_FLOW_SEQUENCE_VECTOR(StringRef)
LLVM_YAML_IS_SEQUENCE_VECTOR(ExportSection)

namespace llvm {
namespace yaml {

template <> struct MappingTraits<ExportSection> {
  static void mapping(IO &io, ExportSection &section) {
    io.mapRequired("archs", section.archs);
    io.mapOptional("allowed-clients", section.allowableClients);
    io.mapOptional("re-exports", section.reexportedLibraries);
    io.mapOptional("symbols", section.symbols);
    io.mapOptional("objc-classes", section.classes);
    io.mapOptional("objc-ivars", section.ivars);
    io.mapOptional("weak-def-symbols", section.weakDefSymbols);
    io.mapOptional("thread-local-symbols", section.tlvSymbols);
  }
};

template <> struct MappingTraits<const InterfaceFile *> {
  struct NormalizedTBD1 {
    NormalizedTBD1(IO &io) {}
    NormalizedTBD1(IO &io, const InterfaceFile *&file) {
      archs = file->getArchitectures();
      platform = file->getPlatform();
      installName = file->getInstallName();
      currentVersion = file->getCurrentVersion();
      compatibilityVersion = file->getCompatibilityVersion();
      swiftVersion = file->getSwiftVersion();
      objcConstraint = file->getObjCConstraint();

      std::set<ArchitectureSet> archSet;
      for (const auto &library : file->allowableClients())
        archSet.insert(library.getArchitectures());

      for (const auto &library : file->reexportedLibraries())
        archSet.insert(library.getArchitectures());

      std::map<const TAPI_INTERNAL::Symbol *, ArchitectureSet> symbolToArchSet;
      for (const auto &it : file->exports()) {
        auto &symbol = it.second;
        if (symbol.isUnavailable())
          continue;

        ArchitectureSet archs;
        for (auto availMap : symbol._availability) {
          if (availMap.second._unavailable)
            continue;
          archs.set(availMap.first);
        }
        symbolToArchSet[&symbol] = archs;
        archSet.insert(archs);
      }

      for (auto archs : archSet) {
        ExportSection section;
        section.archs = archs;

        for (const auto &library : file->allowableClients())
          if (library.getArchitectures() == archs)
            section.allowableClients.emplace_back(library.getInstallName());

        for (const auto &library : file->reexportedLibraries())
          if (library.getArchitectures() == archs)
            section.reexportedLibraries.emplace_back(library.getInstallName());

        for (const auto &symArch : symbolToArchSet) {
          if (symArch.second != archs)
            continue;

          const auto *symbol = symArch.first;
          switch (symbol->getType()) {
          case SymbolType::Symbol:
            if (symbol->isWeakDefined())
              section.weakDefSymbols.emplace_back(symbol->getName());
            else if (symbol->isThreadLocalValue())
              section.tlvSymbols.emplace_back(symbol->getName());
            else
              section.symbols.emplace_back(symbol->getName());
            break;
          case SymbolType::ObjCClass:
            section.classes.emplace_back(symbol->getName());
            break;
          case SymbolType::ObjCInstanceVariable:
            section.ivars.emplace_back(symbol->getName());
            break;
          }
        }
        sort(section.symbols);
        sort(section.classes);
        sort(section.ivars);
        sort(section.weakDefSymbols);
        sort(section.tlvSymbols);
        exports.emplace_back(std::move(section));
      }
    }

    const InterfaceFile *denormalize(IO &io) {
      auto ctx = reinterpret_cast<YAMLContext *>(io.getContext());
      assert(ctx);

      auto *file = new InterfaceFile;
      file->setPath(ctx->_path);
      file->setFileType(TAPI_INTERNAL::FileType::TBD_V1);
      file->setPlatform(platform);
      file->setArchitectures(archs);
      file->setInstallName(installName);
      file->setCurrentVersion(currentVersion);
      file->setCompatibilityVersion(compatibilityVersion);
      file->setSwiftVersion(swiftVersion);
      file->setTwoLevelNamespace();
      file->setApplicationExtensionSafe();
      file->setObjCConstraint(objcConstraint);

      for (const auto &section : exports) {
        for (const auto &client : section.allowableClients)
          file->addAllowableClient(client, section.archs);
        for (const auto &lib : section.reexportedLibraries)
          file->addReexportedLibrary(lib, section.archs);
        for (auto &sym : section.symbols)
          file->addExportedSymbol(sym.str(), SymbolType::Symbol,
                                  SymbolFlags::None, section.archs);
        for (auto &sym : section.classes)
          file->addExportedSymbol(sym.str(), SymbolType::ObjCClass,
                                  SymbolFlags::None, section.archs);
        for (auto &sym : section.ivars)
          file->addExportedSymbol(sym.str(), SymbolType::ObjCInstanceVariable,
                                  SymbolFlags::None, section.archs);
        for (auto &sym : section.weakDefSymbols)
          file->addExportedSymbol(sym.str(), SymbolType::Symbol,
                                  SymbolFlags::WeakDefined, section.archs);
        for (auto &sym : section.tlvSymbols)
          file->addExportedSymbol(sym.str(), SymbolType::Symbol,
                                  SymbolFlags::ThreadLocalValue, section.archs);
      }

      return file;
    }

    ArchitectureSet archs;
    Platform platform;
    StringRef installName;
    PackedVersion currentVersion;
    PackedVersion compatibilityVersion;
    SwiftVersion swiftVersion;
    ObjCConstraint objcConstraint;
    std::vector<ExportSection> exports;
  };

  static void mappingTBD1(IO &io, const InterfaceFile *&file) {
    MappingNormalization<NormalizedTBD1, const InterfaceFile *> keys(io, file);
    // Don't write the tag into the .tbd file for TBD v1.
    if (!io.outputting())
      io.mapTag("tapi-tbd-v1", true);

    io.mapRequired("archs", keys->archs);
    io.mapRequired("platform", keys->platform);
    io.mapRequired("install-name", keys->installName);
    io.mapOptional("current-version", keys->currentVersion,
                   PackedVersion(1, 0, 0));
    io.mapOptional("compatibility-version", keys->compatibilityVersion,
                   PackedVersion(1, 0, 0));
    io.mapOptional("swift-version", keys->swiftVersion, SwiftVersion(0));
    io.mapOptional("objc-constraint", keys->objcConstraint,
                   ObjCConstraint::None);
    io.mapOptional("exports", keys->exports);
  }
};
}
} // end namespace llvm::yaml.

TAPI_NAMESPACE_INTERNAL_BEGIN

namespace stub {
namespace v1 {

bool TextBasedStubDocumentHandler::canRead(llvm::MemoryBufferRef memBufferRef,
                                           FileType types) const {
  if (!(types & FileType::TBD_V1))
    return false;

  if (!memBufferRef.getBufferIdentifier().endswith(".tbd"))
    return false;

  auto str = memBufferRef.getBuffer().trim();
  if (!(str.startswith("---\n") || str.startswith("--- !tapi-tbd-v1\n")) ||
      !str.endswith("..."))
    return false;

  return true;
}

FileType TextBasedStubDocumentHandler::getFileType(
    llvm::MemoryBufferRef memBufferRef) const {
  if (canRead(memBufferRef))
    return FileType::TBD_V1;

  return FileType::Invalid;
}

bool TextBasedStubDocumentHandler::canWrite(const File *file) const {
  auto *interface = dyn_cast<InterfaceFile>(file);
  if (interface == nullptr)
    return false;

  if (!StringRef(interface->getPath()).endswith(".tbd"))
    return false;

  if (interface->getFileType() != FileType::TBD_V1)
    return false;

  return true;
}

bool TextBasedStubDocumentHandler::handleDocument(IO &io,
                                                  const File *&file) const {
  if (io.outputting() && file->getFileType() != FileType::TBD_V1)
    return false;

  if (!io.outputting() && !io.mapTag("!tapi-tbd-v1") &&
      !io.mapTag("tag:yaml.org,2002:map"))
    return false;

  const auto *interface = dyn_cast_or_null<InterfaceFile>(file);
  MappingTraits<const InterfaceFile *>::mappingTBD1(io, interface);
  file = interface;

  return true;
}
}
} // end namespace stub::v1.

TAPI_NAMESPACE_INTERNAL_END
