//===--- ConcreteContraction.cpp - Preprocessing concrete conformances ----===//
//
// This source file is part of the Swift.org open source project
//
// Copyright (c) 2022 Apple Inc. and the Swift project authors
// Licensed under Apache License v2.0 with Runtime Library Exception
//
// See https://swift.org/LICENSE.txt for license information
// See https://swift.org/CONTRIBUTORS.txt for the list of Swift project authors
//
//===----------------------------------------------------------------------===//
//
// The concrete contraction pass runs after requirement desugaring and before
// rewrite rules are built from desugared requirements when constructing a
// rewrite system for a user-written generic signature.
//
// This is an imperfect hack to deal with two issues:
//
// a) When a generic parameter is subject to both a conformance requirement and
//    a concrete type requirement (or a superclass requirement where the
//    superclass also conforms to the protocol), the conformance requirement
//    becomes redundant during property map construction.
//
//    However, unlike other kinds of requirements, dropping a conformance
//    requirement can change canonical types in the rewrite system, and in
//    particular, it can change the canonical types of other minimal
//    requirements, if the protocol in the conformance requirement has
//    associated types.
//
//    Consider this example:
//
//        protocol P {
//          associatedtype T
//        }
//
//        protocol Q {}
//
//        struct S<T> : P {}
//
//        struct Holder<A : P, B : P> where A.T : Q {}
//
//        extension Holder where A == S<B.T> {}
//
//    The signature of the extension is built from these requirements:
//
//    - A : P
//    - B : P
//    - A.T : Q
//    - A == S<B.T>
//
//    In this rewrite system, the canonical type of 'B.T' is 'A.T', so the
//    requirements canonicalize as follows:
//
//    - A : P
//    - B : P
//    - A.T : Q
//    - A == S<A.T>
//
//    Also, the first requirement 'A : P' is redundant in this rewrite system,
//    because 'A == S<B.T>' and S conforms to P.
//
//    However, simply dropping 'A : P' is not enough. If the user instead
//    directly wrote a signature with the original requirements omitting
//    'A : P', we would have:
//
//    - B : P
//    - A == S<B.T>
//    - B.T : Q
//
//    Indeed, computing canonical types from the first rewrite system produces
//    different results, because 'B.T' canonicalizes to 'A.T' and not 'B.T'.
//
// b) The GenericSignatureBuilder permitted references to "fully concrete"
//    member types of a dependent type that were not associated types from
//    conformance requirements.
//
//    That is, the following was permitted:
//
//    class C {
//      typealias A = Int
//    }
//
//    <T, U where T : C, U == T.A>
//
//    The requirement 'U == T.A' was resolved to 'U == Int', despite 'T' not
//    declaring any protocol conformance requirements with an associated type
//    named 'A' (or any conformance requirements at all).
//
// The GenericSignatureBuilder dealt with a) using a "rebuilding" pass that
// build a new generic signature after dropping redundant conformance
// requirements, feeding the original requirements back in. The behavior b)
// was a consequence of how requirement resolution was implemented, by calling
// into name lookup.
//
// The Requirement Machine's approach to both a) and b) doesn't handle as many
// cases, but is much simpler and hopefully more robust. Before building the
// rewrite system, we pre-process the requirements and identify generic
// parameters subject to a superclass or concrete type requirement. Then, we
// substitute this generic parameter for the superclass or concrete type,
// respectively, in all other requirements.
//
// In the above example, it produces the following list of requirements:
//
//    - S<B.T> : P
//    - B : P
//    - S<B.T>.T : Q
//    - A == S<B.T>
//
// The requirements are fed back into desugarRequirements(), and we get:
//
//    - B : P
//    - B.T : Q
//    - A == S<B.T>
//
// This also handles b), where the original requirements are:
//
//    - T : C
//    - U == T.A
//
// and after concrete contraction, we get
//
//    - T : C
//    - U == Int
//
// Since this is all a heuristic that is applied before the rewrite system is
// built, it doesn't handle cases where a nested type of a generic parameter is
// subject to both a concrete type and conformance requirement, nor does it
// handle more complex cases where the redundant conformance is only discovered
// via an intermediate same-type requirement, such as the following:
//
//    <T, U, V where T == S<V>, T == U, U : P>
//
// If concrete contraction fails, the minimized generic signature will fail
// verification if it still contains incorrectly-canonicalized types.
//
// We might need a more general solution eventually, but for now this is good
// enough to handle the cases where this arises in practice.
//
//===----------------------------------------------------------------------===//

