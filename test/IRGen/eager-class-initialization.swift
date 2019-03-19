// RUN: %empty-directory(%t)
// RUN: %build-irgen-test-overlays
// RUN: %target-swift-frontend(mock-sdk: %clang-importer-sdk) %s -emit-ir | %FileCheck %s -DINT=i%target-ptrsize --check-prefix=CHECK-OLD
// RUN: %target-swift-frontend(mock-sdk: %clang-importer-sdk) %s -target x86_64-apple-macosx10.14.4 -emit-ir | %FileCheck %s -DINT=i%target-ptrsize --check-prefix=CHECK-NEW
// REQUIRES: CPU=x86_64
// REQUIRES: OS=macosx

import Foundation

class Generic<T> : NSObject {}

class GenericAncestry : Generic<Int>, NSCoding {
  required init(coder: NSCoder) { fatalError() }

  func encode(coder: NSCoder) { fatalError() }
}

// CHECK-OLD-LABEL: define {{.*}} @_swift_eager_class_initialization
// CHECK-OLD-NEXT:  entry:
// CHECK-OLD:         call swiftcc %swift.metadata_response @"$s4main15GenericAncestryCMa"([[INT]] 0)
// CHECK-OLD:         ret

// CHECK-NEW-NOT: define {{.*}} @_swift_eager_class_initialization
