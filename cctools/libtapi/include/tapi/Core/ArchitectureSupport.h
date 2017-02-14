//===- tapi/Core/ArchitectureSupport.h - Architecture Support ---*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// \brief Defines architecture specific enums and helper functions.
///
//===----------------------------------------------------------------------===//

#ifndef TAPI_CORE_ARCHITECTURE_SUPPORT_H
#define TAPI_CORE_ARCHITECTURE_SUPPORT_H

#include "tapi/tapi.h"
#include "tapi/Core/LLVM.h"
#include "tapi/PackedVersion32.h"

TAPI_NAMESPACE_INTERNAL_BEGIN

static constexpr unsigned numPlatformBits = 3;

enum Arch : uint32_t {
  unknown = 0U,
  armv7   = 1U << 0,
  armv7s  = 1U << 1,
  armv7k  = 1U << 2,
  arm64   = 1U << 3,
  i386    = 1U << 4,
  x86_64  = 1U << 5,
  x86_64h = 1U << 6,
};

class ArchitectureSet {
private:
  typedef uint32_t ArchSetType;

  const static ArchSetType _endIndexVal =
      std::numeric_limits<ArchSetType>::max();
  ArchSetType _archSet;

public:
  ArchitectureSet() : _archSet(Arch::unknown) {}
  ArchitectureSet(ArchSetType raw) : _archSet(raw) {}

  void set(Arch arch) { _archSet |= (ArchSetType)arch; }
  bool has(Arch arch) const { return _archSet & arch; }

  size_t count() const {
    // popcnt
    size_t cnt = 0;
    for (unsigned i = 0; i < sizeof(ArchSetType) * 8; ++i)
      if (_archSet & (1U << i))
        ++cnt;
    return cnt;
  }

  bool empty() const { return _archSet == 0; }

  bool hasX86() const { return _archSet & (i386 | x86_64 | x86_64h); }

  bool hasABICompatibleSlice(Arch arch) const {
    switch (arch) {
    case unknown:
      return false;
    case armv7:
    case armv7s:
      return has(armv7) || has(armv7s);
    case armv7k:
      return has(armv7k);
    case arm64:
      return has(arm64);
    case i386:
      return has(i386);
    case x86_64:
    case x86_64h:
      return has(x86_64) || has(x86_64h);
    }
  }

  Arch getABICompatibleSlice(Arch arch) const {
    switch (arch) {
    case unknown:
      return unknown;
    case armv7:
    case armv7s:
    case armv7k:
      if (has(armv7))
        return armv7;
      if (has(armv7s))
        return armv7s;
      if (has(armv7k))
        return armv7k;
      return unknown;
    case arm64:
      if (has(arm64))
        return arm64;
      return unknown;
    case i386:
      if (has(i386))
        return i386;
      return unknown;
    case x86_64:
    case x86_64h:
      if (has(x86_64))
        return x86_64;
      if (has(x86_64h))
        return x86_64h;
      return unknown;
    }
  }

  template <typename Ty>
  class arch_iterator
      : public std::iterator<std::forward_iterator_tag, Arch, size_t> {
  private:
    ArchSetType _index;
    Ty *_archSet;

    void findNextSetBit() {
      if (_index == _endIndexVal)
        return;

      do {
        if (*_archSet & (1UL << ++_index))
          return;
      } while (_index < sizeof(Ty) * 8);

      _index = _endIndexVal;
    }

  public:
    arch_iterator(Ty *archSet, ArchSetType index = 0)
        : _index(index), _archSet(archSet) {
      if (index != _endIndexVal && !(*_archSet & (1UL << index)))
        findNextSetBit();
    }

    Arch operator*() const { return (Arch)(1U << _index); }

    arch_iterator &operator++() {
      findNextSetBit();
      return *this;
    }

    arch_iterator operator++(int) {
      auto tmp = *this;
      findNextSetBit();
      return tmp;
    }

    bool operator==(const arch_iterator &o) const {
      return std::tie(_index, _archSet) == std::tie(o._index, o._archSet);
    }

    bool operator!=(const arch_iterator &o) const { return !(*this == o); }
  };

  ArchitectureSet operator&(const ArchitectureSet &o) {
    return _archSet & o._archSet;
  }

  ArchitectureSet operator|(const ArchitectureSet &o) {
    return _archSet | o._archSet;
  }

  ArchitectureSet &operator|=(const ArchitectureSet &o) {
    _archSet |= o._archSet;
    return *this;
  }

  bool operator==(const ArchitectureSet &o) const {
    return _archSet == o._archSet;
  }

  bool operator!=(const ArchitectureSet &o) const {
    return _archSet != o._archSet;
  }

  bool operator<(const ArchitectureSet &o) const {
    return _archSet < o._archSet;
  }

  typedef arch_iterator<ArchSetType> iterator;
  typedef arch_iterator<const ArchSetType> const_iterator;

  iterator begin() { return iterator(&_archSet); }
  iterator end() { return iterator(&_archSet, _endIndexVal); }

  const_iterator begin() const { return const_iterator(&_archSet); }
  const_iterator end() const { return const_iterator(&_archSet, _endIndexVal); }
};

struct PackedVersion {
  uint32_t _version;

  PackedVersion() : _version(0) {}
  PackedVersion(uint32_t version) : _version(version) {}
  PackedVersion(unsigned major, unsigned minor, unsigned subminor)
      : _version((major << 16) | ((minor & 0xff) << 8) | (subminor & 0xff)) {}

  bool empty() const { return _version == 0; }

  /// \brief Retrieve the major version number.
  unsigned getMajor() const { return _version >> 16; }

  /// \brief Retrieve the minor version number, if provided.
  unsigned getMinor() const { return (_version >> 8) & 0xff; }

  /// \brief Retrieve the subminor version number, if provided.
  unsigned getSubminor() const { return _version & 0xff; }

  bool parse32(StringRef str);
  std::pair<bool, bool> parse64(StringRef str);

  bool operator<(const PackedVersion &rhs) const {
    return _version < rhs._version;
  }

  bool operator==(const PackedVersion &rhs) const {
    return _version == rhs._version;
  }

  bool operator!=(const PackedVersion &rhs) const {
    return _version != rhs._version;
  }

  void print(raw_ostream &os) const;

  operator PackedVersion32() const {
    return PackedVersion32(getMajor(), getMinor(), getSubminor());
  }
};

inline raw_ostream &operator<<(raw_ostream &os, const PackedVersion &version) {
  version.print(os);
  return os;
}

StringRef getPlatformName(Platform platform);
Arch getArchType(uint32_t CPUType, uint32_t CPUSubType);
Arch getArchType(StringRef name);
StringRef getArchName(Arch arch);

TAPI_NAMESPACE_INTERNAL_END

#endif // TAPI_CORE_ARCHITECTURE_SUPPORT_H
