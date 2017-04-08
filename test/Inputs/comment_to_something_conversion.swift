// This is an input file for comment-to-{XML,Doxygen} conversion tests.
//
// Please keep this file in alphabetical order!

@objc public class A000 {}
// CHECK: {{.*}}DocCommentAsXML=none

/// Aaa.  A010.  Bbb.
@objc public class A010_AttachToEntities {
// CHECK: {{.*}}DocCommentAsXML=[<Class file="{{.*}}" line="{{.*}}" column="{{.*}}"><Name>A010_AttachToEntities</Name><USR>c:objc(cs)A010_AttachToEntities</USR><Declaration>@objc public class A010_AttachToEntities</Declaration><Abstract><Para>Aaa.  A010.  Bbb.</Para></Abstract></Class>]

/// Aaa.  init().
@objc public init() {}
// CHECK: {{.*}}DocCommentAsXML=[<Function file="{{.*}}" line="{{.*}}" column="{{.*}}"><Name>init()</Name><USR>c:objc(cs)A010_AttachToEntities(im)init</USR><Declaration>@objc public init()</Declaration><Abstract><Para>Aaa.  init().</Para></Abstract></Function>]

/// Aaa.  subscript(i: Int).
@objc public subscript(i: Int) -> Int {
// CHECK: {{.*}}DocCommentAsXML=[<Other file="{{.*}}" line="{{.*}}" column="{{.*}}"><Name>subscript(_:)</Name><USR>s:14swift_ide_test21A010_AttachToEntitiesC9subscriptS2ici</USR><Declaration>@objc public subscript(i: Int) -&gt; Int { get set }</Declaration><Abstract><Para>Aaa.  subscript(i: Int).</Para></Abstract></Other>]
    get {
// CHECK: {{.*}}DocCommentAsXML=none
      return 0
    }
    set {}
// CHECK: {{.*}}DocCommentAsXML=none
  }
// CHECK: {{.*}}DocCommentAsXML=none

/// Aaa.  v1.
@objc public var v1: Int = 0
// CHECK: {{.*}}DocCommentAsXML=[<Other file="{{.*}}" line="{{.*}}" column="{{.*}}"><Name>v1</Name><USR>c:objc(cs)A010_AttachToEntities(py)v1</USR><Declaration>@objc public var v1: Int</Declaration><Abstract><Para>Aaa.  v1.</Para></Abstract></Other>]

/// Aaa.  v2.
@objc public class var v2: Int { return 0 }
// CHECK: {{.*}}DocCommentAsXML=[<Other file="{{.*}}" line="{{.*}}" column="{{.*}}"><Name>v2</Name><USR>c:objc(cs)A010_AttachToEntities(cpy)v2</USR><Declaration>@objc public class var v2: Int { get }</Declaration><Abstract><Para>Aaa.  v2.</Para></Abstract></Other>]
// CHECK: {{.*}}DocCommentAsXML=none
}

/// Aaa.  A011.
public struct A011_AttachToEntities {
}
// CHECK: {{.*}}DocCommentAsXML=[<Class file="{{.*}}" line="{{.*}}" column="{{.*}}"><Name>A011_AttachToEntities</Name><USR>s:14swift_ide_test21A011_AttachToEntitiesV</USR><Declaration>public struct A011_AttachToEntities</Declaration><Abstract><Para>Aaa.  A011.</Para></Abstract></Class>]

/// Aaa.  A012.
public enum A012_AttachToEntities {
  case A
}
// CHECK: {{.*}}DocCommentAsXML=[<Other file="{{.*}}" line="{{.*}}" column="{{.*}}"><Name>A012_AttachToEntities</Name><USR>s:14swift_ide_test21A012_AttachToEntitiesO</USR><Declaration>public enum A012_AttachToEntities</Declaration><Abstract><Para>Aaa.  A012.</Para></Abstract></Other>]
// CHECK: {{.*}}DocCommentAsXML=none

/// Aaa.  A013.
@objc public protocol A013_AttachToEntities {}
// CHECK: {{.*}}DocCommentAsXML=[<Class file="{{.*}}" line="{{.*}}" column="{{.*}}"><Name>A013_AttachToEntities</Name><USR>c:objc(pl)A013_AttachToEntities</USR><Declaration>@objc public protocol A013_AttachToEntities</Declaration><Abstract><Para>Aaa.  A013.</Para></Abstract></Class>]

@objc public class ATXHeaders {
// CHECK: {{.*}}DocCommentAsXML=none
  /// LEVEL ONE
  /// =========
  ///
  /// LEVEL TWO
  /// ---------
  @objc public func f0() {}
// CHECK: {{.*}}DocCommentAsXML=[<Function file="{{.*}}" line="{{.*}}" column="{{.*}}"><Name>f0()</Name><USR>c:objc(cs)ATXHeaders(im)f0</USR><Declaration>@objc public func f0()</Declaration><Discussion><rawHTML><![CDATA[<h1>]]></rawHTML>LEVEL ONE<rawHTML><![CDATA[</h1>]]></rawHTML><rawHTML><![CDATA[<h2>]]></rawHTML>LEVEL TWO<rawHTML><![CDATA[</h2>]]></rawHTML></Discussion></Function>]
}

