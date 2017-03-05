//===- tapi/Core/ArchitectureSupport.cpp - Architecture Support -*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// \brief Implements architecture specific helper functions.
///
//===----------------------------------------------------------------------===//

#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/MachO.h"
#include "llvm/Support/raw_ostream.h"
#include "tapi/Core/ArchitectureSupport.h"

using namespace llvm;

TAPI_NAMESPACE_INTERNAL_BEGIN

void PackedVersion::print(raw_ostream &os) const {
  os << format("%d.%d", getMajor(), getMinor());
  if (getSubminor())
    os << format(".%d", getSubminor());
}

bool PackedVersion::parse32(StringRef str) {
  _version = 0;

  if (str.empty())
    return false;

  SmallVector<StringRef, 3> parts;
  SplitString(str, parts, ".");

  if (parts.size() > 3)
    return false;

  unsigned long long num;
  if (getAsUnsignedInteger(parts[0], 10, num))
    return false;

  if (num > UINT16_MAX)
    return false;

  _version = num << 16;

  for (unsigned i = 1, shiftNum = 8; i < parts.size(); ++i, shiftNum -= 8) {
    if (getAsUnsignedInteger(parts[i], 10, num))
      return false;

    if (num > UINT8_MAX)
      return false;

    _version |= (num << shiftNum);
  }

  return true;
}

std::pair<bool, bool> PackedVersion::parse64(StringRef str) {
  bool truncated = false;
  _version = 0;

  if (str.empty())
    return std::make_pair(false, truncated);

  SmallVector<StringRef, 5> parts;
  SplitString(str, parts, ".");

  if (parts.size() > 5)
    return std::make_pair(false, truncated);

  unsigned long long num;
  if (getAsUnsignedInteger(parts[0], 10, num))
    return std::make_pair(false, truncated);

  if (num > 0xFFFFFFULL)
    return std::make_pair(false, truncated);

  if (num > 0xFFFFULL) {
    num = 0xFFFFULL;
    truncated = true;
  }
  _version = num << 16;

  for (unsigned i = 1, shiftNum = 8; i < parts.size() && i < 3;
       ++i, shiftNum -= 8) {
    if (getAsUnsignedInteger(parts[i], 10, num))
      return std::make_pair(false, truncated);

    if (num > 0x3FFULL)
      return std::make_pair(false, truncated);

    if (num > 0xFFULL) {
      num = 0xFFULL;
      truncated = true;
    }
    _version |= (num << shiftNum);
  }

  if (parts.size() > 3)
    truncated = true;

  return std::make_pair(true, truncated);
}

StringRef getPlatformName(Platform platform) {
  switch (platform) {
  case Platform::Unknown:
    return "Unknown";
  case Platform::OSX:
    return "OS X";
  case Platform::iOS:
    return "iOS";
  case Platform::watchOS:
    return "watchOS";
  case Platform::tvOS:
    return "tvOS";
  }
}

Arch getArchType(uint32_t CPUType, uint32_t CPUSubType) {
  switch (CPUType) {
  default:
    return Arch::unknown;
  case MachO::CPU_TYPE_I386:
    switch (CPUSubType & ~MachO::CPU_SUBTYPE_MASK) {
    case MachO::CPU_SUBTYPE_I386_ALL:
      return Arch::i386;
    default:
      return Arch::unknown;
    }
  case MachO::CPU_TYPE_X86_64:
    switch (CPUSubType & ~MachO::CPU_SUBTYPE_MASK) {
    case MachO::CPU_SUBTYPE_X86_64_ALL:
      return Arch::x86_64;
    case MachO::CPU_SUBTYPE_X86_64_H:
      return Arch::x86_64h;
    default:
      return Arch::unknown;
    }
  case MachO::CPU_TYPE_ARM:
    switch (CPUSubType & ~MachO::CPU_SUBTYPE_MASK) {
    case MachO::CPU_SUBTYPE_ARM_V7:
      return Arch::armv7;
    case MachO::CPU_SUBTYPE_ARM_V7K:
      return Arch::armv7k;
    case MachO::CPU_SUBTYPE_ARM_V7S:
      return Arch::armv7s;
    default:
      return Arch::unknown;
    }
  case MachO::CPU_TYPE_ARM64:
    switch (CPUSubType & ~MachO::CPU_SUBTYPE_MASK) {
    case MachO::CPU_SUBTYPE_ARM64_ALL:
      return Arch::arm64;
    default:
      return Arch::unknown;
    }
  }
}

Arch getArchType(StringRef name) {
  return StringSwitch<Arch>(name)
    .Case("armv7", Arch::armv7)
    .Case("armv7s", Arch::armv7s)
    .Case("armv7k", Arch::armv7k)
    .Case("arm64", Arch::arm64)
    .Case("i386", Arch::i386)
    .Case("x86_64", Arch::x86_64)
    .Case("x86_64h", Arch::x86_64h)
    .Default(Arch::unknown);
}

StringRef getArchName(Arch arch) {
  switch (arch) {
  case Arch::unknown:
    return "unknown";
  case Arch::armv7:
    return "armv7";
  case Arch::armv7s:
    return "armv7s";
  case Arch::armv7k:
    return "armv7k";
  case Arch::arm64:
    return "arm64";
  case Arch::i386:
    return "i386";
  case Arch::x86_64:
    return "x86_64";
  case Arch::x86_64h:
    return "x86_64h";
  }
}

TAPI_NAMESPACE_INTERNAL_END
