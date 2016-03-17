
public protocol OtherResilientProtocol {
}

var x: Int = 0

extension OtherResilientProtocol {
  public var propertyInExtension: Int {
    get { return x }
    set { x = newValue }
  }

  public static var staticPropertyInExtension: Int {
    get { return x }
    set { x = newValue }
  }
}

public protocol OtherResilientRefinedProtocol : OtherResilientProtocol {
  var propertyInExtension: Int { get set }
  static var staticPropertyInExtension: Int { get set }
}
