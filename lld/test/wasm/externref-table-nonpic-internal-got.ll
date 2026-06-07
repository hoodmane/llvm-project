; RUN: llc -relocation-model=pic -filetype=obj -mattr=+reference-types %s -o %t.o

; A global externref variable compiled with -fPIC is accessed via
; `global.get sym@GOT` (an R_WASM_GLOBAL_INDEX_LEB relocation).  When such a
; PIC-compiled object is statically linked into a *non-PIC* executable, the
; symbol is defined in this module, so it gets an internal GOT entry.  Unlike
; the PIC case there is no __externref_table_base: the symbol's
; __externref_table slot index is an absolute link-time constant, and the GOT
; entry global is initialized to it directly.
;
; This used to crash wasm-ld with a null dereference of __externref_table_base
; (which is only created under PIC), and would otherwise have mis-initialized
; the GOT entry with the symbol's memory address instead of its table slot.

; RUN: wasm-ld --no-entry --export=get -o %t.wasm %t.o
; RUN: obj2yaml %t.wasm | FileCheck %s

target triple = "wasm32-unknown-emscripten"

%externref = type ptr addrspace(10)

; Default visibility (preemptible from codegen) but defined here.
@g = global %externref null, align 1

define %externref @get() {
  %v = load %externref, ptr @g, align 1
  ret %externref %v
}

; No __externref_table_base is imported in a non-PIC link.
; CHECK-NOT: __externref_table_base

; The __externref_table has the reserved null slot plus the one allocated slot.
; CHECK:      - Type:            TABLE
; CHECK:            ElemType:        EXTERNREF
; CHECK:            Minimum:         0x2

; The internal GOT entry is an immutable global initialized directly to the
; symbol's absolute __externref_table slot index (1; slot 0 is the reserved
; null externref for executables).
; CHECK:      - Type:            GLOBAL
; CHECK:        Globals:
; CHECK:          - Index:           0
; CHECK-NEXT:       Type:            I32
; CHECK-NEXT:       Mutable:         false
; CHECK-NEXT:       InitExpr:
; CHECK-NEXT:         Opcode:          I32_CONST
; CHECK-NEXT:         Value:           1

; The GOT entry corresponds to the global externref variable.
; CHECK:        GlobalNames:
; CHECK:          - Index:           0
; CHECK-NEXT:       Name:            GOT.data.internal.g
