//===--- RewriteSystem.h - Generics with term rewriting ---------*- C++ -*-===//
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

#ifndef SWIFT_REWRITESYSTEM_H
#define SWIFT_REWRITESYSTEM_H

#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/PointerUnion.h"

#include "Debug.h"
#include "RewriteLoop.h"
#include "Symbol.h"
#include "Term.h"
#include "Trie.h"
#include "TypeDifference.h"

namespace llvm {
  class raw_ostream;
}

namespace swift {

namespace rewriting {

class PropertyMap;
class RewriteContext;
class RewriteSystem;

/// A rewrite rule that replaces occurrences of LHS with RHS.
///
/// LHS must be greater than RHS in the linear order over terms.
///
/// Out-of-line methods are documented in RewriteSystem.cpp.
class Rule final {
  Term LHS;
  Term RHS;

  /// A 'permanent' rule cannot be deleted by homotopy reduction. These
  /// do not correspond to generic requirements and are re-added when the
  /// rewrite system is built.
  unsigned Permanent : 1;

  /// An 'explicit' rule is a generic requirement written by the user.
  unsigned Explicit : 1;

  /// An 'LHS simplified' rule's left hand side was reduced via another rule.
  /// Set by simplifyLeftHandSides().
  unsigned LHSSimplified : 1;

  /// An 'RHS simplified' rule's right hand side can be reduced via another rule.
  /// Set by simplifyRightHandSides().
  unsigned RHSSimplified : 1;

  /// A 'substitution simplified' rule's left hand side contains substitutions
  /// which can be reduced via another rule.
  /// Set by simplifyLeftHandSideSubstitutions().
  unsigned SubstitutionSimplified : 1;

  /// A 'redundant' rule was eliminated by homotopy reduction. Redundant rules
  /// still participate in term rewriting, but they are not part of the minimal
  /// set of requirements in a generic signature.
  unsigned Redundant : 1;

  /// A 'conflicting' rule is a property rule which cannot be satisfied by any
  /// concrete type because it is mutually exclusive with some other rule.
  /// An example would be a pair of concrete type rules:
  ///
  ///    T.[concrete: Int] => T
  ///    T.[concrete: String] => T
  ///
  /// Conflicting rules are detected in property map construction, and are
  /// dropped from the minimal set of requirements.
  unsigned Conflicting : 1;

public:
  Rule(Term lhs, Term rhs)
      : LHS(lhs), RHS(rhs) {
    Permanent = false;
    Explicit = false;
    LHSSimplified = false;
    RHSSimplified = false;
    SubstitutionSimplified = false;
    Redundant = false;
    Conflicting = false;
  }

  const Term &getLHS() const { return LHS; }
  const Term &getRHS() const { return RHS; }

  Optional<Symbol> isPropertyRule() const;

  const ProtocolDecl *isProtocolConformanceRule() const;

  const ProtocolDecl *isAnyConformanceRule() const;

  bool isIdentityConformanceRule() const;

  bool isProtocolRefinementRule() const;

  /// See above for an explanation of these predicates.
  bool isPermanent() const {
    return Permanent;
  }

  bool isExplicit() const {
    return Explicit;
  }

  bool isLHSSimplified() const {
    return LHSSimplified;
  }

  bool isRHSSimplified() const {
    return RHSSimplified;
  }

  bool isSubstitutionSimplified() const {
    return SubstitutionSimplified;
  }

  bool isRedundant() const {
    return Redundant;
  }

  bool isConflicting() const {
    return Conflicting;
  }

  bool containsUnresolvedSymbols() const {
    return (LHS.containsUnresolvedSymbols() ||
            RHS.containsUnresolvedSymbols());
  }

  void markLHSSimplified() {
    assert(!LHSSimplified);
    LHSSimplified = true;
  }

  void markRHSSimplified() {
    assert(!RHSSimplified);
    RHSSimplified = true;
  }

