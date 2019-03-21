//===--- TypeCheckAccess.cpp - Type Checking for Access Control -----------===//
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
// This file implements access control checking.
//
//===----------------------------------------------------------------------===//

#include "TypeChecker.h"
#include "TypeCheckAccess.h"
#include "swift/AST/AccessScopeChecker.h"
#include "swift/AST/ASTVisitor.h"
#include "swift/AST/ASTWalker.h"
#include "swift/AST/ExistentialLayout.h"
#include "swift/AST/Pattern.h"
#include "swift/AST/ParameterList.h"
#include "swift/AST/TypeCheckRequests.h"

using namespace swift;

#define DEBUG_TYPE "TypeCheckAccess"

namespace {

/// A uniquely-typed boolean to reduce the chances of accidentally inverting
/// a check.
///
/// \see checkTypeAccess
enum class DowngradeToWarning: bool {
  No,
  Yes
};

/// \see checkTypeAccess
using CheckTypeAccessCallback =
    void(AccessScope, const TypeRepr *, DowngradeToWarning);

class AccessControlCheckerBase {
protected:
  TypeChecker &TC;
  bool checkUsableFromInline;

  void checkTypeAccessImpl(
      Type type, TypeRepr *typeRepr, AccessScope contextAccessScope,
      const DeclContext *useDC, bool mayBeInferred,
      llvm::function_ref<CheckTypeAccessCallback> diagnose);

  void checkTypeAccess(
      Type type, TypeRepr *typeRepr, const ValueDecl *context,
      bool mayBeInferred,
      llvm::function_ref<CheckTypeAccessCallback> diagnose);

  void checkTypeAccess(
      const TypeLoc &TL, const ValueDecl *context, bool mayBeInferred,
      llvm::function_ref<CheckTypeAccessCallback> diagnose) {
    return checkTypeAccess(TL.getType(), TL.getTypeRepr(), context,
                           mayBeInferred, diagnose);
  }

  void checkRequirementAccess(
      WhereClauseOwner source,
      AccessScope accessScope,
      const DeclContext *useDC,
      llvm::function_ref<CheckTypeAccessCallback> diagnose);

  AccessControlCheckerBase(TypeChecker &TC, bool checkUsableFromInline)
    : TC(TC), checkUsableFromInline(checkUsableFromInline) {}

public:
  void checkGenericParamAccess(
    const GenericParamList *params,
    const Decl *owner,
    AccessScope accessScope,
    AccessLevel contextAccess);

  void checkGenericParamAccess(
    const GenericParamList *params,
    const ValueDecl *owner);
};

class TypeAccessScopeDiagnoser : private ASTWalker {
  AccessScope accessScope;
  const DeclContext *useDC;
  bool treatUsableFromInlineAsPublic;
  const ComponentIdentTypeRepr *offendingType = nullptr;

  bool walkToTypeReprPre(TypeRepr *TR) override {
    // Exit early if we've already found a problem type.
    if (offendingType)
      return false;

    auto CITR = dyn_cast<ComponentIdentTypeRepr>(TR);
    if (!CITR)
      return true;

    const ValueDecl *VD = CITR->getBoundDecl();
    if (!VD)
      return true;

    if (VD->getFormalAccessScope(useDC, treatUsableFromInlineAsPublic)
        != accessScope)
      return true;

    offendingType = CITR;
    return false;
  }

  bool walkToTypeReprPost(TypeRepr *T) override {
    // Exit early if we've already found a problem type.
    return offendingType != nullptr;
  }

  explicit TypeAccessScopeDiagnoser(AccessScope accessScope,
                                    const DeclContext *useDC,
                                    bool treatUsableFromInlineAsPublic)
    : accessScope(accessScope), useDC(useDC),
      treatUsableFromInlineAsPublic(treatUsableFromInlineAsPublic) {}

public:
  static const TypeRepr *findTypeWithScope(TypeRepr *TR,
                                           AccessScope accessScope,
                                           const DeclContext *useDC,
                                           bool treatUsableFromInlineAsPublic) {
    assert(!accessScope.isPublic() &&
           "why would we need to find a public access scope?");
    if (TR == nullptr)
      return nullptr;
    TypeAccessScopeDiagnoser diagnoser(accessScope, useDC,
                                       treatUsableFromInlineAsPublic);
    TR->walk(diagnoser);
    return diagnoser.offendingType;
  }
};

} // end anonymous namespace

/// Checks if the access scope of the type described by \p TL contains
/// \p contextAccessScope. If it isn't, calls \p diagnose with a TypeRepr
/// representing the offending part of \p TL.
///
/// The TypeRepr passed to \p diagnose may be null, in which case a particular
/// part of the type that caused the problem could not be found. The DeclContext
/// is never null.
///
/// If \p type might be partially inferred even when \p typeRepr is present
/// (such as for properties), pass \c true for \p mayBeInferred. (This does not
/// include implicitly providing generic parameters for the Self type, such as
/// using `Array` to mean `Array<Element>` in an extension of Array.) If
/// \p typeRepr is known to be absent, it's okay to pass \c false for
/// \p mayBeInferred.
void AccessControlCheckerBase::checkTypeAccessImpl(
    Type type, TypeRepr *typeRepr, AccessScope contextAccessScope,
    const DeclContext *useDC, bool mayBeInferred,
    llvm::function_ref<CheckTypeAccessCallback> diagnose) {
  if (TC.Context.isAccessControlDisabled())
    return;
  if (!type)
    return;
  // Don't spend time checking local declarations; this is always valid by the
  // time we get to this point.
  if (!contextAccessScope.isPublic() &&
      contextAccessScope.getDeclContext()->isLocalContext())
    return;

  Optional<AccessScope> typeAccessScope =
      TypeAccessScopeChecker::getAccessScope(type, useDC,
                                             checkUsableFromInline);
  auto downgradeToWarning = DowngradeToWarning::No;

  // Note: This means that the type itself is invalid for this particular
  // context, because it references declarations from two incompatible scopes.
  // In this case we should have diagnosed the bad reference already.
  if (!typeAccessScope.hasValue())
    return;

  AccessScope problematicAccessScope = *typeAccessScope;

  if (contextAccessScope.hasEqualDeclContextWith(*typeAccessScope) ||
      contextAccessScope.isChildOf(*typeAccessScope)) {

    // /Also/ check the TypeRepr, if present. This can be important when we're
    // unable to preserve typealias sugar that's present in the TypeRepr.
    if (!typeRepr)
      return;

    Optional<AccessScope> typeReprAccessScope =
        TypeReprAccessScopeChecker::getAccessScope(typeRepr, useDC,
                                                   checkUsableFromInline);
    assert(typeReprAccessScope && "valid Type but not valid TypeRepr?");
    if (contextAccessScope.hasEqualDeclContextWith(*typeReprAccessScope) ||
        contextAccessScope.isChildOf(*typeReprAccessScope)) {
      // Only if both the Type and the TypeRepr follow the access rules can
      // we exit; otherwise we have to emit a diagnostic.
      return;
    }
    problematicAccessScope = *typeReprAccessScope;

  } else {
    // The type violates the rules of access control (with or without taking the
    // TypeRepr into account).

    if (typeRepr && mayBeInferred &&
        !TC.getLangOpts().isSwiftVersionAtLeast(5) &&
        !useDC->getParentModule()->isResilient()) {
      // Swift 4.2 and earlier didn't check the Type when a TypeRepr was
      // present. However, this is a major hole when generic parameters are
      // inferred:
      //
      //   public let foo: Optional = VeryPrivateStruct()
      //
      // Downgrade the error to a warning in this case for source compatibility.
      Optional<AccessScope> typeReprAccessScope =
          TypeReprAccessScopeChecker::getAccessScope(typeRepr, useDC,
                                                     checkUsableFromInline);
      assert(typeReprAccessScope && "valid Type but not valid TypeRepr?");
      if (contextAccessScope.hasEqualDeclContextWith(*typeReprAccessScope) ||
          contextAccessScope.isChildOf(*typeReprAccessScope)) {
        downgradeToWarning = DowngradeToWarning::Yes;
      }
    }
  }

  const TypeRepr *complainRepr = TypeAccessScopeDiagnoser::findTypeWithScope(
      typeRepr, problematicAccessScope, useDC, checkUsableFromInline);
  diagnose(problematicAccessScope, complainRepr, downgradeToWarning);
}

