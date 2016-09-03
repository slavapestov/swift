//===--- SubstitutedGenericSignature.cpp ----------------------------------===//
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
// This file implements the SubstitutedGenericSignature class.
//
// Both witness method and class method calls have the property that the
// apply instruction used for dynamic calls has a different generic
// signature than the static callee. 
// 
// This requires care when building witness and vtable thunks, and also
// when devirtualizing dynamic calls in the optimizer, because the
// substitution list used at the apply instruction might not be directly
// suitable for the static thunk:
// 
// - With witness methods, the method is called using the protocol
//   requirement's signature, <Self : P, ...>, however the
//   witness thunk has a generic signature derived from the
//   concrete witness.
// 
//   For example, the requirement might have a signature
//   <Self : P, T>, where the concrete witness thunk might
//   have a signature <X, Y>, where the concrete conforming type
//   is G<X, Y>.
// 
//   At the call site, we substitute Self := G<X', Y'>; however
//   to be able to call the witness thunk directly, we need to
//   form substitutions X := X' and Y := Y'.
// 
// - A similar situation occurs with class methods when the
//   dynamically-dispatched call is performed against a derived
//   class, but devirtualization actually finds the method on a
//   base class of the derived class.
// 
//   The base class may have a different number of generic
//   parameters than the derived class, either because the
//   derived class makes some generic parameters of the base
//   class concrete, or if the derived class introduces new
//   generic parameters of its own.
// 
// In both cases, we need to consider the generic signature of the
// dynamic call site (the protocol requirement or the derived
// class method) as well as the generic signature of the static
// thunk, and carefully remap the substitutions from one form
// into another.
//
// This class encapsulates the logic for building a generic
// signature for the thunk, as well as remapping substitutions
// from the original signature to the thunk signature.
//
//===----------------------------------------------------------------------===//

#include "swift/AST/ArchetypeBuilder.h"
#include "swift/AST/Decl.h"
#include "swift/AST/Module.h"
#include "swift/AST/ProtocolConformance.h"
#include "swift/AST/Type.h"
#include "swift/AST/Types.h"
#include "swift/SIL/SubstitutedGenericSignature.h"

using namespace swift;

// Creates a new substituted generic signature.
SubstitutedGenericSignature::SubstitutedGenericSignature(
    ModuleDecl *M,
    CanGenericSignature originalSig,
    CanGenericSignature replacementSig,
    const TypeSubstitutionMap &subMap,
    const TypeConformanceMap &conformanceMap)
  : originalSig(originalSig),
    replacementSig(replacementSig),
    subMap(subMap),
    conformanceMap(conformanceMap) {
  thunkSig = substGenericSignature(M, originalSig, replacementSig,
                                   subMap, conformanceMap);
}

// Constructor for the case where the thunk signature is already known.
SubstitutedGenericSignature::SubstitutedGenericSignature(
    CanGenericSignature originalSig,
    CanGenericSignature replacementSig,
    CanGenericSignature thunkSig,
    const TypeSubstitutionMap &subMap,
    const TypeConformanceMap &conformanceMap)
  : originalSig(originalSig),
    replacementSig(replacementSig),
    thunkSig(thunkSig),
    subMap(subMap),
    conformanceMap(conformanceMap) {}

/// This is a hack to deal with the fact that TypeBase::subst() does not have
/// a conformanceMap plumbed through; it needs to go away.
static void addConformanceToSubstitutionMap(ModuleDecl *M,
                                TypeSubstitutionMap &subMap,
                                CanType base,
                                const ProtocolConformance *conformance) {
  conformance->forEachTypeWitness(nullptr, [&](AssociatedTypeDecl *assocTy,
                                               Substitution sub,
                                               TypeDecl *) -> bool {
    auto depTy =
      CanDependentMemberType::get(base, assocTy, M->getASTContext());
    auto replacement = sub.getReplacement()->getCanonicalType();
    auto *genericEnv = conformance->getGenericEnvironment();
    replacement = ArchetypeBuilder::mapTypeOutOfContext(M,
                                                        genericEnv,
                                                        replacement)
      ->getCanonicalType();
    subMap.insert({depTy.getPointer(), replacement});
    for (auto conformance : sub.getConformances()) {
      if (conformance.isAbstract())
        continue;
      addConformanceToSubstitutionMap(M, subMap, depTy,
                                      conformance.getConcrete());
    }
    return false;
  });
}

