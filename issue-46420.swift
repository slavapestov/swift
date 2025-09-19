// https://github.com/swiftlang/swift/issues/46420

func slow() {
  let new_M: [Int] = Array<Int>(repeating: 0, count: 200)
  let s = (0 ..< 32).map({ (a) -> Int in
      return new_M[a >> 3] >> ((1 ^ a & 7) * 4) & 15
  })
}
