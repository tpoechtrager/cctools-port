//===- tapi/Core/TextStub_v1.h - Text Stub v1 -------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// \brief Defines the content of a text stub v1 file.
///
//===----------------------------------------------------------------------===//

#ifndef TAPI_CORE_TEXT_STUB_V1_H
#define TAPI_CORE_TEXT_STUB_V1_H

#include "tapi/Core/ArchitectureSupport.h"
#include "tapi/Core/YAML.h"
#include "tapi/Core/YAMLReaderWriter.h"
#include "tapi/Defines.h"
#include "llvm/Support/YAMLTraits.h"

using llvm::MemoryBufferRef;
using llvm::yaml::IO;

TAPI_NAMESPACE_INTERNAL_BEGIN

namespace stub {
namespace v1 {

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

class TextBasedStubDocumentHandler : public DocumentHandler {
  bool canRead(MemoryBufferRef memBufferRef,
               FileType types = FileType::All) const override;
  FileType getFileType(MemoryBufferRef memBufferRef) const override;
  bool canWrite(const File *file) const override;
  bool handleDocument(IO &io, const File *&f) const override;
};
}
} // end namespace stub::v1.

TAPI_NAMESPACE_INTERNAL_END

#endif // TAPI_CORE_TEXT_STUB_V1_H
