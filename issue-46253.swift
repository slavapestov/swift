// https://github.com/swiftlang/swift/issues/46253

func slow() {
  let d: [Int: (Int) -> Bool] =
    [ 0: { $0 == $0 }, 1: { $0 == $0 }, 2: { $0 == $0 }, 3: { $0 == $0 } ]
}

func slow2() {
  let d: [Int: (Int) -> Int] =
    [ 0: { -$0 }, 1: { -$0 }, 2: { -$0 }, 3: { -$0 }, 4: { -$0 },
      5: { -$0 }, 6: { -$0 }, 7: { -$0 }, 8: { -$0 } ]
}
