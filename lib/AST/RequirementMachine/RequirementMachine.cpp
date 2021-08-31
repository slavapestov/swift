//===--- RequirementMachine.cpp - Generics with term rewriting ------------===//
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

#include "swift/AST/ASTContext.h"
#include "swift/AST/Decl.h"
#include "swift/AST/GenericSignature.h"
#include "swift/AST/Module.h"
#include "swift/AST/PrettyStackTrace.h"
#include "swift/AST/Requirement.h"
#include <vector>

#include "RequirementMachine.h"

using namespace swift;
using namespace rewriting;

namespace {

/// A utility class for bulding a rewrite system from the top-level requirements
/// of a generic signature, and all protocol requirement signatures from all
/// transitively-referenced protocols.
struct RewriteSystemBuilder {
  RewriteContext &Context;
  bool Dump;

  ProtocolGraph Protocols;
  std::vector<std::pair<MutableTerm, MutableTerm>> Rules;

  CanType getConcreteSubstitutionSchema(CanType concreteType,
                                        const ProtocolDecl *proto,
                                        SmallVectorImpl<Term> &result);

  RewriteSystemBuilder(RewriteContext &ctx, bool dump)
    : Context(ctx), Dump(dump) {}
  void addGenericSignature(CanGenericSignature sig);
  void addAssociatedType(const AssociatedTypeDecl *type,
                         const ProtocolDecl *proto);
  void addRequirement(const Requirement &req,
                      const ProtocolDecl *proto);
};

} // end namespace

/// Given a concrete type that may contain type parameters in structural positions,
/// collect all the structural type parameter components, and replace them all with
/// fresh generic parameters. The fresh generic parameters all have a depth of 0,
/// and the index is an index into the 'result' array.
///
/// For example, given the concrete type Foo<X.Y, Array<Z>>, this produces the
/// result type Foo<τ_0_0, Array<τ_0_1>>, with result array {X.Y, Z}.
CanType
RewriteSystemBuilder::getConcreteSubstitutionSchema(CanType concreteType,
                                                    const ProtocolDecl *proto,
                                                    SmallVectorImpl<Term> &result) {
  assert(!concreteType->isTypeParameter() && "Must have a concrete type here");

  if (!concreteType->hasTypeParameter())
    return concreteType;

  return CanType(concreteType.transformRec(
    [&](Type t) -> Optional<Type> {
      if (!t->isTypeParameter())
        return None;

      unsigned index = result.size();
      result.push_back(Context.getTermForType(CanType(t), proto));

      return CanGenericTypeParamType::get(/*depth=*/0, index, Context.getASTContext());
    }));
}

void RewriteSystemBuilder::addGenericSignature(CanGenericSignature sig) {
  // Collect all protocols transitively referenced from the generic signature's
  // requirements.
  Protocols.visitRequirements(sig.getRequirements());
  Protocols.compute();

  // Add rewrite rules for each protocol.
  for (auto *proto : Protocols.getProtocols()) {
    if (Dump) {
      llvm::dbgs() << "protocol " << proto->getName() << " {\n";
    }

    const auto &info = Protocols.getProtocolInfo(proto);

    for (auto *assocType : info.AssociatedTypes)
      addAssociatedType(assocType, proto);

    for (auto *assocType : info.InheritedAssociatedTypes)
      addAssociatedType(assocType, proto);

    for (auto req : proto->getRequirementSignature())
      addRequirement(req.getCanonical(), proto);

    if (Dump) {
      llvm::dbgs() << "}\n";
    }
  }

  // Add rewrite rules for all requirements in the top-level signature.
  for (const auto &req : sig.getRequirements())
    addRequirement(req, /*proto=*/nullptr);
}

/// For an associated type T in a protocol P, we add a rewrite rule:
///
///   [P].T => [P:T]
///
/// Intuitively, this means "if a type conforms to P, it has a nested type
/// named T".
void RewriteSystemBuilder::addAssociatedType(const AssociatedTypeDecl *type,
                                             const ProtocolDecl *proto) {
  MutableTerm lhs;
  lhs.add(Symbol::forProtocol(proto, Context));
  lhs.add(Symbol::forName(type->getName(), Context));

  MutableTerm rhs;
  rhs.add(Symbol::forAssociatedType(proto, type->getName(), Context));

  Rules.emplace_back(lhs, rhs);
}

