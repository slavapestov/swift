let u = SIMD2<Float>(0, 1)
let v = SIMD2<Float>(1, 2)
let r: [SIMD2<Float>] = [
    2*u + 3*v,
    4*u + 5*v
]
print(r)
