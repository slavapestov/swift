// https://github.com/swiftlang/swift/issues/48588

func toUInt32(_ a: [UInt8]) -> UInt32 {
    return (UInt32(a[0]) << 24) +
        (UInt32(a[1]) << 16) +
        (UInt32(a[2]) << 8) +
        UInt32(a[3])
}