  void markSubstitutionSimplified() {
    assert(!SubstitutionSimplified);
    SubstitutionSimplified = true;
  }

  void markPermanent() {
    assert(!Explicit && !Permanent &&
           "Permanent and explicit are mutually exclusive");
    Permanent = true;
  }

  void markExplicit() {
    assert(!Explicit && !Permanent &&
           "Permanent and explicit are mutually exclusive");
    Explicit = true;
  }

  void markRedundant() {
    assert(!Redundant);
    Redundant = true;
  }

  void markConflicting() {
    // It's okay to mark a rule as conflicting multiple times, but it must not
    // be a permanent rule.
    assert(!Permanent && "Permanent rule should not conflict with anything");
    Conflicting = true;
  }

  unsigned getDepth() const;

  unsigned getNesting() const;

  int compare(const Rule &other, RewriteContext &ctx) const;

  void dump(llvm::raw_ostream &out) const;

  friend llvm::raw_ostream &operator<<(llvm::raw_ostream &out,
                                       const Rule &rule) {
    rule.dump(out);
    return out;
  }
};

/// Result type for RequirementMachine::computeCompletion().
enum class CompletionResult {
  /// Completion was successful.
  Success,

  /// Maximum number of rules exceeded.
  MaxRuleCount,

  /// Maximum rule length exceeded.
  MaxRuleLength,

  /// Maximum concrete type nesting depth exceeded.
  MaxConcreteNesting
};

/// A term rewrite system for working with types in a generic signature.
///
/// Out-of-line methods are documented in RewriteSystem.cpp.
class RewriteSystem final {
  /// Rewrite context for memory allocation.
  RewriteContext &Context;

  /// If this is a rewrite system for a connected component of protocols,
  /// this array is non-empty. Otherwise, it is a rewrite system for a
  /// top-level generic signature and this array is empty.
  ArrayRef<const ProtocolDecl *> Protos;

  /// The rules added so far, including rules from our client, as well
  /// as rules introduced by the completion procedure.
  std::vector<Rule> Rules;

  /// A prefix trie of rule left hand sides to optimize lookup. The value
  /// type is an index into the Rules array defined above.
  Trie<unsigned, MatchKind::Shortest> Trie;

  /// The set of protocols known to this rewrite system. The boolean associated
  /// with each key is true if the protocol is part of the 'Protos' set above,
  /// otherwies it is false.
  ///
  /// See RuleBuilder::ProtocolMap for a more complete explanation. For the most
  /// part, this is only used while building the rewrite system, but conditional
  /// requirement inference forces us to be able to add new protocols to the
  /// rewrite system after the fact, so this little bit of RuleBuilder state
  /// outlives the initialization phase.
  llvm::DenseMap<const ProtocolDecl *, bool> ProtocolMap;

  DebugOptions Debug;

  /// Whether we've initialized the rewrite system with a call to initialize().
  unsigned Initialized : 1;

  /// Whether we've computed the confluent completion at least once.
  ///
  /// It might be computed multiple times if the property map's concrete type
  /// unification procedure adds new rewrite rules.
  unsigned Complete : 1;

  /// Whether we've minimized the rewrite system.
  unsigned Minimized : 1;

  /// If set, the completion procedure records rewrite loops describing the
  /// identities among rewrite rules discovered while resolving critical pairs.
  unsigned RecordLoops : 1;

public:
  explicit RewriteSystem(RewriteContext &ctx);
  ~RewriteSystem();

  RewriteSystem(const RewriteSystem &) = delete;
  RewriteSystem(RewriteSystem &&) = delete;
  RewriteSystem &operator=(const RewriteSystem &) = delete;
  RewriteSystem &operator=(RewriteSystem &&) = delete;

  /// Return the rewrite context used for allocating memory.
  RewriteContext &getRewriteContext() const { return Context; }

  llvm::DenseMap<const ProtocolDecl *, bool> &getProtocolMap() {
    return ProtocolMap;
  }

