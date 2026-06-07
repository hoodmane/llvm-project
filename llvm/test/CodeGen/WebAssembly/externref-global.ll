; RUN: llc < %s --mtriple=wasm32-unknown-unknown -asm-verbose=false -mattr=+reference-types | FileCheck %s
; RUN: llc < %s --mtriple=wasm64-unknown-unknown -asm-verbose=false -mattr=+reference-types | FileCheck %s

; Under PIC this module's externref slots live at a runtime offset within a
; (possibly shared) __externref_table, so the slot index is computed relative to
; __externref_table_base for a symbol defined in this module, mirroring how a
; function table index is computed relative to __table_base. A preemptible
; symbol (defined in another dynamic library) is instead loaded from a GOT
; global. The relative slot index is an i32; on wasm64 the pointer-width base
; is truncated to i32 before the add (PIC64's i32.wrap_i64).
; RUN: llc < %s --mtriple=wasm32-unknown-unknown -asm-verbose=false -mattr=+reference-types -relocation-model=pic | FileCheck %s --check-prefixes=PIC
; RUN: llc < %s --mtriple=wasm64-unknown-unknown -asm-verbose=false -mattr=+reference-types -relocation-model=pic | FileCheck %s --check-prefixes=PIC,PIC64

; A global variable whose value type is externref cannot live in linear memory.
; References to it are lowered to table.get/table.set against the
; linker-synthesized __externref_table, with the slot index materialized as an
; i32.const carrying an R_WASM_EXTERNREF_TABLE_INDEX_LEB relocation against the
; global's symbol.

%externref = type ptr addrspace(10) ;; addrspace 10 is nonintegral

; CHECK: .tabletype __externref_table, externref
; PIC: .tabletype __externref_table, externref

@g = hidden global %externref null, align 1

define %externref @get_global() {
; CHECK-LABEL: get_global:
; CHECK-NEXT:  .functype       get_global () -> (externref)
; CHECK-NEXT:  i32.const       g@EXTERNREF_TABLE_INDEX
; CHECK-NEXT:  table.get       __externref_table
; CHECK-NEXT:  end_function
;
; PIC-LABEL:    get_global:
; PIC-NEXT:     .functype       get_global () -> (externref)
; PIC-NEXT:     global.get      __externref_table_base
; PIC64-NEXT:   i32.wrap_i64
; PIC-NEXT:     i32.const       g@EXTERNREF_TABLE_INDEX_REL
; PIC-NEXT:     i32.add
; PIC-NEXT:     table.get       __externref_table
; PIC-NEXT:     end_function
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
;
; PIC-LABEL:    set_global:
; PIC-NEXT:     .functype       set_global (externref) -> ()
; PIC-NEXT:     global.get      __externref_table_base
; PIC64-NEXT:   i32.wrap_i64
; PIC-NEXT:     i32.const       g@EXTERNREF_TABLE_INDEX_REL
; PIC-NEXT:     i32.add
; PIC-NEXT:     local.get       0
; PIC-NEXT:     table.set       __externref_table
; PIC-NEXT:     end_function
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
;
; PIC-LABEL:    get_other_global:
; PIC-NEXT:     .functype       get_other_global () -> (externref)
; PIC-NEXT:     global.get      __externref_table_base
; PIC64-NEXT:   i32.wrap_i64
; PIC-NEXT:     i32.const       h@EXTERNREF_TABLE_INDEX_REL
; PIC-NEXT:     i32.add
; PIC-NEXT:     table.get       __externref_table
  %v = load %externref, ptr @h, align 1
  ret %externref %v
}

; A preemptible (default-visibility, external) global is not known to live in
; this module's region, so under PIC its slot index is loaded from a GOT global.
@ext = external global %externref, align 1

define %externref @get_external_global() {
; CHECK-LABEL: get_external_global:
; CHECK-NEXT:  .functype       get_external_global () -> (externref)
; CHECK-NEXT:  i32.const       ext@EXTERNREF_TABLE_INDEX
; CHECK-NEXT:  table.get       __externref_table
; CHECK-NEXT:  end_function
;
; PIC-LABEL:    get_external_global:
; PIC-NEXT:     .functype       get_external_global () -> (externref)
; PIC-NEXT:     global.get      ext@GOT
; PIC64-NEXT:   i32.wrap_i64
; PIC-NEXT:     table.get       __externref_table
; PIC-NEXT:     end_function
  %v = load %externref, ptr @ext, align 1
  ret %externref %v
}
