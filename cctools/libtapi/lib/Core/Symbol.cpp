//===- lib/Core/Symbol.cpp - Symbol -----------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "tapi/Core/Symbol.h"
#include "tapi/Core/LLVM.h"
#include "llvm/Config/config.h"
#include "llvm/Support/raw_ostream.h"

#if HAVE_CXXABI_H
#include <cxxabi.h>
#endif

using namespace llvm;

TAPI_NAMESPACE_INTERNAL_BEGIN

void AvailabilityInfo::print(raw_ostream &os) const {
  os << "[" << getPlatformName(getPlatform()) << " i:" << _introduced
     << " o:" << _obsoleted << " u:" << (bool)_unavailable << "]";
}

std::string Symbol::getPrettyName(bool demangle) const {
  if (!demangle)
    return _name;

#if HAVE_CXXABI_H
  if (demangle && StringRef(_name).startswith("__Z")) {
    int status = 0;
    char *demangledName =
        abi::__cxa_demangle(&_name[1], nullptr, nullptr, &status);
    if (status == 0) {
      std::string result = demangledName;
      free(demangledName);
      return result;
    }
  }
#endif

  if (_name[0] == '_')
    return _name.substr(1);

  return _name;
}

std::string Symbol::getAnnotatedName(bool demangle) const {
  std::string name;
  if (_isReexport)
    name += "(reexported) ";
  if (isWeakDefined())
    name += "(weak-def) ";
  if (isWeakReferenced())
    name += "(weak-ref) ";
  if (isThreadLocalValue())
    name += "(tlv) ";
  if (isObjCClass())
    name += "(ObjC Class) ";
  else if (isObjCInstanceVariable())
    name += "(ObjC IVar) ";
  return name + getPrettyName(demangle);
}

void Symbol::print(raw_ostream &os) const {
  os << getAnnotatedName();
  for (const auto &avail : _availability)
    os << " " << getArchName(avail.first) << ":" << avail.second;
}

TAPI_NAMESPACE_INTERNAL_END