  DebugOptions getDebugOptions() const { return Debug; }

  void initialize(bool recordLoops, ArrayRef<const ProtocolDecl *> protos,
                  std::vector<std::pair<MutableTerm, MutableTerm>> &&permanentRules,
                  std::vector<std::pair<MutableTerm, MutableTerm>> &&requirementRules);

  ArrayRef<const ProtocolDecl *> getProtocols() const {
    return Protos;
  }

  bool isKnownProtocol(const ProtocolDecl *proto) const {
    return ProtocolMap.find(proto) != ProtocolMap.end();
  }

  unsigned getRuleID(const Rule &rule) const {
    assert((unsigned)(&rule - &*Rules.begin()) < Rules.size());
    return (unsigned)(&rule - &*Rules.begin());
  }

  ArrayRef<Rule> getRules() const {
    return Rules;
  }

  Rule &getRule(unsigned ruleID) {
    return Rules[ruleID];
  }

  const Rule &getRule(unsigned ruleID) const {
    return Rules[ruleID];
  }

  bool addRule(MutableTerm lhs, MutableTerm rhs,
               const RewritePath *path=nullptr);

  bool addPermanentRule(MutableTerm lhs, MutableTerm rhs);

  bool addExplicitRule(MutableTerm lhs, MutableTerm rhs);

  bool simplify(MutableTerm &term, RewritePath *path=nullptr) const;

  bool simplifySubstitutions(Symbol &symbol, RewritePath *path=nullptr) const;

  //////////////////////////////////////////////////////////////////////////////
  ///
  /// Completion
  ///
  //////////////////////////////////////////////////////////////////////////////

  /// Pairs of rules which have already been checked for overlap.
  llvm::DenseSet<std::pair<unsigned, unsigned>> CheckedOverlaps;

  CompletionResult computeConfluentCompletion(unsigned maxRuleCount,
                                              unsigned maxRuleLength);

  void simplifyLeftHandSides();

  void simplifyRightHandSides();

  void simplifyLeftHandSideSubstitutions();

  enum ValidityPolicy {
    AllowInvalidRequirements,
    DisallowInvalidRequirements
  };

  void verifyRewriteRules(ValidityPolicy policy) const;

private:
  struct CriticalPair {
    MutableTerm LHS;
    MutableTerm RHS;
    RewritePath Path;

    CriticalPair(MutableTerm lhs, MutableTerm rhs, RewritePath path)
      : LHS(lhs), RHS(rhs), Path(path) {}
  };

  bool
  computeCriticalPair(
      ArrayRef<Symbol>::const_iterator from,
      const Rule &lhs, const Rule &rhs,
      std::vector<CriticalPair> &pairs,
      std::vector<RewriteLoop> &loops) const;

  /// Constructed from a rule of the form X.[P2:T] => X.[P1:T] by
  /// checkMergedAssociatedType().
  struct MergedAssociatedType {
    /// The *right* hand side of the original rule, X.[P1:T].
    Term rhs;

    /// The associated type symbol appearing at the end of the *left*
    /// hand side of the original rule, [P2:T].
    Symbol lhsSymbol;

    /// The merged associated type symbol, [P1&P2:T].
    Symbol mergedSymbol;
  };

  /// A list of pending terms for the associated type merging completion
  /// heuristic. Entries are added by checkMergedAssociatedType(), and
  /// consumed in processMergedAssociatedTypes().
  std::vector<MergedAssociatedType> MergedAssociatedTypes;

  void processMergedAssociatedTypes();

  void checkMergedAssociatedType(Term lhs, Term rhs);

