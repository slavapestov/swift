// RUN: %target-swift-frontend -typecheck %s -debug-generic-signatures -requirement-machine-protocol-signatures=verify 2>&1 | %FileCheck %s

protocol P {
  associatedtype T
}
struct S : P {
  typealias T = S
}

// CHECK-LABEL: concrete_conformances_in_protocol.(file).R0@
// CHECK-LABEL: Requirement signature: <Self where Self.A == S>

protocol R0 {
  associatedtype A where A : P, A == S
}

////

struct G<T> : P {}

// CHECK-LABEL: concrete_conformances_in_protocol.(file).R1@
// CHECK-LABEL: Requirement signature: <Self where Self.B == G<Self.A>>

protocol R1 {
  associatedtype A
  associatedtype B where B : P, B == G<A>
}

// CHECK-LABEL: concrete_conformances_in_protocol.(file).R2@
// CHECK-LABEL: Requirement signature: <Self where Self.A == G<Self.B>>

protocol R2 {
  associatedtype A where A : P, A == G<B>
  associatedtype B
}

////

protocol PP {
  associatedtype T : P
}

struct GG<T : P> : PP {}

// CxHECK-LABEL: concrete_conformances_in_protocol.(file).RR3a@
// CxHECK-LABEL: Requirement signature: <Self where Self.A : P, Self.B == GG<Self.A>>

// GSB broken here
/*protocol RR3a {
  associatedtype A
  associatedtype B where B : PP, B == GG<A>
}*/

// CHECK-LABEL: concrete_conformances_in_protocol.(file).RR3b@
// CHECK-LABEL: Requirement signature: <Self where Self.A : P, Self.B == GG<Self.A>>

protocol RR3b {
  associatedtype A : P
  associatedtype B where B : PP, B == GG<A>
}

// CxHECK-LABEL: concrete_conformances_in_protocol.(file).RR4a@
// CxHECK-LABEL: Requirement signature: <Self where Self.A == GG<Self.B>, Self.B : P>

// GSB broken here
/*
protocol RR4a {
  associatedtype A where A : PP, A == GG<B>
  associatedtype B
}*/

// CHECK-LABEL: concrete_conformances_in_protocol.(file).RR4b@
// CHECK-LABEL: Requirement signature: <Self where Self.A == GG<Self.B>, Self.B : P>

protocol RR4b {
  associatedtype A where A : PP, A == GG<B>
  associatedtype B : P
}

// CxHECK-LABEL: concrete_conformances_in_protocol.(file).RR5a@
// CxHECK-LABEL: Requirement signature: <Self where Self.A : P, Self.B == GG<Self.A.T>, Self.A.T : P>

// GSB broken here
/*
protocol RR5a {
  associatedtype A : P
  associatedtype B where B : PP, B == GG<A.T>
}*/

// CHECK-LABEL: concrete_conformances_in_protocol.(file).RR5b@
// CHECK-LABEL: Requirement signature: <Self where Self.A : PP, Self.B == GG<Self.A.T>>

protocol RR5b {
  associatedtype A : PP
  associatedtype B where B : PP, B == GG<A.T>
}

// CxHECK-LABEL: concrete_conformances_in_protocol.(file).RR6a@
// CxHECK-LABEL: Requirement signature: <Self where Self.A == GG<Self.B.T>, Self.B : P, Self.B.T : P>

// RQM is broken here; extra req Self.A.T == Self.B.T, missing req Self.B.T : P
/*protocol RR6a {
  associatedtype A where A : PP, A == GG<B.T>
  associatedtype B : P
}*/

// CxHECK-LABEL: concrete_conformances_in_protocol.(file).RR6b@
// CxHECK-LABEL: Requirement signature: <Self where Self.A == GG<Self.B.T>, Self.B : PP>

// RQM is broken here; extra req Self.A.T == Self.B.T, missing req Self.B.T : P
/*
protocol RR6b {
  associatedtype A where A : PP, A == GG<B.T>
  associatedtype B : PP
}*/