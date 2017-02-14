//===- tapi/Core/Registry.h - TAPI Registry ---------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// \brief The TAPI registry keeps track of the supported file formats.
///
//===----------------------------------------------------------------------===//

#ifndef TAPI_CORE_REGISTRY_H
#define TAPI_CORE_REGISTRY_H

#include "tapi/Core/File.h"
#include "tapi/Defines.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/MemoryBuffer.h"

using llvm::sys::fs::file_magic;
using llvm::MemoryBufferRef;

TAPI_NAMESPACE_INTERNAL_BEGIN

class Registry;

/// Abstract Reader class - all readers need to inherit from this class and
/// implement the interface.
class Reader {
public:
  virtual ~Reader() {}
  virtual bool canRead(file_magic fileType, MemoryBufferRef bufferRef,
                       FileType types = FileType::All) const = 0;
  virtual FileType getFileType(file_magic magic,
                               MemoryBufferRef bufferRef) const = 0;
  virtual std::unique_ptr<File> readFile(MemoryBufferRef memBuffer) const = 0;
};

/// Abstract Writer class - all writers need to inherit from this class and
/// implement the interface.
class Writer {
public:
  virtual ~Writer() {}
  virtual bool canWrite(const File *file) const = 0;
  virtual std::error_code writeFile(const File *file) const = 0;
};

class Registry {
public:
  bool canRead(MemoryBufferRef memBuffer, FileType types = FileType::All) const;
  FileType getFileType(MemoryBufferRef memBuffer) const;
  bool canWrite(const File *file) const;

  std::unique_ptr<File> readFile(MemoryBufferRef memBuffer) const;
  std::error_code writeFile(const File *file) const;

  void add(std::unique_ptr<Reader> reader) {
    _readers.emplace_back(std::move(reader));
  }

  void add(std::unique_ptr<Writer> writer) {
    _writers.emplace_back(std::move(writer));
  }

  void addBinaryReaders();
  void addYAMLReaders();
  void addYAMLWriters();

private:
  std::vector<std::unique_ptr<Reader>> _readers;
  std::vector<std::unique_ptr<Writer>> _writers;
};

TAPI_NAMESPACE_INTERNAL_END

#endif // TAPI_CORE_REGISTRY_H
