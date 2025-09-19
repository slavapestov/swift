// https://github.com/swiftlang/swift/issues/49635

func slow() {
  (Optional(1) ?? 1) * 1 * 1 * 1 * 1 * 1 * 1
}
