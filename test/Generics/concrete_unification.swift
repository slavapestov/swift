protocol P1 {
  associatedtype T
  associatedtype U where T == Array<U>
}

protocol P2 {
  associatedtype T where T == Array<Int>
}

protocol P3 {
  associatedtype T
  associatedtype U
}

func f1<T>(_: T) where T : P1, T : P2 {}
func f2<T>(_: T) where T : P2, T : P1 {}

func f3<T>(_: T) where T : P1, T.T == Array<Int> {} // -- ok
func f4<T>(_: T) where T : P1, T.T == Array<T.U> {}

func f5<T>(_: T) where T : P2, T.T == Array<Int> {}
func f6<T>(_: T) where T : P3, T : P2, T.T == Array<T.U> {} // -- ok

func f7<T>(_: T) where T : P3, T.T == Array<Int>, T : P1 {} // -- dropped requirement
func f8<T>(_: T) where T : P3, T.T == Array<Int>, T : P2 {}

func f9<T>(_: T) where T : P3, T.T == Array<T.U>, T : P1 {}
func f10<T>(_: T) where T : P3, T.T == Array<T.U>, T : P2 {} // -- dropped requirement

func f11<T>(_: T) where T : P1, T.T == Array<Int>, T : P2 {} // -- extra requirement
func f12<T>(_: T) where T : P1, T.T == Array<T.U>, T : P2 {}

func f13<T>(_: T) where T : P2, T.T == Array<Int>, T : P1 {}
func f14<T>(_: T) where T : P2, T.T == Array<T.U>, T : P1 {}

/*
sil hidden [ossa] @$s4conc2f1yyxAA2P1RzAA2P2RzlF : $@convention(thin) <T where T : P1, T : P2> (@in_guaranteed T) -> () {
sil hidden [ossa] @$s4conc2f2yyxAA2P1RzAA2P2RzlF : $@convention(thin) <T where T : P1, T : P2> (@in_guaranteed T) -> () {
sil hidden [ossa] @$s4conc2f3yyxAA2P1RzSi1URtzlF : $@convention(thin) <T where T : P1, T.U == Int> (@in_guaranteed T) -> () {
sil hidden [ossa] @$s4conc2f4yyxAA2P1RzlF : $@convention(thin) <T where T : P1> (@in_guaranteed T) -> () {
sil hidden [ossa] @$s4conc2f5yyxAA2P2RzlF : $@convention(thin) <T where T : P2> (@in_guaranteed T) -> () {
sil hidden [ossa] @$s4conc2f6yyxAA2P2RzAA2P3RzSi1UAaDPRtzlF : $@convention(thin) <T where T : P2, T : P3, T.U == Int> (@in_guaranteed T) -> () {
sil hidden [ossa] @$s4conc2f7yyxAA2P1RzAA2P3RzlF : $@convention(thin) <T where T : P1, T : P3> (@in_guaranteed T) -> () {
sil hidden [ossa] @$s4conc2f8yyxAA2P2RzAA2P3RzlF : $@convention(thin) <T where T : P2, T : P3> (@in_guaranteed T) -> () {
sil hidden [ossa] @$s4conc2f9yyxAA2P1RzAA2P3RzlF : $@convention(thin) <T where T : P1, T : P3> (@in_guaranteed T) -> () {
sil hidden [ossa] @$s4conc3f10yyxAA2P2RzAA2P3RzlF : $@convention(thin) <T where T : P2, T : P3> (@in_guaranteed T) -> () {
sil hidden [ossa] @$s4conc3f11yyxAA2P1RzAA2P2RzSi1UAaCPRtzlF : $@convention(thin) <T where T : P1, T : P2, T.U == Int> (@in_guaranteed T) -> () {
sil hidden [ossa] @$s4conc3f12yyxAA2P1RzAA2P2RzlF : $@convention(thin) <T where T : P1, T : P2> (@in_guaranteed T) -> () {
sil hidden [ossa] @$s4conc3f13yyxAA2P1RzAA2P2RzlF : $@convention(thin) <T where T : P1, T : P2> (@in_guaranteed T) -> () {
sil hidden [ossa] @$s4conc3f14yyxAA2P1RzAA2P2RzlF : $@convention(thin) <T where T : P1, T : P2> (@in_guaranteed T) -> () {
*/
