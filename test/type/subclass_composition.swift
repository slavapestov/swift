// RUN: %target-typecheck-verify-swift -swift-version 4 -enable-experimental-subclass-existentials

protocol P1 {
  typealias DependentInConcreteConformance = Self
}

class Base<T> : P1 {
  typealias DependentClass = T

  func classSelfReturn() -> Self {}
}

protocol P2 {
  typealias FullyConcrete = Int
  typealias DependentProtocol = Self

  func protocolSelfReturn() -> Self
}

class Derived : Base<Int>, P2 {
  func protocolSelfReturn() -> Self {}
}

class Other : Base<Int> {}

typealias OtherAndP2 = Other & P2

protocol P3 : class {}

func basicDiagnostics(
  // These could be made to work with little effort... but why?
  _: Base<Int> & Base<Int>, // expected-error{{protocol composition cannot contain class 'Base<Int>' because it already contains class 'Base<Int>'}}
  _: Base<Int> & Derived, // expected-error{{protocol composition cannot contain class 'Derived' because it already contains class 'Base<Int>'}}

  // Invalid typealias case
  _: Derived & OtherAndP2, // expected-error{{protocol composition cannot contain class 'Other' because it already contains class 'Derived'}}

  // Valid typealias case
  _: OtherAndP2 & P3) {}

// FIXME: We can't just say (P & Q).R for some reason.
typealias BaseAndP2 = Base<Int> & P2

func dependentMemberTypes<T : BaseAndP2>(,
  _: T.DependentInConcreteConformance,
  _: T.DependentProtocol,
  _: T.DependentClass,
  _: T.FullyConcrete,

  _: BaseAndP2.DependentInConcreteConformance, // FIXME expected-error {{}}
  _: BaseAndP2.DependentProtocol, // expected-error {{typealias 'DependentProtocol' can only be used with a concrete type or generic parameter base}}
  _: BaseAndP2.DependentClass, // FIXME expected-error {{}}
  _: BaseAndP2.FullyConcrete) {}

func takesABunchOfValues(
  base: Base<Int>,
  baseAndP1: Base<Int> & P1,
  baseAndP2: Base<Int> & P2,
  baseAndP2AndAnyObject: Base<Int> & P2 & AnyObject,
  baseAndAnyObject: Base<Int> & AnyObject,
  derived: Derived,
  derivedAndP2: Derived & P2,
  derivedAndAnyObject: Derived & AnyObject,
  p1AndAnyObject: P1 & AnyObject,
  p2AndAnyObject: P2 & AnyObject) {

  let _: P1 = baseAndP1
  let _: P1 & AnyObject = baseAndP1 // FIXME expected-error {{}}
  let _: Base = baseAndP1 // FIXME expected-error {{}}
  let _: Base & P1 = base
  let _: Base & AnyObject = base

  let _: P1 = derived
  let _: P1 & AnyObject = derived
  let _: Base & P2 = derived
  let _: Base & P2 & AnyObject = derived
  let _: Derived & AnyObject = derived
  let _: Derived = derivedAndP2 // FIXME expected-error {{}}
  let _: Derived = derivedAndAnyObject // FIXME expected-error {{}}

  let _: Base & P2 = baseAndP2.classSelfReturn()
  let _: Base & P2 = baseAndP2.protocolSelfReturn()
}