/// Checks if the access scope of the type described by \p TL is valid for the
/// type to be the type of \p context. If it isn't, calls \p diagnose with a
/// TypeRepr representing the offending part of \p TL.
///
/// The TypeRepr passed to \p diagnose may be null, in which case a particular
/// part of the type that caused the problem could not be found.
///
/// If \p type might be partially inferred even when \p typeRepr is present
/// (such as for properties), pass \c true for \p mayBeInferred. (This does not
/// include implicitly providing generic parameters for the Self type, such as
/// using `Array` to mean `Array<Element>` in an extension of Array.) If
/// \p typeRepr is known to be absent, it's okay to pass \c false for
/// \p mayBeInferred.
void AccessControlCheckerBase::checkTypeAccess(
    Type type, TypeRepr *typeRepr, const ValueDecl *context, bool mayBeInferred,
    llvm::function_ref<CheckTypeAccessCallback> diagnose) {
  assert(!isa<ParamDecl>(context));
  const DeclContext *DC = context->getDeclContext();
  AccessScope contextAccessScope =
    context->getFormalAccessScope(
      nullptr, checkUsableFromInline);
  checkTypeAccessImpl(type, typeRepr, contextAccessScope, DC, mayBeInferred,
                      diagnose);
}

/// Highlights the given TypeRepr, and adds a note pointing to the type's
/// declaration if possible.
///
/// Just flushes \p diag as is if \p complainRepr is null.
static void highlightOffendingType(TypeChecker &TC, InFlightDiagnostic &diag,
                                   const TypeRepr *complainRepr) {
  if (!complainRepr) {
    diag.flush();
    return;
  }

  diag.highlight(complainRepr->getSourceRange());
  diag.flush();

  if (auto CITR = dyn_cast<ComponentIdentTypeRepr>(complainRepr)) {
    const ValueDecl *VD = CITR->getBoundDecl();
    TC.diagnose(VD, diag::kind_declared_here, DescriptiveDeclKind::Type);
  }
}

void AccessControlCheckerBase::checkRequirementAccess(
    WhereClauseOwner source,
    AccessScope accessScope,
    const DeclContext *useDC,
    llvm::function_ref<CheckTypeAccessCallback> diagnose) {
  RequirementRequest::visitRequirements(
      source, TypeResolutionStage::Interface,
      [&](const Requirement &req, RequirementRepr* reqRepr) {
        switch (req.getKind()) {
        case RequirementKind::Conformance:
        case RequirementKind::SameType:
        case RequirementKind::Superclass:
          checkTypeAccessImpl(req.getFirstType(),
                              RequirementRepr::getFirstTypeRepr(reqRepr),
                              accessScope, useDC, /*mayBeInferred*/false,
                              diagnose);
          checkTypeAccessImpl(req.getSecondType(),
                              RequirementRepr::getSecondTypeRepr(reqRepr),
                              accessScope, useDC, /*mayBeInferred*/false,
                              diagnose);
          break;

        case RequirementKind::Layout:
          checkTypeAccessImpl(req.getFirstType(),
                              RequirementRepr::getFirstTypeRepr(reqRepr),
                              accessScope, useDC, /*mayBeInferred*/false,
                              diagnose);
          break;
        }
        return false;
      });
}

void AccessControlCheckerBase::checkGenericParamAccess(
    const GenericParamList *params,
    const Decl *owner,
    AccessScope accessScope,
    AccessLevel contextAccess) {
  if (!params)
    return;

  // This must stay in sync with diag::generic_param_access.
  enum class ACEK {
    Parameter = 0,
    Requirement
  } accessControlErrorKind;
  auto minAccessScope = AccessScope::getPublic();
  const TypeRepr *complainRepr = nullptr;
  auto downgradeToWarning = DowngradeToWarning::Yes;

  auto callbackACEK = ACEK::Parameter;

  auto callback = [&](AccessScope typeAccessScope,
                      const TypeRepr *thisComplainRepr,
                      DowngradeToWarning thisDowngrade) {
    if (typeAccessScope.isChildOf(minAccessScope) ||
        (thisDowngrade == DowngradeToWarning::No &&
         downgradeToWarning == DowngradeToWarning::Yes) ||
        (!complainRepr &&
         typeAccessScope.hasEqualDeclContextWith(minAccessScope))) {
      minAccessScope = typeAccessScope;
      complainRepr = thisComplainRepr;
      accessControlErrorKind = callbackACEK;
      downgradeToWarning = thisDowngrade;
    }
  };

  auto *DC = owner->getDeclContext();

  for (auto param : *params) {
    if (param->getInherited().empty())
      continue;
    assert(param->getInherited().size() == 1);
    checkTypeAccessImpl(param->getInherited().front().getType(),
                        param->getInherited().front().getTypeRepr(),
                        accessScope, DC, /*mayBeInferred*/false, callback);
  }
  callbackACEK = ACEK::Requirement;

  checkRequirementAccess(WhereClauseOwner(
                           owner->getInnermostDeclContext(),
                           const_cast<GenericParamList *>(params)),
                         accessScope, DC, callback);

  if (minAccessScope.isPublic())
    return;

  if (checkUsableFromInline) {
    auto diagID = diag::generic_param_usable_from_inline;
    if (!TC.Context.isSwiftVersionAtLeast(5))
      diagID = diag::generic_param_usable_from_inline_warn;
    auto diag = TC.diagnose(owner,
                            diagID,
                            owner->getDescriptiveKind(),
                            accessControlErrorKind == ACEK::Requirement);
    highlightOffendingType(TC, diag, complainRepr);
    return;
  }

  auto minAccess = minAccessScope.accessLevelForDiagnostics();

  bool isExplicit =
    owner->getAttrs().hasAttribute<AccessControlAttr>() ||
    isa<ProtocolDecl>(owner->getDeclContext());
  auto diagID = diag::generic_param_access;
  if (downgradeToWarning == DowngradeToWarning::Yes)
    diagID = diag::generic_param_access_warn;
  auto diag = TC.diagnose(owner, diagID,
                          owner->getDescriptiveKind(), isExplicit,
                          contextAccess, minAccess,
                          isa<FileUnit>(owner->getDeclContext()),
                          accessControlErrorKind == ACEK::Requirement);
  highlightOffendingType(TC, diag, complainRepr);
}

