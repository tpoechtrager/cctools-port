//===- tapi/Core/TextStub_v2.h - Text Stub v2 -------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// \brief Defines the content of a text stub v2 file.
///
//===----------------------------------------------------------------------===//

#ifndef TAPI_CORE_TEXT_STUB_V2_H
#define TAPI_CORE_TEXT_STUB_V2_H

#include "tapi/Core/ArchitectureSupport.h"
#include "tapi/Core/YAML.h"
#include "tapi/Core/YAMLReaderWriter.h"
#include "tapi/Defines.h"
#include "llvm/Support/YAMLTraits.h"

using llvm::MemoryBufferRef;
using llvm::yaml::IO;

TAPI_NAMESPACE_INTERNAL_BEGIN

namespace stub {
namespace v2 {

struct ExportSection {
  ArchitectureSet archs;
  std::vector<StringRef> allowableClients;
  std::vector<StringRef> reexportedLibraries;
  std::vector<StringRef> symbols;
  std::vector<StringRef> classes;
  std::vector<StringRef> ivars;
  std::vector<StringRef> weakDefSymbols;
  std::vector<StringRef> tlvSymbols;
};

struct UndefinedSection {
  ArchitectureSet archs;
  std::vector<StringRef> symbols;
  std::vector<StringRef> classes;
  std::vector<StringRef> ivars;
  std::vector<StringRef> weakRefSymbols;
};

enum Flags : unsigned {
  None                         = 0U,
  FlatNamespace                = 1U << 0,
  NotApplicationExtensionSafe  = 1U << 1,
};

inline Flags operator|(const Flags a, const Flags b) {
  return static_cast<Flags>(static_cast<unsigned>(a) |
                            static_cast<unsigned>(b));
}

inline Flags operator|=(Flags &a, const Flags b) {
  a = static_cast<Flags>(static_cast<unsigned>(a) | static_cast<unsigned>(b));
  return a;
}

inline Flags operator&=(Flags &a, const Flags b) {
  a = static_cast<Flags>(static_cast<unsigned>(a) & static_cast<unsigned>(b));
  return a;
}

class TextBasedStubDocumentHandler : public DocumentHandler {
  bool canRead(MemoryBufferRef memBufferRef,
               FileType types = FileType::All) const override;
  FileType getFileType(MemoryBufferRef memBufferRef) const override;
  bool canWrite(const File *file) const override;
  bool handleDocument(IO &io, const File *&f) const override;
};

} // end namespace v2.
} // end namespace stub.

TAPI_NAMESPACE_INTERNAL_END

#endif // TAPI_CORE_TEXT_STUB_V2_H
