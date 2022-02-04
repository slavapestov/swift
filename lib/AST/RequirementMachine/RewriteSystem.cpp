//===--- RewriteSystem.cpp - Generics with term rewriting -----------------===//
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

#include "swift/AST/Decl.h"
#include "swift/AST/Types.h"
#include "swift/AST/TypeWalker.h"
#include "llvm/ADT/FoldingSet.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <vector>
#include "RewriteContext.h"
#include "RewriteSystem.h"

using namespace swift;
using namespace rewriting;

/// If this is a rule of the form T.[p] => T where [p] is a property symbol,
/// returns the symbol. Otherwise, returns None.
///
/// Note that this is meant to be used with a simplified rewrite system,
/// where the right hand sides of rules are canonical, since this also means
/// that T is canonical.
Optional<Symbol> Rule::isPropertyRule() const {
  auto property = LHS.back();

  if (!property.isProperty())
    return None;

  if (LHS.size() - 1 != RHS.size())
    return None;

  if (!std::equal(RHS.begin(), RHS.end(), LHS.begin()))
    return None;

  return property;
}

/// If this is a rule of the form T.[P] => T where [P] is a protocol symbol,
/// return the protocol P, otherwise return nullptr.
const ProtocolDecl *Rule::isProtocolConformanceRule() const {
  if (auto property = isPropertyRule()) {
    if (property->getKind() == Symbol::Kind::Protocol)
      return property->getProtocol();
  }

  return nullptr;
}

/// If this is a rule of the form T.[concrete: C : P] => T where
/// [concrete: C : P] is a concrete conformance symbol, return the protocol P,
/// otherwise return nullptr.
const ProtocolDecl *Rule::isAnyConformanceRule() const {
  if (auto property = isPropertyRule()) {
    switch (property->getKind()) {
    case Symbol::Kind::ConcreteConformance:
    case Symbol::Kind::Protocol:
      return property->getProtocol();

    case Symbol::Kind::Layout:
    case Symbol::Kind::Superclass:
    case Symbol::Kind::ConcreteType:
      return nullptr;

    case Symbol::Kind::Name:
    case Symbol::Kind::AssociatedType:
    case Symbol::Kind::GenericParam:
      break;
    }

    llvm_unreachable("Bad symbol kind");
  }

  return nullptr;
}

/// If this is a rule of the form [P].[P] => [P] where [P] is a protocol
/// symbol, return true, otherwise return false.
bool Rule::isIdentityConformanceRule() const {
  return (LHS.size() == 2 &&
          RHS.size() == 1 &&
          LHS[0] == RHS[0] &&
          LHS[0] == LHS[1] &&
          LHS[0].getKind() == Symbol::Kind::Protocol);
}

/// If this is a rule of the form [P].[Q] => [P] where [P] and [Q] are
/// protocol symbols, return true, otherwise return false.
bool Rule::isProtocolRefinementRule() const {
  if (LHS.size() == 2 &&
      RHS.size() == 1 &&
      LHS[0] == RHS[0] &&
      LHS[0].getKind() == Symbol::Kind::Protocol &&
      (LHS[1].getKind() == Symbol::Kind::Protocol ||
       LHS[1].getKind() == Symbol::Kind::ConcreteConformance) &&
      LHS[0] != LHS[1]) {

    // A protocol refinement rule must be from a directly-stated
    // inheritance clause entry. It can only become redundant if it is
    // written in terms of other protocol refinement rules; otherwise, it
    // must appear in the protocol's requirement signature.
    //
    // See RewriteSystem::isValidRefinementPath() for an explanation.
    auto *proto = LHS[0].getProtocol();
    auto *otherProto = LHS[1].getProtocol();

    auto inherited = proto->getInheritedProtocols();
    return (std::find(inherited.begin(), inherited.end(), otherProto)
            != inherited.end());
  }

  return false;
}

/// Returns the length of the left hand side.
unsigned Rule::getDepth() const {
  auto result = LHS.size();

  if (LHS.back().hasSubstitutions()) {
    for (auto substitution : LHS.back().getSubstitutions()) {
      result = std::max(result, substitution.size());
    }
  }

  return result;
}

