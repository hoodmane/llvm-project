; RUN: llc < %s --mtriple=wasm32-unknown-unknown -asm-verbose=false -mattr=+reference-types | FileCheck %s
; RUN: llc < %s --mtriple=wasm64-unknown-unknown -asm-verbose=false -mattr=+reference-types | FileCheck %s

; The slot index must be an i32.const immediate; under PIC the i32 global-address
; Wrapper would instead lower through global.get (a GOT access), which is not a
; valid lowering for a table slot index, so reject it with a clear diagnostic.
; RUN: not llc < %s --mtriple=wasm32-unknown-unknown -mattr=+reference-types -relocation-model=pic 2>&1 | FileCheck %s --check-prefix=PIC
; RUN: not llc < %s --mtriple=wasm64-unknown-unknown -mattr=+reference-types -relocation-model=pic 2>&1 | FileCheck %s --check-prefix=PIC
; PIC: reference-type globals are not yet supported with PIC

; A global variable whose value type is externref cannot live in linear memory.
; References to it are lowered to table.get/table.set against the
; linker-synthesized __externref_table, with the slot index materialized as an
; i32.const carrying an R_WASM_EXTERNREF_TABLE_INDEX_LEB relocation against the
; global's symbol.

%externref = type ptr addrspace(10) ;; addrspace 10 is nonintegral

; CHECK: .tabletype __externref_table, externref

@g = hidden global %externref null, align 1

define %externref @get_global() {
; CHECK-LABEL: get_global:
; CHECK-NEXT:  .functype       get_global () -> (externref)
; CHECK-NEXT:  i32.const       g@EXTERNREF_TABLE_INDEX
; CHECK-NEXT:  table.get       __externref_table
; CHECK-NEXT:  end_function
  %v = load %externref, ptr @g, align 1
  ret %externref %v
}

define void @set_global(%externref %v) {
; CHECK-LABEL: set_global:
; CHECK-NEXT:  .functype       set_global (externref) -> ()
; CHECK-NEXT:  i32.const       g@EXTERNREF_TABLE_INDEX
; CHECK-NEXT:  local.get       0
; CHECK-NEXT:  table.set       __externref_table
; CHECK-NEXT:  end_function
  store %externref %v, ptr @g, align 1
  ret void
}

; A second global gets its own slot symbol.
@h = hidden global %externref null, align 1

define %externref @get_other_global() {
; CHECK-LABEL: get_other_global:
; CHECK-NEXT:  .functype       get_other_global () -> (externref)
; CHECK-NEXT:  i32.const       h@EXTERNREF_TABLE_INDEX
; CHECK-NEXT:  table.get       __externref_table
; CHECK-NEXT:  end_function
  %v = load %externref, ptr @h, align 1
  ret %externref %v
}