#include "swift/AST/Decl.h"
#include "swift/AST/GenericParamKey.h"
#include "swift/AST/Module.h"
#include "swift/AST/Requirement.h"
#include "swift/AST/Type.h"
#include "swift/AST/Types.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallVector.h"
#include "RequirementLowering.h"

using namespace swift;
using namespace rewriting;

namespace {

/// Utility class to store some shared state.
class ConcreteContraction {
  bool Debug;

  llvm::SmallDenseMap<GenericParamKey,
                      llvm::SmallDenseSet<Type, 1>> ConcreteTypes;
  llvm::SmallDenseMap<GenericParamKey,
                      llvm::SmallDenseSet<Type, 1>> Superclasses;
  llvm::SmallDenseMap<GenericParamKey,
                      llvm::SmallVector<ProtocolDecl *, 1>> Conformances;

  Optional<Type> substTypeParameter(GenericTypeParamType *rootParam,
                                    DependentMemberType *memberType,
                                    Type concreteType) const;
  Type substTypeParameter(Type type,
                          bool subjectTypeOfConformance=false) const;
  Type substType(Type type) const;
  Requirement substRequirement(const Requirement &req) const;

public:
  ConcreteContraction(bool debug) : Debug(debug) {}

  bool performConcreteContraction(
      ArrayRef<StructuralRequirement> requirements,
      SmallVectorImpl<StructuralRequirement> &result);
};

}  // end namespace

/// Find the most canonical member type of \p decl named \p name, using the
/// canonical type order.
static TypeDecl *lookupConcreteNestedType(ModuleDecl *module,
                                          NominalTypeDecl *decl,
                                          Identifier name) {
  SmallVector<ValueDecl *, 2> foundMembers;
  module->lookupQualified(
      decl, DeclNameRef(name),
      NL_QualifiedDefault | NL_OnlyTypes | NL_ProtocolMembers,
      foundMembers);

  SmallVector<TypeDecl *, 2> concreteDecls;
  for (auto member : foundMembers)
    concreteDecls.push_back(cast<TypeDecl>(member));

  if (concreteDecls.empty())
    return nullptr;

  return *std::min_element(concreteDecls.begin(), concreteDecls.end(),
                           [](TypeDecl *type1, TypeDecl *type2) {
                             return TypeDecl::compare(type1, type2) < 0;
                           });
}