void AccessControlCheckerBase::checkGenericParamAccess(
    const GenericParamList *params,
    const ValueDecl *owner) {
  checkGenericParamAccess(params, owner,
                          owner->getFormalAccessScope(nullptr,
                                                      checkUsableFromInline),
                          owner->getFormalAccess());
}

namespace {
class AccessControlChecker : public AccessControlCheckerBase,
                             public DeclVisitor<AccessControlChecker> {
public:
  explicit AccessControlChecker(TypeChecker &TC)
    : AccessControlCheckerBase(TC, /*checkUsableFromInline=*/false) {}

  void visit(Decl *D) {
    if (D->isInvalid() || D->isImplicit())
      return;

    DeclVisitor<AccessControlChecker>::visit(D);
  }

  // Force all kinds to be handled at a lower level.
  void visitDecl(Decl *D) = delete;
  void visitValueDecl(ValueDecl *D) = delete;

#define UNREACHABLE(KIND, REASON) \
  void visit##KIND##Decl(KIND##Decl *D) { \
    llvm_unreachable(REASON); \
  }
  UNREACHABLE(Import, "cannot appear in a type context")
  UNREACHABLE(Extension, "cannot appear in a type context")
  UNREACHABLE(TopLevelCode, "cannot appear in a type context")
  UNREACHABLE(Operator, "cannot appear in a type context")
  UNREACHABLE(PrecedenceGroup, "cannot appear in a type context")
  UNREACHABLE(Module, "cannot appear in a type context")

  UNREACHABLE(Param, "does not have access control")
  UNREACHABLE(GenericTypeParam, "does not have access control")
  UNREACHABLE(MissingMember, "does not have access control")
#undef UNREACHABLE

#define UNINTERESTING(KIND) \
  void visit##KIND##Decl(KIND##Decl *D) {}

  UNINTERESTING(IfConfig) // Does not have access control.
  UNINTERESTING(PoundDiagnostic) // Does not have access control.
  UNINTERESTING(EnumCase) // Handled at the EnumElement level.
  UNINTERESTING(Var) // Handled at the PatternBinding level.
  UNINTERESTING(Destructor) // Always correct.
  UNINTERESTING(Accessor) // Handled by the Var or Subscript.

  /// \see visitPatternBindingDecl
  void checkNamedPattern(const NamedPattern *NP, bool isTypeContext,
                         const llvm::DenseSet<const VarDecl *> &seenVars) {
    const VarDecl *theVar = NP->getDecl();
    if (seenVars.count(theVar) || theVar->isInvalid())
      return;

    checkTypeAccess(theVar->getInterfaceType(), nullptr, theVar,
                    /*mayBeInferred*/false,
                    [&](AccessScope typeAccessScope,
                        const TypeRepr *complainRepr,
                        DowngradeToWarning downgradeToWarning) {
      auto typeAccess = typeAccessScope.accessLevelForDiagnostics();
      bool isExplicit = theVar->getAttrs().hasAttribute<AccessControlAttr>() ||
                        isa<ProtocolDecl>(theVar->getDeclContext());
      auto theVarAccess =
          isExplicit ? theVar->getFormalAccess()
                     : typeAccessScope.requiredAccessForDiagnostics();
      auto diagID = diag::pattern_type_access_inferred;
      if (downgradeToWarning == DowngradeToWarning::Yes)
        diagID = diag::pattern_type_access_inferred_warn;
      auto diag = TC.diagnose(NP->getLoc(), diagID,
                              theVar->isLet(),
                              isTypeContext,
                              isExplicit,
                              theVarAccess,
                              isa<FileUnit>(theVar->getDeclContext()),
                              typeAccess,
                              theVar->getInterfaceType());
    });
  }

  void checkTypedPattern(const TypedPattern *TP, bool isTypeContext,
                         llvm::DenseSet<const VarDecl *> &seenVars) {
    const VarDecl *anyVar = nullptr;
    TP->forEachVariable([&](VarDecl *V) {
      seenVars.insert(V);
      anyVar = V;
    });
    if (!anyVar)
      return;

    checkTypeAccess(TP->getTypeLoc(), anyVar, /*mayBeInferred*/true,
                    [&](AccessScope typeAccessScope,
                        const TypeRepr *complainRepr,
                        DowngradeToWarning downgradeToWarning) {
      auto typeAccess = typeAccessScope.accessLevelForDiagnostics();
      bool isExplicit = anyVar->getAttrs().hasAttribute<AccessControlAttr>() ||
                        isa<ProtocolDecl>(anyVar->getDeclContext());
      auto diagID = diag::pattern_type_access;
      if (downgradeToWarning == DowngradeToWarning::Yes)
        diagID = diag::pattern_type_access_warn;
      auto anyVarAccess =
          isExplicit ? anyVar->getFormalAccess()
                     : typeAccessScope.requiredAccessForDiagnostics();
      auto diag = TC.diagnose(TP->getLoc(), diagID,
                              anyVar->isLet(),
                              isTypeContext,
                              isExplicit,
                              anyVarAccess,
                              isa<FileUnit>(anyVar->getDeclContext()),
                              typeAccess);
      highlightOffendingType(TC, diag, complainRepr);
    });
  }

  void visitPatternBindingDecl(PatternBindingDecl *PBD) {
    bool isTypeContext = PBD->getDeclContext()->isTypeContext();

    llvm::DenseSet<const VarDecl *> seenVars;
    for (auto entry : PBD->getPatternList()) {
      entry.getPattern()->forEachNode([&](const Pattern *P) {
        if (auto *NP = dyn_cast<NamedPattern>(P)) {
          // Only check individual variables if we didn't check an enclosing
          // TypedPattern.
          checkNamedPattern(NP, isTypeContext, seenVars);
          return;
        }

        auto *TP = dyn_cast<TypedPattern>(P);
        if (!TP)
          return;
        checkTypedPattern(TP, isTypeContext, seenVars);
      });
      seenVars.clear();
    }
  }

  void visitTypeAliasDecl(TypeAliasDecl *TAD) {
    checkTypeAccess(TAD->getUnderlyingTypeLoc(), TAD, /*mayBeInferred*/false,
                    [&](AccessScope typeAccessScope,
                        const TypeRepr *complainRepr,
                        DowngradeToWarning downgradeToWarning) {
      auto typeAccess = typeAccessScope.accessLevelForDiagnostics();
      bool isExplicit =
        TAD->getAttrs().hasAttribute<AccessControlAttr>() ||
        isa<ProtocolDecl>(TAD->getDeclContext());
      auto diagID = diag::type_alias_underlying_type_access;
      if (downgradeToWarning == DowngradeToWarning::Yes)
        diagID = diag::type_alias_underlying_type_access_warn;
      auto aliasAccess = isExplicit
        ? TAD->getFormalAccess()
        : typeAccessScope.requiredAccessForDiagnostics();
      auto diag = TC.diagnose(TAD, diagID,
                              isExplicit, aliasAccess,
                              typeAccess, isa<FileUnit>(TAD->getDeclContext()));
      highlightOffendingType(TC, diag, complainRepr);
    });
  }

