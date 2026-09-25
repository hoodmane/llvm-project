// REQUIRES: webassembly-registered-target
// RUN: %clang_cc1 %s -triple wasm32-unknown-unknown -target-feature +reference-types -emit-llvm -o - -std=c++17 -disable-llvm-passes | FileCheck %s --check-prefixes=CHECK,W32
// RUN: %clang_cc1 %s -triple wasm64-unknown-unknown -target-feature +reference-types -emit-llvm -o - -std=c++17 -disable-llvm-passes | FileCheck %s --check-prefixes=CHECK,W64
// RUN: %clang_cc1 %s -triple wasm32-unknown-unknown -target-feature +reference-types -S -o - -std=c++17 -O1 | FileCheck %s --check-prefix=ASM

// A pointer to an externref is an __externref_table slot index, not a
// linear-memory address, so the byte-oriented memory builtins cannot be used on
// it as-is. When clang can see (through the casts to void*) that an operand of
// memcpy / memmove / memset is an externref pointer, it lowers the call to
// table.copy / table.fill on __externref_table instead. sizeof(__externref_t)
// is 1, so the byte count is the slot count; table.copy handles overlap, so
// memcpy and memmove lower identically.

typedef __SIZE_TYPE__ size_t;
extern "C" void *memcpy(void *, const void *, size_t);
extern "C" void *memmove(void *, const void *, size_t);
extern "C" void *memset(void *, int, size_t);

// CHECK: @__externref_table = external addrspace(1) global [0 x target("wasm.externref")]

// CHECK-LABEL: define{{.*}} void @_Z4copyPu11externref_tPKu11externref_tm(ptr noundef %d, ptr noundef %s, {{i32|i64}} noundef %n)
// CHECK:         [[D:%.*]] = load ptr, ptr %d.addr
// CHECK:         [[S:%.*]] = load ptr, ptr %s.addr
// CHECK:         [[N:%.*]] = load {{i32|i64}}, ptr %n.addr
// W32:           [[DI:%.*]] = ptrtoint ptr [[D]] to i32
// W32:           [[SI:%.*]] = ptrtoint ptr [[S]] to i32
// W32:           call void @llvm.wasm.table.copy(ptr addrspace(1) @__externref_table, ptr addrspace(1) @__externref_table, i32 [[DI]], i32 [[SI]], i32 [[N]])
// W64:           [[DI64:%.*]] = ptrtoint ptr [[D]] to i64
// W64:           [[DI:%.*]] = trunc i64 [[DI64]] to i32
// W64:           [[SI64:%.*]] = ptrtoint ptr [[S]] to i64
// W64:           [[SI:%.*]] = trunc i64 [[SI64]] to i32
// W64:           [[N32:%.*]] = trunc i64 [[N]] to i32
// W64:           call void @llvm.wasm.table.copy(ptr addrspace(1) @__externref_table, ptr addrspace(1) @__externref_table, i32 [[DI]], i32 [[SI]], i32 [[N32]])
// CHECK-NOT:     llvm.memcpy
// CHECK:         ret void
void copy(__externref_t *d, const __externref_t *s, size_t n) {
  memcpy(d, s, n);
}

// CHECK-LABEL: define{{.*}} void @_Z4movePu11externref_tS_m(
// CHECK:         call void @llvm.wasm.table.copy(ptr addrspace(1) @__externref_table, ptr addrspace(1) @__externref_table,
// CHECK-NOT:     llvm.memmove
void move(__externref_t *d, __externref_t *s, size_t n) { memmove(d, s, n); }

// memset to zero clears the slots to the null reference.
// CHECK-LABEL: define{{.*}} void @_Z5clearPu11externref_tm(
// CHECK:         [[NULL:%.*]] = call target("wasm.externref") @llvm.wasm.ref.null.extern()
// CHECK:         call void @llvm.wasm.table.fill.externref(ptr addrspace(1) @__externref_table, i32 %{{.*}}, target("wasm.externref") [[NULL]], i32 %{{.*}})
// CHECK-NOT:     llvm.memset
void clear(__externref_t *d, size_t n) { memset(d, 0, n); }

// The externref pointer is recognised through explicit casts to void*, which
// is how libc++ spells its memcpy fast paths.
// CHECK-LABEL: define{{.*}} void @_Z13via_void_castPu11externref_tS_m(
// CHECK:         call void @llvm.wasm.table.copy(
// CHECK-NOT:     llvm.memcpy
void via_void_cast(__externref_t *d, __externref_t *s, size_t n) {
  __builtin_memcpy(static_cast<void *>(d), static_cast<const void *>(s), n);
}

// Arrays of externref decay to externref pointers; sizeof gives the slot count.
// CHECK-LABEL: define{{.*}} void @_Z6arraysRA4_u11externref_tS0_(
// CHECK:         call void @llvm.wasm.table.copy(ptr addrspace(1) @__externref_table, ptr addrspace(1) @__externref_table, i32 %{{.*}}, i32 %{{.*}}, i32 4)
void arrays(__externref_t (&d)[4], __externref_t (&s)[4]) {
  memcpy(d, s, sizeof(d));
}

// mempcpy still returns dst + n (in slots).
// CHECK-LABEL: define{{.*}} ptr @_Z5pcopyPu11externref_tS_(
// CHECK:         call void @llvm.wasm.table.copy(ptr addrspace(1) @__externref_table, ptr addrspace(1) @__externref_table, i32 %{{.*}}, i32 %{{.*}}, i32 3)
// CHECK:         [[END:%.*]] = getelementptr inbounds i8, ptr %{{.*}}, {{i32|i64}} 3
// CHECK:         ret ptr [[END]]
void *pcopy(__externref_t *d, __externref_t *s) {
  return __builtin_mempcpy(d, s, 3);
}

// CHECK-LABEL: define{{.*}} void @_Z7inline_Pu11externref_tS_(
// CHECK:         call void @llvm.wasm.table.copy(ptr addrspace(1) @__externref_table, ptr addrspace(1) @__externref_table, i32 %{{.*}}, i32 %{{.*}}, i32 2)
void inline_(__externref_t *d, __externref_t *s) {
  __builtin_memcpy_inline(d, s, 2);
}

// Ordinary pointers are unaffected.
// CHECK-LABEL: define{{.*}} void @_Z5plainPiS_m(
// CHECK:         call void @llvm.memcpy.p0.p0.{{i32|i64}}(
// CHECK-NOT:     table.copy
void plain(int *d, int *s, size_t n) { memcpy(d, s, n); }

// ASM-LABEL: _Z4copyPu11externref_tPKu11externref_tm:
// ASM:         table.copy __externref_table, __externref_table
// ASM-LABEL: _Z5clearPu11externref_tm:
// ASM:         ref.null_extern
// ASM:         table.fill __externref_table
// ASM: .tabletype __externref_table, externref