@objc public class AutomaticLink {
// CHECK: {{.*}}DocCommentAsXML=none
  /// And now for a URL.
  ///
  /// <http://developer.apple.com/swift/>
  @objc public func f0() {}
// CHECK: {{.*}}DocCommentAsXML=[<Function file="{{.*}}" line="{{.*}}" column="{{.*}}"><Name>f0()</Name><USR>c:objc(cs)AutomaticLink(im)f0</USR><Declaration>@objc public func f0()</Declaration><Abstract><Para>And now for a URL.</Para></Abstract><Discussion><Para><Link href="http://developer.apple.com/swift/">http://developer.apple.com/swift/</Link></Para></Discussion></Function>]
}

@objc public class BlockQuote {
// CHECK: {{.*}}DocCommentAsXML=none
  /// Aaa.
  ///
  /// > Bbb.
  ///
  /// > Ccc.
  @objc public func f0() {}
// CHECK: {{.*}}DocCommentAsXML=[<Function file="{{.*}}" line="{{.*}}" column="{{.*}}"><Name>f0()</Name><USR>c:objc(cs)BlockQuote(im)f0</USR><Declaration>@objc public func f0()</Declaration><Abstract><Para>Aaa.</Para></Abstract><Discussion><Para>Bbb.</Para><Para>Ccc.</Para></Discussion></Function>]
}

@objc public class Brief {
// CHECK: {{.*}}DocCommentAsXML=none
  /// Aaa.
  @objc public func f0() {}
// CHECK: {{.*}}DocCommentAsXML=[<Function file="{{.*}}" line="{{.*}}" column="{{.*}}"><Name>f0()</Name><USR>c:objc(cs)Brief(im)f0</USR><Declaration>@objc public func f0()</Declaration><Abstract><Para>Aaa.</Para></Abstract></Function>]

  /// Aaa.
  ///
  /// Bbb.
  @objc public func f1() {}
// CHECK: {{.*}}DocCommentAsXML=[<Function file="{{.*}}" line="{{.*}}" column="{{.*}}"><Name>f1()</Name><USR>c:objc(cs)Brief(im)f1</USR><Declaration>@objc public func f1()</Declaration><Abstract><Para>Aaa.</Para></Abstract><Discussion><Para>Bbb.</Para></Discussion></Function>]

  /// Aaa.
  ///
  ///> Bbb.
  @objc public func f2() {}
// CHECK: {{.*}}DocCommentAsXML=[<Function file="{{.*}}" line="{{.*}}" column="{{.*}}"><Name>f2()</Name><USR>c:objc(cs)Brief(im)f2</USR><Declaration>@objc public func f2()</Declaration><Abstract><Para>Aaa.</Para></Abstract><Discussion><Para>Bbb.</Para></Discussion></Function>]

  /// Aaa.
  ///
  /// Bbb.
  @objc public func f3() {}
// CHECK: {{.*}}DocCommentAsXML=[<Function file="{{.*}}" line="{{.*}}" column="{{.*}}"><Name>f3()</Name><USR>c:objc(cs)Brief(im)f3</USR><Declaration>@objc public func f3()</Declaration><Abstract><Para>Aaa.</Para></Abstract><Discussion><Para>Bbb.</Para></Discussion></Function>]
}

@objc public class ClosingComments {
// CHECK: {{.*}}DocCommentAsXML=none
  /// Some comment. */
  @objc public func closingComment() {}
// CHECK: DocCommentAsXML=[<Function file="{{.*}}" line="{{.*}}" column="{{.*}}"><Name>closingComment()</Name><USR>c:objc(cs)ClosingComments(im)closingComment</USR><Declaration>@objc public func closingComment()</Declaration><Abstract><Para>Some comment. */</Para></Abstract></Function>]
}

