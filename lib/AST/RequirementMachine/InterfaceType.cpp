//===--- InterfaceType.cpp - Type to term conversion ----------------------===//
//
// This source file is part of the Swift.org open source project
//
// Copyright (c) 2021 Apple Inc. and the Swift project authors
// Licensed under Apache License v2.0 with Runtime Library Exception
//
// See https://swift.org/LICENSE.txt for license information
// See https://swift.org/CONTRIBUTORS.txt for the list of Swift project authors
//
//===----------------------------------------------------------------------===//
//
// This file implements routines for converting Swift AST interface types to
// rewrite system terms.
//
// The rewrite system's reduction order differs from the canonical type order
// used by Swift's ABI and name mangling. What this means in practice is that
// converting a canonical type to a term does not necessarily produce a
// canonical term; the term must further be simplifed to produce a canonical
// term. Converting a canonical term back to a type, however, does produce a
// canonical type.
//
// Type to term conversion is implemented on the RewriteContext, and does not
// depend on the specific RewriteSystem used.
//
// Term to type conversion is implemented on the PropertyMap, and must only
// be performed after completion. This is because it relies on the property map
// to map associated type symbols back to Swift types.
//
// The specific difference between the canonical type order and the reduction
// order is as follows. In both orders, an associated type P1.T1 orders before
// an associated type P2.T2 if T1 < T2, or T1 == T2 and P1 < P2. The difference
// is in the protocol order relation P1 < P2.
//
// In the canonical type order, P1 < P2 based on the protocol names alone (or
// their module names, if the unqualified names are the same). In the reduction
// order, we want P1 < P2 to also hold if P1 inherits from P2.
//
//===----------------------------------------------------------------------===//

#include "swift/AST/Decl.h"
#include "swift/AST/Types.h"
#include "PropertyMap.h"
#include "RewriteSystem.h"
#include "RewriteContext.h"

using namespace swift;
using namespace rewriting;

Term RewriteContext::getTermForType(CanType paramType,
                                    const ProtocolDecl *proto) {
  return Term::get(getMutableTermForType(paramType, proto), *this);
}

/// Map an interface type to a term.
///
/// If \p proto is null, this is a term relative to a generic
/// parameter in a top-level signature. The term is rooted in a generic
/// parameter symbol.
///
/// If \p proto is non-null, this is a term relative to a protocol's
/// 'Self' type. The term is rooted in a protocol symbol for this protocol,
/// or an associated type symbol for some associated type in this protocol.
///
/// Resolved DependentMemberTypes map to associated type symbols.
/// Unresolved DependentMemberTypes map to name symbols.
///
/// Note the behavior of the root term is special if it is an associated
/// type symbol. The protocol of the associated type is always mapped to
/// \p proto if it was provided. This ensures we get the correct behavior
/// if a protocol places a constraint on an associated type inherited from
/// another protocol:
///
/// protocol P {
///   associatedtype Foo
/// }
///
/// protocol Q : P where Foo : R {}
///
/// protocol R {}
///
/// The DependentMemberType in the requirement signature of Q refers to
/// P::Foo.
///
/// However, we want Q's requirement signature to introduce the rewrite rule
///
///   [Q:Foo].[R] => [Q:Foo]
///
/// and not
///
///   [P:Foo].[R] => [P:Foo]
///
/// This is because the rule only applies to Q's logical override of Foo, and
/// not P's Foo.
///
/// To handle this, getMutableTermForType() behaves as follows:
///
/// Self.P::Foo with proto = P         => [P:Foo]
/// Self.P::Foo with proto = Q         => [Q:Foo]
/// τ_0_0.P::Foo with proto == nullptr => τ_0_0.[P:Foo]
///
MutableTerm RewriteContext::getMutableTermForType(CanType paramType,
                                                  const ProtocolDecl *proto) {
  assert(paramType->isTypeParameter());

  // Collect zero or more nested type names in reverse order.
  bool innermostAssocTypeWasResolved = false;

  SmallVector<Symbol, 3> symbols;
  while (auto memberType = dyn_cast<DependentMemberType>(paramType)) {
    paramType = memberType.getBase();

    if (auto *assocType = memberType->getAssocType()) {
      if (proto && isa<GenericTypeParamType>(paramType)) {
        innermostAssocTypeWasResolved = true;
        if (proto == assocType->getProtocol())
          symbols.push_back(Symbol::forAssociatedType(assocType, *this));
        else {
          symbols.push_back(Symbol::forAssociatedType(proto, assocType->getName(),
                                                      *this));
        }
      } else {
        symbols.push_back(Symbol::forAssociatedType(assocType, *this));
      }
    } else {
      symbols.push_back(Symbol::forName(memberType->getName(), *this));
      innermostAssocTypeWasResolved = false;
    }
  }

  // Add the root symbol at the end.
  if (proto) {
    assert(proto->getSelfInterfaceType()->isEqual(paramType));

    // Self.Foo becomes [P].Foo
    // Self.Q::Foo becomes [P:Foo] (not [Q:Foo] or [P].[Q:Foo])
    if (!innermostAssocTypeWasResolved)
      symbols.push_back(Symbol::forProtocol(proto, *this));
  } else {
    symbols.push_back(Symbol::forGenericParam(
        cast<GenericTypeParamType>(paramType), *this));
  }

  std::reverse(symbols.begin(), symbols.end());

  return MutableTerm(symbols);
}

