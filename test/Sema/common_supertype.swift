// RUN: %target-typecheck-verify-swift

class A {}
class B: A {}
class C: B {}
class D: B {}
class E: D {}
class F {}

protocol P {}
protocol Q: P {}
protocol R: P {}
protocol S {}

extension E: P {}

extension Int: Q {}
extension String: R {}

struct Exactly<T: ~Copyable> {}

func test<T: ~Copyable>(_: consuming T, _: consuming T) -> Exactly<T> { fatalError() }

func testSimple(x: Int, y: String) {
  test(x, y) // expected-error {{conflicting arguments to generic parameter 'T' ('Int' vs. 'String')}}
}

func testClass1(x: C, y: E) -> Exactly<B> {
  let result = test(x, y)
  return result
}

func testClass2(x: E, y: F) {
  _ = test(x, y) // expected-error {{conflicting arguments to generic parameter 'T' ('E' vs. 'F')}}
}

func testMetatype1(x: C.Type, y: E.Type) -> Exactly<B.Type> {
  let result = test(x, y)
  return result
}

func testMetatype2(x: E.Type, y: F.Type) {
  _ = test(x, y) // expected-error {{conflicting arguments to generic parameter 'T' ('E.Type' vs. 'F.Type')}}
}

func testOptional1(x: E, y: C?) -> Exactly<B?> {
  let result = test(x, y)
  return result
}

func testOptional2(x: E?, y: C) -> Exactly<B?> {
  let result = test(x, y)
  return result
}

func testOptional3(x: E?, y: C?) -> Exactly<B?> {
  let result = test(x, y)
  return result
}

func testArray1(x: [E], y: [C]) -> Exactly<[B]> {
  let result = test(x, y)
  return result
}

func testTuple1(x: (E, C), y: (C, E)) -> Exactly<(B, B)> {
  let result = test(x, y)
  return result
}

func testFunction1(x: @escaping (E) -> C, y: @escaping (A) -> E) -> Exactly<(E) -> B> {
  let result = test(x, y)
  return result
}

func testFunction2(x: @escaping () -> Int, y: @escaping () -> String) {
  let _ = test(x, y)  // expected-error {{conflicting arguments to generic parameter 'T' ('() -> Int' vs. '() -> String')}}
}

func testExistential1(x: any Q, y: any R) -> Exactly<any P> {
  let result = test(x, y)
  return result
}

func testExistential2(x: any Q & D, y: any R & C) -> Exactly<any P & B> {
  let result = test(x, y)
  return result
}

func testExistential3(x: any Q & D, y: any R & F) -> Exactly<any P> {
  let result = test(x, y)
  return result
}

func testExistential4(x: any P & C, y: any S & D) -> Exactly<B> {
  let result = test(x, y)
  return result
}

func testExistential5(x: consuming any ~Copyable, y: any P) -> Exactly<any ~Copyable> {
  let result = test(x, y)
  return result
}

func testExistentialMetatype1(x: any Q.Type, y: any R.Type) -> Exactly<any P.Type> {
  let result = test(x, y)
  return result
}

func testAnyHashable1(x: Int, y: AnyHashable) -> Exactly<AnyHashable> {
  let result = test(x, y)
  return result
}

func testAnyHashable2(x: AnyObject, y: AnyHashable) {
  let result = test(x, y)  // expected-error {{argument type 'AnyHashable' expected to be an instance of a class or class-constrained type}}
}

func testAnyHashable3(x: Int?, y: AnyHashable) -> Exactly<AnyHashable> {
  let result = test(x, y)
  return result
}

func testAnyHashable4(x: AnyHashable?, y: AnyHashable) -> Exactly<AnyHashable?> {
  let result = test(x, y)
  return result
}

struct URLComponents {
  var queryItems: [URLQueryItem]?
}

struct URLQueryItem {
  var name: String
  var value: String?
}

extension String {
  var removingPercentEncoding: String? { fatalError() }
}

func moreComplexExample(_ urlComponents: URLComponents) -> [String: AnyHashable] {
  let result = urlComponents.queryItems?.reduce(into: [String: AnyHashable]()) { partialResult, queryItem in
      partialResult[queryItem.name] = queryItem.value?.removingPercentEncoding
  } ?? [:]

  return result
}