  void visitAssociatedTypeDecl(AssociatedTypeDecl *assocType) {
    // This must stay in sync with diag::associated_type_access.
    enum {
      ACEK_DefaultDefinition = 0,
      ACEK_Requirement
    } accessControlErrorKind;
    auto minAccessScope = AccessScope::getPublic();
    const TypeRepr *complainRepr = nullptr;
    auto downgradeToWarning = DowngradeToWarning::No;

    std::for_each(assocType->getInherited().begin(),
                  assocType->getInherited().end(),
                  [&](TypeLoc requirement) {
      checkTypeAccess(requirement, assocType, /*mayBeInferred*/false,
                      [&](AccessScope typeAccessScope,
                          const TypeRepr *thisComplainRepr,
                          DowngradeToWarning downgradeDiag) {
        if (typeAccessScope.isChildOf(minAccessScope) ||
            (!complainRepr &&
             typeAccessScope.hasEqualDeclContextWith(minAccessScope))) {
          minAccessScope = typeAccessScope;
          complainRepr = thisComplainRepr;
          accessControlErrorKind = ACEK_Requirement;
          downgradeToWarning = downgradeDiag;
        }
      });
    });
    checkTypeAccess(assocType->getDefaultDefinitionLoc(), assocType,
                    /*mayBeInferred*/false,
                    [&](AccessScope typeAccessScope,
                        const TypeRepr *thisComplainRepr,
                        DowngradeToWarning downgradeDiag) {
      if (typeAccessScope.isChildOf(minAccessScope) ||
          (!complainRepr &&
           typeAccessScope.hasEqualDeclContextWith(minAccessScope))) {
        minAccessScope = typeAccessScope;
        complainRepr = thisComplainRepr;
        accessControlErrorKind = ACEK_DefaultDefinition;
        downgradeToWarning = downgradeDiag;
      }
    });

    checkRequirementAccess(assocType,
                           assocType->getFormalAccessScope(),
                           assocType->getDeclContext(),
                           [&](AccessScope typeAccessScope,
                               const TypeRepr *thisComplainRepr,
                               DowngradeToWarning downgradeDiag) {
      if (typeAccessScope.isChildOf(minAccessScope) ||
          (!complainRepr &&
           typeAccessScope.hasEqualDeclContextWith(minAccessScope))) {
        minAccessScope = typeAccessScope;
        complainRepr = thisComplainRepr;
        accessControlErrorKind = ACEK_Requirement;
        downgradeToWarning = downgradeDiag;

        // Swift versions before 5.0 did not check requirements on the
        // protocol's where clause, so emit a warning.
        if (!TC.Context.isSwiftVersionAtLeast(5))
          downgradeToWarning = DowngradeToWarning::Yes;
      }
    });

    if (!minAccessScope.isPublic()) {
      auto minAccess = minAccessScope.accessLevelForDiagnostics();
      auto diagID = diag::associated_type_access;
      if (downgradeToWarning == DowngradeToWarning::Yes)
        diagID = diag::associated_type_access_warn;
      auto diag = TC.diagnose(assocType, diagID,
                              assocType->getFormalAccess(),
                              minAccess, accessControlErrorKind);
      highlightOffendingType(TC, diag, complainRepr);
    }
  }

  void visitEnumDecl(EnumDecl *ED) {
    checkGenericParamAccess(ED->getGenericParams(), ED);

    if (ED->hasRawType()) {
      Type rawType = ED->getRawType();
      auto rawTypeLocIter = std::find_if(ED->getInherited().begin(),
                                         ED->getInherited().end(),
                                         [&](TypeLoc inherited) {
        if (!inherited.wasValidated())
          return false;
        return inherited.getType().getPointer() == rawType.getPointer();
      });
      if (rawTypeLocIter == ED->getInherited().end())
        return;
      checkTypeAccess(rawType, rawTypeLocIter->getTypeRepr(), ED,
                      /*mayBeInferred*/false,
                      [&](AccessScope typeAccessScope,
                          const TypeRepr *complainRepr,
                          DowngradeToWarning downgradeToWarning) {
        auto typeAccess = typeAccessScope.accessLevelForDiagnostics();
        bool isExplicit = ED->getAttrs().hasAttribute<AccessControlAttr>();
        auto diagID = diag::enum_raw_type_access;
        if (downgradeToWarning == DowngradeToWarning::Yes)
          diagID = diag::enum_raw_type_access_warn;
        auto enumDeclAccess = isExplicit
          ? ED->getFormalAccess()
          : typeAccessScope.requiredAccessForDiagnostics();
        auto diag = TC.diagnose(ED, diagID, isExplicit,
                                enumDeclAccess, typeAccess,
                                isa<FileUnit>(ED->getDeclContext()));
        highlightOffendingType(TC, diag, complainRepr);
      });
    }
  }

  void visitStructDecl(StructDecl *SD) {
    checkGenericParamAccess(SD->getGenericParams(), SD);
  }

  void visitClassDecl(ClassDecl *CD) {
    checkGenericParamAccess(CD->getGenericParams(), CD);

    if (const NominalTypeDecl *superclassDecl = CD->getSuperclassDecl()) {
      // Be slightly defensive here in the presence of badly-ordered
      // inheritance clauses.
      auto superclassLocIter = std::find_if(CD->getInherited().begin(),
                                            CD->getInherited().end(),
                                            [&](TypeLoc inherited) {
        if (!inherited.wasValidated())
          return false;
        Type ty = inherited.getType();
        if (ty->is<ProtocolCompositionType>())
          if (auto superclass = ty->getExistentialLayout().explicitSuperclass)
            ty = superclass;
        return ty->getAnyNominal() == superclassDecl;
      });
      // Sanity check: we couldn't find the superclass for whatever reason
      // (possibly because it's synthetic or something), so don't bother
      // checking it.
      if (superclassLocIter == CD->getInherited().end())
        return;

      auto outerDowngradeToWarning = DowngradeToWarning::No;
      if (superclassDecl->isGenericContext() &&
          !TC.getLangOpts().isSwiftVersionAtLeast(5)) {
        // Swift 4 failed to properly check this if the superclass was generic,
        // because the above loop was too strict.
        outerDowngradeToWarning = DowngradeToWarning::Yes;
      }

      checkTypeAccess(CD->getSuperclass(), superclassLocIter->getTypeRepr(), CD,
                      /*mayBeInferred*/false,
                      [&](AccessScope typeAccessScope,
                          const TypeRepr *complainRepr,
                          DowngradeToWarning downgradeToWarning) {
        auto typeAccess = typeAccessScope.accessLevelForDiagnostics();
        bool isExplicit = CD->getAttrs().hasAttribute<AccessControlAttr>();
        auto diagID = diag::class_super_access;
        if (downgradeToWarning == DowngradeToWarning::Yes ||
            outerDowngradeToWarning == DowngradeToWarning::Yes)
          diagID = diag::class_super_access_warn;
        auto classDeclAccess = isExplicit
          ? CD->getFormalAccess()
          : typeAccessScope.requiredAccessForDiagnostics();

        auto diag = TC.diagnose(CD, diagID, isExplicit, classDeclAccess,
                                typeAccess,
                                isa<FileUnit>(CD->getDeclContext()),
                                superclassLocIter->getTypeRepr() != complainRepr);
        highlightOffendingType(TC, diag, complainRepr);
      });
    }
  }

