// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/wasm/wasm-subtyping.h"
#include "test/unittests/test-utils.h"

namespace v8 {
namespace internal {
namespace wasm {
namespace subtyping_unittest {

class WasmSubtypingTest : public ::testing::Test {};
using FieldInit = std::pair<ValueType, bool>;

ValueType ref(uint32_t index) { return ValueType::Ref(index, kNonNullable); }
ValueType optRef(uint32_t index) { return ValueType::Ref(index, kNullable); }

FieldInit mut(ValueType type) { return FieldInit(type, true); }
FieldInit immut(ValueType type) { return FieldInit(type, false); }

void DefineStruct(WasmModule* module, std::initializer_list<FieldInit> fields) {
  StructType::Builder builder(module->signature_zone.get(),
                              static_cast<uint32_t>(fields.size()));
  for (FieldInit field : fields) {
    builder.AddField(field.first, field.second);
  }
  return module->add_struct_type(builder.Build());
}

void DefineArray(WasmModule* module, FieldInit element_type) {
  module->add_array_type(module->signature_zone->New<ArrayType>(
      element_type.first, element_type.second));
}

TEST_F(WasmSubtypingTest, Subtyping) {
  v8::internal::AccountingAllocator allocator;
  WasmModule module_(std::make_unique<Zone>(&allocator, ZONE_NAME));

  WasmModule* module = &module_;

  /* 0 */ DefineStruct(module, {mut(ref(2)), immut(optRef(2))});
  /* 1 */ DefineStruct(module, {mut(ref(2)), immut(ref(2))});
  /* 2 */ DefineArray(module, immut(ref(0)));
  /* 3 */ DefineArray(module, immut(ref(1)));
  /* 4 */ DefineStruct(module, {mut(ref(2)), immut(ref(3)), immut(kWasmF64)});
  /* 5 */ DefineStruct(module, {mut(optRef(2)), immut(ref(2))});
  /* 6 */ DefineArray(module, mut(kWasmI32));
  /* 7 */ DefineArray(module, immut(kWasmI32));
  /* 8 */ DefineStruct(module, {mut(kWasmI32), immut(optRef(8))});
  /* 9 */ DefineStruct(module, {mut(kWasmI32), immut(optRef(8))});

  ValueType numeric_types[] = {kWasmI32, kWasmI64, kWasmF32, kWasmF64,
                               kWasmS128};
  ValueType ref_types[] = {kWasmExternRef, kWasmFuncRef, kWasmExnRef,
                           kWasmEqRef,     kWasmI31Ref,  optRef(0),
                           ref(0),         optRef(2),    ref(2)};

  // Value types are unrelated, except if they are equal.
  for (ValueType subtype : numeric_types) {
    for (ValueType supertype : numeric_types) {
      CHECK_EQ(IsSubtypeOf(subtype, supertype, module), subtype == supertype);
    }
  }

  // Value types are unrelated with reference types.
  for (ValueType value_type : numeric_types) {
    for (ValueType ref_type : ref_types) {
      CHECK(!IsSubtypeOf(value_type, ref_type, module));
      CHECK(!IsSubtypeOf(ref_type, value_type, module));
    }
  }

  for (ValueType ref_type : ref_types) {
    // Reference types are a subtype of eqref, except funcref.
    CHECK_EQ(IsSubtypeOf(ref_type, kWasmEqRef, module),
             ref_type != kWasmFuncRef);
    // Each reference type is a subtype of itself.
    CHECK(IsSubtypeOf(ref_type, ref_type, module));
  }

  // The rest of ref. types are unrelated.
  for (ValueType type_1 :
       {kWasmExternRef, kWasmFuncRef, kWasmExnRef, kWasmI31Ref}) {
    for (ValueType type_2 :
         {kWasmExternRef, kWasmFuncRef, kWasmExnRef, kWasmI31Ref}) {
      CHECK_EQ(IsSubtypeOf(type_1, type_2, module), type_1 == type_2);
    }
  }

  // Unrelated refs are unrelated.
  CHECK(!IsSubtypeOf(ref(0), ref(2), module));
  CHECK(!IsSubtypeOf(optRef(3), optRef(1), module));
  // ref is a subtype of optref for the same struct/array.
  CHECK(IsSubtypeOf(ref(0), optRef(0), module));
  CHECK(IsSubtypeOf(ref(2), optRef(2), module));
  // optref is not a subtype of ref for the same struct/array.
  CHECK(!IsSubtypeOf(optRef(0), ref(0), module));
  CHECK(!IsSubtypeOf(optRef(2), ref(2), module));
  // ref is a subtype of optref if the same is true for the underlying
  // structs/arrays.
  CHECK(IsSubtypeOf(ref(3), optRef(2), module));
  // Prefix subtyping for structs.
  CHECK(IsSubtypeOf(optRef(4), optRef(0), module));
  // Mutable fields are invariant.
  CHECK(!IsSubtypeOf(ref(0), ref(5), module));
  // Immutable fields are covariant.
  CHECK(IsSubtypeOf(ref(1), ref(0), module));
  // Prefix subtyping + immutable field covariance for structs.
  CHECK(IsSubtypeOf(optRef(4), optRef(1), module));
  // No subtyping between mutable/immutable fields.
  CHECK(!IsSubtypeOf(ref(7), ref(6), module));
  CHECK(!IsSubtypeOf(ref(6), ref(7), module));
  // Recursive types.
  CHECK(IsSubtypeOf(ref(9), ref(8), module));
}

}  // namespace subtyping_unittest
}  // namespace wasm
}  // namespace internal
}  // namespace v8
