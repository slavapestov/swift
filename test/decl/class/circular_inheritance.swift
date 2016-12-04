// RUN: %target-typecheck-verify-swift

class C : B { } // expected-error{{circular class inheritance 'C' -> 'B' -> 'A' -> 'C'}}
class B : A { } // expected-note{{class 'B' declared here}}
class A : C { } // expected-note{{class 'A' declared here}}

class TrivialCycle : TrivialCycle {} // expected-error{{circular class inheritance TrivialCycle}}
protocol P : P {} // expected-error{{circular protocol inheritance P}}

class Isomorphism : Automorphism { }
class Automorphism : Automorphism { } // expected-error{{circular class inheritance Automorphism}}

// FIXME: Useless error
let _ = A() // expected-error{{'A' cannot be constructed because it has no accessible initializers}}

class Left : Right.Hand {
  class Hand {}
}

class Right : Left.Hand {
  class Hand {}
}

class Outer {
  class Inner : Outer {}
}

class Outer2 : Outer2.Inner {
  class Inner {}
}

class Outer3 : Outer3.Inner<Int> {
  class Inner<T> {}
}

class GenericLeft<T> : GenericRight<T>.Hand {
// expected-error@-1 {{circular reference}}
// expected-note@-2 {{through reference here}}
  class Hand {}
}

class GenericRight<T> : GenericLeft<T>.Hand { // expected-note 2 {{through reference here}}
  class Hand {}
}

class GenericOuter<T> {
  class Inner : GenericOuter<Int> {}
}

class GenericOuter2<T> : GenericOuter2<T>.Inner {
// expected-error@-1 {{circular reference}}
// expected-note@-2 {{through reference here}}
  class Inner {}
}
