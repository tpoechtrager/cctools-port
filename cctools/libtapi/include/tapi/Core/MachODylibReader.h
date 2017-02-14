//===- tapi/Core/MachODylibReader.h - TAPI MachO Dylib Reader ---*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// \brief Defines the MachO Dynamic Library Reader.
///
//===----------------------------------------------------------------------===//

#ifndef TAPI_CORE_MACHO_DYLIB_READER_H
#define TAPI_CORE_MACHO_DYLIB_READER_H

#include "tapi/Core/LLVM.h"
#include "tapi/Core/Registry.h"
#include "tapi/Defines.h"

TAPI_NAMESPACE_INTERNAL_BEGIN

class MachODylibReader final : public Reader {
public:
  bool canRead(file_magic magic, llvm::MemoryBufferRef bufferRef,
               FileType types) const override;
  FileType getFileType(file_magic magic,
                       llvm::MemoryBufferRef bufferRef) const override;
  std::unique_ptr<File>
  readFile(llvm::MemoryBufferRef memBuffer) const override;
};

TAPI_NAMESPACE_INTERNAL_END

#endif // TAPI_CORE_MACHO_DYLIB_READER_H
