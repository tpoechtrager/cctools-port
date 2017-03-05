//===- tapi/Core/YAMLReaderWriter.cpp - YAML Reader/Writer ------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// \brief Implements the YAML reader/writer.
///
//===----------------------------------------------------------------------===//

#include "tapi/Core/YAMLReaderWriter.h"
#include "tapi/Core/InterfaceFile.h"
#include "tapi/Core/Registry.h"

using namespace llvm;
using namespace llvm::yaml;
using namespace tapi::internal;

namespace llvm {
namespace yaml {

template <> struct DocumentListTraits<std::vector<const File *>> {
  static size_t size(IO &io, std::vector<const File *> &seq) {
    return seq.size();
  }
  static const File *&element(IO &io, std::vector<const File *> &seq,
                              size_t index) {
    if (index >= seq.size())
      seq.resize(index + 1);
    return seq[index];
  }
};

template <> struct MappingTraits<const File *> {
  static void mapping(IO &io, const File *&file) {
    auto ctx = reinterpret_cast<YAMLContext *>(io.getContext());
    assert(ctx != nullptr);
    ctx->_base.handleDocument(io, file);
  }
};
}
} // end namespace llvm::yaml.

TAPI_NAMESPACE_INTERNAL_BEGIN

static void DiagHandler(const SMDiagnostic &diag, void *context) {
  auto *file = static_cast<YAMLContext *>(context);
  SmallString<1024> message;
  raw_svector_ostream s(message);

  SMDiagnostic newdiag(*diag.getSourceMgr(), diag.getLoc(), file->_path,
                       diag.getLineNo(), diag.getColumnNo(), diag.getKind(),
                       diag.getMessage(), diag.getLineContents(),
                       diag.getRanges(), diag.getFixIts());

  newdiag.print(nullptr, s);
  file->_errorMessage = message.str();
}

bool TextBasedStubBase::canRead(MemoryBufferRef memBufferRef,
                                FileType types) const {
  for (const auto &handler : _documentHandlers) {
    if (handler->canRead(memBufferRef, types))
      return true;
  }
  return false;
}

bool TextBasedStubBase::canWrite(const File *file) const {
  for (const auto &handler : _documentHandlers) {
    if (handler->canWrite(file))
      return true;
  }
  return false;
}

FileType TextBasedStubBase::getFileType(MemoryBufferRef bufferRef) const {
  for (const auto &handler : _documentHandlers) {
    auto fileType = handler->getFileType(bufferRef);
    if (fileType != FileType::Invalid)
      return fileType;
  }
  return FileType::Invalid;
}

bool TextBasedStubBase::handleDocument(IO &io, const File *&file) const {
  for (const auto &handler : _documentHandlers) {
    if (handler->handleDocument(io, file))
      return true;
  }
  return false;
}

bool TextBasedStubReader::canRead(file_magic magic,
                                  MemoryBufferRef memBufferRef,
                                  FileType types) const {
  return TextBasedStubBase::canRead(memBufferRef, types);
}

FileType TextBasedStubReader::getFileType(file_magic magic,
                                          MemoryBufferRef memBufferRef) const {
  return TextBasedStubBase::getFileType(memBufferRef);
}

std::unique_ptr<File>
TextBasedStubReader::readFile(MemoryBufferRef memBuffer) const {
  // Create YAML Input Reader.
  YAMLContext ctx(*this);
  ctx._path = memBuffer.getBufferIdentifier();
  llvm::yaml::Input yin(memBuffer.getBuffer(), &ctx, DiagHandler, &ctx);

  // Fill vector with File objects created by parsing yaml.
  std::vector<const File *> files;
  yin >> files;
  if (files.size() != 1)
    return nullptr;

  auto *file = const_cast<File *>(files.front());
  if (yin.error()) {
    file->setErrorCode(yin.error());
    file->setParsingError(ctx._errorMessage);
  }
  return std::unique_ptr<File>(file);
}

bool TextBasedStubWriter::canWrite(const File *file) const {
  return TextBasedStubBase::canWrite(file);
}

std::error_code TextBasedStubWriter::writeFile(const File *file) const {
  if (file == nullptr)
    return std::make_error_code(std::errc::invalid_argument);

  // Create YAML Output Writer.
  std::error_code ec;

  raw_fd_ostream out(file->getPath(), ec, sys::fs::F_Text);
  if (ec)
    return ec;

  YAMLContext ctx(*this);
  ctx._path = file->getPath();
  llvm::yaml::Output yout(out, &ctx, /*WrapColumn=*/80);

  // Stream out yaml.
  yout << file;

  return ec;
}

TAPI_NAMESPACE_INTERNAL_END
