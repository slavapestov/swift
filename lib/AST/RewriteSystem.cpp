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

#include "swift/AST/RewriteSystem.h"
#include "swift/Basic/Defer.h"
#include "llvm/ADT/FoldingSet.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <deque>
#include <vector>

using namespace swift;
using namespace rewriting;

/// Atoms are uniqued and immutable, stored as a single pointer;
/// the Storage type is the allocated backing storage.
struct Atom::Storage final
  : public llvm::FoldingSetNode,
    public llvm::TrailingObjects<Storage, const ProtocolDecl *, Term> {
  friend class Atom;

  unsigned Kind : 3;
  unsigned NumProtocols : 15;
  unsigned NumSubstitutions : 14;

  union {
    Identifier Name;
    CanType ConcreteType;
    LayoutConstraint Layout;
    const ProtocolDecl *Proto;
    GenericTypeParamType *GenericParam;
  };

  explicit Storage(Identifier name) {
    Kind = unsigned(Atom::Kind::Name);
    NumProtocols = 0;
    NumSubstitutions = 0;
    Name = name;
  }

  explicit Storage(LayoutConstraint layout) {
    Kind = unsigned(Atom::Kind::Layout);
    NumProtocols = 0;
    NumSubstitutions = 0;
    Layout = layout;
  }

  explicit Storage(const ProtocolDecl *proto) {
    Kind = unsigned(Atom::Kind::Protocol);
    NumProtocols = 0;
    NumSubstitutions = 0;
    Proto = proto;
  }

  explicit Storage(GenericTypeParamType *param) {
    Kind = unsigned(Atom::Kind::GenericParam);
    NumProtocols = 0;
    NumSubstitutions = 0;
    GenericParam = param;
  }

  Storage(ArrayRef<const ProtocolDecl *> protos, Identifier name) {
    assert(!protos.empty());

    Kind = unsigned(Atom::Kind::AssociatedType);
    NumProtocols = protos.size();
    assert(NumProtocols == protos.size() && "Overflow");
    NumSubstitutions = 0;
    Name = name;

    for (unsigned i : indices(protos))
      getProtocols()[i] = protos[i];
  }

  Storage(Atom::Kind kind, CanType type, ArrayRef<Term> substitutions) {
    assert(kind == Atom::Kind::Superclass ||
           kind == Atom::Kind::ConcreteType);
    assert(type->hasTypeParameter() != substitutions.empty());

    Kind = unsigned(kind);
    NumProtocols = 0;
    NumSubstitutions = substitutions.size();
    ConcreteType = type;

    for (unsigned i : indices(substitutions))
      getSubstitutions()[i] = substitutions[i];
  }

  size_t numTrailingObjects(OverloadToken<const ProtocolDecl *>) const {
    return NumProtocols;
  }

  size_t numTrailingObjects(OverloadToken<Term>) const {
    return NumSubstitutions;
  }

  MutableArrayRef<const ProtocolDecl *> getProtocols() {
    return {getTrailingObjects<const ProtocolDecl *>(), NumProtocols};
  }

  ArrayRef<const ProtocolDecl *> getProtocols() const {
    return {getTrailingObjects<const ProtocolDecl *>(), NumProtocols};
  }

  MutableArrayRef<Term> getSubstitutions() {
    return {getTrailingObjects<Term>(), NumSubstitutions};
  }

  ArrayRef<Term> getSubstitutions() const {
    return {getTrailingObjects<Term>(), NumSubstitutions};
  }

  void Profile(llvm::FoldingSetNodeID &id) const;
};

Atom::Kind Atom::getKind() const {
  return Kind(Ptr->Kind);
}

/// Get the identifier associated with an unbound name atom or an
/// associated type atom.
Identifier Atom::getName() const {
  assert(getKind() == Kind::Name ||
         getKind() == Kind::AssociatedType);
  return Ptr->Name;
}

/// Get the single protocol declaration associate with a protocol atom.
const ProtocolDecl *Atom::getProtocol() const {
  assert(getKind() == Kind::Protocol);
  return Ptr->Proto;
}

/// Get the list of protocols associated with an associated type atom.
ArrayRef<const ProtocolDecl *> Atom::getProtocols() const {
  assert(getKind() == Kind::AssociatedType);
  auto protos = Ptr->getProtocols();
  assert(!protos.empty());
  return protos;
}

/// Get the generic parameter associated with a generic parameter atom.
GenericTypeParamType *Atom::getGenericParam() const {
  assert(getKind() == Kind::GenericParam);
  return Ptr->GenericParam;
}

/// Get the layout constraint associated with a layout constraint atom.
LayoutConstraint Atom::getLayoutConstraint() const {
  assert(getKind() == Kind::Layout);
  return Ptr->Layout;
}

/// Get the superclass type associated with a superclass atom.
CanType Atom::getSuperclass() const {
  assert(getKind() == Kind::Superclass);
  return Ptr->ConcreteType;
}

/// Get the concrete type associated with a concrete type atom.
CanType Atom::getConcreteType() const {
  assert(getKind() == Kind::ConcreteType);
  return Ptr->ConcreteType;
}

ArrayRef<Term> Atom::getSubstitutions() const {
  assert(getKind() == Kind::Superclass ||
         getKind() == Kind::ConcreteType);
  return Ptr->getSubstitutions();
}

