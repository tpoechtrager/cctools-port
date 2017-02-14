//===- tapi/Core/File.h - TAPI File -----------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// \brief TAPI File abstraction.
///
//===----------------------------------------------------------------------===//

#ifndef TAPI_CORE_FILE_H
#define TAPI_CORE_FILE_H

#include "tapi/Defines.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/TimeValue.h"
#include <system_error>

TAPI_NAMESPACE_INTERNAL_BEGIN

enum FileType : unsigned {
  /// \brief Invalid file type.
  Invalid                   = 0U,

  /// \brief MachO Dynamic Library file.
  MachO_DynamicLibrary      = 1U << 0,

  /// \brief MachO Dynamic Library Stub file.
  MachO_DynamicLibrary_Stub = 1U << 1,

  /// \brief Text-based stub file (.tbd) version 1.0
  TBD_V1                    = 1U << 2,

  /// \brief Text-based stub file (.tbd) version 2.0
  TBD_V2                    = 1U << 3,

  All                       = ~0U,
};

inline FileType operator&(const FileType lhs, const FileType rhs) {
  return static_cast<FileType>(static_cast<unsigned>(lhs) &
                               static_cast<unsigned>(rhs));
}

inline FileType operator|(const FileType lhs, const FileType rhs) {
  return static_cast<FileType>(static_cast<unsigned>(lhs) |
                               static_cast<unsigned>(rhs));
}

/// \brief Abstract TAPI file.
class File {
public:
  enum class Kind : unsigned {
    InterfaceFile,
  };

  virtual ~File() {}

  template <typename T> void setPath(T &&path) {
    _path = std::forward<T &&>(path);
  }
  const std::string &getPath() const { return _path; }

  llvm::StringRef getFileName() const {
    return llvm::sys::path::filename(_path);
  }

  void setFileType(FileType type) { _fileType = type; }
  FileType getFileType() const { return _fileType; }

  void setErrorCode(std::error_code ec) { _ec = ec; }
  std::error_code getErrorCode() const { return _ec; }

  void setParsingError(std::string msg) { _parsingError = msg; }
  const std::string &getParsingError() const { return _parsingError; }

  Kind kind() const { return _kind; }

protected:
  File(Kind kind) : _kind(kind) {}

private:
  Kind _kind;
  std::string _path;
  FileType _fileType = FileType::Invalid;
  std::error_code _ec;
  std::string _parsingError;
};

TAPI_NAMESPACE_INTERNAL_END

#endif // TAPI_CORE_FILE_H