@objc public class ClosureContainer {
/// Partially applies a binary operator.
///
/// - Parameter a: The left-hand side to partially apply.
/// - Parameter combine: A binary operator.
///   - Parameter lhs: The left-hand side of the operator
///   - Parameter rhs: The right-hand side of the operator
///   - Returns: A result.
///   - Throws: Nothing.
@objc public func closureParameterExplodedExploded(a: Int, combine: (_ lhs: Int, _ rhs: Int) -> Int) {}
// CHECK: DocCommentAsXML=[<Function file="{{.*}}" line="{{.*}}" column="{{.*}}"><Name>closureParameterExplodedExploded(a:combine:)</Name><USR>c:objc(cs)ClosureContainer(im)closureParameterExplodedExplodedWithA:combine:</USR><Declaration>@objc public func closureParameterExplodedExploded(a: Int, combine: (_ lhs: Int, _ rhs: Int) -&gt; Int)</Declaration><Abstract><Para>Partially applies a binary operator.</Para></Abstract><Parameters><Parameter><Name>a</Name><Direction isExplicit="0">in</Direction><Discussion><Para>The left-hand side to partially apply.</Para></Discussion></Parameter><Parameter><Name>combine</Name><Direction isExplicit="0">in</Direction><ClosureParameter><Abstract><Para>A binary operator.</Para></Abstract><Parameters><Parameter><Name>lhs</Name><Direction isExplicit="0">in</Direction><Discussion><Para>The left-hand side of the operator</Para></Discussion></Parameter><Parameter><Name>rhs</Name><Direction isExplicit="0">in</Direction><Discussion><Para>The right-hand side of the operator</Para></Discussion></Parameter></Parameters><ResultDiscussion><Para>A result.</Para></ResultDiscussion><ThrowsDiscussion><Para>Nothing.</Para></ThrowsDiscussion></ClosureParameter></Parameter></Parameters></Function>]

/// Partially applies a binary operator.
///
/// - Parameters:
///   - a: The left-hand side to partially apply.
///   - combine: A binary operator.
///     - Parameter lhs: The left-hand side of the operator
///     - Parameter rhs: The right-hand side of the operator
///     - Returns: A result.
///     - Throws: Nothing.
@objc public func closureParameterOutlineExploded(a: Int, combine: (_ lhs: Int, _ rhs: Int) -> Int) {}
// CHECK: DocCommentAsXML=[<Function file="{{.*}}" line="{{.*}}" column="{{.*}}"><Name>closureParameterOutlineExploded(a:combine:)</Name><USR>c:objc(cs)ClosureContainer(im)closureParameterOutlineExplodedWithA:combine:</USR><Declaration>@objc public func closureParameterOutlineExploded(a: Int, combine: (_ lhs: Int, _ rhs: Int) -&gt; Int)</Declaration><Abstract><Para>Partially applies a binary operator.</Para></Abstract><Parameters><Parameter><Name>a</Name><Direction isExplicit="0">in</Direction><Discussion><Para>The left-hand side to partially apply.</Para></Discussion></Parameter><Parameter><Name>combine</Name><Direction isExplicit="0">in</Direction><ClosureParameter><Abstract><Para>A binary operator.</Para></Abstract><Parameters><Parameter><Name>lhs</Name><Direction isExplicit="0">in</Direction><Discussion><Para>The left-hand side of the operator</Para></Discussion></Parameter><Parameter><Name>rhs</Name><Direction isExplicit="0">in</Direction><Discussion><Para>The right-hand side of the operator</Para></Discussion></Parameter></Parameters><ResultDiscussion><Para>A result.</Para></ResultDiscussion><ThrowsDiscussion><Para>Nothing.</Para></ThrowsDiscussion></ClosureParameter></Parameter></Parameters></Function>]

/// Partially applies a binary operator.
///
/// - Parameters:
///   - a: The left-hand side to partially apply.
///   - combine: A binary operator.
///     - Parameters:
///       - lhs: The left-hand side of the operator
///       - rhs: The right-hand side of the operator
///     - Returns: A result.
///     - Throws: Nothing.
@objc public func closureParameterOutlineOutline(a: Int, combine: (_ lhs: Int, _ rhs: Int) -> Int) {}
// CHECK: DocCommentAsXML=[<Function file="{{.*}}" line="{{.*}}" column="{{.*}}"><Name>closureParameterOutlineOutline(a:combine:)</Name><USR>c:objc(cs)ClosureContainer(im)closureParameterOutlineOutlineWithA:combine:</USR><Declaration>@objc public func closureParameterOutlineOutline(a: Int, combine: (_ lhs: Int, _ rhs: Int) -&gt; Int)</Declaration><Abstract><Para>Partially applies a binary operator.</Para></Abstract><Parameters><Parameter><Name>a</Name><Direction isExplicit="0">in</Direction><Discussion><Para>The left-hand side to partially apply.</Para></Discussion></Parameter><Parameter><Name>combine</Name><Direction isExplicit="0">in</Direction><ClosureParameter><Abstract><Para>A binary operator.</Para></Abstract><Parameters><Parameter><Name>lhs</Name><Direction isExplicit="0">in</Direction><Discussion><Para>The left-hand side of the operator</Para></Discussion></Parameter><Parameter><Name>rhs</Name><Direction isExplicit="0">in</Direction><Discussion><Para>The right-hand side of the operator</Para></Discussion></Parameter></Parameters><ResultDiscussion><Para>A result.</Para></ResultDiscussion><ThrowsDiscussion><Para>Nothing.</Para></ThrowsDiscussion></ClosureParameter></Parameter></Parameters></Function>]
}

@objc public class CodeBlock {
// CHECK: {{.*}}DocCommentAsXML=none
  /// This is how you use this code.
  ///
  ///     f0() // WOW!
  ///     f0() // WOW!
  ///     f0() // WOW!
  @objc public func f0() {}
// CHECK: DocCommentAsXML=[<Function file="{{.*}}" line="{{.*}}" column="{{.*}}"><Name>f0()</Name><USR>c:objc(cs)CodeBlock(im)f0</USR><Declaration>@objc public func f0()</Declaration><Abstract><Para>This is how you use this code.</Para></Abstract><Discussion><CodeListing language="swift"><zCodeLineNumbered><![CDATA[f0() // WOW!]]></zCodeLineNumbered><zCodeLineNumbered><![CDATA[f0() // WOW!]]></zCodeLineNumbered><zCodeLineNumbered><![CDATA[f0() // WOW!]]></zCodeLineNumbered><zCodeLineNumbered></zCodeLineNumbered></CodeListing></Discussion></Function>]
}

