// https://github.com/swiftlang/swift/issues/50046

func slow(x: UInt64) {
	let _: UInt64 = ((x >> (6 * 1)) & 0xFF) << (8 * 0) | ((x >> (6 * 3)) & 0xFF) << (8 * 1)
}
