// https://github.com/swiftlang/swift/issues/49476

// This is invalid

func slow() {
  let offset: Double = 5.0
  let index: Int = 10
  let angle = (180.0 - offset + index * 5.0) * .pi / 18
}
