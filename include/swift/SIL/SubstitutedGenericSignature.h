//===--- SubstitutedGenericSignature.h ---------------------------*-C++ -*-===//
//
// This source file is part of the Swift.org open source project
//
// Copyright (c) 2014 - 2016 Apple Inc. and the Swift project authors
// Licensed under Apache License v2.0 with Runtime Library Exception
//
// See http://swift.org/LICENSE.txt for license information
// See http://swift.org/CONTRIBUTORS.txt for the list of Swift project authors
//
//===----------------------------------------------------------------------===//
//
// This file defines the SubstitutedGenericSignature class.
//
//===----------------------------------------------------------------------===//

#ifndef SWIFT_SIL_SUBSTITUTED_GENERIC_SIGNATURE_H
#define SWIFT_SIL_SUBSTITUTED_GENERIC_SIGNATURE_H

#include "swift/AST/GenericSignature.h"
#include "swift/AST/Substitution.h"

namespace swift {

/// Describes the mapping of substitution lists between two generic signatures.
class SubstitutedGenericSignature final {
  // The original generic signature as seen by callers of a function.
  CanGenericSignature originalSig;

  // The replacement generic signature of the function's context.
  CanGenericSignature replacementSig;

  // The signature of the thunk.
  CanGenericSignature thunkSig;

  // Substitutions mapping generic parameters of the original signature to
  // interface types written in terms of the substituted signature.
  TypeSubstitutionMap subMap;
  TypeConformanceMap conformanceMap;

  static CanGenericSignature substGenericSignature(
      ModuleDecl *M,
      CanGenericSignature originalSig,
      CanGenericSignature substSig,
      const TypeSubstitutionMap &subMap,
      const TypeConformanceMap &conformanceMap);

public:
  // Creates a new substituted generic signature.
  SubstitutedGenericSignature(ModuleDecl *M,
                              CanGenericSignature originalSig,
                              CanGenericSignature substSig,
                              const TypeSubstitutionMap &subMap,
                              const TypeConformanceMap &conformanceMap);

  // Constructor for the case where the thunk signature is already known.
  SubstitutedGenericSignature(CanGenericSignature originalSig,
                              CanGenericSignature replacementSig,
                              CanGenericSignature thunkSig,
                              const TypeSubstitutionMap &subMap,
                              const TypeConformanceMap &conformanceMap);

  // Returns the substituted GenericSignature.
  CanGenericSignature getGenericSignature() {
    return thunkSig;
  }

  void transformSubstitutions(
      ModuleDecl *M,
      ArrayRef<Substitution> originalSubs,
      SmallVectorImpl<Substitution> &thunkSubs);
};

} // end namespace swift

#endif // SWIFT_SIL_SUBSTITUTED_GENERIC_SIGNATURE_H