/// Returns the nesting depth of the concrete symbol at the end of the
/// left hand side, or 0 if there isn't one.
unsigned Rule::getNesting() const {
  if (LHS.back().hasSubstitutions()) {
    auto type = LHS.back().getConcreteType();

    struct Walker : TypeWalker {
      unsigned Nesting = 0;
      unsigned MaxNesting = 0;

      Action walkToTypePre(Type ty) override {
        ++Nesting;
        MaxNesting = std::max(Nesting, MaxNesting);

        return Action::Continue;
      }

      Action walkToTypePost(Type ty) override {
        --Nesting;

        return Action::Continue;
      }
    };

    Walker walker;
    type.walk(walker);

    return walker.MaxNesting;
  }

  return 0;
}

/// Linear order on rules; compares LHS followed by RHS.
int Rule::compare(const Rule &other, RewriteContext &ctx) const {
  int compare = LHS.compare(other.LHS, ctx);
  if (compare != 0)
    return compare;

  return RHS.compare(other.RHS, ctx);
}

void Rule::dump(llvm::raw_ostream &out) const {
  out << LHS << " => " << RHS;
  if (Permanent)
    out << " [permanent]";
  if (Explicit)
    out << " [explicit]";
  if (LHSSimplified)
    out << " [lhs↓]";
  if (RHSSimplified)
    out << " [rhs↓]";
  if (SubstitutionSimplified)
    out << " [subst↓]";
  if (Redundant)
    out << " [redundant]";
  if (Conflicting)
    out << " [conflicting]";
}

RewriteSystem::RewriteSystem(RewriteContext &ctx)
    : Context(ctx), Debug(ctx.getDebugOptions()) {
  Initialized = 0;
  Complete = 0;
  Minimized = 0;
  RecordLoops = 0;
}

RewriteSystem::~RewriteSystem() {
  Trie.updateHistograms(Context.RuleTrieHistogram,
                        Context.RuleTrieRootHistogram);
}

void RewriteSystem::initialize(
    bool recordLoops, ArrayRef<const ProtocolDecl *> protos,
    std::vector<std::pair<MutableTerm, MutableTerm>> &&permanentRules,
    std::vector<std::pair<MutableTerm, MutableTerm>> &&requirementRules) {
  assert(!Initialized);
  Initialized = 1;

  RecordLoops = recordLoops;
  Protos = protos;

  for (const auto &rule : permanentRules)
    addPermanentRule(rule.first, rule.second);

  for (const auto &rule : requirementRules)
    addExplicitRule(rule.first, rule.second);
}

/// Reduce a term by applying all rewrite rules until fixed point.
///
/// If \p path is non-null, records the series of rewrite steps taken.
bool RewriteSystem::simplify(MutableTerm &term, RewritePath *path) const {
  bool changed = false;

  MutableTerm original;
  RewritePath subpath;

  bool debug = false;
  if (Debug.contains(DebugFlags::Simplify)) {
    original = term;
    debug = true;
  }

  while (true) {
    bool tryAgain = false;

    auto from = term.begin();
    auto end = term.end();
    while (from < end) {
      auto ruleID = Trie.find(from, end);
      if (ruleID) {
        const auto &rule = getRule(*ruleID);

        auto to = from + rule.getLHS().size();
        assert(std::equal(from, to, rule.getLHS().begin()));

        unsigned startOffset = (unsigned)(from - term.begin());
        unsigned endOffset = term.size() - rule.getLHS().size() - startOffset;

        term.rewriteSubTerm(from, to, rule.getRHS());

        if (path || debug) {
          subpath.add(RewriteStep::forRewriteRule(startOffset, endOffset, *ruleID,
                                                  /*inverse=*/false));
        }

        changed = true;
        tryAgain = true;
        break;
      }

      ++from;
    }

    if (!tryAgain)
      break;
  }

  if (debug) {
    if (changed) {
      llvm::dbgs() << "= Simplified " << original << " to " << term << " via ";
      subpath.dump(llvm::dbgs(), original, *this);
      llvm::dbgs() << "\n";
    } else {
      llvm::dbgs() << "= Irreducible term: " << term << "\n";
    }
  }

  if (path != nullptr) {
    assert(changed != subpath.empty());
    path->append(subpath);
  }

  return changed;
}

