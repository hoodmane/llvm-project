// REQUIRES: webassembly-registered-target
// RUN: %clang_cc1 %s -triple wasm32-unknown-unknown -target-feature +reference-types -emit-llvm -o - -std=c++17 -disable-llvm-passes | FileCheck %s
// RUN: %clang_cc1 %s -triple wasm64-unknown-unknown -target-feature +reference-types -emit-llvm -o - -std=c++17 -disable-llvm-passes | FileCheck %s
// RUN: %clang_cc1 %s -triple wasm32-unknown-unknown -target-feature +reference-types -S -o - -std=c++17 -O1 | FileCheck %s --check-prefix=ASM

// A local array of externref is an ordinary alloca of an array of externref
// addressed through the externref-pointer address space; the backend spills it
// to a run of consecutive __externref_table slots (one per element) in the
// externref stack region, and element accesses become table.get / table.set.

void helper(__externref_t);
void take(__externref_t *);

// CHECK-LABEL: define{{.*}} target("wasm.externref") @_Z4pickiu11externref_tu11externref_t(
// CHECK:         [[ARR:%.*]] = alloca [4 x target("wasm.externref")], align 1
// CHECK:         [[ARR_AS:%.*]] = addrspacecast ptr [[ARR]] to ptr addrspace(2)
// CHECK:         [[E0:%.*]] = getelementptr inbounds [4 x target("wasm.externref")], ptr addrspace(2) [[ARR_AS]], {{i32|i64}} 0, {{i32|i64}} 0
// CHECK:         store target("wasm.externref") %{{.*}}, ptr addrspace(2) [[E0]], align 1
// CHECK:         [[E3:%.*]] = getelementptr inbounds [4 x target("wasm.externref")], ptr addrspace(2) [[ARR_AS]], {{i32|i64}} 0, {{i32|i64}} 3
// CHECK:         store target("wasm.externref") %{{.*}}, ptr addrspace(2) [[E3]], align 1
// CHECK:         [[EI:%.*]] = getelementptr inbounds [4 x target("wasm.externref")], ptr addrspace(2) [[ARR_AS]], {{i32|i64}} 0, {{i32|i64}} %{{.*}}
// CHECK:         load target("wasm.externref"), ptr addrspace(2) [[EI]], align 1
// CHECK-NOT:     memcpy
// CHECK-NOT:     memset
//
// (At -O1 the stack pointer stores are dead, since no call can observe them
// before the restore, and get folded away; the slot run is still reserved
// relative to the stack pointer and cleared on exit.)
// ASM-LABEL: _Z4pickiu11externref_tu11externref_t:
// ASM:         global.get __externref_stack_pointer
// ASM:         i32.const -4
// ASM:         i32.add
// ASM:         table.set __externref_table
// ASM:         table.set __externref_table
// ASM:         table.get __externref_table
// ASM:         ref.null_extern
// ASM:         i32.const 4
// ASM:         table.fill __externref_table
__externref_t pick(int i, __externref_t a, __externref_t b) {
  __externref_t arr[4];
  arr[0] = a;
  arr[3] = b;
  return arr[i];
}

// Multi-dimensional arrays are flattened; decay yields a slot pointer.
// CHECK-LABEL: define{{.*}} void @_Z5two_diiu11externref_t(
// CHECK:         [[M:%.*]] = alloca [2 x [3 x target("wasm.externref")]], align 1
// CHECK:         [[M_AS:%.*]] = addrspacecast ptr [[M]] to ptr addrspace(2)
// CHECK:         getelementptr inbounds [2 x [3 x target("wasm.externref")]], ptr addrspace(2) [[M_AS]],
// CHECK:         call void @_Z4takePu11externref_t(ptr addrspace(2) noundef %{{.*}})
//
// ASM-LABEL: _Z5two_diiu11externref_t:
// ASM:         i32.const -6
// ASM:         i32.const 6
// ASM:         table.fill __externref_table
void two_d(int i, int j, __externref_t v) {
  __externref_t m[2][3];
  m[i][j] = v;
  helper(m[1][2]);
  take(m[0]);
}
