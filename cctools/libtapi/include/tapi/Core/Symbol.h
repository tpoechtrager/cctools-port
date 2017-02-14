//===- tapi/Core/Symbol.h - TAPI Symbol -------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// \brief Defines a symbol.
///
//===----------------------------------------------------------------------===//

#ifndef TAPI_CORE_SYMBOL_H
#define TAPI_CORE_SYMBOL_H

#include "llvm/ADT/StringSwitch.h"
#include "tapi/Core/ArchitectureSupport.h"
#include "tapi/Core/LLVM.h"
#include <map>

TAPI_NAMESPACE_INTERNAL_BEGIN

struct AvailabilityInfo {
  PackedVersion _introduced;
  PackedVersion _obsoleted;
  uint8_t _platform : numPlatformBits;
  uint8_t _unavailable : 1;

  AvailabilityInfo()
      : _introduced(0), _obsoleted(0), _platform((uint8_t)Platform::Unknown),
        _unavailable(false) {}

  AvailabilityInfo(StringRef p, PackedVersion i, PackedVersion o, bool u)
      : _introduced(i), _obsoleted(o), _unavailable(u) {
    _platform = (uint8_t)llvm::StringSwitch<Platform>(p)
                    .Case("macosx", Platform::OSX)
                    .Case("ios", Platform::iOS)
                    .Case("watchos", Platform::watchOS)
                    .Case("tvos", Platform::tvOS)
                    .Default(Platform::Unknown);
  }

  AvailabilityInfo(StringRef p)
      : AvailabilityInfo(p, PackedVersion(1, 0, 0), 0, false) {}

  Platform getPlatform() const { return (Platform)_platform; }

  void print(raw_ostream &os) const;

  bool operator==(const AvailabilityInfo &rhs) const {
    return std::tie(_introduced, _obsoleted, _platform, _unavailable) ==
           std::tie(rhs._introduced, rhs._obsoleted, rhs._platform,
                    rhs._unavailable);
  }

  bool operator<(const AvailabilityInfo &rhs) const {
    return _platform < rhs._platform || _introduced < rhs._introduced ||
           _obsoleted < rhs._obsoleted || (!_unavailable && rhs._unavailable);
  }
};

inline raw_ostream &operator<<(raw_ostream &os, const AvailabilityInfo &avail) {
  avail.print(os);
  return os;
}

enum class SymbolType {
  Symbol,
  ObjCClass,
  ObjCInstanceVariable,
};
static const unsigned numSymbolTypeBits = 2;

using SymbolFlags = tapi::v1::SymbolFlags;
static const unsigned numSymbolFlagsBits = 3;

inline SymbolFlags operator&(const SymbolFlags &lhs,
                             const SymbolFlags &rhs) noexcept {
  return static_cast<SymbolFlags>(static_cast<unsigned>(lhs) &
                                  static_cast<unsigned>(rhs));
}

inline SymbolFlags operator|(const SymbolFlags &lhs,
                             const SymbolFlags &rhs) noexcept {
  return static_cast<SymbolFlags>(static_cast<unsigned>(lhs) |
                                  static_cast<unsigned>(rhs));
}

struct Symbol {
  std::string _name;
  std::map<Arch, AvailabilityInfo> _availability;
  uint8_t _type : numSymbolTypeBits;
  uint8_t _flags : numSymbolFlagsBits;
  uint8_t _isPrivate : 1;
  uint8_t _isReexport : 1;

  Symbol() = default;

  Symbol(StringRef name, SymbolType type, bool isPrivate = false,
         bool isReexport = false)
      : _name(name), _type((uint8_t)type), _flags((uint8_t)SymbolFlags::None),
        _isPrivate(isPrivate), _isReexport(isReexport) {}

  Symbol(StringRef name, SymbolType type, SymbolFlags flags)
      : _name(name), _type((uint8_t)type), _flags((uint8_t)flags),
        _isPrivate(false), _isReexport(false) {}

  Symbol(const Symbol &sym)
      : _name(sym._name), _type(sym._type), _flags(sym._flags),
        _isPrivate(sym._isPrivate), _isReexport(sym._isReexport) {
    for (auto &avail : sym._availability)
      _availability.emplace(std::piecewise_construct,
                            std::forward_as_tuple(avail.first),
                            std::forward_as_tuple(avail.second));
  }

  const std::string &getName() const { return _name; }
  std::string getPrettyName(bool demangle = false) const;
  std::string getAnnotatedName(bool demangle = false) const;

  SymbolType getType() const { return (SymbolType)_type; }
  bool isSymbol() const { return getType() == SymbolType::Symbol; }
  bool isObjCClass() const { return getType() == SymbolType::ObjCClass; }
  bool isObjCInstanceVariable() const {
    return getType() == SymbolType::ObjCInstanceVariable;
  }

  void setThreadLocalValue(bool v = true) {
    if (v)
      _flags |= (uint8_t)SymbolFlags::ThreadLocalValue;
    else
      _flags &= ~(uint8_t)SymbolFlags::ThreadLocalValue;
  }

  bool isThreadLocalValue() const {
    return _flags & (uint8_t)SymbolFlags::ThreadLocalValue;
  }

  void setWeakDefined(bool v = true) {
    if (v)
      _flags |= (uint8_t)SymbolFlags::WeakDefined;
    else
      _flags &= ~(uint8_t)SymbolFlags::WeakDefined;
  }
  bool isWeakDefined() const {
    return _flags & (uint8_t)SymbolFlags::WeakDefined;
  }

  void setWeakReferenced(bool v = true) {
    if (v)
      _flags = (uint8_t)SymbolFlags::WeakReferenced;
    else
      _flags &= ~(uint8_t)SymbolFlags::WeakReferenced;
  }
  bool isWeakReferenced() const {
    return _flags & (uint8_t)SymbolFlags::WeakReferenced;
  }

  SymbolFlags getFlags() const { return static_cast<SymbolFlags>(_flags); }

  bool isUnavailable() const {
    for (auto avail : _availability)
      if (!avail.second._unavailable)
        return false;

    return true;
  }

  ArchitectureSet getArchitectures() const {
    ArchitectureSet archs;
    for (auto avail : _availability)
      if (!avail.second._unavailable)
        archs.set(avail.first);

    return archs;
  }

  bool hasArch(Arch arch) const {
    auto it = _availability.find(arch);
    if (it == _availability.end())
      return false;

    if (it->second._unavailable)
      return false;

    return true;
  }

  bool removeArch(Arch arch) {
    auto it = _availability.find(arch);
    if (it == _availability.end())
      return false;

    _availability.erase(it);
    return true;
  }

  void print(raw_ostream &os) const;

  bool operator<(const Symbol &o) const {
    return (_name < o._name) || (_type < o._type);
  }
};

inline raw_ostream &operator<<(raw_ostream &os, const Symbol &symbol) {
  symbol.print(os);
  return os;
}

TAPI_NAMESPACE_INTERNAL_END

#endif // TAPI_CORE_SYMBOL_H
