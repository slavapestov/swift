// https://github.com/swiftlang/swift/issues/69277

import Foundation

struct Monster {
    var weaponP1: Int? { nil }
    var weaponP2: Int? { nil }
    var weaponP3: Int? { nil }
}

func slow() {
  let predicate = #Predicate<Monster> { monster in
      ((monster.weaponP1 == 1) &&
       (monster.weaponP2 == 2) &&
       (monster.weaponP3 == 3))
  }
}
