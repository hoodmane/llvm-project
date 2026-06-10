; RUN: llc < %s --mtriple=wasm32-unknown-unknown -asm-verbose=false -mattr=+reference-types | FileCheck %s --check-prefixes=CHECK,W32
; RUN: llc < %s --mtriple=wasm64-unknown-unknown -asm-verbose=false -mattr=+reference-types | FileCheck %s --check-prefixes=CHECK,W64
; RUN: llc < %s --mtriple=wasm32-unknown-unknown -asm-verbose=false -mattr=+reference-types -relocation-model=pic | FileCheck %s --check-prefixes=PIC,PIC32
; RUN: llc < %s --mtriple=wasm64-unknown-unknown -asm-verbose=false -mattr=+reference-types -relocation-model=pic | FileCheck %s --check-prefixes=PIC,PIC64

; Taking the address of a global whose value type is externref yields its slot
; index in the linker-synthesized __externref_table, not a linear-memory
; address: a pointer to an externref is a table slot index. The slot is
; materialized exactly like a load/store of the global (see externref-global.ll),
; so address-of and dereference always agree. The slot index is an i32; on
; wasm64 it is zero-extended to the i64 pointer width.

%externref = type ptr addrspace(10) ;; addrspace 10 is nonintegral

@g = hidden global %externref null, align 1

define ptr @addr_of_global() {
; CHECK-LABEL: addr_of_global:
; W32-NEXT:     .functype       addr_of_global () -> (i32)
; W64-NEXT:     .functype       addr_of_global () -> (i64)
; CHECK-NEXT:   i32.const       g@EXTERNREF_TABLE_INDEX
; W64-NEXT:     i64.extend_i32_u
; CHECK-NEXT:   end_function
;
; PIC-LABEL:    addr_of_global:
; PIC32-NEXT:   .functype       addr_of_global () -> (i32)
; PIC64-NEXT:   .functype       addr_of_global () -> (i64)
; PIC-NEXT:     global.get      __externref_table_base
; PIC64-NEXT:   i32.wrap_i64
; PIC-NEXT:     i32.const       g@EXTERNREF_TABLE_INDEX_REL
; PIC-NEXT:     i32.add
; PIC64-NEXT:   i64.extend_i32_u
; PIC-NEXT:     end_function
  ret ptr @g
}

; An external (preemptible) externref global: under PIC its slot index is loaded
; from a GOT global rather than computed relative to __externref_table_base. On
; wasm64 the i64 GOT value is truncated to the i32 slot, then zero-extended back
; to the i64 pointer width (folded to an i64.and with the low-32-bit mask).
@ext = external global %externref, align 1

define ptr @addr_of_external_global() {
; CHECK-LABEL: addr_of_external_global:
; W32-NEXT:     .functype       addr_of_external_global () -> (i32)
; W64-NEXT:     .functype       addr_of_external_global () -> (i64)
; CHECK-NEXT:   i32.const       ext@EXTERNREF_TABLE_INDEX
; W64-NEXT:     i64.extend_i32_u
; CHECK-NEXT:   end_function
;
; PIC-LABEL:    addr_of_external_global:
; PIC32-NEXT:   .functype       addr_of_external_global () -> (i32)
; PIC64-NEXT:   .functype       addr_of_external_global () -> (i64)
; PIC-NEXT:     global.get      ext@GOT
; PIC64-NEXT:   i64.const       4294967295
; PIC64-NEXT:   i64.and
; PIC-NEXT:     end_function
  ret ptr @ext
}

; An array of externref is an ordinary (linear-memory-address-space) global
; whose elements are allocated as a contiguous block of __externref_table slots
; - not a WebAssembly table (those require the wasmtable attribute / wasm-var
; address space). The address of an element is the array's base slot plus the
; element index (an externref pointer is one byte wide, so the byte offset into
; the array equals the slot offset).
@arr = hidden global [4 x %externref] zeroinitializer, align 1

define ptr @addr_of_elem0() {
; CHECK-LABEL: addr_of_elem0:
; W32-NEXT:     .functype       addr_of_elem0 () -> (i32)
; W64-NEXT:     .functype       addr_of_elem0 () -> (i64)
; CHECK-NEXT:   i32.const       arr@EXTERNREF_TABLE_INDEX
; W64-NEXT:     i64.extend_i32_u
; CHECK-NEXT:   end_function
;
; PIC-LABEL:    addr_of_elem0:
; PIC32-NEXT:   .functype       addr_of_elem0 () -> (i32)
; PIC64-NEXT:   .functype       addr_of_elem0 () -> (i64)
; PIC-NEXT:     global.get      __externref_table_base
; PIC64-NEXT:   i32.wrap_i64
; PIC-NEXT:     i32.const       arr@EXTERNREF_TABLE_INDEX_REL
; PIC-NEXT:     i32.add
; PIC64-NEXT:   i64.extend_i32_u
; PIC-NEXT:     end_function
  ret ptr @arr
}

define ptr @addr_of_elem2() {
; CHECK-LABEL: addr_of_elem2:
; W32-NEXT:     .functype       addr_of_elem2 () -> (i32)
; W64-NEXT:     .functype       addr_of_elem2 () -> (i64)
; CHECK-NEXT:   i32.const       arr@EXTERNREF_TABLE_INDEX
; CHECK-NEXT:   i32.const       2
; CHECK-NEXT:   i32.add
; W64-NEXT:     i64.extend_i32_u
; CHECK-NEXT:   end_function
  ret ptr getelementptr inbounds (i8, ptr @arr, i32 2)
}

define ptr @addr_of_elem_dyn(i32 %i) {
; CHECK-LABEL:  addr_of_elem_dyn:
; W32-NEXT:     .functype       addr_of_elem_dyn (i32) -> (i32)
; W32-NEXT:     local.get       0
; W32-NEXT:     i32.const       arr@EXTERNREF_TABLE_INDEX
; W32-NEXT:     i32.add
; W64-NEXT:     .functype       addr_of_elem_dyn (i32) -> (i64)
; W64-NEXT:     local.get       0
; W64-NEXT:     i64.extend_i32_s
; W64-NEXT:     i32.const       arr@EXTERNREF_TABLE_INDEX
; W64-NEXT:     i64.extend_i32_u
; W64-NEXT:     i64.add
; CHECK-NEXT:   end_function
  %p = getelementptr inbounds %externref, ptr @arr, i32 %i
  ret ptr %p
}
