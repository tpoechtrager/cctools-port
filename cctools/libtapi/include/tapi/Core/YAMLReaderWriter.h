//===- tapi/Core/YAMLReaderWriter.h - YAML Reader/Writer --------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// \brief Defines the YAML Reader/Writer.
///
//===----------------------------------------------------------------------===//

#ifndef TAPI_CORE_YAML_READER_WRITER_H
#define TAPI_CORE_YAML_READER_WRITER_H

#include "tapi/Core/File.h"
#include "tapi/Core/Registry.h"
#include "tapi/Defines.h"
#include "llvm/Support/YAMLTraits.h"

using llvm::MemoryBufferRef;
using llvm::yaml::IO;

TAPI_NAMESPACE_INTERNAL_BEGIN

class TextBasedStubBase;

struct YAMLContext {
  const TextBasedStubBase &_base;
  std::string _path;
  std::string _errorMessage;

  YAMLContext(const TextBasedStubBase &base) : _base(base) {}
};

class DocumentHandler {
public:
  virtual ~DocumentHandler() {}
  virtual bool canRead(MemoryBufferRef memBufferRef, FileType types) const = 0;
  virtual FileType getFileType(MemoryBufferRef bufferRef) const = 0;
  virtual bool canWrite(const File *file) const = 0;
  virtual bool handleDocument(IO &io, const File *&file) const = 0;
};

class TextBasedStubBase {
public:
  bool canRead(MemoryBufferRef memBufferRef, FileType types) const;
  FileType getFileType(MemoryBufferRef bufferRef) const;
  bool canWrite(const File *file) const;
  bool handleDocument(IO &io, const File *&file) const;

  void add(std::unique_ptr<DocumentHandler> handler) {
    _documentHandlers.emplace_back(std::move(handler));
  }

private:
  std::vector<std::unique_ptr<DocumentHandler>> _documentHandlers;
};

class TextBasedStubReader final : public TextBasedStubBase, public Reader {
public:
  bool canRead(file_magic magic, MemoryBufferRef memBufferRef,
               FileType types) const override;
  FileType getFileType(file_magic magic,
                       MemoryBufferRef bufferRef) const override;
  std::unique_ptr<File> readFile(MemoryBufferRef memBuffer) const override;
};

class TextBasedStubWriter final : public TextBasedStubBase, public Writer {
public:
  bool canWrite(const File *file) const override;
  std::error_code writeFile(const File *file) const override;
};

TAPI_NAMESPACE_INTERNAL_END

#endif // TAPI_CORE_YAML_READER_WRITER_H