/// Lowers a generic requirement to a rewrite rule.
///
/// If \p proto is null, this is a generic requirement from the top-level
/// generic signature. The added rewrite rule will be rooted in a generic
/// parameter symbol.
///
/// If \p proto is non-null, this is a generic requirement in the protocol's
/// requirement signature. The added rewrite rule will be rooted in a
/// protocol symbol.
void RewriteSystemBuilder::addRequirement(const Requirement &req,
                                          const ProtocolDecl *proto) {
  if (Dump) {
    llvm::dbgs() << "+ ";
    req.dump(llvm::dbgs());
    llvm::dbgs() << "\n";
  }

  // Compute the left hand side.
  auto subjectType = CanType(req.getFirstType());
  auto subjectTerm = Context.getMutableTermForType(subjectType, proto);

  // Compute the right hand side.
  MutableTerm constraintTerm;

  switch (req.getKind()) {
  case RequirementKind::Conformance: {
    // A conformance requirement T : P becomes a rewrite rule
    //
    //   T.[P] == T
    //
    // Intuitively, this means "any type ending with T conforms to P".
    auto *proto = req.getProtocolDecl();

    constraintTerm = subjectTerm;
    constraintTerm.add(Symbol::forProtocol(proto, Context));
    break;
  }

  case RequirementKind::Superclass: {
    // A superclass requirement T : C<X, Y> becomes a rewrite rule
    //
    //   T.[superclass: C<X, Y>] => T
    //
    // Together with a rewrite rule
    //
    //   T.[layout: L] => T
    //
    // Where 'L' is either AnyObject or _NativeObject, depending on the
    // ancestry of C.
    auto otherType = CanType(req.getSecondType());

    SmallVector<Term, 1> substitutions;
    otherType = getConcreteSubstitutionSchema(otherType, proto,
                                              substitutions);

    constraintTerm = subjectTerm;
    constraintTerm.add(Symbol::forSuperclass(otherType, substitutions,
                                             Context));
    Rules.emplace_back(subjectTerm, constraintTerm);

    constraintTerm = subjectTerm;
    auto layout =
      LayoutConstraint::getLayoutConstraint(
        otherType->getClassOrBoundGenericClass()->usesObjCObjectModel()
          ? LayoutConstraintKind::Class
          : LayoutConstraintKind::NativeClass,
        Context.getASTContext());
    constraintTerm.add(Symbol::forLayout(layout, Context));
    break;
  }

  case RequirementKind::Layout: {
    // A layout requirement T : L becomes a rewrite rule
    //
    //   T.[layout: L] == T
    constraintTerm = subjectTerm;
    constraintTerm.add(Symbol::forLayout(req.getLayoutConstraint(),
                                         Context));
    break;
  }

  case RequirementKind::SameType: {
    auto otherType = CanType(req.getSecondType());

    if (!otherType->isTypeParameter()) {
      // A concrete same-type requirement T == C<X, Y> becomes a
      // rewrite rule
      //
      //   T.[concrete: C<X, Y>] => T
      SmallVector<Term, 1> substitutions;
      otherType = getConcreteSubstitutionSchema(otherType, proto,
                                                substitutions);

      constraintTerm = subjectTerm;
      constraintTerm.add(Symbol::forConcreteType(otherType, substitutions,
                                                 Context));
      break;
    }

    constraintTerm = Context.getMutableTermForType(otherType, proto);
    break;
  }
  }

  Rules.emplace_back(subjectTerm, constraintTerm);
}

void RequirementMachine::verify(const MutableTerm &term) const {
#ifndef NDEBUG
  // If the term is in the generic parameter domain, ensure we have a valid
  // generic parameter.
  if (term.begin()->getKind() == Symbol::Kind::GenericParam) {
    auto *genericParam = term.begin()->getGenericParam();
    auto genericParams = Sig.getGenericParams();
    auto found = std::find(genericParams.begin(),
                           genericParams.end(),
                           genericParam);
    if (found == genericParams.end()) {
      llvm::errs() << "Bad generic parameter in " << term << "\n";
      dump(llvm::errs());
      abort();
    }
  }

  MutableTerm erased;

  // First, "erase" resolved associated types from the term, and try
  // to simplify it again.
  for (auto symbol : term) {
    if (erased.empty()) {
      switch (symbol.getKind()) {
      case Symbol::Kind::Protocol:
      case Symbol::Kind::GenericParam:
        erased.add(symbol);
        continue;

      case Symbol::Kind::AssociatedType:
        erased.add(Symbol::forProtocol(symbol.getProtocols()[0], Context));
        break;

      case Symbol::Kind::Name:
      case Symbol::Kind::Layout:
      case Symbol::Kind::Superclass:
      case Symbol::Kind::ConcreteType:
        llvm::errs() << "Bad initial symbol in " << term << "\n";
        abort();
        break;
      }
    }

    switch (symbol.getKind()) {
    case Symbol::Kind::Name:
      assert(!erased.empty());
      erased.add(symbol);
      break;

    case Symbol::Kind::AssociatedType:
      erased.add(Symbol::forName(symbol.getName(), Context));
      break;

    case Symbol::Kind::Protocol:
    case Symbol::Kind::GenericParam:
    case Symbol::Kind::Layout:
    case Symbol::Kind::Superclass:
    case Symbol::Kind::ConcreteType:
      llvm::errs() << "Bad interior symbol " << symbol << " in " << term << "\n";
      abort();
      break;
    }
  }

  MutableTerm simplified = erased;
  System.simplify(simplified);

  // We should end up with the same term.
  if (simplified != term) {
    llvm::errs() << "Term verification failed\n";
    llvm::errs() << "Initial term:    " << term << "\n";
    llvm::errs() << "Erased term:     " << erased << "\n";
    llvm::errs() << "Simplified term: " << simplified << "\n";
    llvm::errs() << "\n";
    dump(llvm::errs());
    abort();
  }
#endif
}

