// REQUIRES: webassembly-registered-target
// RUN: %clang_cc1 %s -triple wasm32-unknown-unknown -target-feature +reference-types -emit-llvm -o - -std=c++17 -disable-llvm-passes | FileCheck %s --check-prefixes=CHECK,W32
// RUN: %clang_cc1 %s -triple wasm64-unknown-unknown -target-feature +reference-types -emit-llvm -o - -std=c++17 -disable-llvm-passes | FileCheck %s --check-prefixes=CHECK,W64

// An externref cannot live in linear memory, so `new` of an __externref_t (or
// an array of them) does not call operator new: it calls the compiler-rt
// externref table allocator with the number of slots, and `delete` /
// `delete[]` release the block with a single unsized call (the allocator
// records block sizes itself since there is nowhere in the table for an array
// cookie). Freshly allocated slots are null, so only explicit initializers
// produce stores.

// CHECK-LABEL: define{{.*}} ptr @_Z6scalarv()
// W32:           call ptr @__externref_table_alloc(i32 1)
// W64:           call ptr @__externref_table_alloc(i64 1)
// CHECK-NOT:     operator new
// CHECK:         ret ptr
__externref_t *scalar() { return new __externref_t; }

// W32: declare {{.*}} ptr @__externref_table_alloc(i32 noundef)
// W64: declare {{.*}} ptr @__externref_table_alloc(i64 noundef)

// The initializer is stored through the returned pointer (which lowers to
// table.set), guarded by a null check on the allocation.
// CHECK-LABEL: define{{.*}} ptr @_Z11scalar_initu11externref_t(target("wasm.externref") %v)
// CHECK:         [[P:%.*]] = call ptr @__externref_table_alloc({{i32|i64}} 1)
// CHECK:         [[ISNULL:%.*]] = icmp eq ptr [[P]], null
// CHECK:         br i1 [[ISNULL]], label %new.cont, label %new.notnull
// CHECK:       new.notnull:
// CHECK:         store target("wasm.externref") %{{.*}}, ptr [[P]], align 1
// CHECK:       new.cont:
// CHECK:         ret ptr [[P]]
__externref_t *scalar_init(__externref_t v) { return new __externref_t(v); }

// Value-initialization stores the null externref.
// CHECK-LABEL: define{{.*}} ptr @_Z17scalar_value_initv()
// CHECK:         store target("wasm.externref") zeroinitializer, ptr %{{.*}}, align 1
__externref_t *scalar_value_init() { return new __externref_t(); }

// Array new passes the element count as the slot count.
// CHECK-LABEL: define{{.*}} ptr @_Z5arrayj(i32 noundef %n)
// CHECK:         [[N:%.*]] = load i32, ptr %n.addr
// W32:           call ptr @__externref_table_alloc(i32 [[N]])
// W64:           [[N64:%.*]] = zext i32 [[N]] to i64
// W64:           call ptr @__externref_table_alloc(i64 [[N64]])
// CHECK-NOT:     store
// CHECK:         ret ptr
__externref_t *array(unsigned n) { return new __externref_t[n]; }

// Only the explicit init-list elements are stored; the rest stay null.
// CHECK-LABEL: define{{.*}} ptr @_Z10array_listu11externref_tu11externref_t(
// CHECK:         [[P:%.*]] = call ptr @__externref_table_alloc({{i32|i64}} 4)
// CHECK:       new.notnull:
// CHECK:         store target("wasm.externref") %{{.*}}, ptr [[P]], align 1
// CHECK:         [[E1:%.*]] = getelementptr inbounds target("wasm.externref"), ptr [[P]], {{i32|i64}} 1
// CHECK:         store target("wasm.externref") %{{.*}}, ptr [[E1]], align 1
// CHECK-NOT:     store
// CHECK-NOT:     memset
// CHECK:       new.cont:
__externref_t *array_list(__externref_t a, __externref_t b) {
  return new __externref_t[4]{a, b};
}

// Value-initializing an array is a no-op on fresh (null) slots: no memset.
// CHECK-LABEL: define{{.*}} ptr @_Z16array_value_initi(
// CHECK:         call ptr @__externref_table_alloc(
// CHECK-NOT:     memset
// CHECK-NOT:     store target
// CHECK:         ret ptr
__externref_t *array_value_init(int n) { return new __externref_t[n](); }

