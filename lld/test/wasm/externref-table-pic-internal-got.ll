; RUN: llc -relocation-model=pic -filetype=obj -mattr=+reference-types %s -o %t.o

; A global externref variable with default visibility is preemptible from
; codegen's point of view, so under PIC it is accessed via `global.get sym@GOT`
; (an R_WASM_GLOBAL_INDEX_LEB relocation).  When such a symbol turns out to be
; defined in this module and we are building an executable (-pie, not -shared),
; it does not actually need to be preempted, so it gets an *internal* GOT entry
; whose value is initialized at startup by __wasm_apply_global_relocs to
; __externref_table_base + the symbol's __externref_table slot index.
;
; This exercises two things that the common defined-symbol path (the
; base-relative R_WASM_EXTERNREF_TABLE_INDEX_REL_LEB relocation) does not:
;   - reference-types is enabled only via the input object's target features,
;     not the command line, so __externref_table_base must still be imported.
;   - __wasm_apply_global_relocs dereferences __externref_table_base, so it must
;     have been created and assigned a (valid) imported-global index.

; RUN: wasm-ld -pie --no-entry --experimental-pic --export=get -o %t.wasm %t.o
; RUN: obj2yaml %t.wasm | FileCheck %s
; RUN: llvm-objdump -d %t.wasm | FileCheck %s --check-prefix=DIS

target triple = "wasm32-unknown-emscripten"

%externref = type ptr addrspace(10)

; Default visibility (preemptible from codegen) but defined here.
@g = global %externref null, align 1

define %externref @get() {
  %v = load %externref, ptr @g, align 1
  ret %externref %v
}

; __externref_table_base is imported even though only the internal-GOT path (not
; a base-relative relocation) references it.
; CHECK:      - Type:            IMPORT
; CHECK:          - Module:          env
; CHECK:            Field:           __externref_table
; CHECK-NEXT:         Kind:            TABLE
; CHECK:            Field:           __externref_table_base
; CHECK-NEXT:         Kind:            GLOBAL
; CHECK-NEXT:         GlobalType:      I32

; __wasm_apply_global_relocs initializes the internal GOT entry to
; __externref_table_base (a valid global index, not INVALID_INDEX) plus the
; symbol's slot index.
; DIS:      <__wasm_apply_global_relocs>:
; DIS:        global.get      2
; DIS-NEXT:   i32.const       0
; DIS-NEXT:   i32.add
; DIS-NEXT:   global.set      3
