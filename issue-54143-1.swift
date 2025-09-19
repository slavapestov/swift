// https://github.com/swiftlang/swift/issues/54143

import UIKit

class SomeViewController: UIViewController {
    private func updatePreferredContentSize() {
        preferredContentSize = CGSize(
            width: view.bounds.width,
            height: (view.subviews.map { $0.frame.height }.reduce(+) ?? 0) + 20.0
        )
    }
}