/// Creates a new name atom.
Atom Atom::forName(Identifier name,
                   RewriteContext &ctx) {
  llvm::FoldingSetNodeID id;
  id.AddInteger(unsigned(Kind::Name));
  id.AddPointer(name.get());

  void *insertPos = nullptr;
  if (auto *atom = ctx.Atoms.FindNodeOrInsertPos(id, insertPos))
    return atom;

  unsigned size = Storage::totalSizeToAlloc<const ProtocolDecl *, Term>(0, 0);
  void *mem = ctx.Allocator.Allocate(size, alignof(Storage));
  auto *atom = new (mem) Storage(name);

  ctx.Atoms.InsertNode(atom, insertPos);

  return atom;
}

/// Creates a new protocol atom.
Atom Atom::forProtocol(const ProtocolDecl *proto,
                       RewriteContext &ctx) {
  assert(proto != nullptr);

  llvm::FoldingSetNodeID id;
  id.AddInteger(unsigned(Kind::Protocol));
  id.AddPointer(proto);

  void *insertPos = nullptr;
  if (auto *atom = ctx.Atoms.FindNodeOrInsertPos(id, insertPos))
    return atom;

  unsigned size = Storage::totalSizeToAlloc<const ProtocolDecl *, Term>(0, 0);
  void *mem = ctx.Allocator.Allocate(size, alignof(Storage));
  auto *atom = new (mem) Storage(proto);

  ctx.Atoms.InsertNode(atom, insertPos);

  return atom;
}

/// Creates a new associated type atom for a single protocol.
Atom Atom::forAssociatedType(const ProtocolDecl *proto,
                             Identifier name,
                             RewriteContext &ctx) {
  SmallVector<const ProtocolDecl *, 1> protos;
  protos.push_back(proto);

  return forAssociatedType(protos, name, ctx);
}

/// Creates a merged associated type atom to represent a nested
/// type that conforms to multiple protocols, all of which have
/// an associated type with the same name.
Atom Atom::forAssociatedType(ArrayRef<const ProtocolDecl *> protos,
                             Identifier name,
                             RewriteContext &ctx) {
  llvm::FoldingSetNodeID id;
  id.AddInteger(unsigned(Kind::AssociatedType));
  id.AddInteger(protos.size());
  for (const auto *proto : protos)
    id.AddPointer(proto);
  id.AddPointer(name.get());

  void *insertPos = nullptr;
  if (auto *atom = ctx.Atoms.FindNodeOrInsertPos(id, insertPos))
    return atom;

  unsigned size = Storage::totalSizeToAlloc<const ProtocolDecl *, Term>(
      protos.size(), 0);
  void *mem = ctx.Allocator.Allocate(size, alignof(Storage));
  auto *atom = new (mem) Storage(protos, name);

  ctx.Atoms.InsertNode(atom, insertPos);

  return atom;
}

/// Creates a generic parameter atom, representing a generic
/// parameter in the top-level generic signature from which the
/// rewrite system is built.
Atom Atom::forGenericParam(GenericTypeParamType *param,
                           RewriteContext &ctx) {
  assert(param->isCanonical());

  llvm::FoldingSetNodeID id;
  id.AddInteger(unsigned(Kind::GenericParam));
  id.AddPointer(param);

  void *insertPos = nullptr;
  if (auto *atom = ctx.Atoms.FindNodeOrInsertPos(id, insertPos))
    return atom;

  unsigned size = Storage::totalSizeToAlloc<const ProtocolDecl *, Term>(0, 0);
  void *mem = ctx.Allocator.Allocate(size, alignof(Storage));
  auto *atom = new (mem) Storage(param);

  ctx.Atoms.InsertNode(atom, insertPos);

  return atom;
}

/// Creates a layout atom, representing a layout constraint.
Atom Atom::forLayout(LayoutConstraint layout,
                     RewriteContext &ctx) {
  llvm::FoldingSetNodeID id;
  id.AddInteger(unsigned(Kind::Layout));
  id.AddPointer(layout.getPointer());

  void *insertPos = nullptr;
  if (auto *atom = ctx.Atoms.FindNodeOrInsertPos(id, insertPos))
    return atom;

  unsigned size = Storage::totalSizeToAlloc<const ProtocolDecl *, Term>(0, 0);
  void *mem = ctx.Allocator.Allocate(size, alignof(Storage));
  auto *atom = new (mem) Storage(layout);

  ctx.Atoms.InsertNode(atom, insertPos);

  return atom;
}

/// Creates a superclass atom, representing a superclass constraint.
Atom Atom::forSuperclass(CanType type, ArrayRef<Term> substitutions,
                         RewriteContext &ctx) {
  llvm::FoldingSetNodeID id;
  id.AddInteger(unsigned(Kind::Superclass));
  id.AddPointer(type.getPointer());

  for (auto substitution : substitutions)
    id.AddPointer(substitution.getOpaquePointer());

  void *insertPos = nullptr;
  if (auto *atom = ctx.Atoms.FindNodeOrInsertPos(id, insertPos))
    return atom;

  unsigned size = Storage::totalSizeToAlloc<const ProtocolDecl *, Term>(
      0, substitutions.size());
  void *mem = ctx.Allocator.Allocate(size, alignof(Storage));
  auto *atom = new (mem) Storage(Kind::Superclass, type, substitutions);

  ctx.Atoms.InsertNode(atom, insertPos);

  return atom;
}

