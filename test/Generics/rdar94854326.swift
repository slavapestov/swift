protocol P1  {
  associatedtype T: P2 where T.T == Self
}

protocol P2 {
  associatedtype T
}

protocol P3: P2 {}

protocol P4 {
  associatedtype T
}

class G<T: P4> where T.T: P1, T.T.T: P3 {}
