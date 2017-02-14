//===- lib/Core/InterfaceFile.cpp - Interface File --------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// \brief Implements the Interface File
///
//===----------------------------------------------------------------------===//

#include "tapi/Core/InterfaceFile.h"
#include "llvm/ADT/SmallString.h"
#include <iomanip>
#include <sstream>

TAPI_NAMESPACE_INTERNAL_BEGIN

bool InterfaceFile::removeExportedSymbol(const std::string &name,
                                         SymbolType type) {
  auto it = _exports.find(std::make_pair(name, type));
  if (it == _exports.end())
    return false;

  _exports.erase(it);
  return true;
}

bool InterfaceFile::removeExportedSymbol(const std::string &name,
                                         SymbolType type,
                                         ArchitectureSet archs) {
  auto it = _exports.find(std::make_pair(name, type));
  if (it == _exports.end())
    return false;

  for (const auto arch : archs)
    it->second.removeArch(arch);

  if (it->second.getArchitectures().empty())
    _exports.erase(it);
  return true;
}

bool InterfaceFile::removeReexportedLibrary(StringRef installName) {
  auto it = remove_if(_reexportedLibraries,
                      [&installName](const InterfaceFileRef &ref) {
                        return ref.getInstallName() == installName;
                      });

  if (it == _reexportedLibraries.end())
    return false;

  _reexportedLibraries.erase(it, _reexportedLibraries.end());
  return true;
}

void InterfaceFile::addUUID(uint8_t uuid[16], Arch arch) {
  std::stringstream stream;
  for (unsigned i = 0; i < 16; ++i) {
    if (i == 4 || i == 6 || i == 8 || i == 10)
      stream << '-';
    stream << std::setfill('0') << std::setw(2) << std::uppercase << std::hex
           << static_cast<int>(uuid[i]);
  }
  addUUID(arch, stream.str());
}

bool InterfaceFile::contains(const Symbol &symbol, Symbol &result) const {
  auto it = _exports.find(std::make_pair(symbol.getName(), symbol.getType()));
  if (it == _exports.end())
    return false;

  result = it->second;
  return true;
}

TAPI_NAMESPACE_INTERNAL_END