void RequirementMachine::dump(llvm::raw_ostream &out) const {
  out << "Requirement machine for " << Sig << "\n";
  System.dump(out);
  Map.dump(out);

  out << "\nConformance access paths:\n";
  for (auto pair : ConformanceAccessPaths) {
    out << "- " << pair.first.first << " : ";
    out << pair.first.second->getName() << " => ";
    pair.second.print(out);
    out << "\n";
  }
}

RequirementMachine::RequirementMachine(RewriteContext &ctx)
    : Context(ctx), System(ctx), Map(ctx, System.getProtocols()) {
  auto &langOpts = ctx.getASTContext().LangOpts;
  Dump = langOpts.DumpRequirementMachine;
  RequirementMachineStepLimit = langOpts.RequirementMachineStepLimit;
  RequirementMachineDepthLimit = langOpts.RequirementMachineDepthLimit;
  Stats = ctx.getASTContext().Stats;
}

RequirementMachine::~RequirementMachine() {}

void RequirementMachine::addGenericSignature(CanGenericSignature sig) {
  Sig = sig;

  PrettyStackTraceGenericSignature debugStack("building rewrite system for", sig);

  auto &ctx = Context.getASTContext();
  auto *Stats = ctx.Stats;

  if (Stats)
    ++Stats->getFrontendCounters().NumRequirementMachines;

  FrontendStatsTracer tracer(Stats, "build-rewrite-system");

  if (Dump) {
    llvm::dbgs() << "Adding generic signature " << sig << " {\n";
  }


  // Collect the top-level requirements, and all transtively-referenced
  // protocol requirement signatures.
  RewriteSystemBuilder builder(Context, Dump);
  builder.addGenericSignature(sig);

  // Add the initial set of rewrite rules to the rewrite system, also
  // providing the protocol graph to use for the linear order on terms.
  System.initialize(std::move(builder.Rules),
                    std::move(builder.Protocols));

  computeCompletion();

  if (Dump) {
    llvm::dbgs() << "}\n";
  }
}

/// Attempt to obtain a confluent rewrite system using the completion
/// procedure.
void RequirementMachine::computeCompletion() {
  while (true) {
    // First, run the Knuth-Bendix algorithm to resolve overlapping rules.
    auto result = System.computeConfluentCompletion(
        RequirementMachineStepLimit,
        RequirementMachineDepthLimit);

    if (Stats) {
      Stats->getFrontendCounters()
          .NumRequirementMachineCompletionSteps += result.second;
    }

    // Check for failure.
    auto checkCompletionResult = [&]() {
      switch (result.first) {
      case RewriteSystem::CompletionResult::Success:
        break;

      case RewriteSystem::CompletionResult::MaxIterations:
        llvm::errs() << "Generic signature " << Sig
                     << " exceeds maximum completion step count\n";
        System.dump(llvm::errs());
        abort();

      case RewriteSystem::CompletionResult::MaxDepth:
        llvm::errs() << "Generic signature " << Sig
                     << " exceeds maximum completion depth\n";
        System.dump(llvm::errs());
        abort();
      }
    };

    checkCompletionResult();

    // Check invariants.
    System.verify();

    // Build the property map, which also performs concrete term
    // unification; if this added any new rules, run the completion
    // procedure again.
    result = System.buildPropertyMap(
        Map,
        RequirementMachineStepLimit,
        RequirementMachineDepthLimit);

    if (Stats) {
      Stats->getFrontendCounters()
        .NumRequirementMachineUnifiedConcreteTerms += result.second;
    }

    checkCompletionResult();

    // If buildPropertyMap() added new rules, we run another round of
    // Knuth-Bendix, and build the property map again.
    if (result.second == 0)
      break;
  }

  if (Dump) {
    dump(llvm::dbgs());
  }

  assert(!Complete);
  Complete = true;
}

bool RequirementMachine::isComplete() const {
  return Complete;
}