  void visitProtocolDecl(ProtocolDecl *proto) {
    // This must stay in sync with diag::protocol_access.
    enum {
      PCEK_Refine = 0,
      PCEK_Requirement
    } protocolControlErrorKind;

    auto minAccessScope = AccessScope::getPublic();
    const TypeRepr *complainRepr = nullptr;
    auto downgradeToWarning = DowngradeToWarning::No;

    // FIXME: Hack to ensure that we've computed the types involved here.
    ASTContext &ctx = proto->getASTContext();
    for (unsigned i : indices(proto->getInherited())) {
      (void)evaluateOrDefault(ctx.evaluator,
                              InheritedTypeRequest{
                                proto, i, TypeResolutionStage::Interface},
                              Type());
    }

    std::for_each(proto->getInherited().begin(),
                  proto->getInherited().end(),
                  [&](TypeLoc requirement) {
      checkTypeAccess(requirement, proto, /*mayBeInferred*/false,
                      [&](AccessScope typeAccessScope,
                          const TypeRepr *thisComplainRepr,
                          DowngradeToWarning downgradeDiag) {
        if (typeAccessScope.isChildOf(minAccessScope) ||
            (!complainRepr &&
             typeAccessScope.hasEqualDeclContextWith(minAccessScope))) {
          minAccessScope = typeAccessScope;
          complainRepr = thisComplainRepr;
          protocolControlErrorKind = PCEK_Refine;
          downgradeToWarning = downgradeDiag;
        }
      });
    });

    checkRequirementAccess(proto,
                           proto->getFormalAccessScope(),
                           proto->getDeclContext(),
                           [&](AccessScope typeAccessScope,
                               const TypeRepr *thisComplainRepr,
                               DowngradeToWarning downgradeDiag) {
      if (typeAccessScope.isChildOf(minAccessScope) ||
          (!complainRepr &&
           typeAccessScope.hasEqualDeclContextWith(minAccessScope))) {
        minAccessScope = typeAccessScope;
        complainRepr = thisComplainRepr;
        protocolControlErrorKind = PCEK_Requirement;
        downgradeToWarning = downgradeDiag;

        // Swift versions before 5.0 did not check requirements on the
        // protocol's where clause, so emit a warning.
        if (!TC.Context.isSwiftVersionAtLeast(5))
          downgradeToWarning = DowngradeToWarning::Yes;
      }
    });

    if (!minAccessScope.isPublic()) {
      auto minAccess = minAccessScope.accessLevelForDiagnostics();
      bool isExplicit = proto->getAttrs().hasAttribute<AccessControlAttr>();
      auto protoAccess = isExplicit
          ? proto->getFormalAccess()
          : minAccessScope.requiredAccessForDiagnostics();
      auto diagID = diag::protocol_access;
      if (downgradeToWarning == DowngradeToWarning::Yes)
        diagID = diag::protocol_access_warn;
      auto diag = TC.diagnose(proto, diagID,
                              isExplicit, protoAccess,
                              protocolControlErrorKind, minAccess,
                              isa<FileUnit>(proto->getDeclContext()));
      highlightOffendingType(TC, diag, complainRepr);
    }
  }

  void visitSubscriptDecl(SubscriptDecl *SD) {
    auto minAccessScope = AccessScope::getPublic();
    const TypeRepr *complainRepr = nullptr;
    auto downgradeToWarning = DowngradeToWarning::No;
    bool problemIsElement = false;

    for (auto &P : *SD->getIndices()) {
      checkTypeAccess(P->getTypeLoc(), SD, /*mayBeInferred*/false,
                      [&](AccessScope typeAccessScope,
                          const TypeRepr *thisComplainRepr,
                          DowngradeToWarning downgradeDiag) {
        if (typeAccessScope.isChildOf(minAccessScope) ||
            (!complainRepr &&
             typeAccessScope.hasEqualDeclContextWith(minAccessScope))) {
          minAccessScope = typeAccessScope;
          complainRepr = thisComplainRepr;
          downgradeToWarning = downgradeDiag;
        }
      });
    }

    checkTypeAccess(SD->getElementTypeLoc(), SD, /*mayBeInferred*/false,
                    [&](AccessScope typeAccessScope,
                        const TypeRepr *thisComplainRepr,
                        DowngradeToWarning downgradeDiag) {
      if (typeAccessScope.isChildOf(minAccessScope) ||
          (!complainRepr &&
           typeAccessScope.hasEqualDeclContextWith(minAccessScope))) {
        minAccessScope = typeAccessScope;
        complainRepr = thisComplainRepr;
        downgradeToWarning = downgradeDiag;
        problemIsElement = true;
      }
    });

    if (!minAccessScope.isPublic()) {
      auto minAccess = minAccessScope.accessLevelForDiagnostics();
      bool isExplicit =
        SD->getAttrs().hasAttribute<AccessControlAttr>() ||
        isa<ProtocolDecl>(SD->getDeclContext());
      auto diagID = diag::subscript_type_access;
      if (downgradeToWarning == DowngradeToWarning::Yes)
        diagID = diag::subscript_type_access_warn;
      auto subscriptDeclAccess = isExplicit
        ? SD->getFormalAccess()
        : minAccessScope.requiredAccessForDiagnostics();
      auto diag = TC.diagnose(SD, diagID,
                              isExplicit,
                              subscriptDeclAccess,
                              minAccess,
                              problemIsElement);
      highlightOffendingType(TC, diag, complainRepr);
    }
  }

  void visitAbstractFunctionDecl(AbstractFunctionDecl *fn) {
    bool isTypeContext = fn->getDeclContext()->isTypeContext();

    checkGenericParamAccess(fn->getGenericParams(), fn);

    // This must stay in sync with diag::function_type_access.
    enum {
      FK_Function = 0,
      FK_Method,
      FK_Initializer
    };

    auto minAccessScope = AccessScope::getPublic();
    const TypeRepr *complainRepr = nullptr;
    auto downgradeToWarning = DowngradeToWarning::No;

    for (auto *P : *fn->getParameters()) {
      checkTypeAccess(P->getTypeLoc(), fn, /*mayBeInferred*/false,
                      [&](AccessScope typeAccessScope,
                          const TypeRepr *thisComplainRepr,
                          DowngradeToWarning downgradeDiag) {
        if (typeAccessScope.isChildOf(minAccessScope) ||
            (!complainRepr &&
             typeAccessScope.hasEqualDeclContextWith(minAccessScope))) {
          minAccessScope = typeAccessScope;
          complainRepr = thisComplainRepr;
          downgradeToWarning = downgradeDiag;
        }
      });
    }

    bool problemIsResult = false;
    if (auto FD = dyn_cast<FuncDecl>(fn)) {
      checkTypeAccess(FD->getBodyResultTypeLoc(), FD, /*mayBeInferred*/false,
                      [&](AccessScope typeAccessScope,
                          const TypeRepr *thisComplainRepr,
                          DowngradeToWarning downgradeDiag) {
        if (typeAccessScope.isChildOf(minAccessScope) ||
            (!complainRepr &&
             typeAccessScope.hasEqualDeclContextWith(minAccessScope))) {
          minAccessScope = typeAccessScope;
          complainRepr = thisComplainRepr;
          downgradeToWarning = downgradeDiag;
          problemIsResult = true;
        }
      });
    }

    if (!minAccessScope.isPublic()) {
      auto minAccess = minAccessScope.accessLevelForDiagnostics();
      auto functionKind = isa<ConstructorDecl>(fn)
        ? FK_Initializer
        : isTypeContext ? FK_Method : FK_Function;
      bool isExplicit =
        fn->getAttrs().hasAttribute<AccessControlAttr>() ||
        isa<ProtocolDecl>(fn->getDeclContext());
      auto diagID = diag::function_type_access;
      if (downgradeToWarning == DowngradeToWarning::Yes)
        diagID = diag::function_type_access_warn;
      auto fnAccess = isExplicit
        ? fn->getFormalAccess()
        : minAccessScope.requiredAccessForDiagnostics();
      auto diag = TC.diagnose(fn, diagID,
                              isExplicit,
                              fnAccess,
                              isa<FileUnit>(fn->getDeclContext()),
                              minAccess,
                              functionKind,
                              problemIsResult);
      highlightOffendingType(TC, diag, complainRepr);
    }
  }

