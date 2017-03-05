//===- tapi/Core/IntefaceFile.h - TAPI Interface File --------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// \brief A generic and abstract interface representation for linkable objects.
///        This could be an MachO executable, bundle, dylib, or text-based stub
///        file.
///
//===----------------------------------------------------------------------===//

#ifndef TAPI_CORE_INTERFACE_FILE_H
#define TAPI_CORE_INTERFACE_FILE_H

#include "tapi/Core/ArchitectureSupport.h"
#include "tapi/Core/File.h"
#include "tapi/Core/STLExtras.h"
#include "tapi/Core/Symbol.h"
#include "tapi/Defines.h"
#include "llvm/Support/YAMLTraits.h"

TAPI_NAMESPACE_INTERNAL_BEGIN

using TAPI_INTERNAL::Symbol;

class InterfaceFile;

class InterfaceFileRef {
public:
  InterfaceFileRef() = default;

  template <typename T>
  InterfaceFileRef(T &&installName)
      : _installName(std::forward<T &&>(installName)) {}

  template <typename T>
  InterfaceFileRef(T &&installName, ArchitectureSet archs)
      : _installName(std::forward<T &&>(installName)), _architectures(archs) {}

  const std::string &getInstallName() const { return _installName; };
  void setArchitectures(ArchitectureSet archs) { _architectures |= archs; }
  ArchitectureSet getArchitectures() const { return _architectures; }
  bool hasArchitecture(Arch arch) const { return _architectures.has(arch); }
  void clearArchitectures() { _architectures = Arch::unknown; }

  void setInterfaceFile(const InterfaceFile *file) { _file = file; }
  const InterfaceFile *getInterfaceFile() const { return _file; }

  bool operator==(const InterfaceFileRef &o) const {
    return std::tie(_installName, _architectures) ==
           std::tie(o._installName, o._architectures);
  }

  bool operator<(const InterfaceFileRef &o) const {
    return std::tie(_installName, _architectures) <
           std::tie(o._installName, o._architectures);
  }

  // FIXME: Make this temporary public.
  std::string _installName;

private:
  ArchitectureSet _architectures;
  const InterfaceFile *_file = nullptr;
};

enum class SearchMode {
  All,
  Public,
  Private,
};

class InterfaceFile : public File {
public:
  static bool classof(const File *file) {
    return file->kind() == File::Kind::InterfaceFile;
  }

  using SymbolMap = std::map<std::pair<std::string, SymbolType>, Symbol>;

  InterfaceFile() : File(File::Kind::InterfaceFile) {}
  virtual ~InterfaceFile() = default;

  void setPlatform(Platform platform) { _platform = platform; }
  Platform getPlatform() const { return _platform; }

  void setArchitectures(ArchitectureSet archs) { _architectures |= archs; }
  void setArch(Arch arch) { _architectures.set(arch); }
  ArchitectureSet getArchitectures() const { return _architectures; }
  void clearArchitectures() { _architectures = Arch::unknown; }

  template <typename T> void setInstallName(T &&installName) {
    _installName = std::forward<T &&>(installName);
  }
  const std::string &getInstallName() const { return _installName; }

  void setCurrentVersion(PackedVersion version) { _currentVersion = version; }
  PackedVersion getCurrentVersion() const { return _currentVersion; }

  void setCompatibilityVersion(PackedVersion version) {
    _compatibilityVersion = version;
  }
  PackedVersion getCompatibilityVersion() const {
    return _compatibilityVersion;
  }

  void setSwiftVersion(uint8_t version) { _swiftVersion = version; }
  uint8_t getSwiftVersion() const { return _swiftVersion; }

  void setTwoLevelNamespace(bool v = true) { _isTwoLevelNamespace = v; }
  bool isTwoLevelNamespace() const { return _isTwoLevelNamespace; }

  void setApplicationExtensionSafe(bool v = true) { _isAppExtensionSafe = v; }
  bool isApplicationExtensionSafe() const { return _isAppExtensionSafe; }

  void setObjCConstraint(ObjCConstraint constraint) {
    _objcConstraint = constraint;
  }
  ObjCConstraint getObjCConstraint() const { return _objcConstraint; }

  template <typename T> void setParentUmbrella(T &&parent) {
    _parentUmbrella = std::forward<T &&>(parent);
  }
  const std::string &getParentUmbrella() const { return _parentUmbrella; }

  template <typename T>
  void addAllowableClient(T &&installName, ArchitectureSet archs) {
    auto client = addEntry(_allowableClients, std::forward<T &&>(installName));
    client->setArchitectures(archs);
  }
  const std::vector<InterfaceFileRef> &allowableClients() const {
    return _allowableClients;
  }