@objc public class Emphasis {
// CHECK: {{.*}}DocCommentAsXML=none
  /// Aaa *bbb* ccc.
  /// Aaa _bbb_ ccc.
  @objc public func f0() {}
// CHECK: {{.*}}DocCommentAsXML=[<Function file="{{.*}}" line="{{.*}}" column="{{.*}}"><Name>f0()</Name><USR>c:objc(cs)Emphasis(im)f0</USR><Declaration>@objc public func f0()</Declaration><Abstract><Para>Aaa <emphasis>bbb</emphasis> ccc. Aaa <emphasis>bbb</emphasis> ccc.</Para></Abstract></Function>]
}

@objc public class EmptyComments {
// CHECK: {{.*}}DocCommentAsXML=none

  ///
  @objc public func f0() {}
// CHECK: {{.*}}DocCommentAsXML=[<Function file="{{.*}}" line="{{.*}}" column="{{.*}}"><Name>f0()</Name><USR>c:objc(cs)EmptyComments(im)f0</USR><Declaration>@objc public func f0()</Declaration></Function>]

  /// Aaa.
  @objc public func f1() {}
// CHECK: {{.*}}DocCommentAsXML=[<Function file="{{.*}}" line="{{.*}}" column="{{.*}}"><Name>f1()</Name><USR>c:objc(cs)EmptyComments(im)f1</USR><Declaration>@objc public func f1()</Declaration><Abstract><Para>Aaa.</Para></Abstract></Function>]

  /** */
  @objc public func f2() {}
// CHECK: {{.*}}DocCommentAsXML=[<Function file="{{.*}}" line="{{.*}}" column="{{.*}}"><Name>f2()</Name><USR>c:objc(cs)EmptyComments(im)f2</USR><Declaration>@objc public func f2()</Declaration></Function>]

  /**
   */
  @objc public func f3() {}
// CHECK: {{.*}}DocCommentAsXML=[<Function file="{{.*}}" line="{{.*}}" column="{{.*}}"><Name>f3()</Name><USR>c:objc(cs)EmptyComments(im)f3</USR><Declaration>@objc public func f3()</Declaration></Function>]

  /**
   * Aaa.
   */
  @objc public func f4() {}
// CHECK: {{.*}}DocCommentAsXML=[<Function file="{{.*}}" line="{{.*}}" column="{{.*}}"><Name>f4()</Name><USR>c:objc(cs)EmptyComments(im)f4</USR><Declaration>@objc public func f4()</Declaration><Abstract><Para>Aaa.</Para></Abstract></Function>]
}

@objc public class HasThrowingFunction {
// CHECK: {{.*}}DocCommentAsXML=none

  /// Might throw something.
  ///
  /// - parameter x: A number
  /// - throws: An error if `x == 0`
  @objc public func f1(_ x: Int) /*throws*/ {}
// CHECK: {{.*}}DocCommentAsXML=[<Function file="{{.*}}" line="{{.*}}" column="{{.*}}"><Name>f1(_:)</Name><USR>c:objc(cs)HasThrowingFunction(im)f1:</USR><Declaration>@objc public func f1(_ x: Int)</Declaration><Abstract><Para>Might throw something.</Para></Abstract><Parameters><Parameter><Name>x</Name><Direction isExplicit="0">in</Direction><Discussion><Para>A number</Para></Discussion></Parameter></Parameters><ThrowsDiscussion><Para>An error if <codeVoice>x == 0</codeVoice></Para></ThrowsDiscussion></Function>]
}

@objc public class HorizontalRules {
// CHECK: {{.*}}DocCommentAsXML=none
  /// Briefly.
  ///
  /// ------------------------------------
  ///
  /// The end.
  @objc public func f0() {}
// CHECK: {{.*}}DocCommentAsXML=[<Function file="{{.*}}" line="{{.*}}" column="{{.*}}"><Name>f0()</Name><USR>c:objc(cs)HorizontalRules(im)f0</USR><Declaration>@objc public func f0()</Declaration><Abstract><Para>Briefly.</Para></Abstract><Discussion><rawHTML><![CDATA[<hr/>]]></rawHTML><Para>The end.</Para></Discussion></Function>]
}

@objc public class ImplicitNameLink {
// CHECK: {{.*}}DocCommentAsXML=none
  /// [Apple][]
  ///
  /// [Apple]: https://www.apple.com/
  @objc public func f0() {}
}