/// Creates a concrete type atom, representing a superclass constraint.
Atom Atom::forConcreteType(CanType type, ArrayRef<Term> substitutions,
                           RewriteContext &ctx) {
  llvm::FoldingSetNodeID id;
  id.AddInteger(unsigned(Kind::ConcreteType));
  id.AddPointer(type.getPointer());

  for (auto substitution : substitutions)
    id.AddPointer(substitution.getOpaquePointer());

  void *insertPos = nullptr;
  if (auto *atom = ctx.Atoms.FindNodeOrInsertPos(id, insertPos))
    return atom;

  unsigned size = Storage::totalSizeToAlloc<const ProtocolDecl *, Term>(
      0, substitutions.size());
  void *mem = ctx.Allocator.Allocate(size, alignof(Storage));
  auto *atom = new (mem) Storage(Kind::ConcreteType, type, substitutions);

  ctx.Atoms.InsertNode(atom, insertPos);

  return atom;
}

/// Linear order on atoms.
///
/// First, we order different kinds as follows:
///
///   AssociatedType < GenericParam < Name < Protocol < Layout
///
/// Then we break ties when both atoms have the same kind as follows:
///
/// * For associated type atoms, we first order the number of protocols,
///   with atoms containing more protocols coming first. This ensures
///   that the following holds:
///
///     [P1&P2:T] < [P1:T]
///     [P1&P2:T] < [P2:T]
///
///   If both atoms have the same number of protocols, we perform a
///   lexicographic comparison on the protocols pair-wise, using the
///   protocol order defined by \p graph (see
///   ProtocolGraph::compareProtocols()).
///
/// * For generic parameter atoms, we first order by depth, then index.
///
/// * For unbound name atoms, we compare identifiers lexicographically.
///
/// * For protocol atoms, we compare the protocols using the protocol
///   linear order on \p graph.
///
/// * For layout atoms, we use LayoutConstraint::compare().
int Atom::compare(Atom other, const ProtocolGraph &graph) const {
  // Exit early if the atoms are equal.
  if (Ptr == other.Ptr)
    return 0;

  auto kind = getKind();
  auto otherKind = other.getKind();

  if (kind != otherKind)
    return int(kind) < int(otherKind) ? -1 : 1;

  int result = 0;

  switch (kind) {
  case Kind::Name:
    result = getName().compare(other.getName());
    break;

  case Kind::Protocol:
    result = graph.compareProtocols(getProtocol(), other.getProtocol());
    break;

  case Kind::AssociatedType: {
    auto protos = getProtocols();
    auto otherProtos = other.getProtocols();

    // Atoms with more protocols are 'smaller' than those with fewer.
    if (protos.size() != otherProtos.size())
      return protos.size() > otherProtos.size() ? -1 : 1;

    for (unsigned i : indices(protos)) {
      int result = graph.compareProtocols(protos[i], otherProtos[i]);
      if (result)
        return result;
    }

    result = getName().compare(other.getName());
    break;
  }

  case Kind::GenericParam: {
    auto *param = getGenericParam();
    auto *otherParam = other.getGenericParam();

    if (param->getDepth() != otherParam->getDepth())
      return param->getDepth() < otherParam->getDepth() ? -1 : 1;

    if (param->getIndex() != otherParam->getIndex())
      return param->getIndex() < otherParam->getIndex() ? -1 : 1;

    break;
  }

  case Kind::Layout:
    result = getLayoutConstraint().compare(other.getLayoutConstraint());
    break;

  case Kind::Superclass:
  case Kind::ConcreteType: {
    assert(false && "Cannot compare concrete types yet");
    break;
  }
  }

  assert(result != 0 && "Two distinct atoms should not compare equal");
  return result;
}

/// Print the atom using our mnemonic representation.
void Atom::dump(llvm::raw_ostream &out) const {
  auto dumpSubstitutions = [&]() {
    if (getSubstitutions().size() > 0) {
      out << " with <";

      bool first = true;
      for (auto substitution : getSubstitutions()) {
        if (first) {
          first = false;
        } else {
          out << ", ";
        }
        substitution.dump(out);
      }

      out << ">";
    }
  };

  switch (getKind()) {
  case Kind::Name:
    out << getName();
    return;

  case Kind::Protocol:
    out << "[" << getProtocol()->getName() << "]";
    return;

  case Kind::AssociatedType: {
    out << "[";
    bool first = true;
    for (const auto *proto : getProtocols()) {
      if (first) {
        first = false;
      } else {
        out << "&";
      }
      out << proto->getName();
    }
    out << ":" << getName() << "]";
    return;
  }

  case Kind::GenericParam:
    out << Type(getGenericParam());
    return;

  case Kind::Layout:
    out << "[layout: ";
    getLayoutConstraint()->print(out);
    out << "]";
    return;

  case Kind::Superclass:
    out << "[superclass: " << getSuperclass();
    dumpSubstitutions();
    out << "]";
    return;

  case Kind::ConcreteType:
    out << "[concrete: " << getConcreteType();
    dumpSubstitutions();
    out << "]";
    return;
  }

  llvm_unreachable("Bad atom kind");
}

