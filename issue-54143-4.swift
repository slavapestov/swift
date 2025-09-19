// https://github.com/swiftlang/swift/issues/54143

extension Array {
    public func reduce(_ nextPartialResult: (Element, Element) -> Element) -> Element? {
        switch self.count {
        case 0: return nil
        case 1: return self.first!
        default:
            let first = self.first!
            let a = Array(self.dropFirst())
            return a.reduce(first, nextPartialResult)
        }
    }
}