/// Simplify terms appearing in the substitutions of the last symbol of \p term,
/// which must be a superclass or concrete type symbol.
bool RewriteSystem::simplifySubstitutions(Symbol &symbol,
                                          RewritePath *path) const {
  assert(symbol.hasSubstitutions());

  // Fast path if the type is fully concrete.
  auto substitutions = symbol.getSubstitutions();
  if (substitutions.empty())
    return false;

  // Save the original rewrite path length so that we can reset if if we don't
  // find anything to simplify.
  unsigned oldSize = (path ? path->size() : 0);

  if (path) {
    // The term is at the top of the primary stack. Push all substitutions onto
    // the primary stack.
    path->add(RewriteStep::forDecompose(substitutions.size(),
                                        /*inverse=*/false));

    // Move all substitutions but the first one to the secondary stack.
    for (unsigned i = 1; i < substitutions.size(); ++i)
      path->add(RewriteStep::forShift(/*inverse=*/false));
  }

  // Simplify and collect substitutions.
  SmallVector<Term, 2> newSubstitutions;
  newSubstitutions.reserve(substitutions.size());

  bool first = true;
  bool anyChanged = false;
  for (auto substitution : substitutions) {
    // Move the next substitution from the secondary stack to the primary stack.
    if (!first && path)
      path->add(RewriteStep::forShift(/*inverse=*/true));
    first = false;

    // The current substitution is at the top of the primary stack; simplify it.
    MutableTerm mutTerm(substitution);
    anyChanged |= simplify(mutTerm, path);

    // Record the new substitution.
    newSubstitutions.push_back(Term::get(mutTerm, Context));
  }

  // All simplified substitutions are now on the primary stack. Collect them to
  // produce the new term.
  if (path) {
    path->add(RewriteStep::forDecompose(substitutions.size(),
                                        /*inverse=*/true));
  }

  // If nothing changed, we don't have to rebuild the symbol.
  if (!anyChanged) {
    if (path) {
      // The rewrite path should consist of a Decompose, followed by a number
      // of Shifts, followed by a Compose.
  #ifndef NDEBUG
      for (auto iter = path->begin() + oldSize; iter < path->end(); ++iter) {
        assert(iter->Kind == RewriteStep::Shift ||
               iter->Kind == RewriteStep::Decompose);
      }
  #endif

      path->resize(oldSize);
    }
    return false;
  }

  // Build the new symbol with simplified substitutions.
  symbol = symbol.withConcreteSubstitutions(newSubstitutions, Context);
  return true;
}

