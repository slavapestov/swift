// https://github.com/swiftlang/swift/issues/47685

func slow() {
  let a = 0
  let b = 0
  let c = 0

  let d = 0
  let e = 0
  let f = 0

  let diff = abs(d - a) + abs(e + b) + abs(f - c)
}
