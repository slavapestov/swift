// https://github.com/swiftlang/swift/issues/44186

class Pick {
  var pick_line: Int? = 0
  var position: Int? = 0
}

extension Optional: Comparable where Wrapped: Comparable {
  public static func <(_ lhs: Self, _ rhs: Self) -> Bool { return false }
}

func slow(picks: [Pick]) {
  let sorted = picks.sorted { $0.pick_line == $1.pick_line ? $0.position < $1.position : $0.pick_line < $1.pick_line }
}

