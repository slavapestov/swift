// https://github.com/swiftlang/swift/issues/54018

typealias vec2 = SIMD2<Float>
struct Quadratic {
 let p0 : vec2
 let p1 : vec2
 let p2 : vec2
 init(_ p0:vec2, _ p1:vec2, _ p2:vec2) {
   self.p0 = p0
   self.p1 = p1
   self.p2 = p2
 }
 func eval(_ t:Float) -> vec2 {
  let mt : Float = 1.0 - t;
  let mtmt : Float = mt * mt
 
  // replace p0*mt*mt with p0mtmt and it succeeds but (subjectively) takes
  // a long time to compile 25 lines of code. 2s vs 0.2s when returning a literal
  //let p0mtmt : vec2 = p0 * mtmt
  return p0*mt*mt + (p1 * (2.0*mt) + p2 * t)
 }
}
