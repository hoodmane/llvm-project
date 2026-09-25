// REQUIRES: webassembly-registered-target
// RUN: %clang_cc1 %s -triple wasm32-unknown-unknown -target-feature +reference-types -emit-llvm -o - -std=c++11 | FileCheck %s
// RUN: %clang_cc1 %s -triple wasm64-unknown-unknown -target-feature +reference-types -emit-llvm -o - -std=c++11 | FileCheck %s

// Test that C++ references to __externref_t are accepted and lowered like
// references to any other local: a pointer to the (non-linear-memory) slot
// holding the externref value.

void helper(__externref_t);

// The closure of a lambda capturing an externref by reference (see
// capture_by_ref below) holds a pointer to the slot.
// CHECK: %class.anon = type { ptr }

// Reading through an lvalue reference.
// CHECK-LABEL: define{{.*}} void @_Z4readRu11externref_t(ptr {{[^,]*}} %r)
// CHECK:         [[R_ADDR:%.*]] = alloca ptr
// CHECK:         store ptr %r, ptr [[R_ADDR]]
// CHECK:         [[R:%.*]] = load ptr, ptr [[R_ADDR]]
// CHECK:         [[V:%.*]] = load target("wasm.externref"), ptr [[R]], align 1
// CHECK:         call void @_Z6helperu11externref_t(target("wasm.externref") [[V]])
void read(__externref_t &r) { helper(r); }

// Writing through an lvalue reference.
// CHECK-LABEL: define{{.*}} void @_Z5writeRu11externref_tu11externref_t(ptr {{[^,]*}} %r, target("wasm.externref") %v)
// CHECK:         [[V:%.*]] = load target("wasm.externref"), ptr %{{.*}}, align 1
// CHECK:         [[R:%.*]] = load ptr, ptr %{{.*}}
// CHECK:         store target("wasm.externref") [[V]], ptr [[R]], align 1
void write(__externref_t &r, __externref_t v) { r = v; }

// Const reference.
// CHECK-LABEL: define{{.*}} void @_Z10read_constRKu11externref_t(ptr {{[^,]*}} %r)
// CHECK:         load target("wasm.externref"), ptr %{{.*}}, align 1
void read_const(const __externref_t &r) { helper(r); }

// Rvalue reference.
// CHECK-LABEL: define{{.*}} void @_Z9read_rvalOu11externref_t(ptr {{[^,]*}} %r)
// CHECK:         load target("wasm.externref"), ptr %{{.*}}, align 1
void read_rval(__externref_t &&r) { helper(r); }

// Returning a reference.
// CHECK-LABEL: define{{.*}} ptr @_Z8identityRu11externref_t(ptr {{[^,]*}} %r)
// CHECK:         ret ptr %{{.*}}
__externref_t &identity(__externref_t &r) { return r; }

// Binding a reference to a local and passing it along.
// CHECK-LABEL: define{{.*}} void @_Z6callerv()
// CHECK:         [[LOCAL:%.*]] = alloca target("wasm.externref"), align 1
// CHECK:         [[REF:%.*]] = alloca ptr
// CHECK:         store ptr [[LOCAL]], ptr [[REF]]
// CHECK:         [[P:%.*]] = load ptr, ptr [[REF]]
// CHECK:         call void @_Z4readRu11externref_t(ptr {{[^,]*}} [[P]])
// CHECK:         call void @_Z5writeRu11externref_tu11externref_t(ptr {{[^,]*}} [[LOCAL]], target("wasm.externref") %{{.*}})
// CHECK:         call {{.*}} ptr @_Z8identityRu11externref_t(ptr {{[^,]*}} [[LOCAL]])
void caller() {
  __externref_t local = __builtin_wasm_ref_null_extern();
  __externref_t &ref = local;
  read(ref);
  write(local, ref);
  helper(identity(local));
}

// A lambda capturing an externref by reference holds an __externref_t& (a
// ptr to the slot) in its closure and reads the value through it.
// CHECK-LABEL: define{{.*}} void @_Z14capture_by_refu11externref_t(target("wasm.externref") %v)
// CHECK:         [[V_ADDR:%.*]] = alloca target("wasm.externref"), align 1
// CHECK:         [[CLOSURE:%.*]] = alloca %class.anon
// CHECK:         [[FIELD:%.*]] = getelementptr inbounds nuw %class.anon, ptr [[CLOSURE]], i32 0, i32 0
// CHECK:         store ptr [[V_ADDR]], ptr [[FIELD]]
// CHECK:         call void @"_ZZ14capture_by_refu11externref_tENK3$_0clEv"(ptr {{.*}} [[CLOSURE]])
void capture_by_ref(__externref_t v) {
  [&] { helper(v); }();
}
// CHECK-LABEL: define internal void @"_ZZ14capture_by_refu11externref_tENK3$_0clEv"(ptr {{.*}} %this)
// CHECK:         [[F:%.*]] = getelementptr inbounds nuw %class.anon, ptr %{{.*}}, i32 0, i32 0
// CHECK:         [[REF:%.*]] = load ptr, ptr [[F]]
// CHECK:         [[VAL:%.*]] = load target("wasm.externref"), ptr [[REF]], align 1
// CHECK:         call void @_Z6helperu11externref_t(target("wasm.externref") [[VAL]])
