//===--- CSPropagate.cpp - Constraint Propagation -------------------------===//
//
// This source file is part of the Swift.org open source project
//
// Copyright (c) 2014 - 2017 Apple Inc. and the Swift project authors
// Licensed under Apache License v2.0 with Runtime Library Exception
//
// See https://swift.org/LICENSE.txt for license information
// See https://swift.org/CONTRIBUTORS.txt for the list of Swift project authors
//
//===----------------------------------------------------------------------===//
//
// This file implements the constraint propagation algorithm in the solver.
//
//===----------------------------------------------------------------------===//
#include "ConstraintGraph.h"
#include "ConstraintSystem.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/Debug.h"

using namespace swift;
using namespace constraints;

// Find the disjunction of bind overload constraints related to this
// applicable function constraint, if it exists.
Constraint *
getBindOverloadDisjunction(ConstraintSystem &CS, Constraint *applicableFn) {
  assert(applicableFn->getKind() == ConstraintKind::ApplicableFunction
         && "Expected ApplicableFunction disjunction!");
  auto *tyvar = applicableFn->getSecondType()->getAs<TypeVariableType>();
  assert(tyvar && "Expected type variable!");

  Constraint *found = nullptr;
  for (auto *constraint : CS.getConstraintGraph()[tyvar].getConstraints()) {
    if (constraint->getKind() == ConstraintKind::Disjunction) {
      found = constraint;
      break;
    }
  }

  if (!found)
    return nullptr;

#if !defined(NDEBUG)
  for (auto *constraint : CS.getConstraintGraph()[tyvar].getConstraints()) {
    if (constraint == found)
      continue;

    assert(constraint->getKind() != ConstraintKind::Disjunction
           && "Type variable is involved in more than one disjunction!");
  }
#endif

  // Verify the disjunction consists of BindOverload constraints.
  assert(found->getNestedConstraints().front()->getKind() ==
         ConstraintKind::BindOverload);

  return found;
}

// Simplify the active constraints, collecting any new applicable
// function constraints we find along the way, and bailing if we fail
// to simplify a constraint successfully.
bool ConstraintSystem::simplifyForConstraintPropagation() {
  while (!ActiveConstraints.empty()) {
    auto *constraint = &ActiveConstraints.front();
    ActiveConstraints.pop_front();

    assert(constraint->isActive()
           && "Expected constraints to be active?");
    assert(!constraint->isDisabled() && "Unexpected disabled constraint!");

    bool failed = false;

    // Simplify this constraint.
    switch (simplifyConstraint(*constraint)) {
    case SolutionKind::Error:
      failed = true;
      LLVM_FALLTHROUGH;

    case SolutionKind::Solved:
      solverState->retireConstraint(constraint);
      CG.removeConstraint(constraint);
      break;

    case SolutionKind::Unsolved:
      InactiveConstraints.push_back(constraint);
      break;
    }

    constraint->setActive(false);

    if (failed)
      return true;
  }

  return false;
}

// Gather the applicable function constraints associated with a
// particular typevar, but do not include the one that is passed in
// since that is the constraint that we're starting from.
void ConstraintSystem::gatherNeighboringApplicableFunctionConstraints(
    Constraint *applicableFn,
    SmallVectorImpl<Constraint *> &otherApplicableFn) {

  auto *tyvar = applicableFn->getSecondType()->getAs<TypeVariableType>();
  assert(tyvar && "Expected type variable!");

  SmallVector<Constraint *, 8> neighbors;
  CG.gatherConstraints(tyvar, neighbors,
                       ConstraintGraph::GatheringKind::AllMentions);

  for (auto *constraint : neighbors) {
    if (constraint->getKind() != ConstraintKind::ApplicableFunction ||
        constraint == applicableFn)
      continue;

    otherApplicableFn.push_back(constraint);
  }
}