void Atom::Storage::Profile(llvm::FoldingSetNodeID &id) const {
  id.AddInteger(Kind);

  switch (Atom::Kind(Kind)) {
  case Atom::Kind::Name:
    id.AddPointer(Name.get());
    return;

  case Atom::Kind::Layout:
    id.AddPointer(Layout.getPointer());
    return;

  case Atom::Kind::Protocol:
    id.AddPointer(Proto);
    return;

  case Atom::Kind::GenericParam:
    id.AddPointer(GenericParam);
    return;

  case Atom::Kind::AssociatedType: {
    auto protos = getProtocols();
    id.AddInteger(protos.size());

    for (const auto *proto : protos)
      id.AddPointer(proto);

    id.AddPointer(Name.get());
    return;
  }

  case Atom::Kind::Superclass:
  case Atom::Kind::ConcreteType: {
    id.AddPointer(ConcreteType.getPointer());

    id.AddInteger(NumSubstitutions);
    for (auto term : getSubstitutions())
      id.AddPointer(term.getOpaquePointer());

    return;
  }
  }

  llvm_unreachable("Bad atom kind");
}

/// Terms are uniqued and immutable, stored as a single pointer;
/// the Storage type is the allocated backing storage.
struct Term::Storage final
  : public llvm::FoldingSetNode,
    public llvm::TrailingObjects<Storage, Atom> {
  friend class Atom;

  unsigned Size;

  explicit Storage(unsigned size) : Size(size) {}

  size_t numTrailingObjects(OverloadToken<Atom>) const {
    return Size;
  }

  MutableArrayRef<Atom> getElements() {
    return {getTrailingObjects<Atom>(), Size};
  }

  ArrayRef<Atom> getElements() const {
    return {getTrailingObjects<Atom>(), Size};
  }

  void Profile(llvm::FoldingSetNodeID &id) const;
};

size_t Term::size() const { return Ptr->Size; }

ArrayRef<Atom>::const_iterator Term::begin() const {
  return Ptr->getElements().begin();
}

ArrayRef<Atom>::const_iterator Term::end() const {
  return Ptr->getElements().end();
}

Atom Term::back() const {
  return Ptr->getElements().back();
}

Atom Term::operator[](size_t index) const {
  return Ptr->getElements()[index];
}

void Term::dump(llvm::raw_ostream &out) const {
  MutableTerm(*this).dump(out);
}

Term Term::get(const MutableTerm &mutableTerm, RewriteContext &ctx) {
  unsigned size = mutableTerm.size();
  assert(size > 0 && "Term must have at least one atom");

  llvm::FoldingSetNodeID id;
  id.AddInteger(size);
  for (auto atom : mutableTerm)
    id.AddPointer(atom.getOpaquePointer());

  void *insertPos = nullptr;
  if (auto *term = ctx.Terms.FindNodeOrInsertPos(id, insertPos))
    return term;

  void *mem = ctx.Allocator.Allocate(
      Storage::totalSizeToAlloc<Atom>(size),
      alignof(Storage));
  auto *term = new (mem) Storage(size);
  for (unsigned i = 0; i < size; ++i)
    term->getElements()[i] = mutableTerm[i];

  ctx.Terms.InsertNode(term, insertPos);

  return term;
}

void Term::Storage::Profile(llvm::FoldingSetNodeID &id) const {
  id.AddInteger(Size);

  for (auto atom : getElements())
    id.AddPointer(atom.getOpaquePointer());
}

/// Linear order on terms.
///
/// First we compare length, then perform a lexicographic comparison
/// on atoms if the two terms have the same length.
int MutableTerm::compare(const MutableTerm &other,
                         const ProtocolGraph &graph) const {
  if (size() != other.size())
    return size() < other.size() ? -1 : 1;

  for (unsigned i = 0, e = size(); i < e; ++i) {
    auto lhs = (*this)[i];
    auto rhs = other[i];

    int result = lhs.compare(rhs, graph);
    if (result != 0) {
      assert(lhs != rhs);
      return result;
    }

    assert(lhs == rhs);
  }

  return 0;
}

/// Find the start of \p other in this term, returning end() if
/// \p other does not occur as a subterm of this term.
decltype(MutableTerm::Atoms)::const_iterator
MutableTerm::findSubTerm(const MutableTerm &other) const {
  if (other.size() > size())
    return end();

  return std::search(begin(), end(), other.begin(), other.end());
}

/// Non-const variant of the above.
decltype(MutableTerm::Atoms)::iterator
MutableTerm::findSubTerm(const MutableTerm &other) {
  if (other.size() > size())
    return end();

  return std::search(begin(), end(), other.begin(), other.end());
}

/// Replace the first occurrence of \p lhs in this term with
/// \p rhs. Note that \p rhs must precede \p lhs in the linear
/// order on terms. Returns true if the term contained \p lhs;
/// otherwise returns false, in which case the term remains
/// unchanged.
bool MutableTerm::rewriteSubTerm(const MutableTerm &lhs,
                                 const MutableTerm &rhs) {
  // Find the start of lhs in this term.
  auto found = findSubTerm(lhs);

  // This term cannot be reduced using this rule.
  if (found == end())
    return false;

  auto oldSize = size();

  assert(rhs.size() <= lhs.size());

  // Overwrite the occurrence of the left hand side with the
  // right hand side.
  auto newIter = std::copy(rhs.begin(), rhs.end(), found);
  auto oldIter = found + lhs.size();

  // If the right hand side is shorter than the left hand side,
  // then newIter will point to a location before oldIter, eg
  // if this term is 'T.A.B.C', lhs is 'A.B' and rhs is 'X',
  // then we now have:
  //
  // T.X  .C
  //       ^--- oldIter
  //     ^--- newIter
  //
  // Shift everything over to close the gap (by one location,
  // in this case).
  if (newIter != oldIter) {
    auto newEnd = std::copy(oldIter, end(), newIter);

    // Now, we've moved the gap to the end of the term; close
    // it by shortening the term.
    Atoms.erase(newEnd, end());
  }

  assert(size() == oldSize - lhs.size() + rhs.size());
  return true;
}