@objc public class IndentedBlockComment {
// CHECK: {{.*}}DocCommentAsXML=none
  /**
      Brief.

      First paragraph line.
      Second paragraph line.

      Now for a code sample:

          var x = 1
          // var y = 2
          var z = 3
  */
  @objc public func f1() {}
// CHECK: DocCommentAsXML=[<Function file="{{.*}}" line="{{.*}}" column="{{.*}}"><Name>f1()</Name><USR>c:objc(cs)IndentedBlockComment(im)f1</USR><Declaration>@objc public func f1()</Declaration><Abstract><Para>Brief.</Para></Abstract><Discussion><Para>First paragraph line. Second paragraph line.</Para><Para>Now for a code sample:</Para><CodeListing language="swift"><zCodeLineNumbered><![CDATA[var x = 1]]></zCodeLineNumbered><zCodeLineNumbered><![CDATA[// var y = 2]]></zCodeLineNumbered><zCodeLineNumbered><![CDATA[var z = 3]]></zCodeLineNumbered><zCodeLineNumbered></zCodeLineNumbered></CodeListing></Discussion></Function>]
  /**
                        Hugely indented brief.

                        First paragraph line.
                        Second paragraph line.

                        Now for a code sample:

                            var x = 1
                            // var y = 2
                            var z = 3
  */
  @objc public func f2() {}
// CHECK: {{.*}}DocCommentAsXML=[<Function file="{{.*}}" line="{{.*}}" column="{{.*}}"><Name>f2()</Name><USR>c:objc(cs)IndentedBlockComment(im)f2</USR><Declaration>@objc public func f2()</Declaration><Abstract><Para>Hugely indented brief.</Para></Abstract><Discussion><Para>First paragraph line. Second paragraph line.</Para><Para>Now for a code sample:</Para><CodeListing language="swift"><zCodeLineNumbered><![CDATA[var x = 1]]></zCodeLineNumbered><zCodeLineNumbered><![CDATA[// var y = 2]]></zCodeLineNumbered><zCodeLineNumbered><![CDATA[var z = 3]]></zCodeLineNumbered><zCodeLineNumbered></zCodeLineNumbered></CodeListing></Discussion></Function>]
}

@objc public class InlineCode {
// CHECK: {{.*}}DocCommentAsXML=none
  /// Aaa `bbb` ccc.
  @objc public func f0() {}
// CHECK: {{.*}}DocCommentAsXML=[<Function file="{{.*}}" line="{{.*}}" column="{{.*}}"><Name>f0()</Name><USR>c:objc(cs)InlineCode(im)f0</USR><Declaration>@objc public func f0()</Declaration><Abstract><Para>Aaa <codeVoice>bbb</codeVoice> ccc.</Para></Abstract></Function>]
}

@objc public class InlineLink {
// CHECK: {{.*}}DocCommentAsXML=none
/// Aaa [bbb](/path/to/something) ccc.
@objc public func f0() {}
// CHECK: {{.*}}DocCommentAsXML=[<Function file="{{.*}}" line="{{.*}}" column="{{.*}}"><Name>f0()</Name><USR>c:objc(cs)InlineLink(im)f0</USR><Declaration>@objc public func f0()</Declaration><Abstract><Para>Aaa <Link href="/path/to/something">bbb</Link> ccc.</Para></Abstract></Function>]
}

@objc public class MultiLineBrief {
// CHECK: {{.*}}DocCommentAsXML=none

  /// Brief first line.
  /// Brief after softbreak.
  ///
  /// Some paragraph text.
  @objc public func f0() {}
// CHECK: {{.*}}DocCommentAsXML=[<Function file="{{.*}}" line="{{.*}}" column="{{.*}}"><Name>f0()</Name><USR>c:objc(cs)MultiLineBrief(im)f0</USR><Declaration>@objc public func f0()</Declaration><Abstract><Para>Brief first line. Brief after softbreak.</Para></Abstract><Discussion><Para>Some paragraph text.</Para></Discussion></Function>]
}

@objc public class OrderedList {
// CHECK: {{.*}}DocCommentAsXML=none
/// 1. Aaa.
///
/// 2. Bbb.
///    Ccc.
@objc public func f0() {}
// CHECK: {{.*}}DocCommentAsXML=[<Function file="{{.*}}" line="{{.*}}" column="{{.*}}"><Name>f0()</Name><USR>c:objc(cs)OrderedList(im)f0</USR><Declaration>@objc public func f0()</Declaration><Discussion><List-Number><Item><Para>Aaa.</Para></Item><Item><Para>Bbb. Ccc.</Para></Item></List-Number></Discussion></Function>]
}

