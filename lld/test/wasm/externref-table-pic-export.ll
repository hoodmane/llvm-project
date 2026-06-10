; RUN: llc -relocation-model=pic -filetype=obj -mattr=+reference-types %s -o %t.o

; When a PIC module exports a global externref (or an array of externref), the
; exported value is its __externref_table slot index relative to
; __externref_table_base, not a linear-memory address.  The linker must
; therefore (a) initialize the exported address-global with the slot index, and
; (b) flag the export as externref in the dylink export info so the dynamic
; loader relocates it against __externref_table_base instead of __memory_base.

; RUN: wasm-ld --experimental-pic -shared --no-entry \
; RUN:     --export=g --export=arr -o %t.wasm %t.o
; RUN: obj2yaml %t.wasm | FileCheck %s

target triple = "wasm32-unknown-emscripten"

%externref = type ptr addrspace(10)

; A scalar externref occupies one slot; the 3-element array occupies three.
@g = global %externref null, align 1
@arr = global [3 x %externref] zeroinitializer, align 1

define %externref @get_g() {
  %v = load %externref, ptr @g, align 1
  ret %externref %v
}

define %externref @get_arr(i32 %i) {
  %p = getelementptr inbounds %externref, ptr @arr, i32 %i
  %v = load %externref, ptr %p, align 1
  ret %externref %v
}

; The dylink section reserves four externref slots (g -> 1, arr -> 3) and flags
; both exports as externref so the loader relocates them against
; __externref_table_base.
; CHECK:      - Type:            CUSTOM
; CHECK-NEXT:    Name:            dylink.0
; CHECK:         ExternrefTableSize: 4
; CHECK:         ExportInfo:
; CHECK-NEXT:      - Name:            g
; CHECK-NEXT:        Flags:           [ EXTERNREF ]
; CHECK-NEXT:      - Name:            arr
; CHECK-NEXT:        Flags:           [ EXTERNREF ]

; Each symbol's slot index is also imported via GOT.externref so the loader can
; fix up internal references.
; CHECK:      - Type:            IMPORT
; CHECK:          - Module:          GOT.externref
; CHECK-NEXT:        Field:           g
; CHECK:          - Module:          GOT.externref
; CHECK-NEXT:        Field:           arr

; The exported address-globals are initialized with the module-relative slot
; index (g -> 0, arr -> 1), not a linear-memory address.
; CHECK:      - Type:            GLOBAL
; CHECK:        - Index:           5
; CHECK-NEXT:     Type:            I32
; CHECK-NEXT:     Mutable:         false
; CHECK-NEXT:     InitExpr:
; CHECK-NEXT:       Opcode:          I32_CONST
; CHECK-NEXT:       Value:           0
; CHECK-NEXT:   - Index:           6
; CHECK-NEXT:     Type:            I32
; CHECK-NEXT:     Mutable:         false
; CHECK-NEXT:     InitExpr:
; CHECK-NEXT:       Opcode:          I32_CONST
; CHECK-NEXT:       Value:           1

; CHECK:      - Type:            EXPORT
; CHECK:          - Name:            g{{$}}
; CHECK-NEXT:        Kind:            GLOBAL
; CHECK-NEXT:        Index:           5
; CHECK:          - Name:            arr{{$}}
; CHECK-NEXT:        Kind:            GLOBAL
; CHECK-NEXT:        Index:           6