/// Adds a rewrite rule, returning true if the new rule was non-trivial.
///
/// If both sides simplify to the same term, the rule is trivial and discarded,
/// and this method returns false.
///
/// If \p path is non-null, the new rule is derived from existing rules in the
/// rewrite system; the path records a series of rewrite steps which transform
/// \p lhs to \p rhs.
bool RewriteSystem::addRule(MutableTerm lhs, MutableTerm rhs,
                            const RewritePath *path) {
  // FIXME:
  // assert(!Complete || path != nullptr &&
  //        "Rules added by completion must have a path");

  assert(!lhs.empty());
  assert(!rhs.empty());

  if (Debug.contains(DebugFlags::Add)) {
    llvm::dbgs() << "# Adding rule " << lhs << " == " << rhs << "\n\n";
  }

  // Now simplify both sides as much as possible with the rules we have so far.
  //
  // This avoids unnecessary work in the completion algorithm.
  RewritePath lhsPath;
  RewritePath rhsPath;

  simplify(lhs, &lhsPath);
  simplify(rhs, &rhsPath);

  RewritePath loop;
  if (path) {
    // Produce a path from the simplified lhs to the simplified rhs.

    // (1) First, apply lhsPath in reverse to produce the original lhs.
    lhsPath.invert();
    loop.append(lhsPath);

    // (2) Now, apply the path from the original lhs to the original rhs
    // given to us by the completion procedure.
    loop.append(*path);

    // (3) Finally, apply rhsPath to produce the simplified rhs, which
    // is the same as the simplified lhs.
    loop.append(rhsPath);
  }

  // If the left hand side and right hand side are already equivalent, we're
  // done.
  int result = lhs.compare(rhs, Context);
  if (result == 0) {
    // If this rule is a consequence of existing rules, add a homotopy
    // generator.
    if (path) {
      // We already have a loop, since the simplified lhs is identical to the
      // simplified rhs.
      recordRewriteLoop(lhs, loop);

      if (Debug.contains(DebugFlags::Add)) {
        llvm::dbgs() << "## Recorded trivial loop at " << lhs << ": ";
        loop.dump(llvm::dbgs(), lhs, *this);
        llvm::dbgs() << "\n\n";
      }
    }

    return false;
  }

  // Orient the two terms so that the left hand side is greater than the
  // right hand side.
  if (result < 0) {
    std::swap(lhs, rhs);
    loop.invert();
  }

  assert(lhs.compare(rhs, Context) > 0);

  if (Debug.contains(DebugFlags::Add)) {
    llvm::dbgs() << "## Simplified and oriented rule " << lhs << " => " << rhs << "\n\n";
  }

  unsigned newRuleID = Rules.size();

  auto uniquedLHS = Term::get(lhs, Context);
  auto uniquedRHS = Term::get(rhs, Context);
  Rules.emplace_back(uniquedLHS, uniquedRHS);

  if (path) {
    // We have a rewrite path from the simplified lhs to the simplified rhs;
    // add a rewrite step applying the new rule in reverse to close the loop.
    loop.add(RewriteStep::forRewriteRule(/*startOffset=*/0, /*endOffset=*/0,
                                         newRuleID, /*inverse=*/true));
    recordRewriteLoop(lhs, loop);

    if (Debug.contains(DebugFlags::Add)) {
      llvm::dbgs() << "## Recorded non-trivial loop at " << lhs << ": ";
      loop.dump(llvm::dbgs(), lhs, *this);
      llvm::dbgs() << "\n\n";
    }
  }

  auto oldRuleID = Trie.insert(lhs.begin(), lhs.end(), newRuleID);
  if (oldRuleID) {
    llvm::errs() << "Duplicate rewrite rule!\n";
    const auto &oldRule = getRule(*oldRuleID);
    llvm::errs() << "Old rule #" << *oldRuleID << ": ";
    oldRule.dump(llvm::errs());
    llvm::errs() << "\nTrying to replay what happened when I simplified this term:\n";
    Debug |= DebugFlags::Simplify;
    MutableTerm term = lhs;
    simplify(lhs);

    dump(llvm::errs());
    abort();
  }

  checkMergedAssociatedType(uniquedLHS, uniquedRHS);

  // Tell the caller that we added a new rule.
  return true;
}

/// Add a new rule, marking it permanent.
bool RewriteSystem::addPermanentRule(MutableTerm lhs, MutableTerm rhs) {
  bool added = addRule(std::move(lhs), std::move(rhs));
  if (added)
    Rules.back().markPermanent();

  return added;
}

/// Add a new rule, marking it explicit.
bool RewriteSystem::addExplicitRule(MutableTerm lhs, MutableTerm rhs) {
  bool added = addRule(std::move(lhs), std::move(rhs));
  if (added)
    Rules.back().markExplicit();

  return added;
}

/// Delete any rules whose left hand sides can be reduced by other rules.
///
/// Must be run after the completion procedure, since the deletion of
/// rules is only valid to perform if the rewrite system is confluent.
void RewriteSystem::simplifyLeftHandSides() {
  assert(Complete);

  for (unsigned ruleID = 0, e = Rules.size(); ruleID < e; ++ruleID) {
    auto &rule = getRule(ruleID);
    if (rule.isLHSSimplified())
      continue;

    // First, see if the left hand side of this rule can be reduced using
    // some other rule.
    auto lhs = rule.getLHS();
    auto begin = lhs.begin();
    auto end = lhs.end();
    while (begin < end) {
      if (auto otherRuleID = Trie.find(begin++, end)) {
        // A rule does not obsolete itself.
        if (*otherRuleID == ruleID)
          continue;

        // Ignore other deleted rules.
        const auto &otherRule = getRule(*otherRuleID);
        if (otherRule.isLHSSimplified())
          continue;

        if (Debug.contains(DebugFlags::Completion)) {
          const auto &otherRule = getRule(*otherRuleID);
          llvm::dbgs() << "$ Deleting rule " << rule << " because "
                       << "its left hand side contains " << otherRule
                       << "\n";
        }

        rule.markLHSSimplified();
        break;
      }
    }
  }
}

