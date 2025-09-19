// https://github.com/swiftlang/swift/issues/55762

struct S {
  var data: [Int16]
  var idx = 0

  func f() {
    let _ = UInt64(self.data[self.idx]) << 56
          | UInt64(self.data[self.idx + 1]) << 48
          | UInt64(self.data[self.idx + 2]) << 40
          | UInt64(self.data[self.idx + 3]) << 32
          | UInt64(self.data[self.idx + 4]) << 24
          | UInt64(self.data[self.idx + 5]) << 16
          | UInt64(self.data[self.idx + 6]) << 8
          | UInt64(self.data[self.idx + 7]) << 0
  }
}
