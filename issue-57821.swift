// https://github.com/swiftlang/swift/issues/57821

func plotSpcPattern(x : Int, y : Int, pattern : Int) -> UInt8 {
  return UInt8(pattern >> ((~y & 1) * 8 + (~x & 3) * 2) & 3)
}
