// https://github.com/swiftlang/swift/issues/47587

func slow() {
  let x = ["x"].count > 6 + 1 + 4 + 1 + 40 + 1
}
