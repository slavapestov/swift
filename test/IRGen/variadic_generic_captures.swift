// RUN: %target-swift-frontend -emit-ir %s -enable-experimental-feature VariadicGenerics | %FileCheck %s -DINT=i%target-ptrsize

// Because of -enable-experimental-feature VariadicGenerics
// REQUIRES: asserts

public func has_metadata_pack<each T>(t: repeat each T) -> () -> () {
  return { _ = (repeat each T).self }
}

// CHECK-LABEL: define{{( protected)?}}{{( dllexport)?}} swiftcc { i8*, %swift.refcounted* } @"$s25variadic_generic_captures17has_metadata_pack1tyycxxQp_tRvzlF"(%swift.opaque** noalias nocapture %0, i64 %1, %swift.type** %T) #0 {
// CHECK: [[CONTEXT0:%.*]] = call noalias %swift.refcounted* @swift_allocObject(
// CHECK: [[CONTEXT:%.*]] = bitcast %swift.refcounted* [[CONTEXT0]] to <{{.*}}>*

// CHECK: [[GENERIC_ARGS_ADDR:%.*]] = getelementptr inbounds {{.*}}* [[CONTEXT]], i32 0, i32 {{(1|2)}}
// CHECK: [[GENERIC_ARGS:%.*]] = bitcast {{.*}} [[GENERIC_ARGS_ADDR]] to %swift.type**
// CHECK: [[SHAPE_PTR:%.*]] = bitcast %swift.type** [[GENERIC_ARGS]] to [[INT]]*
// CHECK: store [[INT]] %1, [[INT]]* [[SHAPE_PTR]]

// CHECK: [[T_ADDR:%.*]] = getelementptr inbounds %swift.type*, %swift.type** [[GENERIC_ARGS]], i32 1
// CHECK: [[T_ADDR2:%.*]] = bitcast %swift.type** [[T_ADDR]] to %swift.type***
// CHECK: store %swift.type** %T, %swift.type*** [[T_ADDR2]]

// CHECK: [[CONTEXT1:%.*]] = insertvalue {{.*}} @"$s25variadic_generic_captures17has_metadata_pack1tyycxxQp_tRvzlFyycfU_TA"
// CHECK: ret { i8*, %swift.refcounted* } [[CONTEXT1]]

// CHECK-LABEL: define internal swiftcc void @"$s25variadic_generic_captures17has_metadata_pack1tyycxxQp_tRvzlFyycfU_TA"(%swift.refcounted* swiftself %0)
// CHECK: [[CONTEXT:%.*]] = bitcast %swift.refcounted* %0 to {{.*}}*
// CHECK: [[GENERIC_ARGS_ADDR:%.*]] = getelementptr inbounds {{.*}}* [[CONTEXT]], i32 0, i32 {{(1|2)}}
// CHECK: [[GENERIC_ARGS:%.*]] = bitcast {{.*}} to %swift.type**

// CHECK: [[SHAPE_PTR:%.*]] = bitcast %swift.type** [[GENERIC_ARGS]] to [[INT]]*
// CHECK: [[SHAPE:%.*]] = load [[INT]], [[INT]]* [[SHAPE_PTR]]

// CHECK: [[T_ADDR:%.*]] = getelementptr inbounds %swift.type*, %swift.type** [[GENERIC_ARGS]], i32 1
// CHECK: [[T_PTR:%.*]] = bitcast %swift.type** [[T_ADDR]] to %swift.type***
// CHECK: [[T:%.*]] = load %swift.type**, %swift.type*** [[T_PTR]]

// CHECK: tail call swiftcc void @"$s25variadic_generic_captures17has_metadata_pack1tyycxxQp_tRvzlFyycfU_"([[INT]] [[SHAPE]], %swift.type** [[T]])
// CHECK: ret void

public func has_witness_table_pack<each T: Sequence>(t: repeat each T) -> () -> () {
  return { _ = (repeat (each T).Element).self }
}

public func has_witness_table_pack2<each T: Sequence>(t: repeat each T) -> () -> ()
    where each T.Element: Sequence {
  return { _ = (repeat (each T).Element.Element).self }
}
