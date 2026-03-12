// RUN: %target-typecheck-verify-swift -solver-scope-threshold=1000
// REQUIRES: tools-release,no_asan

// The original expression had the '0 as Int?', but the expression used to
// be too complex either way, and now that type hint isn't necessary
// either way.
//
// Both versions are below, they are identical except for that one change.

func test1() {
  let i: Int? = 1
  let j: Int?
  let k: Int? = 2

  let _ = [i, j, k].reduce(0 as Int?) {
    $0 != nil && $1 != nil ? $0! + $1! : ($0 != nil ? $0! : ($1 != nil ? $1! : nil))
  }
}

func test2() {
  let i: Int? = 1
  let j: Int?
  let k: Int? = 2

  let _ = [i, j, k].reduce(0) {
    $0 != nil && $1 != nil ? $0! + $1! : ($0 != nil ? $0! : ($1 != nil ? $1! : nil))
  }
}