  void visitEnumElementDecl(EnumElementDecl *EED) {
    if (!EED->hasAssociatedValues())
      return;
    for (auto &P : *EED->getParameterList()) {
      checkTypeAccess(P->getTypeLoc(), EED, /*mayBeInferred*/false,
                      [&](AccessScope typeAccessScope,
                          const TypeRepr *complainRepr,
                          DowngradeToWarning downgradeToWarning) {
        auto typeAccess = typeAccessScope.accessLevelForDiagnostics();
        auto diagID = diag::enum_case_access;
        if (downgradeToWarning == DowngradeToWarning::Yes)
          diagID = diag::enum_case_access_warn;
        auto diag = TC.diagnose(EED, diagID,
                                EED->getFormalAccess(), typeAccess);
        highlightOffendingType(TC, diag, complainRepr);
      });
    }
  }
};

class UsableFromInlineChecker : public AccessControlCheckerBase,
                                public DeclVisitor<UsableFromInlineChecker> {
public:
  explicit UsableFromInlineChecker(TypeChecker &TC)
    : AccessControlCheckerBase(TC, /*checkUsableFromInline=*/true) {}

  static bool shouldSkipChecking(const ValueDecl *VD) {
    if (VD->getFormalAccess() != AccessLevel::Internal)
      return true;
    return !VD->isUsableFromInline();
  };

  void visit(Decl *D) {
    if (!TC.Context.isSwiftVersionAtLeast(4, 2))
      return;

    if (D->isInvalid() || D->isImplicit())
      return;

    if (auto *VD = dyn_cast<ValueDecl>(D))
      if (shouldSkipChecking(VD))
        return;

    DeclVisitor<UsableFromInlineChecker>::visit(D);
  }

  // Force all kinds to be handled at a lower level.
  void visitDecl(Decl *D) = delete;
  void visitValueDecl(ValueDecl *D) = delete;

#define UNREACHABLE(KIND, REASON) \
  void visit##KIND##Decl(KIND##Decl *D) { \
    llvm_unreachable(REASON); \
  }
  UNREACHABLE(Import, "cannot appear in a type context")
  UNREACHABLE(Extension, "cannot appear in a type context")
  UNREACHABLE(TopLevelCode, "cannot appear in a type context")
  UNREACHABLE(Operator, "cannot appear in a type context")
  UNREACHABLE(PrecedenceGroup, "cannot appear in a type context")
  UNREACHABLE(Module, "cannot appear in a type context")

  UNREACHABLE(Param, "does not have access control")
  UNREACHABLE(GenericTypeParam, "does not have access control")
  UNREACHABLE(MissingMember, "does not have access control")
#undef UNREACHABLE

