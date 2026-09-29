; RUN: llc < %s --mtriple=wasm32-unknown-unknown -asm-verbose=false -mattr=+reference-types | FileCheck %s --check-prefixes=CHECK,W32
; RUN: llc < %s --mtriple=wasm64-unknown-unknown -asm-verbose=false -mattr=+reference-types | FileCheck %s --check-prefixes=CHECK,W64

; Address space 2 holds pointers to externref values: 32-bit integral pointers
; (on wasm64 too) whose value is an __externref_table slot index. Loads and
; stores through them are table.get / table.set; the address of an externref
; global is its slot index; arithmetic is in slots; and a cast to or from a
; linear-memory pointer is just an integer width conversion.

%ext = type target("wasm.externref")
@g = hidden global %ext zeroinitializer, align 1

; CHECK-LABEL: load:
; CHECK-NEXT:  .functype load (i32) -> (externref)
; CHECK-NEXT:  local.get 0
; CHECK-NEXT:  table.get __externref_table
define %ext @load(ptr addrspace(2) %p) {
  %v = load %ext, ptr addrspace(2) %p, align 1
  ret %ext %v
}

; CHECK-LABEL: store:
; CHECK-NEXT:  .functype store (i32, externref) -> ()
; CHECK-NEXT:  local.get 0
; CHECK-NEXT:  local.get 1
; CHECK-NEXT:  table.set __externref_table
define void @store(ptr addrspace(2) %p, %ext %v) {
  store %ext %v, ptr addrspace(2) %p, align 1
  ret void
}

; CHECK-LABEL: addr_g:
; CHECK-NEXT:  .functype addr_g () -> (i32)
; CHECK-NEXT:  i32.const g@EXTERNREF_TABLE_INDEX
define ptr addrspace(2) @addr_g() {
  ret ptr addrspace(2) addrspacecast (ptr @g to ptr addrspace(2))
}

; CHECK-LABEL: index:
; CHECK-NEXT:  .functype index (i32, i32) -> (i32)
; CHECK:       i32.add
define ptr addrspace(2) @index(ptr addrspace(2) %p, i32 %i) {
  %q = getelementptr inbounds %ext, ptr addrspace(2) %p, i32 %i
  ret ptr addrspace(2) %q
}

; CHECK-LABEL: roundtrip:
; CHECK-NEXT:  .functype roundtrip (i32) -> (i32)
; CHECK-NEXT:  local.get 0
; CHECK-NEXT:  end_function
define ptr addrspace(2) @roundtrip(ptr addrspace(2) %p) {
  %i = ptrtoint ptr addrspace(2) %p to i32
  %q = inttoptr i32 %i to ptr addrspace(2)
  ret ptr addrspace(2) %q
}

; CHECK-LABEL: to_externref_ptr:
; W32-NEXT:    .functype to_externref_ptr (i32) -> (i32)
; W64-NEXT:    .functype to_externref_ptr (i64) -> (i32)
; CHECK-NEXT:  local.get 0
; W64-NEXT:    i32.wrap_i64
; CHECK-NEXT:  end_function
define ptr addrspace(2) @to_externref_ptr(ptr %p) {
  %q = addrspacecast ptr %p to ptr addrspace(2)
  ret ptr addrspace(2) %q
}

; CHECK-LABEL: from_externref_ptr:
; W32-NEXT:    .functype from_externref_ptr (i32) -> (i32)
; W64-NEXT:    .functype from_externref_ptr (i32) -> (i64)
; CHECK-NEXT:  local.get 0
; W64-NEXT:    i64.extend_i32_u
; CHECK-NEXT:  end_function
define ptr @from_externref_ptr(ptr addrspace(2) %p) {
  %q = addrspacecast ptr addrspace(2) %p to ptr
  ret ptr %q
}