// Test a bind overload constraint to see if it is consistent with the
// rest of the constraint system.
bool ConstraintSystem::isBindOverloadConsistent(
    Constraint *bindConstraint, Constraint *applicableFn,
    llvm::SetVector<Constraint *> &workList, bool topLevel) {

  // Set up a scope that will be torn down when we're done testing
  // this constraint.
  ConstraintSystem::SolverScope scope(*this);

  assert(applicableFn->getKind() == ConstraintKind::ApplicableFunction
         && "Expected an ApplicableFunction constraint!");
  assert(bindConstraint->getKind() == ConstraintKind::BindOverload
         && "Expected a BindOverload constraint!");

  switch (simplifyConstraint(*bindConstraint)) {
  case ConstraintSystem::SolutionKind::Error:
    solverState->retireConstraint(bindConstraint);
    solverState->addGeneratedConstraint(bindConstraint);
    return false;
  case ConstraintSystem::SolutionKind::Solved: {
    solverState->retireConstraint(bindConstraint);
    solverState->addGeneratedConstraint(bindConstraint);

    if (simplifyForConstraintPropagation())
      return false;

    if (!topLevel)
      return true;

    // Test the applicable function constraints that neighbor the bind
    // overload constraint.
    SmallVector<Constraint *, 8> otherApplicableFn;
    gatherNeighboringApplicableFunctionConstraints(applicableFn,
                                                   otherApplicableFn);

    for (auto *constraint : otherApplicableFn)
      if (!isApplicableFunctionConsistent(constraint, workList,
                                          /* topLevel = */ false))
        return false;

    return true;
  }

  case ConstraintSystem::SolutionKind::Unsolved:
    InactiveConstraints.push_back(bindConstraint);
    CG.addConstraint(bindConstraint);
    solverState->addGeneratedConstraint(bindConstraint);
    return true;
  }
}

// Test an applicable function constraint by testing all of the bind
// overload constraints in the related disjunction. If we cannot find
// a related disjunction, or if all the constraints in that
// disjunction are inconsistent with the system, we'll return false.
bool ConstraintSystem::isApplicableFunctionConsistent(
    Constraint *applicableFn, llvm::SetVector<Constraint *> &workList,
    bool topLevel) {

  // Set up a scope that will be torn down when we're done testing
  // this constraint.
  ConstraintSystem::SolverScope scope(*this);

  auto *disjunction = getBindOverloadDisjunction(*this, applicableFn);
  if (!disjunction)
    return true;

  auto insertPt = InactiveConstraints.erase(disjunction);
  CG.removeConstraint(disjunction);

  bool foundConsistent = false;
  for (auto *bindConstraint : disjunction->getNestedConstraints()) {
    assert(bindConstraint->getKind() == ConstraintKind::BindOverload
           && "Expected a BindOverload constraint!");

    if (bindConstraint->isDisabled())
      continue;

    if (!isBindOverloadConsistent(bindConstraint, applicableFn, workList,
                                  topLevel)) {
      if (topLevel) {
        bindConstraint->setDisabled();

        // Queue up other constraints that may be affected by disabling
        // this one.
        SmallVector<Constraint *, 8> otherApplicableFn;
        gatherNeighboringApplicableFunctionConstraints(applicableFn,
                                                       otherApplicableFn);

        for (auto *constraint : otherApplicableFn)
          workList.insert(constraint);
      }
    } else {
      foundConsistent = true;

      // If we find any bind overload that works when we're testing
      // the "other" applicable function constraints, we know there is
      // some working solution and can stop.
      if (!topLevel)
        break;
    }
  }

  CG.addConstraint(disjunction);
  InactiveConstraints.insert(insertPt, disjunction);

  // If none of the nested constraints works, we know there is no
  // solution to this constraint system, otherwise, there may be.
  return foundConsistent;
}

// Do a form of constraint propagation consisting of examining
// applicable function constraints and their associated disjunction of
// bind overload constraints. Disable bind overload constraints in the
// disjunction if they are inconsistent with the rest of the
// constraint system. By doing this we can eliminate a lot of the work
// that we'll perform in the constraint solver.
bool ConstraintSystem::propagateConstraints() {
  assert(!failedConstraint && "Unexpected failed constraint!");
  assert(getActiveConstraints().empty() && "Expected no active constraints!");

  // Queue an initial set of ApplicableFunction constraints to process.
  llvm::SetVector<Constraint *> workList;
  for (auto &constraint : getConstraints()) {
    if (constraint.getKind() != ConstraintKind::ApplicableFunction)
      continue;

    workList.insert(&constraint);
  }

  // Process all constraints in the work list, adding new elements as
  // we process them.
  while (!workList.empty()) {
    auto *constraint = workList.pop_back_val();

    assert(constraint->getKind() == ConstraintKind::ApplicableFunction
           && "Expected ApplicableFunction constraint on work list!");

    if (!isApplicableFunctionConsistent(constraint, workList,
                                        /* topLevel = */ true))
      return true;
  }

  return false;
}
