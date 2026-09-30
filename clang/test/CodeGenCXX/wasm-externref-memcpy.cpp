// REQUIRES: webassembly-registered-target
// RUN: %clang_cc1 %s -triple wasm32-unknown-unknown -target-feature +reference-types -emit-llvm -o - -std=c++17 -disable-llvm-passes | FileCheck %s --check-prefixes=CHECK,W32
// RUN: %clang_cc1 %s -triple wasm64-unknown-unknown -target-feature +reference-types -emit-llvm -o - -std=c++17 -disable-llvm-passes | FileCheck %s --check-prefixes=CHECK,W64
// RUN: %clang_cc1 %s -triple wasm32-unknown-unknown -target-feature +reference-types -S -o - -std=c++17 -O1 | FileCheck %s --check-prefix=ASM

// A pointer to an externref is an __externref_table slot index, not a
// linear-memory address, so it cannot be passed to memcpy / memmove / memset
// (it does not even convert to void *). Bulk copies and fills of externref
// slots go through dedicated builtins that lower to table.copy / table.fill on
// __externref_table. Slot indices and counts are i32 regardless of pointer
// width; table.copy handles overlapping ranges.

typedef __SIZE_TYPE__ size_t;

// CHECK: @__externref_table = external addrspace(1) global [0 x target("wasm.externref")]

// CHECK-LABEL: define{{.*}} void @_Z4copyPu11externref_tPKu11externref_tm(ptr noundef %d, ptr noundef %s, {{i32|i64}} noundef %n)
// CHECK:         [[D:%.*]] = load ptr, ptr %d.addr
// W32:           [[DI:%.*]] = ptrtoint ptr [[D]] to i32
// W64:           [[DI64:%.*]] = ptrtoint ptr [[D]] to i64
// W64:           [[DI:%.*]] = trunc i64 [[DI64]] to i32
// CHECK:         [[S:%.*]] = load ptr, ptr %s.addr
// CHECK:         [[N:%.*]] = load {{i32|i64}}, ptr %n.addr
// W64:           [[N32:%.*]] = trunc i64 [[N]] to i32
// W32:           [[SI:%.*]] = ptrtoint ptr [[S]] to i32
// W64:           [[SI64:%.*]] = ptrtoint ptr [[S]] to i64
// W64:           [[SI:%.*]] = trunc i64 [[SI64]] to i32
// W32:           call void @llvm.wasm.table.copy(ptr addrspace(1) @__externref_table, ptr addrspace(1) @__externref_table, i32 [[DI]], i32 [[SI]], i32 [[N]])
// W64:           call void @llvm.wasm.table.copy(ptr addrspace(1) @__externref_table, ptr addrspace(1) @__externref_table, i32 [[DI]], i32 [[SI]], i32 [[N32]])
// CHECK-NOT:     llvm.memcpy
// CHECK:         ret void
void copy(__externref_t *d, const __externref_t *s, size_t n) {
  __builtin_wasm_externref_copy(d, s, n);
}

// CHECK-LABEL: define{{.*}} void @_Z4fillPu11externref_tu11externref_tm(
// CHECK:         call void @llvm.wasm.table.fill.externref(ptr addrspace(1) @__externref_table, i32 %{{.*}}, target("wasm.externref") %{{.*}}, i32 %{{.*}})
// CHECK-NOT:     llvm.memset
void fill(__externref_t *d, __externref_t v, size_t n) {
  __builtin_wasm_externref_fill(d, v, n);
}

// Arrays of externref decay to externref pointers.
// CHECK-LABEL: define{{.*}} void @_Z6arraysRA4_u11externref_tS0_(
// CHECK:         call void @llvm.wasm.table.copy(ptr addrspace(1) @__externref_table, ptr addrspace(1) @__externref_table, i32 %{{.*}}, i32 %{{.*}}, i32 4)
void arrays(__externref_t (&d)[4], __externref_t (&s)[4]) {
  __builtin_wasm_externref_copy(d, s, sizeof(d));
}

// ASM-LABEL: _Z4copyPu11externref_tPKu11externref_tm:
// ASM:         table.copy __externref_table, __externref_table
// ASM-LABEL: _Z4fillPu11externref_tu11externref_tm:
// ASM:         table.fill __externref_table
// ASM: .tabletype __externref_table, externref