/// Check if this term overlaps with \p other for the purposes
/// of the Knuth-Bendix completion algorithm.
///
/// An overlap occurs if one of the following two cases holds:
///
/// 1) If this == TUV and other == U.
/// 2) If this == TU and other == UV.
///
/// In both cases, we return the subterms T and V, together with
/// an 'overlap kind' identifying the first or second case.
///
/// Note that this relation is not commutative; we need to check
/// for overlap between both (X and Y) and (Y and X).
OverlapKind
MutableTerm::checkForOverlap(const MutableTerm &other,
                             MutableTerm &t,
                             MutableTerm &v) const {
  // If the other term is longer than this term, there's no way
  // we can overlap.
  if (other.size() > size())
    return OverlapKind::None;

  auto first1 = begin();
  auto last1 = end();
  auto first2 = other.begin();
  auto last2 = other.end();

  // Look for an overlap of the first kind, where the other term is
  // wholly contained in this term.
  //
  // A.B.C.D.E
  // X.Y.Z
  //   X.Y.Z
  while (last1 - first1 > last2 - first2) {
    if (std::equal(first2, last2, first1)) {
      // We have an overlap of the first kind, where
      // this == TUV and other == U.
      //
      // Get the subterms for T and V.
      t = MutableTerm(begin(), first1);
      v = MutableTerm(first1 + other.size(), end());
      assert(!v.empty());
      return OverlapKind::First;
    }

    ++first1;
  }

  assert(last1 - first1 == last2 - first2);

  // Look for an overlap of the second kind, where a prefix of the
  // other term is equal to some suffix of this term.
  //
  // A.B.C.D.E
  //     X.Y.Z
  //       X.Y
  //         X
  while (first1 != last1) {
    if (std::equal(first1, last1, first2)) {
      // We have an overlap of the second kind, where
      // this == TU and other == UV.
      //
      // Get the subterms for T and V.
      t = MutableTerm(begin(), first1);
      assert(!t.empty());
      v = MutableTerm(last2, other.end());
      return OverlapKind::Second;
    }

    --last2;
    ++first1;
  }

  // No overlap found.
  return OverlapKind::None;
}

void MutableTerm::dump(llvm::raw_ostream &out) const {
  bool first = true;

  for (auto atom : Atoms) {
    if (!first)
      out << ".";
    else
      first = false;

    atom.dump(out);
  }
}

Term RewriteContext::getTermForType(CanType paramType,
                                    const ProtocolDecl *proto) {
  return Term::get(getMutableTermForType(paramType, proto), *this);
}

/// Map an interface type to a term.
///
/// If \p proto is null, this is a term relative to a generic
/// parameter in a top-level signature. The term is rooted in a generic
/// parameter atom.
///
/// If \p proto is non-null, this is a term relative to a protocol's
/// 'Self' type. The term is rooted in a protocol atom.
///
/// The bound associated types in the interface type are ignored; the
/// resulting term consists entirely of a root atom followed by zero
/// or more name atoms.
MutableTerm RewriteContext::getMutableTermForType(CanType paramType,
                                                  const ProtocolDecl *proto) {
  assert(paramType->isTypeParameter());

  // Collect zero or more nested type names in reverse order.
  SmallVector<Atom, 3> atoms;
  while (auto memberType = dyn_cast<DependentMemberType>(paramType)) {
    atoms.push_back(Atom::forName(memberType->getName(), *this));
    paramType = memberType.getBase();
  }

  // Add the root atom at the end.
  if (proto) {
    assert(proto->getSelfInterfaceType()->isEqual(paramType));
    atoms.push_back(Atom::forProtocol(proto, *this));
  } else {
    atoms.push_back(Atom::forGenericParam(
        cast<GenericTypeParamType>(paramType), *this));
  }

  std::reverse(atoms.begin(), atoms.end());

  return MutableTerm(atoms);
}

void Rule::dump(llvm::raw_ostream &out) const {
  LHS.dump(out);
  out << " => ";
  RHS.dump(out);
  if (deleted)
    out << " [deleted]";
}

void RewriteSystem::initialize(
    std::vector<std::pair<MutableTerm, MutableTerm>> &&rules,
    ProtocolGraph &&graph) {
  Protos = graph;

  // FIXME: Probably this sort is not necessary
  std::sort(rules.begin(), rules.end(),
            [&](std::pair<MutableTerm, MutableTerm> lhs,
                std::pair<MutableTerm, MutableTerm> rhs) -> int {
              return lhs.first.compare(rhs.first, graph) < 0;
            });
  for (const auto &rule : rules)
    addRule(rule.first, rule.second);
}

