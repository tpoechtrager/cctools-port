//===- lib/Core/MachODylibReader.cpp - TAPI MachO Dylib Reader --*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// \brief Implements the object specific parts of reading the dylib files.
///
//===----------------------------------------------------------------------===//

#include "tapi/Core/MachODylibReader.h"
#include "tapi/Core/ArchitectureSupport.h"
#include "tapi/Core/InterfaceFile.h"
#include "llvm/Object/MachOUniversal.h"
#include "llvm/Support/Endian.h"

using namespace llvm;
using namespace llvm::object;

TAPI_NAMESPACE_INTERNAL_BEGIN

FileType MachODylibReader::getFileType(file_magic magic,
                                       MemoryBufferRef bufferRef) const {
  switch (magic) {
  default:
    return FileType::Invalid;
  case sys::fs::file_magic::macho_dynamically_linked_shared_lib:
    return FileType::MachO_DynamicLibrary;
  case sys::fs::file_magic::macho_dynamically_linked_shared_lib_stub:
    return FileType::MachO_DynamicLibrary_Stub;
  case sys::fs::file_magic::macho_universal_binary:
    break;
  }

  auto binaryOrErr = createBinary(bufferRef);
  if (binaryOrErr.getError())
    return FileType::Invalid;

  Binary &bin = *binaryOrErr.get();
  assert(isa<MachOUniversalBinary>(&bin) && "Unexpected MachO binary");
  auto *UB = cast<MachOUniversalBinary>(&bin);

  FileType fileType = FileType::Invalid;
  // Check if any of the architecture slices are a MachO dylib.
  for (auto OI = UB->begin_objects(), OE = UB->end_objects(); OI != OE; ++OI) {
    auto objOrErr = OI->getAsObjectFile();
    // Ignore archives.
    if (objOrErr.getError())
      continue;

    auto &obj = *objOrErr.get();
    switch (obj.getHeader().filetype) {
    default:
      continue;
    case MachO::MH_DYLIB:
      if (fileType == FileType::Invalid)
        fileType = FileType::MachO_DynamicLibrary;
      else if (fileType != FileType::MachO_DynamicLibrary)
        return FileType::Invalid;
      break;
    case MachO::MH_DYLIB_STUB:
      if (fileType == FileType::Invalid)
        fileType = FileType::MachO_DynamicLibrary_Stub;
      else if (fileType != FileType::MachO_DynamicLibrary_Stub)
        return FileType::Invalid;
      break;
    }
  }

  return fileType;
}

bool MachODylibReader::canRead(file_magic magic, MemoryBufferRef bufferRef,
                               FileType types) const {
  if (!(types & FileType::MachO_DynamicLibrary) &&
      !(types & FileType::MachO_DynamicLibrary_Stub))
    return false;
  return getFileType(magic, bufferRef) != FileType::Invalid;
}

static std::tuple<StringRef, SymbolType> parseSymbol(StringRef symbolName) {
  StringRef name;
  SymbolType type;
  if (symbolName.startswith(".objc_class_name")) {
    name = symbolName.drop_front(16);
    type = SymbolType::ObjCClass;
  } else if (symbolName.startswith("_OBJC_CLASS_$")) {
    name = symbolName.drop_front(13);
    type = SymbolType::ObjCClass;
  } else if (symbolName.startswith("_OBJC_METACLASS_$")) {
    name = symbolName.drop_front(17);
    type = SymbolType::ObjCClass;
  } else if (symbolName.startswith("_OBJC_IVAR_$")) {
    name = symbolName.drop_front(12);
    type = SymbolType::ObjCInstanceVariable;
  } else {
    name = symbolName;
    type = SymbolType::Symbol;
  }
  return std::make_tuple(name, type);
}

