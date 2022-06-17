struct G<Value> {}

protocol P {
  associatedtype T
}

extension G where Value: P, Value.T: CaseIterable, Value.T.AllCases: RandomAccessCollection {}
