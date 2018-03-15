struct S {
  static func `init`(x: Int) {}
  static func `init`() {}
}

enum E {
  case `init`
  init(x: Int) {}
}

enum E2 {
  case `init`
  // static var `init`: Int { return 0 } // error
}

enum E3 {
  case `init`(Int)
  init(_ x: Int) { while true {} }
}

_ = S()
// _ = S(x: 3) // error

_ = S.init()
// _ = S.init(x: 3) // error

_ = S.`init`() // OK!
_ = S.`init`(x: 3) // OK!

// let _: E = E.init // error!
let _: E = E.`init` // OK

let _: E = E.init(x: 3)

let _: E2 = E2.`init`
// let _: E2 = E2.init // error

let e3: E3 = E3.`init`(123)

switch e3 {
case .`init`(let x): break
default: break
}

enum Raw : Int {
  case a = 0
  case b = 1
  case c = 2
  case `init` = 3
}
