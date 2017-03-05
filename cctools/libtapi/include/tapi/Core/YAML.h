//===- tapi/Core/YAML.h - YAML ----------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// \brief Defines common YAML mappings
///
//===----------------------------------------------------------------------===//

#ifndef TAPI_CORE_YAML_H
#define TAPI_CORE_YAML_H

#include "llvm/Support/YAMLTraits.h"

LLVM_YAML_STRONG_TYPEDEF(uint8_t, SwiftVersion)

namespace llvm {
namespace yaml {

using namespace tapi;
using namespace tapi::internal;

template <> struct ScalarEnumerationTraits<ObjCConstraint> {
  static void enumeration(IO &io, ObjCConstraint &constraint) {
    io.enumCase(constraint, "none", ObjCConstraint::None);
    io.enumCase(constraint, "retain_release", ObjCConstraint::Retain_Release);
    io.enumCase(constraint, "retain_release_for_simulator",
                ObjCConstraint::Retain_Release_For_Simulator);
    io.enumCase(constraint, "retain_release_or_gc",
                ObjCConstraint::Retain_Release_Or_GC);
    io.enumCase(constraint, "gc", ObjCConstraint::GC);
  }
};

template <> struct ScalarEnumerationTraits<Platform> {
  static void enumeration(IO &io, Platform &platform) {
    io.enumCase(platform, "unknown", Platform::Unknown);
    io.enumCase(platform, "macosx", Platform::OSX);
    io.enumCase(platform, "ios", Platform::iOS);
    io.enumCase(platform, "watchos", Platform::watchOS);
    io.enumCase(platform, "tvos", Platform::tvOS);
  }
};

template <> struct ScalarBitSetTraits<ArchitectureSet> {
  static void bitset(IO &io, ArchitectureSet &archs) {
    io.bitSetCase(archs, "armv7", Arch::armv7);
    io.bitSetCase(archs, "armv7s", Arch::armv7s);
    io.bitSetCase(archs, "armv7k", Arch::armv7k);
    io.bitSetCase(archs, "arm64", Arch::arm64);
    io.bitSetCase(archs, "i386", Arch::i386);
    io.bitSetCase(archs, "x86_64", Arch::x86_64);
    io.bitSetCase(archs, "x86_64h", Arch::x86_64h);
  }
};

template <> struct ScalarTraits<PackedVersion> {
  static void output(const PackedVersion &value, void *, raw_ostream &os) {
    os << value;
  }

  static StringRef input(StringRef scalar, void *, PackedVersion &value) {
    if (!value.parse32(scalar))
      return "invalid packed version string.";
    return StringRef();
  }

  static bool mustQuote(StringRef) { return false; }
};

template <> struct ScalarTraits<SwiftVersion> {
  static void output(const SwiftVersion &value, void *, raw_ostream &os) {
    switch (value) {
    case 1:
      os << "1.0";
      break;
    case 2:
      os << "1.1";
      break;
    case 3:
      os << "2.0";
      break;
    case 4:
      os << "3.0";
      break;
    default:
      report_fatal_error("invalid Swift ABI version.");
      break;
    }
  }

  static StringRef input(StringRef scalar, void *, SwiftVersion &value) {
    value = StringSwitch<SwiftVersion>(scalar)
                .Case("1.0", 1)
                .Case("1.1", 2)
                .Case("2.0", 3)
                .Case("3.0", 4)
                .Default(0);
    if (value == SwiftVersion(0))
      return "invalid Swift ABI version.";
    return StringRef();
  }

  static bool mustQuote(StringRef) { return false; }
};

} // end namespace yaml.
} // end namespace llvm.

#endif // TAPI_CORE_YAML_H
