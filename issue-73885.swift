// https://github.com/swiftlang/swift/issues/73885

enum Dimension: String {
    case dim1
    case dim2
    case dim3
    case dim4
    case dim5
    case dim6
    case dim7
    case dim8
    case dim9
    case dim10
}

struct Event {
    let properties: [Dimension: String]
    let foo: Int
    let bar: String

    init(properties: [Dimension: String] = [:], foo: Int? = nil) {
        self.init(properties: properties, foo: foo, bar: "bar")
    }

    init(properties: [Dimension: String], foo: Int?, bar: String) {
        self.properties = properties
        self.foo = foo ?? 0
        self.bar = bar
    }
}

struct Foo {
    let bar: Bar?
    let baz: Int?
}

struct Bar {
    let str: String
    let bar: String?
    let baz: Int
    let bad: Double?
    let isBar: Bool?
}

extension Event {
    static func test1(foo: Foo) -> Event {
        .init(properties: [
            .dim1: foo.bar?.bar,
            .dim2: foo.bar?.baz,
            .dim3: foo.bar?.bad,
            .dim4: foo.bar?.isBar,
            .dim5: foo.bar?.bar,
            .dim6: foo.bar?.baz,
            .dim7: foo.bar?.bad,
            // comment out the next 3 lines to get a reasonable compiler error
            .dim8: foo.bar?.isBar,
            .dim9: foo.bar?.bar,
            .dim10: foo.bar.str
            // leaving these lines in appears to throw the compiler into an infinite loop,
            // at least one core is 100% pegged by swift-frontend and no apparent progress
            // is made
        ].compactMapValues { $0 })
    }

    // for funsies, duplicate the test1 method to get even longer compile times.
}
