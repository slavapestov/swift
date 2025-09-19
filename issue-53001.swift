// https://github.com/swiftlang/swift/issues/53001

func slow() {
  let bit    : UInt32 = 8
  let offset1: UInt32 = 8
  let offset2: UInt32 = 16
  let offset3: UInt32 = 24
   
  // The following expression took over 1000 ms to type-check.
  let result: UInt32 = UInt32(bit | UInt32(bit << offset1) | UInt32(bit << offset2) | UInt32(bit << offset3))
}