/// A re-implementation of Type::subst() that also handles unresolved
/// DependentMemberTypes by performing name lookup into the base type.
///
/// When substituting a superclass requirement, we have to handle the
/// case where the superclass might not conform to the protocol in
/// question. For example, you can have a generic signature like this
///
///     <T where T : Sequence, T : SomeClass, T.Element == Int>
///
/// If SomeClass does not conform to Sequence, the type T is understood
/// to be some subclass of SomeClass which does conform to Sequence;
/// this is perfectly valid, and we cannot substitute the 'T.Element'
/// requirement. In this case, this method returns None.
Optional<Type> ConcreteContraction::substTypeParameter(
    GenericTypeParamType *rootParam,
    DependentMemberType *memberType,
    Type concreteType) const {
  if (memberType == nullptr)
    return concreteType;

  auto baseType = memberType->getBase()->getAs<DependentMemberType>();
  auto substBaseType = substTypeParameter(rootParam, baseType, concreteType);
  if (!substBaseType)
    return None;

  // A resolved DependentMemberType stores an associated type declaration.
  //
  // Handle this by looking up the corresponding type witness in the base
  // type's conformance to the associated type's protocol.
  if (auto *assocType = memberType->getAssocType()) {
    auto *proto = assocType->getProtocol();
    auto *module = proto->getParentModule();

    auto conformance = ((*substBaseType)->isTypeParameter()
                        ? ProtocolConformanceRef(proto)
                        : module->lookupConformance(*substBaseType, proto));

    // The base type doesn't conform, in which case the requirement remains
    // unsubstituted.
    if (!conformance) {
      if (Debug) {
        llvm::dbgs() << "@@@ " << substBaseType << " does not conform to "
                     << proto->getName() << "\n";
      }
      return None;
    }

    return assocType->getDeclaredInterfaceType()
                    ->castTo<DependentMemberType>()
                    ->substBaseType(module, *substBaseType);
  }

  auto *decl = (*substBaseType)->getAnyNominal();
  if (decl == nullptr) {
    if (Debug) {
      llvm::dbgs() << "@@@ Not a nominal type: " << *substBaseType << "\n";
    }

    return None;
  }

  auto *module = decl->getParentModule();

  // An unresolved DependentMemberType stores an identifier. Handle this
  // by performing a name lookup into the base type.
  auto *typeDecl = lookupConcreteNestedType(module, decl,
                                            memberType->getName());
  if (typeDecl == nullptr) {
    // The base type doesn't contain a member type with this name, in which
    // case the requirement remains unsubstituted.
    if (Debug) {
      llvm::dbgs() << "@@@ Lookup of " << memberType->getName() << " failed on "
                   << *substBaseType << "\n";
    }
    return None;
  }

  // Substitute the base type into the member type.
  auto subMap = (*substBaseType)->getContextSubstitutionMap(
      module, typeDecl->getDeclContext());
  return typeDecl->getDeclaredInterfaceType().subst(subMap);
}

/// Replace the generic parameter at the root of \p type, which must be a
/// type parameter, with the superclass or concrete type requirement that
/// the generic parameter is subject to.
///
/// Note that if the generic parameter has a superclass conformance, we
/// only substitute if it's the root of a member type; the generic parameter
/// itself does not become concrete when it's superclass-constrained, unless
/// it is the subject of a conformance requirement.
Type ConcreteContraction::substTypeParameter(
    Type type, bool subjectTypeOfConformance) const {
  assert(type->isTypeParameter());

  auto rootParam = type->getRootGenericParam();
  auto key = GenericParamKey(rootParam);

  Type concreteType;
  {
    auto found = ConcreteTypes.find(key);
    if (found != ConcreteTypes.end() && found->second.size() == 1)
      concreteType = *found->second.begin();
  }

  Type superclass;
  {
    auto found = Superclasses.find(key);
    if (found != Superclasses.end() && found->second.size() == 1)
      superclass = *found->second.begin();
  }

  if (!concreteType && !superclass)
    return type;

  // If we have both, prefer the concrete type requirement since it is more
  // specific.
  if (!concreteType) {
    assert(superclass);

    // For a superclass requirement, don't substitute the generic parameter
    // itself, only nested types thereof -- unless it's the subject type of
    // a conformance requirement.
    if (type->is<GenericTypeParamType>() &&
        !subjectTypeOfConformance)
      return type;

    concreteType = superclass;
  }

  auto result = substTypeParameter(
      rootParam, type->getAs<DependentMemberType>(),
      concreteType);

  if (!result)
    return type;

  return *result;
}

/// Substitute all type parameters occurring in structural positions of \p type.
Type ConcreteContraction::substType(Type type) const {
  return type.transformRec(
      [&](Type type) -> Optional<Type> {
        if (!type->isTypeParameter())
          return None;

        return substTypeParameter(type);
      });
}