/// Reduce the right hand sides of all remaining rules as much as
/// possible.
///
/// Must be run after the completion procedure, since the deletion of
/// rules is only valid to perform if the rewrite system is confluent.
void RewriteSystem::simplifyRightHandSides() {
  assert(Complete);

  for (unsigned ruleID = 0, e = Rules.size(); ruleID < e; ++ruleID) {
    auto &rule = getRule(ruleID);
    if (rule.isRHSSimplified())
      continue;

    // Now, try to reduce the right hand side.
    RewritePath rhsPath;
    MutableTerm rhs(rule.getRHS());
    if (!simplify(rhs, &rhsPath))
      continue;

    auto lhs = rule.getLHS();

    // We're adding a new rule, so the old rule won't apply anymore.
    rule.markRHSSimplified();

    unsigned newRuleID = Rules.size();

    // Add a new rule with the simplified right hand side.
    Rules.emplace_back(lhs, Term::get(rhs, Context));
    auto oldRuleID = Trie.insert(lhs.begin(), lhs.end(), newRuleID);
    assert(oldRuleID == ruleID);
    (void) oldRuleID;

    // Produce a loop at the original lhs.
    RewritePath loop;

    // (1) First, apply the original rule to produce the original rhs.
    loop.add(RewriteStep::forRewriteRule(/*startOffset=*/0, /*endOffset=*/0,
                                         ruleID, /*inverse=*/false));

    // (2) Next, apply rhsPath to produce the simplified rhs.
    loop.append(rhsPath);

    // (3) Finally, apply the new rule in reverse to produce the original lhs.
    loop.add(RewriteStep::forRewriteRule(/*startOffset=*/0, /*endOffset=*/0,
                                         newRuleID, /*inverse=*/true));

    if (Debug.contains(DebugFlags::Completion)) {
      llvm::dbgs() << "$ Right hand side simplification recorded a loop at ";
      llvm::dbgs() << lhs << ": ";
      loop.dump(llvm::dbgs(), MutableTerm(lhs), *this);
      llvm::dbgs() << "\n";
    }

    recordRewriteLoop(MutableTerm(lhs), loop);
  }
}

/// Simplify substitution terms in superclass, concrete type and concrete
/// conformance symbols.
void RewriteSystem::simplifyLeftHandSideSubstitutions() {
  for (unsigned ruleID = 0, e = Rules.size(); ruleID < e; ++ruleID) {
    auto &rule = getRule(ruleID);
    if (rule.isSubstitutionSimplified())
      continue;

    auto lhs = rule.getLHS();
    auto symbol = lhs.back();
    if (!symbol.hasSubstitutions())
      continue;

    RewritePath path;

    // (1) First, apply the original rule to produce the original lhs.
    path.add(RewriteStep::forRewriteRule(/*startOffset=*/0, /*endOffset=*/0,
                                         ruleID, /*inverse=*/true));

    // (2) Now, simplify the substitutions to get the new lhs.
    if (!simplifySubstitutions(symbol, &path))
      continue;

    // We're either going to add a new rule or record an identity, so
    // mark the old rule as simplified.
    rule.markSubstitutionSimplified();

    MutableTerm newLHS(lhs.begin(), lhs.end() - 1);
    newLHS.add(symbol);

    // Invert the path to get a path from the new lhs to the old rhs.
    path.invert();

    addRule(newLHS, MutableTerm(rule.getRHS()), &path);
  }
}