bool RewriteSystem::addRule(MutableTerm lhs, MutableTerm rhs) {
  // Simplify the rule as much as possible with the rules we have so far.
  //
  // This avoids unnecessary work in the completion algorithm.
  simplify(lhs);
  simplify(rhs);

  // If the left hand side and right hand side are already equivalent, we're
  // done.
  int result = lhs.compare(rhs, Protos);
  if (result == 0)
    return false;

  // Orient the two terms so that the left hand side is greater than the
  // right hand side.
  if (result < 0)
    std::swap(lhs, rhs);

  assert(lhs.compare(rhs, Protos) > 0);

  if (DebugAdd) {
    llvm::dbgs() << "# Adding rule ";
    lhs.dump(llvm::dbgs());
    llvm::dbgs() << " => ";
    rhs.dump(llvm::dbgs());
    llvm::dbgs() << "\n";
  }

  unsigned i = Rules.size();
  Rules.emplace_back(lhs, rhs);

  // Check if we have a rule of the form
  //
  //   X.[P1:T] => X.[P2:T]
  //
  // If so, record this rule for later. We'll try to merge the associated
  // types in RewriteSystem::processMergedAssociatedTypes().
  if (lhs.size() == rhs.size() &&
      std::equal(lhs.begin(), lhs.end() - 1, rhs.begin()) &&
      lhs.back().getKind() == Atom::Kind::AssociatedType &&
      rhs.back().getKind() == Atom::Kind::AssociatedType &&
      lhs.back().getName() == rhs.back().getName()) {
    MergedAssociatedTypes.emplace_back(lhs, rhs);
  }

  // Since we added a new rule, we have to check for overlaps between the
  // new rule and all existing rules.
  for (unsigned j : indices(Rules)) {
    // A rule does not overlap with itself.
    if (i == j)
      continue;

    // We don't have to check for overlap with deleted rules.
    if (Rules[j].isDeleted())
      continue;

    // The overlap check is not commutative so we have to check both
    // directions.
    Worklist.emplace_back(i, j);
    Worklist.emplace_back(j, i);

    if (DebugCompletion) {
      llvm::dbgs() << "$ Queued up (" << i << ", " << j << ") and ";
      llvm::dbgs() << "(" << j << ", " << i << ")\n";
    }
  }

  // Tell the caller that we added a new rule.
  return true;
}

/// Reduce a term by applying all rewrite rules until fixed point.
bool RewriteSystem::simplify(MutableTerm &term) const {
  bool changed = false;

  if (DebugSimplify) {
    llvm::dbgs() << "= Term ";
    term.dump(llvm::dbgs());
    llvm::dbgs() << "\n";
  }

  while (true) {
    bool tryAgain = false;
    for (const auto &rule : Rules) {
      if (rule.isDeleted())
        continue;

      if (DebugSimplify) {
        llvm::dbgs() << "== Rule ";
        rule.dump(llvm::dbgs());
        llvm::dbgs() << "\n";
      }

      if (rule.apply(term)) {
        if (DebugSimplify) {
          llvm::dbgs() << "=== Result ";
          term.dump(llvm::dbgs());
          llvm::dbgs() << "\n";
        }

        changed = true;
        tryAgain = true;
      }
    }

    if (!tryAgain)
      break;
  }

  return changed;
}

/// If we have two atoms [P:T] and [Q:T], produce a merged atom:
///
/// - If P inherits from Q, this is just [P:T].
/// - If Q inherits from P, this is just [Q:T].
/// - If P and Q are unrelated, this is [P&Q:T].
Atom RewriteSystem::mergeAssociatedTypes(Atom lhs, Atom rhs) const {
  // Check preconditions that were established by RewriteSystem::addRule().
  assert(lhs.getKind() == Atom::Kind::AssociatedType);
  assert(rhs.getKind() == Atom::Kind::AssociatedType);
  assert(lhs.getName() == rhs.getName());
  assert(lhs.compare(rhs, Protos) > 0);

  auto protos = lhs.getProtocols();
  auto otherProtos = rhs.getProtocols();

  // This must follow from lhs > rhs.
  assert(protos.size() <= otherProtos.size());

  // Compute sorted and merged list of protocols, with duplicates.
  llvm::TinyPtrVector<const ProtocolDecl *> newProtos;
  std::merge(protos.begin(), protos.end(),
             otherProtos.begin(), otherProtos.end(),
             std::back_inserter(newProtos),
             [&](const ProtocolDecl *lhs,
                 const ProtocolDecl *rhs) -> int {
               return Protos.compareProtocols(lhs, rhs) < 0;
             });

  // Prune duplicates and protocols that are inherited by other
  // protocols.
  llvm::TinyPtrVector<const ProtocolDecl *> minimalProtos;
  for (const auto *newProto : newProtos) {
    auto inheritsFrom = [&](const ProtocolDecl *thisProto) {
      return (thisProto == newProto ||
              Protos.inheritsFrom(thisProto, newProto));
    };

    if (std::find_if(protos.begin(), protos.end(), inheritsFrom)
        == protos.end()) {
      minimalProtos.push_back(newProto);
    }
  }

  // The two input sets are minimal already, so the merged set
  // should have at least as many elements as each input set.
  assert(minimalProtos.size() >= protos.size());
  assert(minimalProtos.size() >= otherProtos.size());

  // The merged set cannot contain more elements than the union
  // of the two sets.
  assert(minimalProtos.size() <= protos.size() + otherProtos.size());

  return Atom::forAssociatedType(minimalProtos, lhs.getName(), Context);
}

