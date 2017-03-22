//===--- SILVTableVisitor.h - Class vtable visitor -------------*- C++ -*-===//
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
// This file defines the SILWitnessVisitor class, which is used to generate and
// perform lookups in witness method tables for protocols and protocol
// conformances.
//
//===----------------------------------------------------------------------===//

#ifndef SWIFT_SIL_SILVTABLEVISITOR_H
#define SWIFT_SIL_SILVTABLEVISITOR_H

#include "swift/AST/ASTVisitor.h"
#include "swift/AST/Decl.h"
#include "swift/AST/Types.h"
#include "swift/SIL/TypeLowering.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"

namespace swift {

/// A CRTP class for visiting virtually-dispatched methods of a class.
///
/// You must override addEntry(SILDeclRef) in your subclass.
template <class T> class SILVTableVisitor : public ASTVisitor<T> {
  T &asDerived() { return *static_cast<T*>(this); }

  void addEntry(SILDeclRef entry) {}

  // Default for members that don't require vtable entries.
  void visitDecl(Decl*) {}

  void visitFuncDecl(FuncDecl *fd) {
    // ObjC decls don't go in vtables.
    if (fd->hasClangNode())
      return;

    // Observers don't get separate vtable entries.
    if (fd->isObservingAccessor())
      return;

    // FIXME: If it doesn't override anything and its final or dynamic
    // skip it.

    asDerived().addEntry(SILDeclRef(fd));
  }

  void visitConstructorDecl(ConstructorDecl *cd) {
    // Stub constructors don't get an entry, unless they were synthesized to
    // override a non-required designated initializer in the superclass.
    if (cd->hasStubImplementation() && !cd->getOverriddenDecl())
      return;

    // Required constructors (or overrides thereof) have their allocating entry
    // point in the vtable.
    if (cd->isRequired())
      asDerived().addEntry(SILDeclRef(cd, SILDeclRef::Kind::Allocator));

    // All constructors have their initializing constructor in the
    // vtable, which can be used by a convenience initializer.
    asDerived().addEntry(SILDeclRef(cd, SILDeclRef::Kind::Initializer));
  }

protected:
  void visitMembers(ClassDecl *theClass) {
    if (!theClass->hasKnownSwiftImplementation())
      return;

    for (auto member : theClass->getMembers()) {
      if (auto *VD = dyn_cast<ValueDecl>(member))
        if (VD->isFinal() && VD->getOverriddenDecl() == nullptr)
          continue;

      visit(member);
    }
  }
}
