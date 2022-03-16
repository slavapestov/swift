// RUN: %target-typecheck-verify-swift -requirement-machine-inferred-signatures=off
// RUN: %target-swift-frontend -typecheck %s -debug-generic-signatures -requirement-machine-inferred-signatures=on 2>&1 | %FileCheck %s

// FIXME: Both RUN lines should pass 'on' once diagnostics are implemented.

protocol P {}
class C {}

class X<T : P> : U {}
class Y<T : C> : V {}
class Z<T : AnyObject> : W {}

protocol U {
  associatedtype T : P
}

protocol V {
  associatedtype T : C
}

protocol W {
  associatedtype T : AnyObject
}

// CHECK-LABEL: .derivedViaConcreteX1@
// CHECK-NEXT: Generic signature: <A, B where A : X<B>, B : P>
func derivedViaConcreteX1<A, B>(_: A, _: B)
  where A : U, A : X<B> {}
// expected-warning@-1 {{redundant conformance constraint 'A' : 'U'}}
// expected-note@-2 {{conformance constraint 'A' : 'U' implied here}}

// CHECK-LABEL: .derivedViaConcreteX2@
// CHECK-NEXT: Generic signature: <A, B where A : X<B>, B : P>
func derivedViaConcreteX2<A, B>(_: A, _: B)
  where A : U, B : P, A : X<B> {}
// expected-warning@-1 {{redundant conformance constraint 'A' : 'U'}}
// expected-note@-2 {{conformance constraint 'A' : 'U' implied here}}

// CHECK-LABEL: .derivedViaConcreteY1@
// CHECK-NEXT: Generic signature: <A, B where A : Y<B>, B : C>
func derivedViaConcreteY1<A, B>(_: A, _: B)
  where A : V, A : Y<B> {}
// expected-warning@-1 {{redundant conformance constraint 'A' : 'V'}}
// expected-note@-2 {{conformance constraint 'A' : 'V' implied here}}

// CHECK-LABEL: .derivedViaConcreteY2@
// CHECK-NEXT: Generic signature: <A, B where A : Y<B>, B : C>
func derivedViaConcreteY2<A, B>(_: A, _: B)
  where A : V, B : C, A : Y<B> {}
// expected-warning@-1 {{redundant conformance constraint 'A' : 'V'}}
// expected-note@-2 {{conformance constraint 'A' : 'V' implied here}}

// CHECK-LABEL: .derivedViaConcreteZ1@
// CHECK-NEXT: Generic signature: <A, B where A : Z<B>, B : AnyObject>
func derivedViaConcreteZ1<A, B>(_: A, _: B)
  where A : W, A : Z<B> {}
// expected-warning@-1 {{redundant conformance constraint 'A' : 'W'}}
// expected-note@-2 {{conformance constraint 'A' : 'W' implied here}}

// CHECK-LABEL: .derivedViaConcreteZ2@
// CHECK-NEXT: Generic signature: <A, B where A : Z<B>, B : AnyObject>
func derivedViaConcreteZ2<A, B>(_: A, _: B)
  where A : W, B : AnyObject, A : Z<B> {}
// expected-warning@-1 {{redundant conformance constraint 'A' : 'W'}}
// expected-note@-2 {{conformance constraint 'A' : 'W' implied here}}

class Base {}
class Derived<T : C> : Base, V {}

struct G<X : Base, Y> {}

// CHECK-LABEL: ExtensionDecl line={{.*}} base=G
// CHECK-NEXT: Generic signature: <X, Y where X == Derived<Y>, Y : C>
extension G where X == Derived<Y> {}
