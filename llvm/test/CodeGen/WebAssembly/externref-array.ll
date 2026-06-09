; RUN: llc < %s --mtriple=wasm32-unknown-unknown -asm-verbose=false -mattr=+reference-types | FileCheck %s
; RUN: llc < %s --mtriple=wasm64-unknown-unknown -asm-verbose=false -mattr=+reference-types | FileCheck %s

; Under PIC the slot index is computed relative to __externref_table_base (see
; externref-global.ll); an array-element offset is added on top of that base.
; RUN: llc < %s --mtriple=wasm32-unknown-unknown -asm-verbose=false -mattr=+reference-types -relocation-model=pic | FileCheck %s --check-prefixes=PIC
; RUN: llc < %s --mtriple=wasm64-unknown-unknown -asm-verbose=false -mattr=+reference-types -relocation-model=pic | FileCheck %s --check-prefixes=PIC,PIC64

; An array global of externref `__externref_t c[N]` lowers to
; `[N x ptr addrspace(10)]`. Each element is one byte wide (p10:8:8), so the
; byte offset into the array equals the slot offset within the array's region of
; the linker-synthesized __externref_table. Element accesses lower to
; table.get/table.set whose slot index is the array's base slot
; (i32.const c@EXTERNREF_TABLE_INDEX) plus the element offset.

%externref = type ptr addrspace(10) ;; addrspace 10 is nonintegral

; CHECK: .tabletype __externref_table, externref
; PIC: .tabletype __externref_table, externref

@c = hidden global [10 x %externref] zeroinitializer, align 1

; A constant index folds to an i32.const element offset added to the base slot.
define %externref @get_const() {
; CHECK-LABEL: get_const:
; CHECK-NEXT:  .functype       get_const () -> (externref)
; CHECK-NEXT:  i32.const       c@EXTERNREF_TABLE_INDEX
; CHECK-NEXT:  i32.const       3
; CHECK-NEXT:  i32.add
; CHECK-NEXT:  table.get       __externref_table
; CHECK-NEXT:  end_function
;
; PIC-LABEL:    get_const:
; PIC-NEXT:     .functype       get_const () -> (externref)
; PIC-NEXT:     global.get      __externref_table_base
; PIC64-NEXT:   i32.wrap_i64
; PIC-NEXT:     i32.const       c@EXTERNREF_TABLE_INDEX_REL
; PIC-NEXT:     i32.add
; PIC-NEXT:     i32.const       3
; PIC-NEXT:     i32.add
; PIC-NEXT:     table.get       __externref_table
; PIC-NEXT:     end_function
  %v = load %externref, ptr getelementptr inbounds nuw (i8, ptr @c, i32 3), align 1
  ret %externref %v
}

; A dynamic index is added to the base slot. (An i32 index needs no widening on
; wasm64: the address-arithmetic zext folds against the slot-index truncation.)
define %externref @get_dynamic(i32 %i) {
; CHECK-LABEL: get_dynamic:
; CHECK-NEXT:  .functype       get_dynamic (i32) -> (externref)
; CHECK-NEXT:  i32.const       c@EXTERNREF_TABLE_INDEX
; CHECK-NEXT:  local.get       0
; CHECK-NEXT:  i32.add
; CHECK-NEXT:  table.get       __externref_table
; CHECK-NEXT:  end_function
;
; PIC-LABEL:    get_dynamic:
; PIC-NEXT:     .functype       get_dynamic (i32) -> (externref)
; PIC-NEXT:     global.get      __externref_table_base
; PIC64-NEXT:   i32.wrap_i64
; PIC-NEXT:     i32.const       c@EXTERNREF_TABLE_INDEX_REL
; PIC-NEXT:     i32.add
; PIC-NEXT:     local.get       0
; PIC-NEXT:     i32.add
; PIC-NEXT:     table.get       __externref_table
; PIC-NEXT:     end_function
  %p = getelementptr inbounds %externref, ptr @c, i32 %i
  %v = load %externref, ptr %p, align 1
  ret %externref %v
}

define void @set_dynamic(i32 %i, %externref %v) {
; CHECK-LABEL: set_dynamic:
; CHECK-NEXT:  .functype       set_dynamic (i32, externref) -> ()
; CHECK-NEXT:  i32.const       c@EXTERNREF_TABLE_INDEX
; CHECK-NEXT:  local.get       0
; CHECK-NEXT:  i32.add
; CHECK-NEXT:  local.get       1
; CHECK-NEXT:  table.set       __externref_table
; CHECK-NEXT:  end_function
  %p = getelementptr inbounds %externref, ptr @c, i32 %i
  store %externref %v, ptr %p, align 1
  ret void
}

; A second array gets its own base slot symbol.
@d = hidden global [4 x %externref] zeroinitializer, align 1

define %externref @get_other(i32 %i) {
; CHECK-LABEL: get_other:
; CHECK-NEXT:  .functype       get_other (i32) -> (externref)
; CHECK-NEXT:  i32.const       d@EXTERNREF_TABLE_INDEX
; CHECK-NEXT:  local.get       0
; CHECK-NEXT:  i32.add
; CHECK-NEXT:  table.get       __externref_table
; CHECK-NEXT:  end_function
  %p = getelementptr inbounds %externref, ptr @d, i32 %i
  %v = load %externref, ptr %p, align 1
  ret %externref %v
}

; A multidimensional array flattens to a single slot-offset computation added to
; the array's base slot.
@e = hidden global [3 x [4 x %externref]] zeroinitializer, align 1

define %externref @get_2d(i32 %i, i32 %j) {
; The element offset is the flattened index i*4 + j (the i*4 as a shl by 2),
; added to the array's base slot. The checks match the common op sequence on
; both wasm32 and wasm64 (wasm64 interleaves i64 extend/wrap around the add).
; CHECK-LABEL: get_2d:
; CHECK-NEXT:  .functype       get_2d (i32, i32) -> (externref)
; CHECK:       i32.const       e@EXTERNREF_TABLE_INDEX
; CHECK:       local.get       0
; CHECK:       i32.const       2
; CHECK:       i32.shl
; CHECK:       local.get       1
; CHECK:       table.get       __externref_table
; CHECK-NEXT:  end_function
  %p = getelementptr inbounds [4 x %externref], ptr @e, i32 %i, i32 %j
  %v = load %externref, ptr %p, align 1
  ret %externref %v
}
