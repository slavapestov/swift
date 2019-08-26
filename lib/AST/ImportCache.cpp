//===--- ImportCache.cpp - Caching the import graph -------------*- C++ -*-===//
//
// This source file is part of the Swift.org open source project
//
// Copyright (c) 2014 - 2019 Apple Inc. and the Swift project authors
// Licensed under Apache License v2.0 with Runtime Library Exception
//
// See https://swift.org/LICENSE.txt for license information
// See https://swift.org/CONTRIBUTORS.txt for the list of Swift project authors
//
//===----------------------------------------------------------------------===//
//
// This file defines interfaces for querying the module import graph in an
// efficient manner.
//
//===----------------------------------------------------------------------===//

#include "llvm/ADT/DenseSet.h"
#include "swift/AST/ASTContext.h"
#include "swift/AST/ImportCache.h"
#include "swift/AST/Module.h"

using namespace swift;
using namespace namelookup;

ImportSet::ImportSet(ArrayRef<ModuleDecl::ImportedModule> topLevelImports,
                     ArrayRef<ModuleDecl::ImportedModule> transitiveImports)
  : NumTopLevelImports(topLevelImports.size()),
    NumTransitiveImports(transitiveImports.size()) {
  auto buffer = getTrailingObjects<ModuleDecl::ImportedModule>();
  std::uninitialized_copy(topLevelImports.begin(), topLevelImports.end(),
                          buffer);
  std::uninitialized_copy(transitiveImports.begin(), transitiveImports.end(),
                          buffer + topLevelImports.size());
}

void ImportSet::Profile(
    llvm::FoldingSetNodeID &ID,
    ArrayRef<ModuleDecl::ImportedModule> topLevelImports) {
  ID.AddInteger(topLevelImports.size());
  for (auto import : topLevelImports) {
    ID.AddInteger(import.first.size());
    for (auto accessPathElt : import.first) {
      ID.AddPointer(accessPathElt.first.getAsOpaquePointer());
    }
    ID.AddPointer(import.second);
  }
}

static void visitExports(ModuleDecl::ImportedModule next,
                         SmallVectorImpl<ModuleDecl::ImportedModule> &stack) {
  SmallVector<ModuleDecl::ImportedModule, 4> exports;
  next.second->getImportedModulesForLookup(exports);
  for (auto exported : exports) {
    if (next.first.empty())
      stack.push_back(exported);
    else if (exported.first.empty()) {
      exported.first = next.first;
      stack.push_back(exported);
    } else if (ModuleDecl::isSameAccessPath(next.first, exported.first)) {
      stack.push_back(exported);
    }
  }
}

ImportSet &
ImportCache::getImportSet(ASTContext &ctx,
                          ArrayRef<ModuleDecl::ImportedModule> imports) {
  SmallVector<ModuleDecl::ImportedModule, 4> topLevelImports;

  SmallVector<ModuleDecl::ImportedModule, 4> transitiveImports;
  llvm::SmallDenseSet<ModuleDecl::ImportedModule, 32> visited;

  for (auto next : imports) {
    if (!visited.insert(next).second)
      continue;

    topLevelImports.push_back(next);
  }

  void *InsertPos = nullptr;

  llvm::FoldingSetNodeID ID;
  ImportSet::Profile(ID, topLevelImports);

  if (ImportSet *result = ImportSets.FindNodeOrInsertPos(ID, InsertPos)) {
    if (ctx.Stats)
      ctx.Stats->getFrontendCounters().ImportSetFoldHit++;
    return *result;
  }

  if (ctx.Stats)
    ctx.Stats->getFrontendCounters().ImportSetFoldMiss++;

  SmallVector<ModuleDecl::ImportedModule, 4> stack;
  for (auto next : topLevelImports) {
    visitExports(next, stack);
  }

  while (!stack.empty()) {
    auto next = stack.pop_back_val();

    if (!visited.insert(next).second)
      continue;

    transitiveImports.push_back(next);
    visitExports(next, stack);
  }

  // Find the insert position again, in case the above traversal invalidated
  // the folding set via re-entrant calls to getImportSet() from
  // getImportedModulesForLookup().
  if (ImportSet *result = ImportSets.FindNodeOrInsertPos(ID, InsertPos))
    return *result;

  void *mem = ctx.Allocate(
    sizeof(ImportSet) +
    sizeof(ModuleDecl::ImportedModule) * topLevelImports.size() +
    sizeof(ModuleDecl::ImportedModule) * transitiveImports.size(),
    alignof(ImportSet), AllocationArena::Permanent);

  auto *result = new (mem) ImportSet(topLevelImports, transitiveImports);
  ImportSets.InsertNode(result, InsertPos);

  return *result;
}