/// When minimizing a generic signature, we only care about loops where the
/// basepoint is a generic parameter symbol.
///
/// When minimizing protocol requirement signatures, we only care about loops
/// where the basepoint is a protocol symbol or associated type symbol whose
/// protocol is part of the connected component.
///
/// All other loops can be discarded since they do not encode redundancies
/// that are relevant to us.
bool RewriteSystem::isInMinimizationDomain(
    ArrayRef<const ProtocolDecl *> protos) const {
  assert(protos.size() <= 1);
  assert(Protos.empty() || !protos.empty());

  if (protos.empty() && Protos.empty())
    return true;

  if (std::find(Protos.begin(), Protos.end(), protos[0]) != Protos.end())
    return true;

  return false;
}

void RewriteSystem::recordRewriteLoop(MutableTerm basepoint,
                                      RewritePath path) {
  if (!RecordLoops)
    return;

  // Ignore the rewrite rule if it is not part of our minimization domain.
  if (!isInMinimizationDomain(basepoint.getRootProtocols()))
    return;

  Loops.emplace_back(basepoint, path);
}

void RewriteSystem::verifyRewriteRules(ValidityPolicy policy) const {
#ifndef NDEBUG

#define ASSERT_RULE(expr) \
  if (!(expr)) { \
    llvm::errs() << "&&& Malformed rewrite rule: " << rule << "\n"; \
    llvm::errs() << "&&& " << #expr << "\n\n"; \
    dump(llvm::errs()); \
    assert(expr); \
  }

  for (const auto &rule : Rules) {
    const auto &lhs = rule.getLHS();
    const auto &rhs = rule.getRHS();

    for (unsigned index : indices(lhs)) {
      auto symbol = lhs[index];

      if (index != lhs.size() - 1) {
        ASSERT_RULE(symbol.getKind() != Symbol::Kind::Layout);
        ASSERT_RULE(!symbol.hasSubstitutions());
      }

      if (index != 0) {
        ASSERT_RULE(symbol.getKind() != Symbol::Kind::GenericParam);
      }

      // Completion can produce rules like [P:T].[Q].[R] => [P:T].[Q]
      // which are immediately simplified away.
      if (!rule.isLHSSimplified() &&
          index != 0 && index != lhs.size() - 1) {
        ASSERT_RULE(symbol.getKind() != Symbol::Kind::Protocol);
      }
    }

    for (unsigned index : indices(rhs)) {
      auto symbol = rhs[index];

      // Permanent rules contain name symbols at the end, like
      // [P].T => [P:T].
      if (!rule.isRHSSimplified() &&
          (!rule.isPermanent() || index == rhs.size() - 1)) {
        // This is only true if the input requirements were valid.
        if (policy == DisallowInvalidRequirements) {
          ASSERT_RULE(symbol.getKind() != Symbol::Kind::Name);
        } else {
          // FIXME: Assert that we diagnosed an error
        }
      }

      ASSERT_RULE(symbol.getKind() != Symbol::Kind::Layout);
      ASSERT_RULE(!symbol.hasSubstitutions());

      if (index != 0) {
        ASSERT_RULE(symbol.getKind() != Symbol::Kind::GenericParam);
      }

      // Completion can produce rules like [P:T].[Q].[R] => [P:T].[Q]
      // which are immediately simplified away.
      if (!rule.isRHSSimplified() && index != 0) {
        ASSERT_RULE(symbol.getKind() != Symbol::Kind::Protocol);
      }
    }

    auto lhsDomain = lhs.getRootProtocols();
    auto rhsDomain = rhs.getRootProtocols();

    ASSERT_RULE(lhsDomain == rhsDomain);
  }

#undef ASSERT_RULE
#endif
}

void RewriteSystem::dump(llvm::raw_ostream &out) const {
  out << "Rewrite system: {\n";
  for (const auto &rule : Rules) {
    out << "- " << rule << "\n";
  }
  out << "}\n";
  out << "Relations: {\n";
  for (const auto &relation : Relations) {
    out << "- " << relation.first << " =>> " << relation.second << "\n";
  }
  out << "}\n";
  out << "Type differences: {\n";
  for (const auto &difference : Differences) {
    difference.dump(out);
    out << "\n";
  }
  out << "}\n";
  out << "Rewrite loops: {\n";
  for (const auto &loop : Loops) {
    if (loop.isDeleted())
      continue;

    out << "- ";
    loop.dump(out, *this);
    out << "\n";
  }
  out << "}\n";
}