  //////////////////////////////////////////////////////////////////////////////
  ///
  /// Relations are "pseudo-rules" introduced by the property map
  ///
  //////////////////////////////////////////////////////////////////////////////

public:
  /// The left hand side is known to be smaller than the right hand side.
  using Relation = std::pair<Term, Term>;

private:
  /// The map's values are indices into the vector. The map is used for
  /// uniquing, then the index is returned and lookups are performed into
  /// the vector.
  llvm::DenseMap<Relation, unsigned> RelationMap;
  std::vector<Relation> Relations;

public:
  unsigned recordRelation(Term lhs, Term rhs);
  Relation getRelation(unsigned index) const;

  unsigned recordRelation(Symbol lhs, Symbol rhs);

  unsigned recordConcreteConformanceRelation(
      Symbol concreteSymbol, Symbol protocolSymbol,
      Symbol concreteConformanceSymbol);

  unsigned recordConcreteTypeWitnessRelation(
      Symbol concreteConformanceSymbol,
      Symbol associatedTypeSymbol,
      Symbol typeWitnessSymbol);

  unsigned recordSameTypeWitnessRelation(
      Symbol concreteConformanceSymbol,
      Symbol associatedTypeSymbol);

private:
  /// The map's values are indices into the vector. The map is used for
  /// uniquing, then the index is returned and lookups are performed into
  /// the vector.
  llvm::DenseMap<std::pair<Symbol, Symbol>, unsigned> DifferenceMap;
  std::vector<TypeDifference> Differences;

public:
  unsigned recordTypeDifference(Symbol lhs, Symbol rhs,
                                const TypeDifference &difference);

  bool
  computeTypeDifference(Symbol lhs, Symbol rhs,
                        Optional<unsigned> &lhsDifferenceID,
                        Optional<unsigned> &rhsDifferenceID);

  const TypeDifference &getTypeDifference(unsigned index) const;

private:
  //////////////////////////////////////////////////////////////////////////////
  ///
  /// Homotopy reduction
  ///
  //////////////////////////////////////////////////////////////////////////////

  /// Homotopy generators for this rewrite system. These are the
  /// rewrite loops which rewrite a term back to itself.
  ///
  /// In the category theory interpretation, a rewrite rule is a generating
  /// 2-cell, and a rewrite path is a 2-cell made from a composition of
  /// generating 2-cells.
  ///
  /// Homotopy generators, in turn, are 3-cells. The special case of a
  /// 3-cell discovered during completion can be viewed as two parallel
  /// 2-cells; this is actually represented as a single 2-cell forming a
  /// loop around a base point.
  ///
  /// This data is used by the homotopy reduction and minimal conformances
  /// algorithms.
  std::vector<RewriteLoop> Loops;

  void propagateExplicitBits();

  Optional<unsigned>
  findRuleToDelete(llvm::function_ref<bool(unsigned)> isRedundantRuleFn,
                   RewritePath &replacementPath);

  void deleteRule(unsigned ruleID, const RewritePath &replacementPath);

  void performHomotopyReduction(
      llvm::function_ref<bool(unsigned)> isRedundantRuleFn);

  void computeMinimalConformances(
      llvm::DenseSet<unsigned> &redundantConformances);

public:
  void recordRewriteLoop(MutableTerm basepoint,
                         RewritePath path);

  bool isInMinimizationDomain(ArrayRef<const ProtocolDecl *> protos) const;

  ArrayRef<RewriteLoop> getLoops() const {
    return Loops;
  }

  void minimizeRewriteSystem();

  bool hadError() const;

  llvm::DenseMap<const ProtocolDecl *, std::vector<unsigned>>
  getMinimizedProtocolRules() const;

  std::vector<unsigned> getMinimizedGenericSignatureRules() const;

private:
  void verifyRewriteLoops() const;

  void verifyRedundantConformances(
      const llvm::DenseSet<unsigned> &redundantConformances) const;

  void verifyMinimizedRules(
      const llvm::DenseSet<unsigned> &redundantConformances) const;

public:
  void dump(llvm::raw_ostream &out) const;
};

} // end namespace rewriting

} // end namespace swift

#endif