void load(MachOObjectFile *object, InterfaceFile *file) {
  auto H = object->getHeader();
  auto arch = getArchType(H.cputype, H.cpusubtype);
  file->setArch(arch);
  auto fileType = H.filetype == MachO::MH_DYLIB
                      ? FileType::MachO_DynamicLibrary
                      : FileType::MachO_DynamicLibrary_Stub;
  file->setFileType(fileType);

  for (const auto &LCI : object->load_commands()) {
    switch (LCI.C.cmd) {
    case MachO::LC_ID_DYLIB: {
      auto DLLC = object->getDylibIDLoadCommand(LCI);
      file->setInstallName(LCI.Ptr + DLLC.dylib.name);
      file->setCurrentVersion(DLLC.dylib.current_version);
      file->setCompatibilityVersion(DLLC.dylib.compatibility_version);
      break;
    }
    case MachO::LC_REEXPORT_DYLIB: {
      auto DLLC = object->getDylibIDLoadCommand(LCI);
      file->addReexportedLibrary(LCI.Ptr + DLLC.dylib.name, arch);
      break;
    }
    case MachO::LC_SUB_FRAMEWORK: {
      auto SFC = object->getSubFrameworkCommand(LCI);
      file->setParentUmbrella(LCI.Ptr + SFC.umbrella);
      break;
    }
    case MachO::LC_SUB_CLIENT: {
      auto SCLC = object->getSubClientCommand(LCI);
      file->addAllowableClient(LCI.Ptr + SCLC.client, arch);
      break;
    }
    case MachO::LC_UUID: {
      auto UUIDLC = object->getUuidCommand(LCI);
      file->addUUID(UUIDLC.uuid, arch);
      break;
    }
    case MachO::LC_VERSION_MIN_MACOSX:
      file->setPlatform(Platform::OSX);
      break;
    case MachO::LC_VERSION_MIN_IPHONEOS:
      file->setPlatform(Platform::iOS);
      break;
    case MachO::LC_VERSION_MIN_WATCHOS:
      file->setPlatform(Platform::watchOS);
      break;
    case MachO::LC_VERSION_MIN_TVOS:
      file->setPlatform(Platform::tvOS);
      break;
    default:
      break;
    }
  }

  if (H.flags & MachO::MH_TWOLEVEL)
    file->setTwoLevelNamespace();

  if (H.flags & MachO::MH_APP_EXTENSION_SAFE)
    file->setApplicationExtensionSafe();

  for (auto &section : object->sections()) {
    StringRef sectionName;
    section.getName(sectionName);
    if (sectionName != "__objc_imageinfo" && sectionName != "__image_info")
      continue;
    StringRef content;
    section.getContents(content);
    if ((content.size() >= 8) && (content[0] == 0)) {
      uint32_t flags;
      if (object->isLittleEndian()) {
        auto *p =
            reinterpret_cast<const support::ulittle32_t *>(content.data() + 4);
        flags = *p;
      } else {
        auto *p =
            reinterpret_cast<const support::ubig32_t *>(content.data() + 4);
        flags = *p;
      }
      if ((flags & 4) == 4)
        file->setObjCConstraint(ObjCConstraint::GC);
      else if ((flags & 2) == 2)
        file->setObjCConstraint(ObjCConstraint::Retain_Release_Or_GC);
      else if ((flags & 32) == 32)
        file->setObjCConstraint(ObjCConstraint::Retain_Release_For_Simulator);
      else
        file->setObjCConstraint(ObjCConstraint::Retain_Release);

      file->setSwiftVersion(((flags >> 8) & 0xFF));
    }
  }

  for (const auto &symbol : object->exports()) {
    StringRef name;
    SymbolType type;
    std::tie(name, type) = parseSymbol(symbol.name());
    SymbolFlags flags = SymbolFlags::None;
    switch (symbol.flags() & MachO::EXPORT_SYMBOL_FLAGS_KIND_MASK) {
    case MachO::EXPORT_SYMBOL_FLAGS_KIND_REGULAR:
      if (symbol.flags() & MachO::EXPORT_SYMBOL_FLAGS_WEAK_DEFINITION)
        flags = SymbolFlags::WeakDefined;
      break;
    case MachO::EXPORT_SYMBOL_FLAGS_KIND_THREAD_LOCAL:
      flags = SymbolFlags::ThreadLocalValue;
      break;
    }
    file->addExportedSymbol(name.str(), type, flags, arch);
  }

  // Only record undef symbols for flat namespace dylibs.
  if (file->isTwoLevelNamespace())
    return;

  for (const auto &symbol : object->symbols()) {
    auto symbolFlags = symbol.getFlags();
    if ((symbolFlags & BasicSymbolRef::SF_Global) == 0)
      continue;
    if ((symbolFlags & BasicSymbolRef::SF_Undefined) == 0)
      continue;

    auto flags = symbolFlags & BasicSymbolRef::SF_Weak
                     ? SymbolFlags::WeakReferenced
                     : SymbolFlags::None;
    auto symbolName = symbol.getName();
    if (symbolName.getError())
      continue;

    StringRef name;
    SymbolType type;
    std::tie(name, type) = parseSymbol(symbolName.get());
    file->addUndefinedSymbol(name.str(), type, flags, arch);
  }
}

std::unique_ptr<File>
MachODylibReader::readFile(MemoryBufferRef memBuffer) const {
  auto file = std::unique_ptr<InterfaceFile>(new InterfaceFile);
  file->setPath(memBuffer.getBufferIdentifier());

  auto binaryOrErr = createBinary(memBuffer);
  if (auto ec = binaryOrErr.getError()) {
    file->setErrorCode(ec);
    return std::move(file);
  }

  Binary &binary = *binaryOrErr.get();
  if (auto *object = dyn_cast<MachOObjectFile>(&binary)) {
    load(object, file.get());
    return std::move(file);
  }

  // Only expecting MachO universal binaries at this point.
  assert(isa<MachOUniversalBinary>(&binary) &&
         "Expected a MachO universal binary.");
  auto *UB = cast<MachOUniversalBinary>(&binary);
  for (auto OI = UB->begin_objects(), OE = UB->end_objects(); OI != OE; ++OI) {
    auto objOrErr = OI->getAsObjectFile();

    // Ignore archives.
    if (objOrErr.getError())
      continue;

    auto &object = *objOrErr.get();
    switch (object.getHeader().filetype) {
    default:
      break;
    case MachO::MH_DYLIB:
    case MachO::MH_DYLIB_STUB:
      load(&object, file.get());
      break;
    }
  }

  return std::move(file);
}

TAPI_NAMESPACE_INTERNAL_END