/// Map an associated type symbol to an associated type declaration.
///
/// Note that the protocol graph is not part of the caching key; each
/// protocol graph is a subgraph of the global inheritance graph, so
/// the specific choice of subgraph does not change the result.
AssociatedTypeDecl *RewriteContext::getAssociatedTypeForSymbol(Symbol symbol) {
  auto found = AssocTypes.find(symbol);
  if (found != AssocTypes.end())
    return found->second;

  assert(symbol.getKind() == Symbol::Kind::AssociatedType);
  auto name = symbol.getName();

  AssociatedTypeDecl *assocType = nullptr;

  // An associated type symbol [P1&P1&...&Pn:A] has one or more protocols
  // P0...Pn and an identifier 'A'.
  //
  // We map it back to a AssociatedTypeDecl as follows:
  //
  // - For each protocol Pn, look for associated types A in Pn itself,
  //   and all protocols that Pn refines.
  //
  // - For each candidate associated type An in protocol Qn where
  //   Pn refines Qn, get the associated type anchor An' defined in
  //   protocol Qn', where Qn refines Qn'.
  //
  // - Out of all the candidiate pairs (Qn', An'), pick the one where
  //   the protocol Qn' is the lowest element according to the linear
  //   order defined by TypeDecl::compare().
  //
  // The associated type An' is then the canonical associated type
  // representative of the associated type symbol [P0&...&Pn:A].
  //
  auto *proto = symbol.getProtocol();

  auto checkOtherAssocType = [&](AssociatedTypeDecl *otherAssocType) {
    otherAssocType = otherAssocType->getAssociatedTypeAnchor();

    if (otherAssocType->getName() == name &&
        (assocType == nullptr ||
         TypeDecl::compare(otherAssocType->getProtocol(),
                           assocType->getProtocol()) < 0)) {
      assocType = otherAssocType;
    }
  };

  for (auto *otherAssocType : proto->getAssociatedTypeMembers()) {
    checkOtherAssocType(otherAssocType);
  }

  for (auto *inheritedProto : getInheritedProtocols(proto)) {
    for (auto *otherAssocType : inheritedProto->getAssociatedTypeMembers()) {
      checkOtherAssocType(otherAssocType);
    }
  }

  assert(assocType && "Need to look harder");
  AssocTypes[symbol] = assocType;
  return assocType;
}

/// Find the most canonical associated type declaration with the given
/// name among a set of conforming protocols stored in this property map
/// entry.
AssociatedTypeDecl *PropertyBag::getAssociatedType(Identifier name) {
  auto found = AssocTypes.find(name);
  if (found != AssocTypes.end())
    return found->second;

  AssociatedTypeDecl *assocType = nullptr;

  for (auto *proto : ConformsTo) {
    auto checkOtherAssocType = [&](AssociatedTypeDecl *otherAssocType) {
      otherAssocType = otherAssocType->getAssociatedTypeAnchor();

      if (otherAssocType->getName() == name &&
          (assocType == nullptr ||
           TypeDecl::compare(otherAssocType->getProtocol(),
                             assocType->getProtocol()) < 0)) {
        assocType = otherAssocType;
      }
    };

    for (auto *otherAssocType : proto->getAssociatedTypeMembers()) {
      checkOtherAssocType(otherAssocType);
    }
  }

  assert(assocType != nullptr && "Need to look harder");

  auto inserted = AssocTypes.insert(std::make_pair(name, assocType)).second;
  assert(inserted);
  (void) inserted;

  return assocType;
}

