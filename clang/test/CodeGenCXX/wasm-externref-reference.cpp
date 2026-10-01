// REQUIRES: webassembly-registered-target
// RUN: %clang_cc1 %s -triple wasm32-unknown-unknown -target-feature +reference-types -emit-llvm -o - -std=c++11 | FileCheck %s
// RUN: %clang_cc1 %s -triple wasm64-unknown-unknown -target-feature +reference-types -emit-llvm -o - -std=c++11 | FileCheck %s

// Test that C++ references to __externref_t are accepted and lowered like
// references to any other object, except that - as for pointers to externref -
// the referent is addressed through the externref-pointer address space (the
// address is an __externref_table slot index): `ptr addrspace(2)`. A local
// externref's alloca is cast into that address space at the point it is
// created, so taking its address or binding a reference to it is just that
// value.

void helper(__externref_t);

// The closure of a lambda capturing an externref by reference (see
// capture_by_ref below) holds a pointer to the slot.
// CHECK: %class.anon = type { ptr addrspace(2) }

// Reading through an lvalue reference.
// CHECK-LABEL: define{{.*}} void @_Z4readRu11externref_t(ptr addrspace(2) {{[^,]*}} %r)
// CHECK:         [[R_ADDR:%.*]] = alloca ptr addrspace(2)
// CHECK:         store ptr addrspace(2) %r, ptr [[R_ADDR]]
// CHECK:         [[R:%.*]] = load ptr addrspace(2), ptr [[R_ADDR]]
// CHECK:         [[V:%.*]] = load target("wasm.externref"), ptr addrspace(2) [[R]], align 1
// CHECK:         call void @_Z6helperu11externref_t(target("wasm.externref") [[V]])
void read(__externref_t &r) { helper(r); }

// Writing through an lvalue reference.
// CHECK-LABEL: define{{.*}} void @_Z5writeRu11externref_tu11externref_t(ptr addrspace(2) {{[^,]*}} %r, target("wasm.externref") %v)
// CHECK:         [[V_ADDR:%.*]] = alloca target("wasm.externref"), align 1
// CHECK:         [[V_AS:%.*]] = addrspacecast ptr [[V_ADDR]] to ptr addrspace(2)
// CHECK:         [[V:%.*]] = load target("wasm.externref"), ptr addrspace(2) [[V_AS]], align 1
// CHECK:         [[R:%.*]] = load ptr addrspace(2), ptr %{{.*}}
// CHECK:         store target("wasm.externref") [[V]], ptr addrspace(2) [[R]], align 1
void write(__externref_t &r, __externref_t v) { r = v; }

// Const reference.
// CHECK-LABEL: define{{.*}} void @_Z10read_constRKu11externref_t(ptr addrspace(2) {{[^,]*}} %r)
// CHECK:         load target("wasm.externref"), ptr addrspace(2) %{{.*}}, align 1
void read_const(const __externref_t &r) { helper(r); }

// Rvalue reference.
// CHECK-LABEL: define{{.*}} void @_Z9read_rvalOu11externref_t(ptr addrspace(2) {{[^,]*}} %r)
// CHECK:         load target("wasm.externref"), ptr addrspace(2) %{{.*}}, align 1
void read_rval(__externref_t &&r) { helper(r); }

// Returning a reference.
// CHECK-LABEL: define{{.*}} ptr addrspace(2) @_Z8identityRu11externref_t(ptr addrspace(2) {{[^,]*}} %r)
// CHECK:         ret ptr addrspace(2) %{{.*}}
__externref_t &identity(__externref_t &r) { return r; }

// Binding a reference to a local and passing it along.
// CHECK-LABEL: define{{.*}} void @_Z6callerv()
// CHECK:         [[LOCAL:%.*]] = alloca target("wasm.externref"), align 1
// CHECK:         [[REF:%.*]] = alloca ptr addrspace(2)
// CHECK:         [[LOCAL_AS:%.*]] = addrspacecast ptr [[LOCAL]] to ptr addrspace(2)
// CHECK:         store ptr addrspace(2) [[LOCAL_AS]], ptr [[REF]]
// CHECK:         [[P:%.*]] = load ptr addrspace(2), ptr [[REF]]
// CHECK:         call void @_Z4readRu11externref_t(ptr addrspace(2) {{[^,]*}} [[P]])
// CHECK:         call void @_Z5writeRu11externref_tu11externref_t(ptr addrspace(2) {{[^,]*}} [[LOCAL_AS]], target("wasm.externref") %{{.*}})
// CHECK:         call {{.*}} ptr addrspace(2) @_Z8identityRu11externref_t(ptr addrspace(2) {{[^,]*}} [[LOCAL_AS]])
void caller() {
  __externref_t local = __builtin_wasm_ref_null_extern();
  __externref_t &ref = local;
  read(ref);
  write(local, ref);
  helper(identity(local));
}

// A lambda capturing an externref by reference holds an __externref_t& (a
// slot pointer) in its closure and reads the value through it.
// CHECK-LABEL: define{{.*}} void @_Z14capture_by_refu11externref_t(target("wasm.externref") %v)
// CHECK:         [[V_ADDR:%.*]] = alloca target("wasm.externref"), align 1
// CHECK:         [[CLOSURE:%.*]] = alloca %class.anon
// CHECK:         [[V_AS:%.*]] = addrspacecast ptr [[V_ADDR]] to ptr addrspace(2)
// CHECK:         [[FIELD:%.*]] = getelementptr inbounds nuw %class.anon, ptr [[CLOSURE]], i32 0, i32 0
// CHECK:         store ptr addrspace(2) [[V_AS]], ptr [[FIELD]]
// CHECK:         call void @"_ZZ14capture_by_refu11externref_tENK3$_0clEv"(ptr {{.*}} [[CLOSURE]])
void capture_by_ref(__externref_t v) {
  [&] { helper(v); }();
}
// CHECK-LABEL: define internal void @"_ZZ14capture_by_refu11externref_tENK3$_0clEv"(ptr {{.*}} %this)
// CHECK:         [[F:%.*]] = getelementptr inbounds nuw %class.anon, ptr %{{.*}}, i32 0, i32 0
// CHECK:         [[REF:%.*]] = load ptr addrspace(2), ptr [[F]]
// CHECK:         [[VAL:%.*]] = load target("wasm.externref"), ptr addrspace(2) [[REF]], align 1
// CHECK:         call void @_Z6helperu11externref_t(target("wasm.externref") [[VAL]])

// A pseudo-destructor call on an externref is a no-op: the base is evaluated
// and nothing else is emitted (no load, no table access).
// CHECK-LABEL: define{{.*}} void @_Z11pseudo_dtorPu11externref_t(ptr addrspace(2) noundef %p)
// CHECK-NEXT:  entry:
// CHECK-NEXT:    %p.addr = alloca ptr addrspace(2)
// CHECK-NEXT:    store ptr addrspace(2) %p, ptr %p.addr
// CHECK-NEXT:    load ptr addrspace(2), ptr %p.addr
// CHECK-NEXT:    ret void
void pseudo_dtor(__externref_t *p) { p->~__externref_t(); }
