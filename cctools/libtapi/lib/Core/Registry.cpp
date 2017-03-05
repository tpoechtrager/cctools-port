//===- lib/Core/Registry.cpp - TAPI Registry --------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// \brief Implements the TAPI Registry.
///
//===----------------------------------------------------------------------===//

#include "tapi/Core/Registry.h"
#include "tapi/Core/MachODylibReader.h"
#include "tapi/Core/TextStub_v1.h"
#include "tapi/Core/TextStub_v2.h"
#include "tapi/Core/YAMLReaderWriter.h"

using namespace llvm;

TAPI_NAMESPACE_INTERNAL_BEGIN

bool Registry::canRead(MemoryBufferRef memBuffer, FileType types) const {
  auto data = memBuffer.getBuffer();
  auto magic = sys::fs::identify_magic(data);

  for (const auto &reader : _readers) {
    if (reader->canRead(magic, memBuffer, types))
      return true;
  }

  return false;
}

FileType Registry::getFileType(MemoryBufferRef memBuffer) const {
  auto data = memBuffer.getBuffer();
  auto magic = sys::fs::identify_magic(data);

  for (const auto &reader : _readers) {
    auto fileType = reader->getFileType(magic, memBuffer);
    if (fileType != FileType::Invalid)
      return fileType;
  }

  return FileType::Invalid;
}

bool Registry::canWrite(const File *file) const {
  for (const auto &writer : _writers) {
    if (writer->canWrite(file))
      return true;
  }

  return false;
}

std::unique_ptr<File> Registry::readFile(MemoryBufferRef memBuffer) const {
  auto data = memBuffer.getBuffer();
  auto fileType = sys::fs::identify_magic(data);

  for (const auto &reader : _readers) {
    if (!reader->canRead(fileType, memBuffer))
      continue;
    return reader->readFile(memBuffer);
  }

  return nullptr;
}

std::error_code Registry::writeFile(const File *file) const {
  for (const auto &writer : _writers) {
    if (!writer->canWrite(file))
      continue;
    return writer->writeFile(file);
  }

  return std::make_error_code(std::errc::not_supported);
}

void Registry::addBinaryReaders() {
  add(std::unique_ptr<Reader>(new MachODylibReader));
}

void Registry::addYAMLReaders() {
  auto reader = std::unique_ptr<TextBasedStubReader>(new TextBasedStubReader);
  reader->add(std::unique_ptr<DocumentHandler>(
      new stub::v1::TextBasedStubDocumentHandler));
  reader->add(std::unique_ptr<DocumentHandler>(
      new stub::v2::TextBasedStubDocumentHandler));
  add(std::unique_ptr<Reader>(std::move(reader)));
}

void Registry::addYAMLWriters() {
  auto writer = std::unique_ptr<TextBasedStubWriter>(new TextBasedStubWriter);
  writer->add(std::unique_ptr<DocumentHandler>(
      new stub::v1::TextBasedStubDocumentHandler));
  writer->add(std::unique_ptr<DocumentHandler>(
      new stub::v2::TextBasedStubDocumentHandler));
  add(std::unique_ptr<Writer>(std::move(writer)));
}

TAPI_NAMESPACE_INTERNAL_END
