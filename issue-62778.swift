// https://github.com/swiftlang/swift/issues/62778

// This is invalid
func slow() {
  let pieces = [1, 2, 3, 4]
  let _ = (UInt(pieces[0]) << 24) | (UInt(pieces[1]) << 16) | (UInt(pieces[2]) << 8) | pieces[3]
}