ImportSet &ImportCache::getImportSet(const DeclContext *dc) {
  dc = dc->getModuleScopeContext();
  auto *file = dyn_cast<FileUnit>(dc);
  auto *mod = dc->getParentModule();
  if (!file)
    dc = mod;

  auto &ctx = mod->getASTContext();

  auto found = ImportSetForDC.find(dc);
  if (found != ImportSetForDC.end()) {
    if (ctx.Stats)
      ctx.Stats->getFrontendCounters().ImportSetCacheHit++;
    return *found->second;
  }

  if (ctx.Stats)
    ctx.Stats->getFrontendCounters().ImportSetCacheMiss++;

  SmallVector<ModuleDecl::ImportedModule, 4> imports;
  imports.emplace_back(ModuleDecl::AccessPathTy(), mod);

  if (file) {
    ModuleDecl::ImportFilter importFilter;
    importFilter |= ModuleDecl::ImportFilterKind::Private;
    importFilter |= ModuleDecl::ImportFilterKind::ImplementationOnly;
    file->getImportedModules(imports, importFilter);
  }

  auto &result = getImportSet(ctx, imports);
  ImportSetForDC[dc] = &result;

  return result;
}

bool ImportCache::isImportedBy(const ModuleDecl *mod, const DeclContext *dc,
                               SmallVectorImpl<ModuleDecl::AccessPathTy> &accessPaths) {
  bool found = false;

  for (auto next : getImportSet(dc).getAllImports()) {
    // If we found 'mod', record the access path.
    if (next.second == mod) {
      // Make sure the list of access paths is unique.
      if (llvm::find(accessPaths, next.first) == accessPaths.end())
        accessPaths.push_back(next.first);

      // Keep going in case we find more access paths.
      found = true;
    }
  }

  return found;
}

ArrayRef<ModuleDecl::AccessPathTy>
ImportCache::getAllAccessPathsNotShadowedBy(const ModuleDecl *mod,
                                            const ModuleDecl *other,
                                            const DeclContext *dc) {
  dc = dc->getModuleScopeContext();
  auto *currentMod = dc->getParentModule();
  auto &ctx = currentMod->getASTContext();

  // Fast path.
  if (currentMod == other)
    return ArrayRef<ModuleDecl::AccessPathTy>();

  auto key = std::make_tuple(mod, other, dc);
  auto found = ShadowCache.find(key);
  if (found != ShadowCache.end()) {
    if (ctx.Stats)
      ctx.Stats->getFrontendCounters().ModuleShadowCacheHit++;
    return found->second;
  }

  if (ctx.Stats)
    ctx.Stats->getFrontendCounters().ModuleShadowCacheMiss++;

  SmallVector<ModuleDecl::ImportedModule, 4> stack;
  llvm::SmallDenseSet<ModuleDecl::ImportedModule, 32> visited;

  stack.emplace_back(ModuleDecl::AccessPathTy(), currentMod);

  if (auto *file = dyn_cast<FileUnit>(dc)) {
    ModuleDecl::ImportFilter importFilter;
    importFilter |= ModuleDecl::ImportFilterKind::Private;
    importFilter |= ModuleDecl::ImportFilterKind::ImplementationOnly;
    file->getImportedModules(stack, importFilter);
  }

  SmallVector<ModuleDecl::AccessPathTy, 4> accessPaths;

  while (!stack.empty()) {
    auto next = stack.pop_back_val();

    // Don't visit a module more than once.
    if (!visited.insert(next).second)
      continue;

    // Don't visit the 'other' module's re-exports.
    if (next.second == other)
      continue;

    // If we found 'mod' via some access path, remember the access
    // path.
    if (next.second == mod) {
      // Make sure the list of access paths is unique.
      if (llvm::find(accessPaths, next.first) == accessPaths.end())
        accessPaths.push_back(next.first);
    }

    visitExports(next, stack);
  }

  auto result = ctx.AllocateCopy(accessPaths);
  ShadowCache[key] = result;
  return result;
};