/// Consider the following example:
///
///   protocol P1 { associatedtype T : P1 }
///   protocol P2 { associatedtype T : P2 }
///   struct G<T : P1 & P2> {}
///
/// We start with these rewrite rules:
///
///   [P1].T => [P1:T]
///   [P2].T => [P2:T]
///   [P1:T].[P1] => [P1:T]
///   [P2:T].[P1] => [P2:T]
///   <T>.[P1] => <T>
///   <T>.[P2] => <T>
///   <T>.T => <T>.[P1:T]
///   <T>.[P2:T] => <T>.[P1:T]
///
/// The completion procedure ends up adding an infinite series of rules of the
/// form
///
///   <T>.[P1:T].[P2]                 => <T>.[P1:T]
///   <T>.[P1:T].[P2:T]               => <T>.[P1:T].[P1:T]
///
///   <T>.[P1:T].[P1:T].[P2]          => <T>.[P1:T].[P1:T]
///   <T>.[P1:T].[P1:T].[P2:T]        => <T>.[P1:T].[P1:T].[P1:T]
///
///   <T>.[P1:T].[P1:T].[P1:T].[P2]   => <T>.[P1:T].[P1:T].[P1.T]
///   <T>.[P1:T].[P1:T].[P1:T].[P2:T] => <T>.[P1:T].[P1:T].[P1:T].[P1.T]
///
/// The difficulty here stems from the fact that an arbitrary sequence of
/// [P1:T] following a <T> is known to conform to P2, but P1:T itself
/// does not conform to P2.
///
/// We use a heuristic to compute a completion in this case by using
/// merged associated type terms.
///
/// The key is the following rewrite rule:
///
///   <T>.[P2:T] => <T>.[P1:T]
///
/// When we add this rule, we introduce a new merged atom [P1&P2:T] in
/// a pair of new rules:
///
///   <T>.[P1:T] => <T>.[P1&P2:T]
///   <T>.[P2:T] => <T>.[P1&P2:T]
///
/// We also look for any existing rules of the form [P1:T].[Q] => [P1:T]
/// or [P2:T].[Q] => [P2:T], and introduce a new rule:
///
///   [P1&P2:T].[Q] => [P1&P2:T]
///
/// In the above example, we have such a rule for Q == P1 and Q == P2, so
/// in total we end up adding the following four rules:
///
///   <T>.[P1:T] => <T>.[P1&P2:T]
///   <T>.[P2:T] => <T>.[P1&P2:T]
///   [P1&P2:T].[P1] => [P1&P2:T]
///   [P1&P2:T].[P2] => [P1&P2:T]
///
/// Intuitively, since the conformance requirements on the merged term
/// are not prefixed by the root <T>, they apply at any level; we've
/// "tied off" the recursion, and now the rewrite system has a confluent
/// completion.
void RewriteSystem::processMergedAssociatedTypes() {
  if (MergedAssociatedTypes.empty())
    return;

  unsigned i = 0;

  // Chase the end of the vector; calls to RewriteSystem::addRule()
  // can theoretically add new elements below.
  while (i < MergedAssociatedTypes.size()) {
    auto pair = MergedAssociatedTypes[i++];
    const auto &lhs = pair.first;
    const auto &rhs = pair.second;

    // If we have X.[P1:T] => Y.[P2:T], add a new pair of rules:
    // X.[P1:T] => X.[P1&P2:T]
    // X.[P2:T] => X.[P1&P2:T]
    if (DebugMerge) {
      llvm::dbgs() << "## Associated type merge candidate ";
      lhs.dump(llvm::dbgs());
      llvm::dbgs() << " => ";
      rhs.dump(llvm::dbgs());
      llvm::dbgs() << "\n";
    }

    auto mergedAtom = mergeAssociatedTypes(lhs.back(), rhs.back());
    if (DebugMerge) {
      llvm::dbgs() << "### Merged atom ";
      mergedAtom.dump(llvm::dbgs());
      llvm::dbgs() << "\n";
    }

    // Build the term X.[P1&P2:T].
    MutableTerm mergedTerm = lhs;
    mergedTerm.back() = mergedAtom;

    // Add the rule X.[P1:T] => X.[P1&P2:T].
    addRule(lhs, mergedTerm);

    // Add the rule X.[P1:T] => X.[P1&P2:T].
    addRule(rhs, mergedTerm);

    // Look for conformance requirements on [P1:T] and [P2:T].
    for (const auto &otherRule : Rules) {
      const auto &otherLHS = otherRule.getLHS();
      if (otherLHS.size() == 2 &&
          otherLHS[1].getKind() == Atom::Kind::Protocol) {
        if (otherLHS[0] == lhs.back() ||
            otherLHS[0] == rhs.back()) {
          // We have a rule of the form
          //
          //   [P1:T].[Q] => [P1:T]
          //
          // or
          //
          //   [P2:T].[Q] => [P2:T]
          if (DebugMerge) {
            llvm::dbgs() << "### Lifting conformance rule ";
            otherRule.dump(llvm::dbgs());
            llvm::dbgs() << "\n";
          }

          // We know that [P1:T] or [P2:T] conforms to Q, therefore the
          // merged type [P1&P2:T] must conform to Q as well. Add a new rule
          // of the form:
          //
          //   [P1&P2].[Q] => [P1&P2]
          //
          MutableTerm newLHS;
          newLHS.add(mergedAtom);
          newLHS.add(otherLHS[1]);

          MutableTerm newRHS;
          newRHS.add(mergedAtom);

          addRule(newLHS, newRHS);
        }
      }
    }
  }

  MergedAssociatedTypes.clear();
}

