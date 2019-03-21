// RUN: %empty-directory(%t)

// RUN: %target-build-swift-dylib(%t/%target-library-name(resilient_struct)) -enable-library-evolution %S/../Inputs/resilient_struct.swift -emit-module -emit-module-path %t/resilient_struct.swiftmodule
// RUN: %target-codesign %t/%target-library-name(resilient_struct)

// RUN: %target-build-swift-dylib(%t/%target-library-name(resilient_objc_class)) -I %t -L %t -lresilient_struct -enable-library-evolution %S/../Inputs/resilient_objc_class.swift -emit-module -emit-module-path %t/resilient_objc_class.swiftmodule -Xfrontend -enable-resilient-objc-class-stubs
// RUN: %target-codesign %t/%target-library-name(resilient_objc_class)

// RUN: %target-build-swift %s -L %t -I %t -lresilient_struct -lresilient_objc_class -o %t/main %target-rpath(%t) -Xfrontend -enable-resilient-objc-class-stubs
// RUN: %target-codesign %t/main

// RUN: %target-run %t/main %t/%target-library-name(resilient_struct) %t/%target-library-name(resilient_objc_class)

// REQUIRES: executable_test
// REQUIRES: objc_interop

import StdlibUnittest
import Foundation
import resilient_objc_class


var ResilientClassTestSuite = TestSuite("ResilientClass")

class ResilientNSObjectSubclass : ResilientNSObjectOutsideParent {}

@objc protocol MyProtocol {
  func myMethod() -> Int
}

extension ResilientNSObjectSubclass : MyProtocol {
  @objc func myMethod() -> Int { return 42 }
}

ResilientClassTestSuite.test("category on my class") {
  let o = ResilientNSObjectSubclass()
  expectEqual(42, (o as MyProtocol).myMethod())
}

/*
@objc protocol AnotherProtocol {
  func anotherMethod() -> Int
}

extension ResilientNSObjectOutsideParent : AnotherProtocol {
  @objc func anotherMethod() -> Int { return 69 }
}

ResilientClassTestSuite.test("category on other class") {
  let o = ResilientNSObjectOutsideParent()
  expectEqual(69, (o as AnotherProtocol).anotherMethod())
}
*/

runAllTests()