  template <typename T>
  void addReexportedLibrary(T &&installName, ArchitectureSet archs) {
    auto lib = addEntry(_reexportedLibraries, std::forward<T &&>(installName));
    lib->setArchitectures(archs);
  }

  bool removeReexportedLibrary(StringRef installName);

  const std::vector<InterfaceFileRef> &reexportedLibraries() const {
    return _reexportedLibraries;
  }

  template <typename T>
  void addExportedSymbol(T &&name, SymbolType type, SymbolFlags flags,
                         ArchitectureSet archs);
  bool removeExportedSymbol(const std::string &name, SymbolType type);
  bool removeExportedSymbol(const std::string &name, SymbolType type,
                            ArchitectureSet archs);
  const SymbolMap &exports() const { return _exports; }

  template <typename T>
  void addUndefinedSymbol(T &&name, SymbolType type, SymbolFlags flags,
                          ArchitectureSet archs);
  const SymbolMap &undefineds() const { return _undefineds; }

  template <typename T> void addUUID(Arch arch, T &&uuid) {
    auto it = find_if(_uuids, [arch](std::pair<Arch, std::string> &u) {
      return u.first == arch;
    });
    if (it == _uuids.end()) {
      auto insertAt =
          lower_bound(_uuids, arch, [](const std::pair<Arch, std::string> &lhs,
                                       Arch rhs) { return lhs.first < rhs; });

      _uuids.emplace(insertAt, arch, std::forward<T &&>(uuid));
      return;
    }

    it->second = std::forward<T &&>(uuid);
  }
  void addUUID(uint8_t uuid[16], Arch arch);
  const std::vector<std::pair<Arch, std::string>> &uuids() const {
    return _uuids;
  }
  void clearUUIDs() { _uuids.clear(); }

  bool contains(const Symbol &symbol, Symbol &result) const;
  bool contains(const Symbol &symbol) const {
    Symbol result;
    return contains(symbol, result);
  }

protected:
  template <typename C, typename T>
  typename C::iterator addEntry(C &container, T &&installName) {
    auto it = find_if(container, [&installName](const InterfaceFileRef &lib) {
      return lib.getInstallName() == installName;
    });
    if (it == std::end(container)) {
      auto insertAt =
          lower_bound(container, installName,
                      [](const InterfaceFileRef &lhs, const T &rhs) {
                        return lhs.getInstallName() < rhs;
                      });

      it = container.emplace(insertAt, std::forward<T &&>(installName));
    }
    return it;
  }

  Platform _platform = Platform::Unknown;
  ArchitectureSet _architectures;
  std::string _installName;
  PackedVersion _currentVersion;
  PackedVersion _compatibilityVersion;
  uint8_t _swiftVersion = 0;
  bool _isTwoLevelNamespace = false;
  bool _isAppExtensionSafe = false;
  ObjCConstraint _objcConstraint = ObjCConstraint::None;
  std::string _parentUmbrella;
  std::vector<InterfaceFileRef> _allowableClients;
  std::vector<InterfaceFileRef> _reexportedLibraries;
  std::vector<std::pair<Arch, std::string>> _uuids;
  SymbolMap _exports;
  SymbolMap _undefineds;
};

template <typename T>
void InterfaceFile::addExportedSymbol(T &&name, SymbolType type,
                                      SymbolFlags flags,
                                      ArchitectureSet archs) {
  auto it = _exports.emplace(
      std::piecewise_construct, std::forward_as_tuple(name, type),
      std::forward_as_tuple(std::forward<T &&>(name), type, flags));
  for (auto arch : archs)
    it.first->second._availability.emplace(std::piecewise_construct,
                                           std::forward_as_tuple(arch),
                                           std::forward_as_tuple());
}

template <typename T>
void InterfaceFile::addUndefinedSymbol(T &&name, SymbolType type,
                                       SymbolFlags flags,
                                       ArchitectureSet archs) {
  auto it = _undefineds.emplace(
      std::piecewise_construct, std::forward_as_tuple(name, type),
      std::forward_as_tuple(std::forward<T &&>(name), type, flags));
  for (auto arch : archs)
    it.first->second._availability.emplace(std::piecewise_construct,
                                           std::forward_as_tuple(arch),
                                           std::forward_as_tuple());
}

TAPI_NAMESPACE_INTERNAL_END

#endif // TAPI_CORE_INTERFACE_FILE_H
