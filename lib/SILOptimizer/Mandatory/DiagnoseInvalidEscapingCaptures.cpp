//===--- DiagnoseInvalidEscapingCaptures.cpp ------------------------------===//
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
// This file implements a diagnostic pass to diagnose escaping closures that
// capture mutable storage locations or noescape function values.
//
//===----------------------------------------------------------------------===//

#include "swift/AST/ASTContext.h"
#include "swift/AST/DiagnosticsSIL.h"
#include "swift/AST/Types.h"
#include "swift/SIL/ApplySite.h"
#include "swift/SIL/SILArgument.h"
#include "swift/SIL/SILBasicBlock.h"
#include "swift/SIL/SILInstruction.h"
#include "swift/SILOptimizer/PassManager/Transforms.h"
#include "llvm/Support/Debug.h"

using namespace swift;

template <typename... T, typename... U>
static InFlightDiagnostic diagnose(ASTContext &Context, SourceLoc loc,
                                   Diag<T...> diag, U &&... args) {
  return Context.Diags.diagnose(loc, diag, std::forward<U>(args)...);
}

static bool isNoEscapeRValueType(CanType type) {
  if (auto silFuncTy = dyn_cast<SILFunctionType>(type))
    return silFuncTy->isNoEscape();

  if (auto tupleTy = dyn_cast<TupleType>(type)) {
    for (auto eltTy : tupleTy.getElementTypes())
      if (isNoEscapeRValueType(eltTy))
        return true;
  }

  if (auto objectTy = type.getOptionalObjectType())
    if (isNoEscapeRValueType(type))
      return true;

  return false;
}

static void checkForViolationsAtInstruction(SILInstruction *I) {
  if (auto *PAI = dyn_cast<PartialApplyInst>(I)) {
    // On-stack closures do not have these restrictions.
    if (PAI->isOnStack() == PartialApplyInst::OnStackKind::OnStack)
      return;
    
    // Visit the partial_apply's captures.
    ApplySite apply(PAI);
    for (auto &Op : apply.getArgumentOperands()) {
      if (apply.getArgumentConvention(Op) ==
            SILArgumentConvention::Indirect_InoutAliasable) {
        assert(false);
        continue;
      }

      if (isNoEscapeRValueType(Op.get()->getType().getASTType())) {
        assert(false);
        continue;
      }
    }
  }
}

static void checkEscapingCaptures(SILFunction &Fn) {
  if (Fn.empty())
    return;

  for (auto &BB : Fn) {
    for (auto &I : BB)
      checkForViolationsAtInstruction(&I);
  }
}

namespace {

class DiagnoseInvalidEscapingCaptures : public SILFunctionTransform {
public:
  DiagnoseInvalidEscapingCaptures() {}

private:
  void run() override {
    SILFunction *Fn = getFunction();

    // Don't rerun diagnostics on deserialized functions.
    if (Fn->wasDeserializedCanonical())
      return;

    checkEscapingCaptures(*Fn);
  }
};

} // end anonymous namespace

SILTransform *swift::createDiagnoseInvalidEscapingCaptures() {
  return new DiagnoseInvalidEscapingCaptures();
}