/// Compute a critical pair from two rewrite rules.
///
/// There are two cases:
///
/// 1) lhs == TUV -> X, rhs == U -> Y. The overlapped term is TUV;
///    applying lhs and rhs, respectively, yields the critical pair
///    (X, TYV).
///
/// 2) lhs == TU -> X, rhs == UV -> Y. The overlapped term is once
///    again TUV; applying lhs and rhs, respectively, yields the
///    critical pair (XV, TY).
static Optional<std::pair<MutableTerm, MutableTerm>>
computeCriticalPair(const Rule &lhs, const Rule &rhs) {
  MutableTerm t, v;

  switch (lhs.checkForOverlap(rhs, t, v)) {
  case OverlapKind::None:
    return None;

  case OverlapKind::First: {
    // lhs == TUV -> X, rhs == U -> Y.

    // Compute the term TYV.
    t.append(rhs.getRHS());
    t.append(v);
    return std::make_pair(lhs.getRHS(), t);
  }

  case OverlapKind::Second: {
    // lhs == TU -> X, rhs == UV -> Y.

    // Compute the term XV.
    MutableTerm xv;
    xv.append(lhs.getRHS());
    xv.append(v);

    // Compute the term TY.
    t.append(rhs.getRHS());
    return std::make_pair(xv, t);
  }
  }

  llvm_unreachable("Bad overlap kind");
}

/// Computes the confluent completion using the Knuth-Bendix algorithm
/// (https://en.wikipedia.org/wiki/Knuth–Bendix_completion_algorithm).
///
/// Returns CompletionResult::MaxIterations if we exceed \p maxIterations
/// iterations.
///
/// Returns CompletionResult::MaxDepth if we produce a rewrite rule whose
/// left hand side has a length exceeding \p maxDepth.
RewriteSystem::CompletionResult
RewriteSystem::computeConfluentCompletion(unsigned maxIterations,
                                          unsigned maxDepth) {
  unsigned steps = 0;

  SWIFT_DEFER {
    if (Context.Stats) {
      Context.Stats->getFrontendCounters()
        .NumRequirementMachineCompletionSteps += steps;
    }
  };

  // The worklist must be processed in first-in-first-out order, to ensure
  // that we resolve all overlaps among the initial set of rules before
  // moving on to overlaps between rules introduced by completion.
  while (!Worklist.empty()) {
    auto next = Worklist.front();
    Worklist.pop_front();

    const auto &lhs = Rules[next.first];
    const auto &rhs = Rules[next.second];

    if (DebugCompletion) {
      llvm::dbgs() << "$ Check for overlap: (#" << next.first << ") ";
      lhs.dump(llvm::dbgs());
      llvm::dbgs() << "\n";
      llvm::dbgs() << "                -vs- (#" << next.second << ") ";
      rhs.dump(llvm::dbgs());
      llvm::dbgs() << ":\n";
    }

    auto pair = computeCriticalPair(lhs, rhs);
    if (!pair) {
      if (DebugCompletion) {
        llvm::dbgs() << " no overlap\n\n";
      }
      continue;
    }

    MutableTerm first, second;

    // We have a critical pair (X, Y).
    std::tie(first, second) = *pair;

    if (DebugCompletion) {
      llvm::dbgs() << "$$ First term of critical pair is ";
      first.dump(llvm::dbgs());
      llvm::dbgs() << "\n";

      llvm::dbgs() << "$$ Second term of critical pair is ";
      second.dump(llvm::dbgs());
      llvm::dbgs() << "\n\n";
    }
    unsigned i = Rules.size();

    // Try to repair the confluence violation by adding a new rule
    // X == Y.
    if (!addRule(first, second))
      continue;

    // Check if we've already done too much work.
    if (++steps >= maxIterations)
      return CompletionResult::MaxIterations;

    const auto &newRule = Rules[i];
    if (newRule.getDepth() > maxDepth)
      return CompletionResult::MaxDepth;

    // Check if the new rule X == Y obsoletes any existing rules.
    for (unsigned j : indices(Rules)) {
      // A rule does not obsolete itself.
      if (i == j)
        continue;

      auto &rule = Rules[j];

      // Ignore rules that have already been obsoleted.
      if (rule.isDeleted())
        continue;

      // If this rule reduces some existing rule, delete the existing rule.
      if (rule.canReduceLeftHandSide(newRule)) {
        if (DebugCompletion) {
          llvm::dbgs() << "$ Deleting rule ";
          rule.dump(llvm::dbgs());
          llvm::dbgs() << "\n";
        }
        rule.markDeleted();
      }
    }

    // If this new rule merges any associated types, process the merge now
    // before we continue with the completion procedure. This is important
    // to perform incrementally since merging is required to repair confluence
    // violations.
    processMergedAssociatedTypes();
  }

  // This isn't necessary for correctness, it's just an optimization.
  for (auto &rule : Rules) {
    if (rule.isDeleted())
      continue;

    auto rhs = rule.getRHS();
    simplify(rhs);
    rule = Rule(rule.getLHS(), rhs);
  }

  return CompletionResult::Success;
}

void RewriteSystem::dump(llvm::raw_ostream &out) const {
  out << "Rewrite system: {\n";
  for (const auto &rule : Rules) {
    out << "- ";
    rule.dump(out);
    out << "\n";
  }
  out << "}\n";
}
