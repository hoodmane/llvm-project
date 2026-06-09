# RUN: llvm-mc -filetype=obj -triple=wasm32-unknown-unknown -mattr=+reference-types %s -o %t.o

## A global externref *array* (`__externref_t arr[N];`) is a single data symbol
## that occupies N contiguous slots in the linker-synthesized __externref_table:
## element arr[i] is accessed as `<slot of arr> + i`, with the per-element
## offset materialized at runtime rather than folded into the relocation.  The
## linker must therefore reserve one slot per element (the symbol's size), not a
## single slot per symbol, so that the whole array fits and the table is sized
## large enough.  Here the scalar g takes one slot and the 10-element arr takes
## ten, after the reserved null slot: g -> 1, arr -> 2 (occupying slots 2..11).
# RUN: wasm-ld --extra-features=reference-types --no-entry --export=_start \
# RUN:     -o %t.wasm %t.o
# RUN: obj2yaml %t.wasm | FileCheck %s
# RUN: llvm-objdump -d %t.wasm | FileCheck %s --check-prefix=DIS

  .globl  _start
_start:
  .functype _start () -> ()
  i32.const 0
  nop
  nop
  nop
  nop
  drop
  i32.const 0
  nop
  nop
  nop
  nop
  drop
  end_function

.reloc _start+2, R_WASM_EXTERNREF_TABLE_INDEX_LEB, g
.reloc _start+9, R_WASM_EXTERNREF_TABLE_INDEX_LEB, arr

  .section .data,"",@
g:
  .int8 0
.size g, 1
arr:
  .zero 10
.size arr, 10

## The table covers the reserved null slot (1) + g (1) + arr (10) = 12 slots.
# CHECK:      - Type:            TABLE
# CHECK-NEXT:   Tables:
# CHECK-NEXT:     - Index:           0
# CHECK-NEXT:       ElemType:        EXTERNREF
# CHECK-NEXT:       Limits:
# CHECK-NEXT:         Minimum:         0xC

## g resolves to the first slot after the reserved null slot, and arr to the
## slot immediately after g; the ten array elements then occupy slots 2..11.
# DIS:      i32.const 1
# DIS:      i32.const 2
