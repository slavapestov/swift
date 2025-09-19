// https://github.com/swiftlang/swift/issues/52563

func slow() {
  let a = 1
  let b = 1
  let c = 1
  let result = [(-b + c) / (2 * a), (-b - c) / (2 * a)]
}
