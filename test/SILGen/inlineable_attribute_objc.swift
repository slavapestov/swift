// RUN: rm -rf %t && mkdir -p %t
// RUN: %build-silgen-test-overlays

// RUN: %target-swift-frontend(mock-sdk: -sdk %S/Inputs -I %t) -Xllvm -sil-full-demangle -primary-file %s -emit-silgen | %FileCheck %s

public class Horse : NSObject {
  public dynamic func gallop() {}
}

@_inlineable public func piglet() {
  _ = Horse().gallop
}
