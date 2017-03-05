//===- tapi/Core/TextStub_v2.cpp - Text Stub v2 -----------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// \brief Implements the text stub file (TBD v2) reader/writer.
///
//===----------------------------------------------------------------------===//

#include "tapi/Core/TextStub_v2.h"
#include "tapi/Core/InterfaceFile.h"
#include "tapi/Core/Registry.h"
#include "tapi/Core/YAML.h"
#include "tapi/Core/YAMLReaderWriter.h"
#include <set>

using namespace llvm;
using namespace llvm::yaml;
using namespace TAPI_INTERNAL;
using namespace TAPI_INTERNAL::stub::v2;
using TAPI_INTERNAL::SymbolFlags;

using UUID = std::pair<Arch, std::string>;
LLVM_YAML_IS_FLOW_SEQUENCE_VECTOR(StringRef)
LLVM_YAML_IS_FLOW_SEQUENCE_VECTOR(UUID)
LLVM_YAML_IS_SEQUENCE_VECTOR(ExportSection)
LLVM_YAML_IS_SEQUENCE_VECTOR(UndefinedSection)

namespace llvm {
namespace yaml {

template <> struct MappingTraits<ExportSection> {
  static void mapping(IO &io, ExportSection &section) {
    io.mapRequired("archs", section.archs);
    io.mapOptional("allowable-clients", section.allowableClients);
    io.mapOptional("re-exports", section.reexportedLibraries);
    io.mapOptional("symbols", section.symbols);
    io.mapOptional("objc-classes", section.classes);
    io.mapOptional("objc-ivars", section.ivars);
    io.mapOptional("weak-def-symbols", section.weakDefSymbols);
    io.mapOptional("thread-local-symbols", section.tlvSymbols);
  }
};

template <> struct MappingTraits<UndefinedSection> {
  static void mapping(IO &io, UndefinedSection &section) {
    io.mapRequired("archs", section.archs);
    io.mapOptional("symbols", section.symbols);
    io.mapOptional("objc-classes", section.classes);
    io.mapOptional("objc-ivars", section.ivars);
    io.mapOptional("weak-ref-symbols", section.weakRefSymbols);
  }
};

template <> struct ScalarBitSetTraits<Flags> {
  static void bitset(IO &io, Flags &flags) {
    io.bitSetCase(flags, "flat_namespace", Flags::FlatNamespace);
    io.bitSetCase(flags, "not_app_extension_safe",
                  Flags::NotApplicationExtensionSafe);
  }
};

template <> struct ScalarTraits<UUID> {
  static void output(const UUID &value, void *, raw_ostream &os) {
    os << getArchName(value.first) << ": " << value.second;
  }

  static StringRef input(StringRef scalar, void *, UUID &value) {
    auto split = scalar.split(':');
    auto arch = split.first.trim();
    auto uuid = split.second.trim();
    if (uuid.empty())
      return "invalid uuid string pair";
    value.first = getArchType(arch);
    if (value.first == Arch::unknown)
      return "unknown architecture";
    value.second = uuid;
    return StringRef();
  }

  static bool mustQuote(StringRef) { return true; }
};

template <> struct MappingTraits<const InterfaceFile *> {
  struct NormalizedTBD2 {
    NormalizedTBD2(IO &io) {}
    NormalizedTBD2(IO &io, const InterfaceFile *&file) {
      archs = file->getArchitectures();
      uuids = file->uuids();
      platform = file->getPlatform();
      installName = file->getInstallName();
      currentVersion = file->getCurrentVersion();
      compatibilityVersion = file->getCompatibilityVersion();
      swiftVersion = file->getSwiftVersion();
      objcConstraint = file->getObjCConstraint();

      flags = Flags::None;
      if (!file->isApplicationExtensionSafe())
        flags |= Flags::NotApplicationExtensionSafe;

      if (!file->isTwoLevelNamespace())
        flags |= Flags::FlatNamespace;

      parentUmbrella = file->getParentUmbrella();

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

      archSet.clear();
      symbolToArchSet.clear();

      for (const auto &it : file->undefineds()) {
        auto &symbol = it.second;

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
        UndefinedSection section;
        section.archs = archs;

        for (const auto &symArch : symbolToArchSet) {
          if (symArch.second != archs)
            continue;

          const auto *symbol = symArch.first;
          switch (symbol->getType()) {
          case SymbolType::Symbol:
            if (symbol->isWeakReferenced())
              section.weakRefSymbols.emplace_back(symbol->getName());
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
        sort(section.weakRefSymbols);
        undefineds.emplace_back(std::move(section));
      }
    }

    const InterfaceFile *denormalize(IO &io) {
      auto ctx = reinterpret_cast<YAMLContext *>(io.getContext());
      assert(ctx);

      auto *file = new InterfaceFile;
      file->setPath(ctx->_path);
      file->setFileType(TAPI_INTERNAL::FileType::TBD_V2);
      for (auto &id : uuids)
        file->addUUID(id.first, id.second);
      file->setPlatform(platform);
      file->setArchitectures(archs);
      file->setInstallName(installName);
      file->setCurrentVersion(currentVersion);
      file->setCompatibilityVersion(compatibilityVersion);
      file->setSwiftVersion(swiftVersion);
      file->setObjCConstraint(objcConstraint);
      file->setParentUmbrella(parentUmbrella);

      file->setTwoLevelNamespace(!(flags & Flags::FlatNamespace));
      file->setApplicationExtensionSafe(
          !(flags & Flags::NotApplicationExtensionSafe));

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

      for (const auto &section : undefineds) {
        for (auto &sym : section.symbols)
          file->addUndefinedSymbol(sym.str(), SymbolType::Symbol,
                                   SymbolFlags::None, section.archs);
        for (auto &sym : section.classes)
          file->addUndefinedSymbol(sym.str(), SymbolType::ObjCClass,
                                   SymbolFlags::None, section.archs);
        for (auto &sym : section.ivars)
          file->addUndefinedSymbol(sym.str(), SymbolType::ObjCInstanceVariable,
                                   SymbolFlags::None, section.archs);
        for (auto &sym : section.weakRefSymbols)
          file->addUndefinedSymbol(sym.str(), SymbolType::Symbol,
                                   SymbolFlags::WeakReferenced, section.archs);
      }

      return file;
    }

    ArchitectureSet archs;
    std::vector<UUID> uuids;
    Platform platform;
    StringRef installName;
    PackedVersion currentVersion;
    PackedVersion compatibilityVersion;
    SwiftVersion swiftVersion;
    ObjCConstraint objcConstraint;
    Flags flags;
    StringRef parentUmbrella;
    std::vector<ExportSection> exports;
    std::vector<UndefinedSection> undefineds;
  };

  static void mappingTBD2(IO &io, const InterfaceFile *&file) {
    MappingNormalization<NormalizedTBD2, const InterfaceFile *> keys(io, file);
    io.mapTag("!tapi-tbd-v2", true);
    io.mapRequired("archs", keys->archs);
    io.mapOptional("uuids", keys->uuids);
    io.mapRequired("platform", keys->platform);
    io.mapOptional("flags", keys->flags, Flags::None);
    io.mapRequired("install-name", keys->installName);
    io.mapOptional("current-version", keys->currentVersion,
                   PackedVersion(1, 0, 0));
    io.mapOptional("compatibility-version", keys->compatibilityVersion,
                   PackedVersion(1, 0, 0));
    io.mapOptional("swift-version", keys->swiftVersion, SwiftVersion(0));
    io.mapOptional("objc-constraint", keys->objcConstraint,
                   ObjCConstraint::Retain_Release);
    io.mapOptional("parent-umbrella", keys->parentUmbrella, StringRef());
    io.mapOptional("exports", keys->exports);
    io.mapOptional("undefineds", keys->undefineds);
  }
};

} // end namespace yaml.
} // end namespace llvm.

TAPI_NAMESPACE_INTERNAL_BEGIN

namespace stub {
namespace v2 {

bool TextBasedStubDocumentHandler::canRead(llvm::MemoryBufferRef memBufferRef,
                                           FileType types) const {
  if (!(types & FileType::TBD_V2))
    return false;

  if (!memBufferRef.getBufferIdentifier().endswith(".tbd"))
    return false;

  auto str = memBufferRef.getBuffer().trim();
  if (!str.startswith("--- !tapi-tbd-v2\n") || !str.endswith("..."))
    return false;

  return true;
}

FileType TextBasedStubDocumentHandler::getFileType(
    llvm::MemoryBufferRef memBufferRef) const {
  if (canRead(memBufferRef))
    return FileType::TBD_V2;

  return FileType::Invalid;
}

bool TextBasedStubDocumentHandler::canWrite(const File *file) const {
  auto *interface = dyn_cast<InterfaceFile>(file);
  if (interface == nullptr)
    return false;

  if (!StringRef(interface->getPath()).endswith(".tbd"))
    return false;

  if (interface->getFileType() != FileType::TBD_V2)
    return false;

  return true;
}

bool TextBasedStubDocumentHandler::handleDocument(IO &io,
                                                  const File *&file) const {
  if (io.outputting() && file->getFileType() != FileType::TBD_V2)
    return false;

  if (!io.outputting() && !io.mapTag("!tapi-tbd-v2"))
    return false;

  const auto *interface = dyn_cast_or_null<InterfaceFile>(file);
  MappingTraits<const InterfaceFile *>::mappingTBD2(io, interface);
  file = interface;

  return true;
}
}
} // end namespace stub::v1.

TAPI_NAMESPACE_INTERNAL_END