#define UNINTERESTING(KIND) \
  void visit##KIND##Decl(KIND##Decl *D) {}

  UNINTERESTING(IfConfig) // Does not have access control.
  UNINTERESTING(PoundDiagnostic) // Does not have access control.
  UNINTERESTING(EnumCase) // Handled at the EnumElement level.
  UNINTERESTING(Var) // Handled at the PatternBinding level.
  UNINTERESTING(Destructor) // Always correct.
  UNINTERESTING(Accessor) // Handled by the Var or Subscript.

  /// If \p PBD declared stored instance properties in a fixed-contents struct,
  /// return said struct.
  static const StructDecl *
  getFixedLayoutStructContext(const PatternBindingDecl *PBD) {
    auto *parentStruct = dyn_cast<StructDecl>(PBD->getDeclContext());
    if (!parentStruct)
      return nullptr;
    if (!parentStruct->getAttrs().hasAttribute<FixedLayoutAttr>() ||
        PBD->isStatic() || !PBD->hasStorage()) {
      return nullptr;
    }
    // We don't check for "in resilient modules" because there's no reason to
    // write '@_fixedLayout' on a struct in a non-resilient module.
    return parentStruct;
  }

  /// \see visitPatternBindingDecl
  void checkNamedPattern(const NamedPattern *NP,
                         const ValueDecl *fixedLayoutStructContext,
                         bool isTypeContext,
                         const llvm::DenseSet<const VarDecl *> &seenVars) {
    const VarDecl *theVar = NP->getDecl();
    if (!fixedLayoutStructContext && shouldSkipChecking(theVar))
      return;
    // Only check individual variables if we didn't check an enclosing
    // TypedPattern.
    if (seenVars.count(theVar) || theVar->isInvalid())
      return;

    checkTypeAccess(theVar->getInterfaceType(), nullptr,
                    fixedLayoutStructContext ? fixedLayoutStructContext
                                             : theVar,
                    /*mayBeInferred*/false,
                    [&](AccessScope typeAccessScope,
                        const TypeRepr *complainRepr,
                        DowngradeToWarning downgradeToWarning) {
      auto diagID = diag::pattern_type_not_usable_from_inline_inferred;
      if (fixedLayoutStructContext) {
        diagID =
            diag::pattern_type_not_usable_from_inline_inferred_fixed_layout;
      } else if (!TC.Context.isSwiftVersionAtLeast(5)) {
        diagID = diag::pattern_type_not_usable_from_inline_inferred_warn;
      }
      TC.diagnose(NP->getLoc(), diagID, theVar->isLet(), isTypeContext,
                  theVar->getInterfaceType());
    });
  }

  /// \see visitPatternBindingDecl
  void checkTypedPattern(const TypedPattern *TP,
                         const ValueDecl *fixedLayoutStructContext,
                         bool isTypeContext,
                         llvm::DenseSet<const VarDecl *> &seenVars) {
    // FIXME: We need an access level to check against, so we pull one out
    // of some random VarDecl in the pattern. They're all going to be the
    // same, but still, ick.
    const VarDecl *anyVar = nullptr;
    TP->forEachVariable([&](VarDecl *V) {
      seenVars.insert(V);
      anyVar = V;
    });
    if (!anyVar)
      return;
    if (!fixedLayoutStructContext && shouldSkipChecking(anyVar))
      return;

    checkTypeAccess(TP->getTypeLoc(),
                    fixedLayoutStructContext ? fixedLayoutStructContext
                                             : anyVar,
                    /*mayBeInferred*/true,
                    [&](AccessScope typeAccessScope,
                        const TypeRepr *complainRepr,
                        DowngradeToWarning downgradeToWarning) {
      auto diagID = diag::pattern_type_not_usable_from_inline;
      if (fixedLayoutStructContext)
        diagID = diag::pattern_type_not_usable_from_inline_fixed_layout;
      else if (!TC.Context.isSwiftVersionAtLeast(5))
        diagID = diag::pattern_type_not_usable_from_inline_warn;
      auto diag = TC.diagnose(TP->getLoc(), diagID, anyVar->isLet(),
                              isTypeContext);
      highlightOffendingType(TC, diag, complainRepr);
    });
  }

  void visitPatternBindingDecl(PatternBindingDecl *PBD) {
    bool isTypeContext = PBD->getDeclContext()->isTypeContext();

    // Stored instance properties in public/@usableFromInline fixed-contents
    // structs in resilient modules must always use public/@usableFromInline
    // types. In these cases, check the access against the struct instead of the
    // VarDecl, and customize the diagnostics.
    const ValueDecl *fixedLayoutStructContext =
        getFixedLayoutStructContext(PBD);

    llvm::DenseSet<const VarDecl *> seenVars;
    for (auto entry : PBD->getPatternList()) {
      entry.getPattern()->forEachNode([&](const Pattern *P) {
        if (auto *NP = dyn_cast<NamedPattern>(P)) {
          checkNamedPattern(NP, fixedLayoutStructContext, isTypeContext,
                            seenVars);
          return;
        }

        auto *TP = dyn_cast<TypedPattern>(P);
        if (!TP)
          return;
        checkTypedPattern(TP, fixedLayoutStructContext, isTypeContext,
                          seenVars);
      });
      seenVars.clear();
    }
  }

  void visitTypeAliasDecl(TypeAliasDecl *TAD) {
    checkTypeAccess(TAD->getUnderlyingTypeLoc(), TAD, /*mayBeInferred*/false,
                    [&](AccessScope typeAccessScope,
                        const TypeRepr *complainRepr,
                        DowngradeToWarning downgradeToWarning) {
      auto diagID = diag::type_alias_underlying_type_not_usable_from_inline;
      if (!TC.Context.isSwiftVersionAtLeast(5))
        diagID = diag::type_alias_underlying_type_not_usable_from_inline_warn;
      auto diag = TC.diagnose(TAD, diagID);
      highlightOffendingType(TC, diag, complainRepr);
    });
  }

  void visitAssociatedTypeDecl(AssociatedTypeDecl *assocType) {
    // This must stay in sync with diag::associated_type_not_usable_from_inline.
    enum {
      ACEK_DefaultDefinition = 0,
      ACEK_Requirement
    };

    std::for_each(assocType->getInherited().begin(),
                  assocType->getInherited().end(),
                  [&](TypeLoc requirement) {
      checkTypeAccess(requirement, assocType, /*mayBeInferred*/false,
                      [&](AccessScope typeAccessScope,
            const TypeRepr *complainRepr,
            DowngradeToWarning downgradeDiag) {
        auto diagID = diag::associated_type_not_usable_from_inline;
        if (!TC.Context.isSwiftVersionAtLeast(5))
          diagID = diag::associated_type_not_usable_from_inline_warn;
        auto diag = TC.diagnose(assocType, diagID, ACEK_Requirement);
        highlightOffendingType(TC, diag, complainRepr);
      });
    });
    checkTypeAccess(assocType->getDefaultDefinitionLoc(), assocType,
                     /*mayBeInferred*/false,
                    [&](AccessScope typeAccessScope,
                        const TypeRepr *complainRepr,
                        DowngradeToWarning downgradeDiag) {
      auto diagID = diag::associated_type_not_usable_from_inline;
      if (!TC.Context.isSwiftVersionAtLeast(5))
        diagID = diag::associated_type_not_usable_from_inline_warn;
      auto diag = TC.diagnose(assocType, diagID, ACEK_DefaultDefinition);
      highlightOffendingType(TC, diag, complainRepr);
    });

    if (assocType->getTrailingWhereClause()) {
      auto accessScope =
        assocType->getFormalAccessScope(nullptr);
      checkRequirementAccess(assocType,
                             accessScope,
                             assocType->getDeclContext(),
                             [&](AccessScope typeAccessScope,
                                 const TypeRepr *complainRepr,
                                 DowngradeToWarning downgradeDiag) {
        auto diagID = diag::associated_type_not_usable_from_inline;
        if (!TC.Context.isSwiftVersionAtLeast(5))
          diagID = diag::associated_type_not_usable_from_inline_warn;
        auto diag = TC.diagnose(assocType, diagID, ACEK_Requirement);
        highlightOffendingType(TC, diag, complainRepr);
      });
    }
  }

  void visitEnumDecl(const EnumDecl *ED) {
    checkGenericParamAccess(ED->getGenericParams(), ED);

    if (ED->hasRawType()) {
      Type rawType = ED->getRawType();
      auto rawTypeLocIter = std::find_if(ED->getInherited().begin(),
                                         ED->getInherited().end(),
                                         [&](TypeLoc inherited) {
        if (!inherited.wasValidated())
          return false;
        return inherited.getType().getPointer() == rawType.getPointer();
      });
      if (rawTypeLocIter == ED->getInherited().end())
        return;
      checkTypeAccess(rawType, rawTypeLocIter->getTypeRepr(), ED,
                       /*mayBeInferred*/false,
                      [&](AccessScope typeAccessScope,
                          const TypeRepr *complainRepr,
                          DowngradeToWarning downgradeToWarning) {
        auto diagID = diag::enum_raw_type_not_usable_from_inline;
        if (!TC.Context.isSwiftVersionAtLeast(5))
          diagID = diag::enum_raw_type_not_usable_from_inline_warn;
        auto diag = TC.diagnose(ED, diagID);
        highlightOffendingType(TC, diag, complainRepr);
      });
    }
  }

  void visitStructDecl(StructDecl *SD) {
    checkGenericParamAccess(SD->getGenericParams(), SD);
  }

  void visitClassDecl(ClassDecl *CD) {
    checkGenericParamAccess(CD->getGenericParams(), CD);

    if (CD->hasSuperclass()) {
      const NominalTypeDecl *superclassDecl = CD->getSuperclassDecl();
      // Be slightly defensive here in the presence of badly-ordered
      // inheritance clauses.
      auto superclassLocIter = std::find_if(CD->getInherited().begin(),
                                            CD->getInherited().end(),
                                            [&](TypeLoc inherited) {
        if (!inherited.wasValidated())
          return false;
        Type ty = inherited.getType();
        if (ty->is<ProtocolCompositionType>())
          if (auto superclass = ty->getExistentialLayout().explicitSuperclass)
            ty = superclass;
        return ty->getAnyNominal() == superclassDecl;
      });
      // Sanity check: we couldn't find the superclass for whatever reason
      // (possibly because it's synthetic or something), so don't bother
      // checking it.
      if (superclassLocIter == CD->getInherited().end())
        return;

      checkTypeAccess(CD->getSuperclass(), superclassLocIter->getTypeRepr(), CD,
                       /*mayBeInferred*/false,
                      [&](AccessScope typeAccessScope,
                          const TypeRepr *complainRepr,
                          DowngradeToWarning downgradeToWarning) {
        auto diagID = diag::class_super_not_usable_from_inline;
        if (!TC.Context.isSwiftVersionAtLeast(5))
          diagID = diag::class_super_not_usable_from_inline_warn;
        auto diag = TC.diagnose(CD, diagID,
                                superclassLocIter->getTypeRepr() != complainRepr);
        highlightOffendingType(TC, diag, complainRepr);
      });
    }
  }

  void visitProtocolDecl(ProtocolDecl *proto) {
    // This must stay in sync with diag::protocol_usable_from_inline.
    enum {
      PCEK_Refine = 0,
      PCEK_Requirement
    };

    std::for_each(proto->getInherited().begin(),
                  proto->getInherited().end(),
                  [&](TypeLoc requirement) {
      checkTypeAccess(requirement, proto, /*mayBeInferred*/false,
                      [&](AccessScope typeAccessScope,
                          const TypeRepr *complainRepr,
                          DowngradeToWarning downgradeDiag) {
        auto diagID = diag::protocol_usable_from_inline;
        if (!TC.Context.isSwiftVersionAtLeast(5))
          diagID = diag::protocol_usable_from_inline_warn;
        auto diag = TC.diagnose(proto, diagID, PCEK_Refine);
        highlightOffendingType(TC, diag, complainRepr);
      });
    });

    if (proto->getTrailingWhereClause()) {
      auto accessScope = proto->getFormalAccessScope(nullptr,
                                                     /*checkUsableFromInline*/true);
      checkRequirementAccess(proto,
                             accessScope,
                             proto->getDeclContext(),
                             [&](AccessScope typeAccessScope,
                                 const TypeRepr *complainRepr,
                                 DowngradeToWarning downgradeDiag) {
        auto diagID = diag::protocol_usable_from_inline;
        if (!TC.Context.isSwiftVersionAtLeast(5))
          diagID = diag::protocol_usable_from_inline_warn;
        auto diag = TC.diagnose(proto, diagID, PCEK_Requirement);
        highlightOffendingType(TC, diag, complainRepr);
      });
    }
  }

  void visitSubscriptDecl(SubscriptDecl *SD) {
    for (auto &P : *SD->getIndices()) {
      checkTypeAccess(P->getTypeLoc(), SD, /*mayBeInferred*/false,
                      [&](AccessScope typeAccessScope,
                          const TypeRepr *complainRepr,
                          DowngradeToWarning downgradeDiag) {
        auto diagID = diag::subscript_type_usable_from_inline;
        if (!TC.Context.isSwiftVersionAtLeast(5))
          diagID = diag::subscript_type_usable_from_inline_warn;
        auto diag = TC.diagnose(SD, diagID,
                                /*problemIsElement=*/false);
        highlightOffendingType(TC, diag, complainRepr);
      });
    }

    checkTypeAccess(SD->getElementTypeLoc(), SD, /*mayBeInferred*/false,
                    [&](AccessScope typeAccessScope,
                        const TypeRepr *complainRepr,
                        DowngradeToWarning downgradeDiag) {
      auto diagID = diag::subscript_type_usable_from_inline;
      if (!TC.Context.isSwiftVersionAtLeast(5))
        diagID = diag::subscript_type_usable_from_inline_warn;
      auto diag = TC.diagnose(SD, diagID,
                              /*problemIsElement=*/true);
      highlightOffendingType(TC, diag, complainRepr);
    });
  }

  void visitAbstractFunctionDecl(AbstractFunctionDecl *fn) {
    bool isTypeContext = fn->getDeclContext()->isTypeContext();

    checkGenericParamAccess(fn->getGenericParams(), fn);

    // This must stay in sync with diag::function_type_usable_from_inline.
    enum {
      FK_Function = 0,
      FK_Method,
      FK_Initializer
    };

    auto functionKind = isa<ConstructorDecl>(fn)
      ? FK_Initializer
      : isTypeContext ? FK_Method : FK_Function;

    for (auto *P : *fn->getParameters()) {
      checkTypeAccess(P->getTypeLoc(), fn, /*mayBeInferred*/false,
                      [&](AccessScope typeAccessScope,
                          const TypeRepr *complainRepr,
                          DowngradeToWarning downgradeDiag) {
        auto diagID = diag::function_type_usable_from_inline;
        if (!TC.Context.isSwiftVersionAtLeast(5))
          diagID = diag::function_type_usable_from_inline_warn;
        auto diag = TC.diagnose(fn, diagID, functionKind,
                                /*problemIsResult=*/false);
        highlightOffendingType(TC, diag, complainRepr);
      });
    }

    if (auto FD = dyn_cast<FuncDecl>(fn)) {
      checkTypeAccess(FD->getBodyResultTypeLoc(), FD, /*mayBeInferred*/false,
                      [&](AccessScope typeAccessScope,
                          const TypeRepr *complainRepr,
                          DowngradeToWarning downgradeDiag) {
        auto diagID = diag::function_type_usable_from_inline;
        if (!TC.Context.isSwiftVersionAtLeast(5))
          diagID = diag::function_type_usable_from_inline_warn;
        auto diag = TC.diagnose(fn, diagID, functionKind,
                                /*problemIsResult=*/true);
        highlightOffendingType(TC, diag, complainRepr);
      });
    }
  }

  void visitEnumElementDecl(EnumElementDecl *EED) {
    if (!EED->hasAssociatedValues())
      return;
    for (auto &P : *EED->getParameterList()) {
      checkTypeAccess(P->getTypeLoc(), EED, /*mayBeInferred*/false,
                      [&](AccessScope typeAccessScope,
                          const TypeRepr *complainRepr,
                          DowngradeToWarning downgradeToWarning) {
        auto diagID = diag::enum_case_usable_from_inline;
        if (!TC.Context.isSwiftVersionAtLeast(5))
          diagID = diag::enum_case_usable_from_inline_warn;
        auto diag = TC.diagnose(EED, diagID);
        highlightOffendingType(TC, diag, complainRepr);
      });
    }
  }
};
} // end anonymous namespace