// A multi-dimensional array allocates count * inner-size slots.
// CHECK-LABEL: define{{.*}} ptr @_Z7array2di(
// CHECK:         [[N:%.*]] = {{load i32|sext i32}}
// CHECK:         [[SLOTS:%.*]] = mul {{i32|i64}} {{.*}}, 3
// CHECK:         call ptr @__externref_table_alloc({{i32|i64}} [[SLOTS]])
__externref_t (*array2d(int n))[3] { return new __externref_t[n][3]; }

// delete, delete[] and delete[] of a multi-dimensional array all release the
// block with the same unsized call; there is no null check or operator delete.
// CHECK-LABEL: define{{.*}} void @_Z3delPu11externref_t(ptr noundef %p)
// CHECK-NOT:     icmp
// CHECK:         call void @__externref_table_free(ptr %{{.*}})
// CHECK-NOT:     operator delete
// CHECK:         ret void
void del(__externref_t *p) { delete p; }

// CHECK: declare void @__externref_table_free(ptr noundef)

// CHECK-LABEL: define{{.*}} void @_Z7del_arrPu11externref_t(ptr noundef %p)
// CHECK:         call void @__externref_table_free(ptr %{{.*}})
void del_arr(__externref_t *p) { delete[] p; }

// CHECK-LABEL: define{{.*}} void @_Z6del_2dPA3_u11externref_t(ptr noundef %p)
// CHECK:         call void @__externref_table_free(ptr %{{.*}})
void del_2d(__externref_t (*p)[3]) { delete[] p; }

// ::new is fine (there is no class-specific operator to bypass anyway).
// CHECK-LABEL: define{{.*}} ptr @_Z10global_newv()
// CHECK:         call ptr @__externref_table_alloc({{i32|i64}} 1)
__externref_t *global_new() { return ::new __externref_t; }

// No operator new / delete is ever referenced for externref allocations.
// CHECK-NOT: @_Znw
// CHECK-NOT: @_Zna
// CHECK-NOT: @_Zdl
// CHECK-NOT: @_Zda

// Placement new into an existing slot uses the implicitly declared
// non-allocating `operator new(size_t, __externref_t *)` (an externref pointer
// cannot convert to void *, so <new> is not needed): the initializer is stored
// through the placement pointer and nothing is allocated. This is what
// std::construct_at expands to.

// CHECK-LABEL: define{{.*}} void @_Z9placementPu11externref_tu11externref_t(ptr noundef %slot, target("wasm.externref") %v)
// CHECK-NOT:     __externref_table_alloc
// CHECK-NOT:     icmp
// CHECK:         [[S:%.*]] = load ptr, ptr %slot.addr
// CHECK-NEXT:    [[V:%.*]] = load target("wasm.externref"), ptr %v.addr, align 1
// CHECK-NEXT:    store target("wasm.externref") [[V]], ptr [[S]], align 1
// CHECK-NOT:     __externref_table_alloc
// CHECK:         ret void
void placement(__externref_t *slot, __externref_t v) {
  ::new (slot) __externref_t(v);
}

// Default-initialising placement new is a no-op besides evaluating the slot.
// CHECK-LABEL: define{{.*}} void @_Z17placement_defaultPu11externref_t(ptr noundef %slot)
// CHECK-NOT:     store target
// CHECK-NOT:     __externref_table_alloc
// CHECK:         ret void
void placement_default(__externref_t *slot) { new (slot) __externref_t; }

// Placement array new stores each explicit element in place.
// CHECK-LABEL: define{{.*}} void @_Z15placement_arrayPu11externref_tu11externref_t(ptr noundef %slot, target("wasm.externref") %v)
// CHECK-NOT:     __externref_table_alloc
// CHECK:         [[S:%.*]] = load ptr, ptr %slot.addr
// CHECK:         store target("wasm.externref") %{{.*}}, ptr [[S]], align 1
// CHECK:         [[E1:%.*]] = getelementptr inbounds target("wasm.externref"), ptr [[S]], {{i32|i64}} 1
// CHECK:         store target("wasm.externref") %{{.*}}, ptr [[E1]], align 1
// CHECK:         ret void
void placement_array(__externref_t *slot, __externref_t v) {
  new (slot) __externref_t[4]{v, v};
}
