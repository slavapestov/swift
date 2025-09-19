// https://github.com/swiftlang/swift/issues/46157

func slow() {
  let f: (Double) -> Double = { x in x*x*x*x - 3*x*x*x + 2 }
}
