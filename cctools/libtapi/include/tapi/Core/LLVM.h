//===- tapi/Core/LLVM.h - Import various common LLVM datatypes --*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file forward declares and imports various common LLVM and clang
// datatypes that tapi wants to use unqualified.
//
//===----------------------------------------------------------------------===//

#ifndef TAPI_CORE_LLVM_H
#define TAPI_CORE_LLVM_H

#include "llvm/Support/Casting.h"

namespace llvm {

// ADT's.
template <typename T> class ErrorOr;
template <typename T> class IntrusiveRefCntPtr;
template <typename T, unsigned N> class SmallPtrSet;
template <unsigned InternalLen> class SmallString;
template <typename T, unsigned N> class SmallVector;
template <typename T> class SmallVectorImpl;
class raw_ostream;
class StringRef;
class Twine;

} // end namespace llvm.

namespace tapi {

// Casting operators.
using llvm::isa;
using llvm::cast;
using llvm::dyn_cast;
using llvm::dyn_cast_or_null;
using llvm::cast_or_null;

// ADT's.
using llvm::ErrorOr;
using llvm::IntrusiveRefCntPtr;
using llvm::SmallPtrSet;
using llvm::SmallString;
using llvm::SmallVector;
using llvm::SmallVectorImpl;
using llvm::raw_ostream;
using llvm::StringRef;
using llvm::Twine;

} // end namespace tapi.

#endif // TAPI_CORE_LLVM_H
