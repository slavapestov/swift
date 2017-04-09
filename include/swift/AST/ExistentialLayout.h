//===--- ExistentialLayout.h - Existential type decomposition ---*- C++ -*-===//
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
// This file defines the ExistentialLayout struct.
//
//===----------------------------------------------------------------------===//

#ifndef SWIFT_EXISTENTIAL_LAYOUT_H
#define SWIFT_EXISTENTIAL_LAYOUT_H

#include "swift/AST/ASTContext.h"
#include "swift/AST/Type.h"
#include "llvm/ADT/SmallVector.h"

namespace swift {
  class ProtocolDecl;

struct ExistentialLayout {
  ExistentialLayout() {
    requiresClass = false;
    requiresClassImplied = false;
    containsNonObjCProtocol = false;
  }

  /// The superclass constraint, if any.
  Type superclass;

  /// Zero or more protocol constraints.
  SmallVector<ProtocolType *, 2> protocols;

  /// Whether the existential requires a class, either via an explicit
  /// '& AnyObject' member or because of a superclass or protocol constraint.
  bool requiresClass : 1;

  /// Whether the class constraint was implied by another constraint and therefore
  /// does not need to be stated explicitly.
  bool requiresClassImplied : 1;

  /// Whether any protocol members are non-@objc.
  bool containsNonObjCProtocol : 1;

  bool isAnyObject() const;

  bool isObjC() const {
    // FIXME: Does the superclass have to be @objc?
    return requiresClass && !containsNonObjCProtocol;
  }

  bool isExistentialWithError(ASTContext &ctx) const;
};

}

#endif  // SWIFT_EXISTENTIAL_LAYOUT_H
