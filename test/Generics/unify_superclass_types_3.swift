// RUN: %target-typecheck-verify-swift -dump-requirement-machine -debug-generic-signatures -requirement-machine-inferred-signatures=verify 2>&1 | %FileCheck %s

// Note: The GSB fails this test, because it doesn't implement unification of
// superclass type constructor arguments.

class Generic<T, U, V> {}

class Derived<TT, UU> : Generic<Int, TT, UU> {}

protocol P1 {
  associatedtype X : Derived<A1, B1>
  associatedtype A1
  associatedtype B1
}

protocol P2 {
  associatedtype X : Generic<A2, String, B2>
  associatedtype A2
  associatedtype B2
}

func sameType<T>(_: T.Type, _: T.Type) {}

func takesDerivedString<U>(_: Derived<String, U>.Type) {}

// CHECK-LABEL: .unifySuperclassTest@
// CHECK-NEXT: Generic signature: <T where T : P1, T : P2>
func unifySuperclassTest<T : P1 & P2>(_: T) {
  sameType(T.A1.self, String.self)
  sameType(T.A2.self, Int.self)
  sameType(T.B1.self, T.B2.self)
  takesDerivedString(T.X.self)
}

// CHECK-LABEL: Requirement machine for <τ_0_0 where τ_0_0 : P1, τ_0_0 : P2>
// CHECK-NEXT: Rewrite system: {
// CHECK:      - [P1:X].[layout: _NativeClass] => [P1:X]
// CHECK:      - [P2:X].[layout: _NativeClass] => [P2:X]
// CHECK:      - τ_0_0.[P2:X] => τ_0_0.[P1:X]
// CHECK:      - τ_0_0.[P1:X].[superclass: Generic<Int, τ_0_0.[P1:A1], τ_0_0.[P1:B1]>] => τ_0_0.[P1:X]
// CHECK:      - τ_0_0.[P1:X].[superclass: Generic<Int, String, τ_0_0.[P1:B1]>] => τ_0_0.[P1:X]
// CHECK:      - τ_0_0.[P2:A2].[concrete: Int] => τ_0_0.[P2:A2]
// CHECK:      - τ_0_0.[P2:B2] => τ_0_0.[P1:B1]
// CHECK:      - τ_0_0.[P1:A1].[concrete: String] => τ_0_0.[P1:A1]
// CHECK:      - τ_0_0.B2 => τ_0_0.[P1:B1]
// CHECK:      }
// CHECK: Property map: {
// CHECK-NEXT:   [P1] => { conforms_to: [P1] }
// CHECK-NEXT:   [P1:X] => { layout: _NativeClass superclass: [superclass: Derived<[P1:A1], [P1:B1]>] }
// CHECK-NEXT:   [P2] => { conforms_to: [P2] }
// CHECK-NEXT:   [P2:X] => { layout: _NativeClass superclass: [superclass: Generic<[P2:A2], String, [P2:B2]>] }
// CHECK-NEXT:   τ_0_0 => { conforms_to: [P1 P2] }
// CHECK-NEXT:   τ_0_0.[P1:X] => { layout: _NativeClass superclass: [superclass: Derived<τ_0_0.[P1:A1], τ_0_0.[P1:B1]>] }
// CHECK-NEXT:   τ_0_0.[P2:A2] => { concrete_type: [concrete: Int] }
// CHECK-NEXT:   τ_0_0.[P1:A1] => { concrete_type: [concrete: String] }
// CHECK-NEXT: }
