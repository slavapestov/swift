// https://github.com/swiftlang/swift/issues/53006

func extractBits(from word: UInt64, offset: Int, numBits: Int) -> UInt {
    return UInt((word >> offset) & ((1 << numBits) - 1) & ((1 << numBits) - 1) & ((1 << numBits) - 1))
}
