protocol P1 {
  associatedtype T: P2 where T.T == Self
}

protocol P2 {
  associatedtype T: P1 where T.T == Self
}

protocol P3: P2 {}

protocol P4 {
  associatedtype T
}

func foo<T: P4>(_: T) where T.T: P1, T.T.T: P3 {}
