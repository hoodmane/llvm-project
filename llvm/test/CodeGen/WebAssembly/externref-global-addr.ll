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
