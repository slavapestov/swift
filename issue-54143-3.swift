// https://github.com/swiftlang/swift/issues/54143

import Foundation
import CoreGraphics

enum PageCalculator {
    static func page(offset: CGPoint, bounds: CGRect, contentSize: CGSize) -> Int {
        let pageWidth = bounds.width
        guard pageWidth != 0 else { return 0 }
        let offsetRatio = offset.x/pageWidth
        return Int(offsetRatio.rounded(.down))
    }
}