void swift::checkAccessControl(TypeChecker &TC, Decl *D) {
  AccessControlChecker(TC).visit(D);
  UsableFromInlineChecker(TC).visit(D);
}

void swift::checkExtensionGenericParamAccess(TypeChecker &TC,
                                             const ExtensionDecl *ED,
                                             AccessLevel userSpecifiedAccess) {
  AccessScope desiredAccessScope = AccessScope::getPublic();
  switch (userSpecifiedAccess) {
  case AccessLevel::Private:
    assert((ED->isInvalid() ||
            ED->getDeclContext()->isModuleScopeContext()) &&
           "non-top-level extensions make 'private' != 'fileprivate'");
    LLVM_FALLTHROUGH;
  case AccessLevel::FilePrivate: {
    const DeclContext *DC = ED->getModuleScopeContext();
    bool isPrivate = (userSpecifiedAccess == AccessLevel::Private);
    desiredAccessScope = AccessScope(DC, isPrivate);
    break;
  }
  case AccessLevel::Internal:
    desiredAccessScope = AccessScope(ED->getModuleContext());
    break;
  case AccessLevel::Public:
  case AccessLevel::Open:
    break;
  }

  AccessControlChecker(TC).checkGenericParamAccess(ED->getGenericParams(), ED,
                                                   desiredAccessScope,
                                                   userSpecifiedAccess);
}