CanGenericSignature
SubstitutedGenericSignature::substGenericSignature(
    ModuleDecl *M,
    CanGenericSignature originalSig,
    CanGenericSignature replacementSig,
    const TypeSubstitutionMap &subMap,
    const TypeConformanceMap &conformanceMap) {
  SmallVector<GenericTypeParamType*, 4> allParams;
  SmallVector<Requirement, 4> allReqts;

  // do something to stick conformanceMap into subMap

  // Add the replacement generic signature.
  if (replacementSig) {
    allParams.append(replacementSig->getGenericParams().begin(),
                     replacementSig->getGenericParams().end());
    allReqts.append(replacementSig->getRequirements().begin(),
                    replacementSig->getRequirements().end());
  }

  // Now we look at the original generic signature.
  //
  // We are going to drop any generic parameter that has been
  // substituted away.
  for (auto *param : originalSig->getGenericParams()) {
    auto replacement = Type(param).subst(M, subMap, SubstFlags::IgnoreMissing);
    if (auto *substParam = replacement->getAs<GenericTypeParamType>())
      allParams.push_back(substParam);
  }

  for (auto &reqt : originalSig->getRequirements()) {
    switch (reqt.getKind()) {
    case RequirementKind::Conformance:
    case RequirementKind::Superclass:
    case RequirementKind::WitnessMarker: {
      // Substituting the parameter eliminates conformance constraints rooted
      // in the parameter.
      auto first = reqt.getFirstType().subst(M, subMap,
                                             SubstFlags::IgnoreMissing);
      if (!first->isTypeParameter())
        continue;
      break;
    }
    case RequirementKind::SameType: {
      // Substitute the constrained types.
      auto first = reqt.getFirstType().subst(M, subMap,
                                             SubstFlags::IgnoreMissing);
      auto second = reqt.getSecondType().subst(M, subMap,
                                               SubstFlags::IgnoreMissing);

      if (!first->isTypeParameter()) {
        if (!second->isTypeParameter())
          continue;

        std::swap(first, second);
      }

      allReqts.push_back(Requirement(RequirementKind::SameType, first, second));

      continue;
    }
    }
    allReqts.push_back(reqt);
  }

  if (!allParams.empty() && !allReqts.empty())
    return GenericSignature::get(allParams, allReqts)->getCanonicalSignature();

  return CanGenericSignature();
}

void SubstitutedGenericSignature::transformSubstitutions(
    ModuleDecl *M,
    ArrayRef<Substitution> originalSubs,
    SmallVectorImpl<Substitution> &thunkSubs) {

  // If the thunk has no generic signature, we just drop the original
  // substitutions on the floor.
  if (!thunkSig)
    return;

  // Otherwise, we need to build new caller-side substitutions
  // written in terms of the thunk's generic signature.

  // We start with a copy of the the replacement signature substitutions.
  TypeSubstitutionMap subMap = this->subMap;
  TypeConformanceMap conformanceMap = this->conformanceMap;

  if (originalSig) {
    // We take apart the original substitutions.
    TypeSubstitutionMap origSubMap;
    TypeConformanceMap origConformanceMap;

    originalSig->getSubstitutionMap(originalSubs, origSubMap, origConformanceMap);

    // Next, filter any original substitutions whose left hand side has been
    // eliminated by replacement substitutions.
    //
    // For example, with protocol witness methods, this is where the `Self`
    // type is eliminated.
    for (auto pair : origSubMap) {
      auto first = Type(pair.first).subst(M, subMap, SubstFlags::IgnoreMissing);
      if (first->isTypeParameter())
        subMap.insert({first->getCanonicalType().getPointer(), pair.second});
    }

    for (auto pair : origConformanceMap) {
      auto first = Type(pair.first).subst(M, subMap, SubstFlags::IgnoreMissing);
      if (first->isTypeParameter())
        conformanceMap.insert({first->getCanonicalType().getPointer(), pair.second});
    }
  }

  // Finally, build a new substitution list using the thunk's generic signature,
  // suitable for statically-dispatched calls to the thunk.
  thunkSig->getSubstitutions(*M, subMap, conformanceMap, thunkSubs);
}