/// - parameter x: A number
@objc public class ParamAndReturns {
// CHECK: {{.*}}DocCommentAsXML=[<Class file="{{.*}}" line="{{.*}}" column="{{.*}}"><Name>ParamAndReturns</Name><USR>c:objc(cs)ParamAndReturns</USR><Declaration>@objc public class ParamAndReturns</Declaration><Parameters><Parameter><Name>x</Name><Direction isExplicit="0">in</Direction><Discussion><Para>A number</Para></Discussion></Parameter></Parameters></Class>]
/// Aaa.  f0.
///
/// - parameter first: Bbb.
///
/// - parameter second: Ccc.  Ddd.
///   Eee.
@objc public func f0(_ first: Int, second: Double) {}
// CHECK: {{.*}}DocCommentAsXML=[<Function file="{{.*}}" line="{{.*}}" column="{{.*}}"><Name>f0(_:second:)</Name><USR>c:objc(cs)ParamAndReturns(im)f0:second:</USR><Declaration>@objc public func f0(_ first: Int, second: Double)</Declaration><Abstract><Para>Aaa.  f0.</Para></Abstract><Parameters><Parameter><Name>first</Name><Direction isExplicit="0">in</Direction><Discussion><Para>Bbb.</Para></Discussion></Parameter><Parameter><Name>second</Name><Direction isExplicit="0">in</Direction><Discussion><Para>Ccc.  Ddd. Eee.</Para></Discussion></Parameter></Parameters></Function>]
// CHECK: {{.*}}DocCommentAsXML=none
// CHECK: {{.*}}DocCommentAsXML=none

/// Aaa.  f1.
///
/// - parameter first: Bbb.
///
/// - returns: Ccc.
///   Ddd.
@objc public func f1(_ first: Int) {}
// CHECK: {{.*}}DocCommentAsXML=[<Function file="{{.*}}" line="{{.*}}" column="{{.*}}"><Name>f1(_:)</Name><USR>c:objc(cs)ParamAndReturns(im)f1:</USR><Declaration>@objc public func f1(_ first: Int)</Declaration><Abstract><Para>Aaa.  f1.</Para></Abstract><Parameters><Parameter><Name>first</Name><Direction isExplicit="0">in</Direction><Discussion><Para>Bbb.</Para></Discussion></Parameter></Parameters><ResultDiscussion><Para>Ccc. Ddd.</Para></ResultDiscussion></Function>]
// CHECK: {{.*}}DocCommentAsXML=none

/// Aaa.  f2.
///
/// - parameter first:
///
/// - parameter second: Aaa.
///
/// - parameter third:
///   Bbb.
@objc public func f2(_ first: Int, second: Double, third: Float) {}
// CHECK: {{.*}}DocCommentAsXML=[<Function file="{{.*}}" line="{{.*}}" column="{{.*}}"><Name>f2(_:second:third:)</Name><USR>c:objc(cs)ParamAndReturns(im)f2:second:third:</USR><Declaration>@objc public func f2(_ first: Int, second: Double, third: Float)</Declaration><Abstract><Para>Aaa.  f2.</Para></Abstract><Parameters><Parameter><Name>first</Name><Direction isExplicit="0">in</Direction><Discussion><Para></Para></Discussion></Parameter><Parameter><Name>second</Name><Direction isExplicit="0">in</Direction><Discussion><Para>Aaa.</Para></Discussion></Parameter><Parameter><Name>third</Name><Direction isExplicit="0">in</Direction><Discussion><Para> Bbb.</Para></Discussion></Parameter></Parameters></Function>]
// CHECK: {{.*}}DocCommentAsXML=none
// CHECK: {{.*}}DocCommentAsXML=none
// CHECK: {{.*}}DocCommentAsXML=none

/// Aaa.  f3.
///
/// - parameter first: Bbb.
/// - parameter second: Ccc.
/// - parameter third: Ddd.
@objc public func f3(_ first: Int, second: Double, third: Float) {}
// CHECK: {{.*}}DocCommentAsXML=[<Function file="{{.*}}" line="{{.*}}" column="{{.*}}"><Name>f3(_:second:third:)</Name><USR>c:objc(cs)ParamAndReturns(im)f3:second:third:</USR><Declaration>@objc public func f3(_ first: Int, second: Double, third: Float)</Declaration><Abstract><Para>Aaa.  f3.</Para></Abstract><Parameters><Parameter><Name>first</Name><Direction isExplicit="0">in</Direction><Discussion><Para>Bbb.</Para></Discussion></Parameter><Parameter><Name>second</Name><Direction isExplicit="0">in</Direction><Discussion><Para>Ccc.</Para></Discussion></Parameter><Parameter><Name>third</Name><Direction isExplicit="0">in</Direction><Discussion><Para>Ddd.</Para></Discussion></Parameter></Parameters></Function>]
// CHECK: {{.*}}DocCommentAsXML=none
// CHECK: {{.*}}DocCommentAsXML=none
// CHECK: {{.*}}DocCommentAsXML=none


/// Aaa.  f4.
///
/// - returns: Ccc.
///   Ddd.
///
/// - returns: Eee.
///   Fff.
@objc public func f4() {}
// CHECK: {{.*}}DocCommentAsXML=[<Function file="{{.*}}" line="{{.*}}" column="{{.*}}"><Name>f4()</Name><USR>c:objc(cs)ParamAndReturns(im)f4</USR><Declaration>@objc public func f4()</Declaration><Abstract><Para>Aaa.  f4.</Para></Abstract><ResultDiscussion><Para>Eee. Fff.</Para></ResultDiscussion></Function>]
}