/// Compute the interface type for a range of symbols, with an optional
/// root type.
///
/// If the root type is specified, we wrap it in a series of
/// DependentMemberTypes. Otherwise, the root is computed from
/// the first symbol of the range.
static Type
getTypeForSymbolRange(const Symbol *begin, const Symbol *end, Type root,
                      TypeArrayView<GenericTypeParamType> genericParams,
                      const PropertyMap &map) {
  auto &ctx = map.getRewriteContext();
  Type result = root;

  auto handleRoot = [&](GenericTypeParamType *genericParam) {
    assert(genericParam->isCanonical());

    if (!genericParams.empty()) {
      // Return a sugared GenericTypeParamType if we're given an array of
      // sugared types to substitute.
      unsigned index = GenericParamKey(genericParam).findIndexIn(genericParams);

      if (index == genericParams.size()) {
        llvm::errs() << "Cannot build interface type for term "
                     << MutableTerm(begin, end) << "\n";
        llvm::errs() << "Invalid generic parameter: "
                     << Type(genericParam) << "\n";
        llvm::errs() << "Valid generic parameters: ";
        for (auto *otherParam : genericParams)
          llvm::errs() << " " << otherParam->getCanonicalType();
        llvm::errs() << "\n";
        abort();
      }

      result = genericParams[index];
      return;
    }

    // Otherwise, we're going to return a canonical type.
    result = genericParam;
  };

  for (auto *iter = begin; iter != end; ++iter) {
    auto symbol = *iter;

    if (!result) {
      // A valid term always begins with a generic parameter, protocol or
      // associated type symbol.
      switch (symbol.getKind()) {
      case Symbol::Kind::GenericParam:
        handleRoot(symbol.getGenericParam());
        continue;

      case Symbol::Kind::Protocol:
        handleRoot(GenericTypeParamType::get(/*type sequence*/ false, 0, 0,
                                             ctx.getASTContext()));
        continue;

      case Symbol::Kind::AssociatedType:
        handleRoot(GenericTypeParamType::get(/*type sequence*/ false, 0, 0,
                                             ctx.getASTContext()));

        // An associated type symbol at the root means we have a dependent
        // member type rooted at Self; handle the associated type below.
        break;

      case Symbol::Kind::Name:
      case Symbol::Kind::Layout:
      case Symbol::Kind::Superclass:
      case Symbol::Kind::ConcreteType:
      case Symbol::Kind::ConcreteConformance:
        llvm::errs() << "Invalid root symbol: " << MutableTerm(begin, end) << "\n";
        abort();
      }
    }

    // An unresolved type can appear if we have invalid requirements.
    if (symbol.getKind() == Symbol::Kind::Name) {
      result = DependentMemberType::get(result, symbol.getName());
      continue;
    }

    // We can end up with an unsimplified term like this:
    //
    // X.[P].[P:X]
    //
    // Simplification will rewrite X.[P] to X, so just ignore a protocol symbol
    // in the middle of a term.
    if (symbol.getKind() == Symbol::Kind::Protocol) {
#ifndef NDEBUG
      // Ensure that the domain of the suffix contains P.
      if (iter + 1 < end) {
        auto proto = (iter + 1)->getProtocol();
        assert(proto == symbol.getProtocol());
      }
#endif
      continue;
    }

    // We should have a resolved type at this point.
    AssociatedTypeDecl *assocType;

    if (begin == iter) {
      // FIXME: Eliminate this case once merged associated types are gone.
      assocType = ctx.getAssociatedTypeForSymbol(symbol);
    } else {
      // The protocol stored in an associated type symbol appearing in a
      // canonical term is not necessarily the right protocol to look for
      // an associated type declaration to get a canonical _type_, because
      // the reduction order on terms is different than the canonical order
      // on types.
      //
      // Instead, find all protocols that the prefix conforms to, and look
      // for an associated type in those protocols.
      MutableTerm prefix(begin, iter);
      assert(prefix.size() > 0);

      auto *props = map.lookUpProperties(prefix.rbegin(), prefix.rend());
      if (props == nullptr) {
        llvm::errs() << "Cannot build interface type for term "
                     << MutableTerm(begin, end) << "\n";
        llvm::errs() << "Prefix does not conform to any protocols: "
                     << prefix << "\n\n";
        map.dump(llvm::errs());
        abort();
      }

      // Assert that the associated type's protocol appears among the set
      // of protocols that the prefix conforms to.
  #ifndef NDEBUG
      auto conformsTo = props->getConformsTo();
      assert(std::find(conformsTo.begin(), conformsTo.end(),
                       symbol.getProtocol())
             != conformsTo.end());
  #endif

      assocType = props->getAssociatedType(symbol.getName());
      if (assocType == nullptr) {
        llvm::errs() << "Cannot build interface type for term "
                     << MutableTerm(begin, end) << "\n";
        llvm::errs() << "Prefix term does not not have a nested type named "
                     << symbol.getName() << ": "
                     << prefix << "\n";
        llvm::errs() << "Property map entry: ";
        props->dump(llvm::errs());
        llvm::errs() << "\n\n";
        map.dump(llvm::errs());
        abort();
      }
    }

    result = DependentMemberType::get(result, assocType);
  }

  return result;
}

