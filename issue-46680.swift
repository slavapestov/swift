// https://github.com/swiftlang/swift/issues/46680

import Foundation

func slow() {
  let x = [
      NSNumber(value : 0),
      NSNumber(value : 1/6),
      NSNumber(value : 3/6),
      NSNumber(value : 5/6),
      NSNumber(value : 1),
  ]
}
