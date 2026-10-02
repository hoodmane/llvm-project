; RUN: not --crash llc < %s -mattr=+reference-types -stop-after=wasm-ref-type-mem2local 2>&1 | FileCheck %s

; Scalars and arrays of externref can be spilled to the externref table (see
; externref-spill.ll, externref-spill-array.ll), but a struct containing an
; externref cannot: its other fields need linear memory while the externref
; cannot live there.

target triple = "wasm32-unknown-unknown"

%externref = type target("wasm.externref")
%mixed = type { i32, %externref }

declare void @take_ptr(ptr)

define void @struct_with_externref() {
entry:
  %s = alloca %mixed, align 4
  call void @take_ptr(ptr %s)
  ret void
}

; CHECK: WebAssembly: cannot allocate an aggregate containing externref (only scalars and arrays of externref can be spilled to the externref table)