Type PropertyMap::getTypeForTerm(Term term,
                      TypeArrayView<GenericTypeParamType> genericParams) const {
  return getTypeForSymbolRange(term.begin(), term.end(), Type(),
                               genericParams, *this);
}

Type PropertyMap::getTypeForTerm(const MutableTerm &term,
                      TypeArrayView<GenericTypeParamType> genericParams) const {
  return getTypeForSymbolRange(term.begin(), term.end(), Type(),
                               genericParams, *this);
}

Type PropertyMap::getRelativeTypeForTerm(
    const MutableTerm &term, const MutableTerm &prefix) const {
  assert(std::equal(prefix.begin(), prefix.end(), term.begin()));

  auto genericParam =
      CanGenericTypeParamType::get(/*type sequence*/ false, 0, 0,
                                   Context.getASTContext());
  return getTypeForSymbolRange(
      term.begin() + prefix.size(), term.end(), genericParam,
      { }, *this);
}

/// Concrete type terms are written in terms of generic parameter types that
/// have a depth of 0, and an index into an array of substitution terms.
///
/// See RewriteSystemBuilder::getConcreteSubstitutionSchema().
unsigned RewriteContext::getGenericParamIndex(Type type) {
  auto *paramTy = type->castTo<GenericTypeParamType>();
  assert(paramTy->getDepth() == 0);
  return paramTy->getIndex();
}

/// Computes the term corresponding to a member type access on a substitution.
///
/// The type witness is a type parameter of the form τ_0_n.X.Y.Z,
/// where 'n' is an index into the substitution array.
///
/// If the nth entry in the array is S, this will produce S.X.Y.Z.
///
/// There is a special behavior if the substitution is a term consisting of a
/// single protocol symbol [P]. If the innermost associated type in
/// \p typeWitness is [Q:Foo], the result will be [P:Foo], not [P].[Q:Foo] or
/// [Q:Foo].
MutableTerm
RewriteContext::getRelativeTermForType(CanType typeWitness,
                                       ArrayRef<Term> substitutions) {
  MutableTerm result;

  // Get the substitution S corresponding to τ_0_n.
  unsigned index = getGenericParamIndex(typeWitness->getRootGenericParam());
  result = MutableTerm(substitutions[index]);

  // If the substitution is a term consisting of a single protocol symbol
  // [P], save P for later.
  const ProtocolDecl *proto = nullptr;
  if (result.size() == 1 &&
      result[0].getKind() == Symbol::Kind::Protocol) {
    proto = result[0].getProtocol();
  }

  // Collect zero or more member type names in reverse order.
  SmallVector<Symbol, 3> symbols;
  while (auto memberType = dyn_cast<DependentMemberType>(typeWitness)) {
    typeWitness = memberType.getBase();

    auto *assocType = memberType->getAssocType();
    assert(assocType != nullptr &&
           "Conformance checking should not produce unresolved member types");

    // If the substitution is a term consisting of a single protocol symbol [P],
    // produce [P:Foo] instead of [P].[Q:Foo] or [Q:Foo].
    if (proto && isa<GenericTypeParamType>(typeWitness)) {
      assert(result.size() == 1);
      assert(result[0].getKind() == Symbol::Kind::Protocol);
      assert(result[0].getProtocol() == proto);
      result = MutableTerm();

      if (proto == assocType->getProtocol())
        symbols.push_back(Symbol::forAssociatedType(assocType, *this));
      else {
        symbols.push_back(Symbol::forAssociatedType(proto,
                                                    assocType->getName(), *this));
      }
    } else {
      symbols.push_back(Symbol::forAssociatedType(assocType, *this));
    }
  }

  // Add the member type names.
  for (auto iter = symbols.rbegin(), end = symbols.rend(); iter != end; ++iter)
    result.add(*iter);

  return result;
}