@objc public class ParameterOutline{
// CHECK: {{.*}}DocCommentAsXML=none
/// - Parameters:
///   - x: A number
///   - y: A number
///
/// - PARAMETERS:
///   - z: A number
@objc public func f0(_ x: Int, y: Int, z: Int) {}
// CHECK: {{.*}}DocCommentAsXML=[<Function file="{{.*}}" line="{{.*}}" column="{{.*}}"><Name>f0(_:y:z:)</Name><USR>c:objc(cs)ParameterOutline(im)f0:y:z:</USR><Declaration>@objc public func f0(_ x: Int, y: Int, z: Int)</Declaration><Parameters><Parameter><Name>x</Name><Direction isExplicit="0">in</Direction><Discussion><Para>A number</Para></Discussion></Parameter><Parameter><Name>y</Name><Direction isExplicit="0">in</Direction><Discussion><Para>A number</Para></Discussion></Parameter><Parameter><Name>z</Name><Direction isExplicit="0">in</Direction><Discussion><Para>A number</Para></Discussion></Parameter></Parameters></Function>]
// CHECK: {{.*}}DocCommentAsXML=none
// CHECK: {{.*}}DocCommentAsXML=none
// CHECK: {{.*}}DocCommentAsXML=none
}

@objc public class ParameterOutlineMiddle {
// CHECK: {{.*}}DocCommentAsXML=none
/// - This line should remain.
/// - Parameters:
///   - x: A number
///   - y: A number
/// - This line should also remain.
/// - parameter z: A number
@objc public func f0(_ x: Int, y: Int, z: Int) {}
// CHECK: {{.*}}DocCommentAsXML=[<Function file="{{.*}}" line="{{.*}}" column="{{.*}}"><Name>f0(_:y:z:)</Name><USR>c:objc(cs)ParameterOutlineMiddle(im)f0:y:z:</USR><Declaration>@objc public func f0(_ x: Int, y: Int, z: Int)</Declaration><Parameters><Parameter><Name>x</Name><Direction isExplicit="0">in</Direction><Discussion><Para>A number</Para></Discussion></Parameter><Parameter><Name>y</Name><Direction isExplicit="0">in</Direction><Discussion><Para>A number</Para></Discussion></Parameter><Parameter><Name>z</Name><Direction isExplicit="0">in</Direction><Discussion><Para>A number</Para></Discussion></Parameter></Parameters><Discussion><List-Bullet><Item><Para>This line should remain.</Para></Item><Item><Para>This line should also remain.</Para></Item></List-Bullet></Discussion></Function>]
// CHECK: {{.*}}DocCommentAsXML=none
// CHECK: {{.*}}DocCommentAsXML=none
// CHECK: {{.*}}DocCommentAsXML=none
}

@objc public class ReferenceLink {
// CHECK: {{.*}}DocCommentAsXML=none
  /// This is [a reference link] [1].
  ///
  /// [1]: http://developer.apple.com/
}


@objc public class Returns {
// CHECK: {{.*}}DocCommentAsXML=none
  /// - returns: A number
  @objc public func f0() -> Int {
    return 0
  }
// CHECK: {{.*}}DocCommentAsXML=[<Function file="{{.*}}" line="{{.*}}" column="{{.*}}"><Name>f0()</Name><USR>c:objc(cs)Returns(im)f0</USR><Declaration>@objc public func f0() -&gt; Int</Declaration><ResultDiscussion><Para>A number</Para></ResultDiscussion></Function>]
}

@objc public class SeparateParameters {
// CHECK: {{.*}}DocCommentAsXML=none
  /// - Parameter x: A number
  @objc public func f0(_ x: Int, y: Int) {}
// CHECK: DocCommentAsXML=[<Function file="{{.*}}" line="{{.*}}" column="{{.*}}"><Name>f0(_:y:)</Name><USR>c:objc(cs)SeparateParameters(im)f0:y:</USR><Declaration>@objc public func f0(_ x: Int, y: Int)</Declaration><Parameters><Parameter><Name>x</Name><Direction isExplicit="0">in</Direction><Discussion><Para>A number</Para></Discussion></Parameter></Parameters></Function>]
// CHECK: {{.*}}DocCommentAsXML=none
// CHECK: {{.*}}DocCommentAsXML=none
}