/// Substitute all type parameters occurring in the given requirement.
Requirement
ConcreteContraction::substRequirement(const Requirement &req) const {
  auto firstType = req.getFirstType();

  switch (req.getKind()) {
  case RequirementKind::Superclass:
  case RequirementKind::SameType: {
    auto substFirstType = firstType;

    // If the requirement is of the form 'T == C' or 'T : C', don't
    // substitute T, since then we end up with 'C == C' or 'C : C',
    // losing the requirement.
    if (!firstType->is<GenericTypeParamType>()) {
      substFirstType = substTypeParameter(firstType);
    }

    auto secondType = req.getSecondType();
    auto substSecondType = substType(secondType);

    return Requirement(req.getKind(),
                       substFirstType,
                       substSecondType);
  }

  case RequirementKind::Conformance: {
    auto substFirstType = substTypeParameter(
        firstType, /*subjectTypeOfConformance=*/true);

    auto *proto = req.getProtocolDecl();
    auto *module = proto->getParentModule();
    if (!substFirstType->isTypeParameter() &&
        !module->lookupConformance(substFirstType, proto)) {
      // Handle the case of <T where T : P, T : C> where C is a class and
      // C does not conform to P by leaving the conformance requirement
      // unsubstituted.
      return req;
    }

    // Otherwise, replace the generic parameter in the conformance
    // requirement with the concrete type. It will desugar to nothing
    // (if the conformance is conditional) or to zero or more
    // conditional requirements needed to satisfy the conformance.
    return Requirement(req.getKind(),
                       substFirstType,
                       req.getSecondType());
  }

  case RequirementKind::Layout: {
    auto substFirstType = substTypeParameter(firstType);

    return Requirement(req.getKind(),
                       substFirstType,
                       req.getLayoutConstraint());
  }
  }
}

