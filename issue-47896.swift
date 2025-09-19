// https://github.com/swiftlang/swift/issues/47896

func slow() {
    return MemoryLayout<OpaquePointer>.size + MemoryLayout<Int32>.size + MemoryLayout<Int32>.size + MemoryLayout<Int32>.size + MemoryLayout<UnsafePointer<UInt8>>.size
}

