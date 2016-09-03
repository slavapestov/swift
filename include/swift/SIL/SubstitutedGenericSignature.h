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
  CanGenericSignature OriginalSig;

  // The replacement generic signature of the function's context.
  CanGenericSignature ReplacementSig;

  // The signature of the thunk.
  CanGenericSignature ThunkSig;

  // Substitutions mapping generic parameters of the original signature to
  // interface types written in terms of the substituted signature.
  TypeSubstitutionMap SubMap;
  TypeConformanceMap ConformanceMap;

  static CanGenericSignature substGenericSignature(
      ModuleDecl *M,
      CanGenericSignature originalSig,
      CanGenericSignature substSig,
      const TypeSubstitutionMap &subMap,
      const TypeConformanceMap &conformanceMap);

public:
  // Creates a new substituted generic signature. Either one of the given
  // generic signatures may be null.
  SubstitutedGenericSignature(ModuleDecl *M,
                              GenericSignature *originalSig,
                              GenericSignature *substSig,
                              const TypeSubstitutionMap &subMap,
                              const TypeConformanceMap &conformanceMap);

  // Creates a new substituted generic signature. Either one of the given
  // generic signatures may be null.
  //
  // This is a special constructor for the case where the thunk signature
  // is already known.
  SubstitutedGenericSignature(GenericSignature *originalSig,
                              GenericSignature *replacementSig,
                              GenericSignature *thunkSig,
                              const TypeSubstitutionMap &subMap,
                              const TypeConformanceMap &conformanceMap);

  // Returns the substituted GenericSignature.
  CanGenericSignature getGenericSignature() const {
    return ThunkSig;
  }

  void transformSubstitutions(
      ModuleDecl *M,
      ArrayRef<Substitution> originalSubs,
      SmallVectorImpl<Substitution> &thunkSubs) const;
};

} // end namespace swift

#endif // SWIFT_SIL_SUBSTITUTED_GENERIC_SIGNATURE_H

