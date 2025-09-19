// RUN: %target-typecheck-verify-swift -solver-scope-threshold=1000

// https://github.com/swiftlang/swift/issues/47896

func slow() {
    return MemoryLayout<OpaquePointer>.size + MemoryLayout<Int32>.size + MemoryLayout<Int32>.size + MemoryLayout<Int32>.size + MemoryLayout<UnsafePointer<UInt8>>.size
    // expected-error@-1 {{reasonable time}}
}

