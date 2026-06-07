; RUN: llc -relocation-model=pic -filetype=obj -mattr=+reference-types %s -o %t.o

; Under PIC, a global externref variable's __externref_table slot is computed
; relative to the imported __externref_table_base (the externref analog of
; __table_base), mirroring how a function table index is computed under PIC.
; A symbol defined in this module uses an R_WASM_EXTERNREF_TABLE_INDEX_REL_LEB
; relocation (a link-time-constant offset added to __externref_table_base); a
; preemptible symbol from another module is loaded from a GOT.externref import.

; RUN: wasm-ld -pie --no-entry --experimental-pic \
; RUN:     --unresolved-symbols=import-dynamic \
; RUN:     --export=get_local --export=get_external -o %t.wasm %t.o
; RUN: obj2yaml %t.wasm | FileCheck %s
; RUN: llvm-objdump -d %t.wasm | FileCheck %s --check-prefix=DIS

target triple = "wasm32-unknown-emscripten"

%externref = type ptr addrspace(10)

; A symbol defined in this module (hidden => dso_local).
@local = hidden global %externref null, align 1
; A preemptible symbol defined in another dynamic library.
@external = external global %externref, align 1

define %externref @get_local() {
  %v = load %externref, ptr @local, align 1
  ret %externref %v
}

define %externref @get_external() {
  %v = load %externref, ptr @external, align 1
  ret %externref %v
}

; The externref table is imported (shared) under PIC, as is __externref_table_base
; (needed by the defined-symbol path) and GOT.externref.external (the preemptible
; symbol's slot index, resolved by the dynamic linker).
; CHECK:      - Type:            IMPORT
; CHECK:          - Module:          env
; CHECK:            Field:           __externref_table_base
; CHECK:              Type:            I32
; CHECK:            Field:           __externref_table
; CHECK-NEXT:         Kind:            TABLE
; CHECK-NEXT:         Table:
; CHECK-NEXT:           Index:           0
; CHECK-NEXT:           ElemType:        EXTERNREF
; CHECK:          - Module:          GOT.externref
; CHECK-NEXT:        Field:           external
; CHECK-NEXT:        Kind:            GLOBAL

; The defined symbol resolves to its module-relative slot (0) added to
; __externref_table_base; the preemptible symbol's slot is loaded from its GOT
; global.
; DIS:      <get_local>:
; DIS:        global.get      2
; DIS-NEXT:   i32.const       0
; DIS-NEXT:   i32.add
; DIS-NEXT:   table.get       0
; DIS:      <get_external>:
; DIS:        global.get      3
; DIS-NEXT:   table.get       0
