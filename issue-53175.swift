// https://github.com/swiftlang/swift/issues/53175

func slow() {
  if 0.1 * 5 == 0.1 + 0.1 + 0.1 + 0.1 + 0.1 { }
}
