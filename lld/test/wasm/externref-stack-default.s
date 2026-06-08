# RUN: llvm-mc -filetype=obj -triple=wasm32-unknown-unknown -mattr=+reference-types %s -o %t.o

## The positive default externref spill stack size is applied only when the
## program actually spills, which the compiler signals by referencing
## __externref_stack_pointer.  This object references the other region boundary
## globals (here __externref_heap_base) but NOT the stack pointer, so no spill
## stack is needed: an externref table is still synthesized (the heap region is
## in use) but its stack region stays empty and its minimum size is 0.
# RUN: wasm-ld --extra-features=reference-types --no-entry --export=_start \
# RUN:     -o %t.wasm %t.o
# RUN: obj2yaml %t.wasm | FileCheck %s

  .globaltype __externref_heap_base, i32, immutable

  .globl  _start
_start:
  .functype _start () -> ()
  global.get __externref_heap_base
  drop
  end_function

# CHECK:      - Type:            TABLE
# CHECK-NEXT:   Tables:
# CHECK-NEXT:     - Index:           0
# CHECK-NEXT:       ElemType:        EXTERNREF
# CHECK-NEXT:       Limits:
# CHECK-NEXT:         Minimum:         0x0