@objc public class SetextHeaders {
// CHECK: {{.*}}DocCommentAsXML=none
  /// # LEVEL ONE
  ///
  /// ## LEVEL TWO
  ///
  /// ### LEVEL THREE
  ///
  /// #### LEVEL FOUR
  ///
  /// ##### LEVEL FIVE
  ///
  /// ##### LEVEL SIX
  @objc public func f0() {}
// CHECK: {{.*}}DocCommentAsXML=[<Function file="{{.*}}" line="{{.*}}" column="{{.*}}"><Name>f0()</Name><USR>c:objc(cs)SetextHeaders(im)f0</USR><Declaration>@objc public func f0()</Declaration><Discussion><rawHTML><![CDATA[<h1>]]></rawHTML>LEVEL ONE<rawHTML><![CDATA[</h1>]]></rawHTML><rawHTML><![CDATA[<h2>]]></rawHTML>LEVEL TWO<rawHTML><![CDATA[</h2>]]></rawHTML><rawHTML><![CDATA[<h3>]]></rawHTML>LEVEL THREE<rawHTML><![CDATA[</h3>]]></rawHTML><rawHTML><![CDATA[<h4>]]></rawHTML>LEVEL FOUR<rawHTML><![CDATA[</h4>]]></rawHTML><rawHTML><![CDATA[<h5>]]></rawHTML>LEVEL FIVE<rawHTML><![CDATA[</h5>]]></rawHTML><rawHTML><![CDATA[<h5>]]></rawHTML>LEVEL SIX<rawHTML><![CDATA[</h5>]]></rawHTML></Discussion></Function>]
}

@objc public class StrongEmphasis {
// CHECK: {{.*}}DocCommentAsXML=none
  /// Aaa **bbb** ccc.
  /// Aaa __bbb__ ccc.
  @objc public func f0() {}
// CHECK: {{.*}}DocCommentAsXML=[<Function file="{{.*}}" line="{{.*}}" column="{{.*}}"><Name>f0()</Name><USR>c:objc(cs)StrongEmphasis(im)f0</USR><Declaration>@objc public func f0()</Declaration><Abstract><Para>Aaa <bold>bbb</bold> ccc. Aaa <bold>bbb</bold> ccc.</Para></Abstract></Function>]
}

@objc public class UnorderedList {
// CHECK: {{.*}}DocCommentAsXML=none
  /// * Aaa.
  ///
  /// * Bbb.
  ///   Ccc.
  ///
  /// - Ddd.
  /// - Eee.
  ///   - Fff.
  @objc public func f0() {}
// CHECK: {{.*}}DocCommentAsXML=[<Function file="{{.*}}" line="{{.*}}" column="{{.*}}"><Name>f0()</Name><USR>c:objc(cs)UnorderedList(im)f0</USR><Declaration>@objc public func f0()</Declaration><Discussion><List-Bullet><Item><Para>Aaa.</Para></Item><Item><Para>Bbb. Ccc.</Para></Item></List-Bullet><List-Bullet><Item><Para>Ddd.</Para></Item><Item><Para>Eee.</Para><List-Bullet><Item><Para>Fff.</Para></Item></List-Bullet></Item></List-Bullet></Discussion></Function>]
}

/// Brief.
///
/// ```
/// thisIsASwiftCodeExample()
/// ```
public func codeListingWithDefaultLanguage() {}
// CHECK: DocCommentAsXML=[<Function file="{{.*}} line="{{.*}}" column="{{.*}}"><Name>codeListingWithDefaultLanguage()</Name><USR>s:14swift_ide_test30codeListingWithDefaultLanguageyyF</USR><Declaration>public func codeListingWithDefaultLanguage()</Declaration><Abstract><Para>Brief.</Para></Abstract><Discussion><CodeListing language="swift"><zCodeLineNumbered><![CDATA[thisIsASwiftCodeExample()]]></zCodeLineNumbered><zCodeLineNumbered></zCodeLineNumbered></CodeListing></Discussion></Function>] CommentXMLValid


/// Brief.
///
/// ```c++
/// Something::Something::create();
/// ```
public func codeListingWithOtherLanguage() {}
// CHECK: DocCommentAsXML=[<Function file="{{.*}}" line="{{.*}}" column="{{.*}}"><Name>codeListingWithOtherLanguage()</Name><USR>s:14swift_ide_test28codeListingWithOtherLanguageyyF</USR><Declaration>public func codeListingWithOtherLanguage()</Declaration><Abstract><Para>Brief.</Para></Abstract><Discussion><CodeListing language="c++"><zCodeLineNumbered><![CDATA[Something::Something::create();]]></zCodeLineNumbered><zCodeLineNumbered></zCodeLineNumbered></CodeListing></Discussion></Function>]

/// Brief.
///
/// - LocalizationKey: ABC
public func localizationKeyShouldNotAppearInDocComments() {}
// CHECK: DocCommentAsXML=[<Function file="{{.*}}" line="{{.*}}" column="{{.*}}"><Name>localizationKeyShouldNotAppearInDocComments()</Name><USR>s:14swift_ide_test43localizationKeyShouldNotAppearInDocCommentsyyF</USR><Declaration>public func localizationKeyShouldNotAppearInDocComments()</Declaration><Abstract><Para>Brief.</Para></Abstract></Function>]

/// - LocalizationKey: ABC
public func localizationKeyShouldNotAppearInDocComments2() {}
// CHECK: DocCommentAsXML=[<Function file="{{.*}}" line="{{.*}}" column="{{.*}}"><Name>localizationKeyShouldNotAppearInDocComments2()</Name><USR>s:14swift_ide_test44localizationKeyShouldNotAppearInDocComments2yyF</USR><Declaration>public func localizationKeyShouldNotAppearInDocComments2()</Declaration></Function>]