/// Substitute all occurrences of generic parameters subject to superclass
/// or concrete type requirements with their corresponding superclass or
/// concrete type.
///
/// If this returns false, \p result should be ignored and the requirements
/// remain unchanged. If this returns true, \p result should replace the
/// original \p requirements.
bool ConcreteContraction::performConcreteContraction(
    ArrayRef<StructuralRequirement> requirements,
    SmallVectorImpl<StructuralRequirement> &result) {

  // Phase 1 - collect concrete type and superclass requirements where the
  // subject type is a generic parameter.
  for (auto req : requirements) {
    auto subjectType = req.req.getFirstType();
    assert(subjectType->isTypeParameter() &&
           "You forgot to call desugarRequirement()");

    auto genericParam = subjectType->getAs<GenericTypeParamType>();
    if (!genericParam)
      continue;

    auto kind = req.req.getKind();
    switch (kind) {
    case RequirementKind::SameType: {
      auto constraintType = req.req.getSecondType();

      // Same-type requirements between type parameters are not interesting
      // to this pass.
      if (constraintType->isTypeParameter())
        break;

      ConcreteTypes[GenericParamKey(genericParam)].insert(constraintType);
      break;
    }
    case RequirementKind::Superclass: {
      auto constraintType = req.req.getSecondType();
      assert(!constraintType->isTypeParameter() &&
             "You forgot to call desugarRequirement()");

      Superclasses[GenericParamKey(genericParam)].insert(constraintType);
      break;
    }
    case RequirementKind::Conformance: {
      auto *protoDecl = req.req.getProtocolDecl();
      Conformances[GenericParamKey(genericParam)].push_back(protoDecl);

      break;
    }
    case RequirementKind::Layout:
      break;
    }
  }

  // Block concrete contraction if a generic parameter conforms to a protocol P
  // which has a superclass bound C which again conforms to P. This is a really
  // silly edge case, but we go to great pains to produce the same minimized
  // signature as the GenericSignatureBuilder in this case, <T : P>, and not the
  // more logical <T : C>.
  for (const auto &pair : Conformances) {
    auto subjectType = pair.first;
    auto found = Superclasses.find(subjectType);
    if (found == Superclasses.end() || found->second.size() != 1)
      continue;

    auto superclassTy = *found->second.begin();

    for (const auto *proto : pair.second) {
      if (auto otherSuperclassTy = proto->getSuperclass()) {
        if (Debug) {
          llvm::dbgs() << "@ Subject type of superclass requirement "
                       << "τ_" << subjectType.Depth << "_" << subjectType.Index
                       << " : " << superclassTy << " conforms to "
                       << proto->getName() << " which has a superclass bound "
                       << otherSuperclassTy << "\n";
        }

        if (superclassTy->isEqual(otherSuperclassTy)) {
          Superclasses.erase(subjectType);
          break;
        }
      }
    }
  }

  // If there's nothing to do just return.
  if (ConcreteTypes.empty() && Superclasses.empty())
    return false;

  if (Debug) {
    llvm::dbgs() << "@ Concrete types: @\n";
    for (auto pair : ConcreteTypes) {
      llvm::dbgs() << "- τ_" << pair.first.Depth
                   << "_" << pair.first.Index;
      if (pair.second.size() == 1) {
        llvm::dbgs() << " == " << *pair.second.begin() << "\n";
      } else {
        llvm::dbgs() << " has duplicate concrete type requirements\n";
      }
    }

    llvm::dbgs() << "@ Superclasses: @\n";
    for (auto pair : Superclasses) {
      llvm::dbgs() << "- τ_" << pair.first.Depth
                   << "_" << pair.first.Index;
      if (pair.second.size() == 1) {
        llvm::dbgs() << " : " << *pair.second.begin() << "\n";
      } else {
        llvm::dbgs() << " has duplicate superclass requirements\n";
      }
    }
  }

  // Phase 2: Replace each concretely-conforming generic parameter with its
  // concrete type.
  for (auto req : requirements) {
    if (Debug) {
      llvm::dbgs() << "@ Original requirement: ";
      req.req.dump(llvm::dbgs());
      llvm::dbgs() << "\n";
    }

    // Substitute the requirement.
    Optional<Requirement> substReq = substRequirement(req.req);

    // If substitution failed, we have a conflict; bail out here so that we can
    // diagnose the conflict later.
    if (!substReq) {
      if (Debug) {
        llvm::dbgs() << "@ Concrete contraction cannot proceed; requirement ";
        llvm::dbgs() << "substitution failed:\n";
        req.req.dump(llvm::dbgs());
        llvm::dbgs() << "\n";
      }

      return false;
    }

    if (Debug) {
      llvm::dbgs() << "@ Substituted requirement: ";
      substReq->dump(llvm::dbgs());
      llvm::dbgs() << "\n";
    }

    // Otherwise, desugar the requirement again, since we might now have a
    // requirement where the left hand side is not a type parameter.
    //
    // FIXME: Do we need to check for errors? Right now they're just ignored.
    SmallVector<Requirement, 4> reqs;
    SmallVector<RequirementError, 1> errors;
    desugarRequirement(*substReq, reqs, errors);
    for (auto desugaredReq : reqs) {
      if (Debug) {
        llvm::dbgs() << "@@ Desugared requirement: ";
        desugaredReq.dump(llvm::dbgs());
        llvm::dbgs() << "\n";
      }
      result.push_back({desugaredReq, req.loc, req.inferred});
    }
  }

  if (Debug) {
    llvm::dbgs() << "@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@\n";
    llvm::dbgs() << "@ Concrete contraction succeeded @\n";
    llvm::dbgs() << "@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@\n";
  }

  return true;
}

/// Substitute all occurrences of generic parameters subject to superclass
/// or concrete type requirements with their corresponding superclass or
/// concrete type.
///
/// If this returns false, \p result should be ignored and the requirements
/// remain unchanged. If this returns true, \p result should replace the
/// original \p requirements.
bool swift::rewriting::performConcreteContraction(
    ArrayRef<StructuralRequirement> requirements,
    SmallVectorImpl<StructuralRequirement> &result,
    bool debug) {
  ConcreteContraction concreteContraction(debug);
  return concreteContraction.performConcreteContraction(requirements, result);
}