/// Reverses the transformation performed by
/// RewriteSystemBuilder::getConcreteSubstitutionSchema().
Type PropertyMap::getTypeFromSubstitutionSchema(
    Type schema, ArrayRef<Term> substitutions,
    TypeArrayView<GenericTypeParamType> genericParams,
    const MutableTerm &prefix) const {
  assert(!schema->isTypeParameter() && "Must have a concrete type here");

  if (!schema->hasTypeParameter())
    return schema;

  return schema.transformRec([&](Type t) -> Optional<Type> {
    if (t->is<GenericTypeParamType>()) {
      auto index = RewriteContext::getGenericParamIndex(t);
      auto substitution = substitutions[index];

      // Prepend the prefix of the lookup key to the substitution.
      if (prefix.empty()) {
        // Skip creation of a new MutableTerm in the case where the
        // prefix is empty.
        return getTypeForTerm(substitution, genericParams);
      } else {
        // Otherwise build a new term by appending the substitution
        // to the prefix.
        MutableTerm result(prefix);
        result.append(substitution);
        return getTypeForTerm(result, genericParams);
      }
    }

    assert(!t->isTypeParameter());
    return None;
  });
}

/// This method takes a concrete type that was derived from a concrete type
/// produced by RewriteContext::getSubstitutionSchemaFromType() either by
/// extracting a structural sub-component or performing a (Swift AST)
/// substitution using subst(). It returns a new concrete substitution schema
/// and a new list of substitution terms.
///
/// For example, suppose we start with the concrete type
///
///   Dictionary<τ_0_0, Array<τ_0_1>> with substitutions {X.Y, Z}
///
/// We can extract out the structural sub-component Array<τ_0_1>. If we wish
/// to build a new concrete substitution schema, we call this method with
/// Array<τ_0_1> and the original substitutions {X.Y, Z}. This will produce
/// the new schema Array<τ_0_0> with substitutions {Z}.
///
/// As another example, consider we start with the schema Bar<τ_0_0> with
/// original substitutions {X.Y}, and perform a Swift AST subst() to get
/// Foo<τ_0_0.A.B>. We can then call this method with Foo<τ_0_0.A.B> and
/// the original substitutions {X.Y} to produce the new schema Foo<τ_0_0>
/// with substitutions {X.Y.A.B}.
CanType
RewriteContext::getRelativeSubstitutionSchemaFromType(
    CanType concreteType,
    ArrayRef<Term> substitutions,
    SmallVectorImpl<Term> &result) {
  assert(!concreteType->isTypeParameter() && "Must have a concrete type here");

  if (!concreteType->hasTypeParameter())
    return concreteType;

  return CanType(concreteType.transformRec([&](Type t) -> Optional<Type> {
    if (!t->isTypeParameter())
      return None;

    auto term = getRelativeTermForType(CanType(t), substitutions);

    unsigned newIndex = result.size();
    result.push_back(Term::get(term, *this));

    return CanGenericTypeParamType::get(/*type sequence=*/ false,
                                        /*depth=*/ 0, newIndex,
                                        Context);
  }));
}

/// Given a concrete type that may contain type parameters in structural positions,
/// collect all the structural type parameter components, and replace them all with
/// fresh generic parameters. The fresh generic parameters all have a depth of 0,
/// and the index is an index into the 'result' array.
///
/// For example, given the concrete type Foo<X.Y, Array<Z>>, this produces the
/// result type Foo<τ_0_0, Array<τ_0_1>>, with result array {X.Y, Z}.
CanType
RewriteContext::getSubstitutionSchemaFromType(CanType concreteType,
                                              const ProtocolDecl *proto,
                                              SmallVectorImpl<Term> &result) {
  assert(!concreteType->isTypeParameter() && "Must have a concrete type here");

  if (!concreteType->hasTypeParameter())
    return concreteType;

  return CanType(concreteType.transformRec([&](Type t) -> Optional<Type> {
    if (!t->isTypeParameter())
      return None;

    unsigned index = result.size();
    result.push_back(getTermForType(CanType(t), proto));

    return CanGenericTypeParamType::get(/*type sequence=*/ false,
                                        /*depth=*/0, index,
                                        Context);
  }));
}
