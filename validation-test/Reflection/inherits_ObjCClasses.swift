// RUN: rm -rf %t && mkdir -p %t

// RUN: %clang %target-cc-options -isysroot %sdk -fobjc-arc %S/Inputs/ObjCClasses/ObjCClasses.m -c -o %t/ObjCClasses.o
// RUN: %target-build-swift -I %S/Inputs/ObjCClasses/ -lswiftSwiftReflectionTest %t/ObjCClasses.o %s -o %t/inherits_ObjCClasses
// RUN: %target-run %target-swift-reflection-test %t/inherits_ObjCClasses | tee /tmp/xxx | %FileCheck %s --check-prefix=CHECK --check-prefix=CHECK-%target-ptrsize

// REQUIRES: objc_interop
// REQUIRES: executable_test

import simd
import Foundation
import ObjCClasses
import SwiftReflectionTest

class DerivedFirstClass : FirstClass {
  var xx: int4 = [1,2,3,4]
  var yy: AnyObject? = nil // 16?
  var zz: Int = 0
}

let firstClass = DerivedFirstClass()
//reflect(object: firstClass)

class DerivedSecondClass : SecondClass {
  var xx: int4 = [1,2,3,4]
  var yy: AnyObject? = nil // 24?
  var zz: Int = 0
}

let secondClass = DerivedSecondClass()
//reflect(object: secondClass)

class DerivedThirdClass : ThirdClass {
  var xx: int4 = [1,2,3,4]
  var yy: AnyObject? = nil // 32?
  var zz: Int = 0
}

let thirdClass = DerivedThirdClass()
//reflect(object: thirdClass)

func g() -> int4 {
  return thirdClass.xx
}

g()
//doneReflecting()
