# RUN: llvm-mc -filetype=obj -triple=wasm32-unknown-unknown -mattr=+reference-types %s -o %t.o

## R_WASM_EXTERNREF_TABLE_INDEX_LEB relocations request a slot in the
## linker-synthesized __externref_table for the referenced data symbol.  Slot 0
## is reserved as the canonical null externref, and the linker allocates one
## slot per distinct symbol after it (the table's bss region), resolving each
## relocation to that slot's index.  Here g0 is referenced twice and g1 once, so
## two slots are allocated after the reserved null slot: g0 -> 1, g1 -> 2.
# RUN: wasm-ld --extra-features=reference-types --no-entry --export=_start \
# RUN:     -o %t.wasm %t.o
# RUN: obj2yaml %t.wasm | FileCheck %s
# RUN: llvm-objdump -d %t.wasm | FileCheck %s --check-prefix=DIS

## The reserved null slot and the bss slots precede the spill stack region, so
## with two slots, the reserved slot, and -z externref-stack-size=4 the
## boundaries shift by B = N + 1 = 3:
##   data_end = 3, stack_low = 3, stack_high = 7, stack_pointer = 7,
##   heap_base = 7, and the table minimum size is B + S = 7.
# RUN: wasm-ld --extra-features=reference-types --no-entry --export=_start \
# RUN:     -z externref-stack-size=4 -o %t.stack.wasm %t.o
# RUN: obj2yaml %t.stack.wasm | FileCheck %s --check-prefix=STACK

## --export-all emits the region boundary globals, confirming the shifted
## layout: data_end = stack_low = 3, and stack_high = stack_pointer =
## heap_base = 7.
# RUN: wasm-ld --extra-features=reference-types --no-entry --export-all \
# RUN:     -z externref-stack-size=4 -o %t.exp.wasm %t.o
# RUN: obj2yaml %t.exp.wasm | FileCheck %s --check-prefix=GLOBALS

## In relocatable mode no slots are allocated and no __externref_table is
## synthesized; the relocations are passed through for the final link to
## resolve.  The resulting object must be re-linkable (it would not be if a
## defined table without a symbol-table entry leaked into the output).
# RUN: wasm-ld --extra-features=reference-types -r -o %t.reloc.o %t.o
# RUN: obj2yaml %t.reloc.o | FileCheck %s --check-prefix=RELOC
# RUN: wasm-ld --extra-features=reference-types --no-entry --export=_start \
# RUN:     -o %t.relink.wasm %t.reloc.o
# RUN: obj2yaml %t.relink.wasm | FileCheck %s

## No defined TABLE section is emitted in relocatable output, and the three
## externref relocations are preserved.
# RELOC-NOT:  Type:            TABLE
# RELOC:      Type:            CODE
# RELOC:      Relocations:
# RELOC-NEXT:   - Type:            R_WASM_EXTERNREF_TABLE_INDEX_LEB
# RELOC:        - Type:            R_WASM_EXTERNREF_TABLE_INDEX_LEB
# RELOC:        - Type:            R_WASM_EXTERNREF_TABLE_INDEX_LEB

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
  i32.const 0
  nop
  nop
  nop
  nop
  drop
  end_function

.reloc _start+2,  R_WASM_EXTERNREF_TABLE_INDEX_LEB, g0
.reloc _start+9,  R_WASM_EXTERNREF_TABLE_INDEX_LEB, g0
.reloc _start+16, R_WASM_EXTERNREF_TABLE_INDEX_LEB, g1

  .section .data,"",@
g0:
  .int32 0
.size g0, 4
g1:
  .int32 0
.size g1, 4

## A defined externref table is synthesized with minimum size B = N + 1 = 3 (the
## reserved null slot plus two symbol slots, no stack).
# CHECK:      - Type:            TABLE
# CHECK-NEXT:   Tables:
# CHECK-NEXT:     - Index:           0
# CHECK-NEXT:       ElemType:        EXTERNREF
# CHECK-NEXT:       Limits:
# CHECK-NEXT:         Minimum:         0x3

## The three relocations resolve to the per-symbol slot indices after the
## reserved null slot: g0 -> 1 (twice) and g1 -> 2.
# DIS:      i32.const 1
# DIS:      i32.const 1
# DIS:      i32.const 2

# STACK:      - Type:            TABLE
# STACK-NEXT:   Tables:
# STACK-NEXT:     - Index:           0
# STACK-NEXT:       ElemType:        EXTERNREF
# STACK-NEXT:       Limits:
# STACK-NEXT:         Minimum:         0x7

## The boundary globals form a contiguous block holding the shifted region
## indices.  The block is anchored on __externref_data_end, which is the first
## global with value 3 (the preceding globals are 65536/0/1); __externref_stack_low
## immediately follows it and its index is captured as STACK_LOW.  The mutable
## global is the stack pointer.  data_end = stack_low = B = 3, and
## stack_high = stack_pointer = heap_base = B + S = 7.
# GLOBALS:      Globals:
# GLOBALS:          Value:           3
# GLOBALS-NEXT:   - Index:           [[#STACK_LOW:]]
# GLOBALS-NEXT:     Type:            I32
# GLOBALS-NEXT:     Mutable:         false
# GLOBALS-NEXT:     InitExpr:
# GLOBALS-NEXT:       Opcode:          I32_CONST
# GLOBALS-NEXT:       Value:           3
# GLOBALS-NEXT:   - Index:           [[#STACK_LOW+1]]
# GLOBALS-NEXT:     Type:            I32
# GLOBALS-NEXT:     Mutable:         false
# GLOBALS-NEXT:     InitExpr:
# GLOBALS-NEXT:       Opcode:          I32_CONST
# GLOBALS-NEXT:       Value:           7
# GLOBALS-NEXT:   - Index:           [[#STACK_LOW+2]]
# GLOBALS-NEXT:     Type:            I32
# GLOBALS-NEXT:     Mutable:         true
# GLOBALS-NEXT:     InitExpr:
# GLOBALS-NEXT:       Opcode:          I32_CONST
# GLOBALS-NEXT:       Value:           7
# GLOBALS-NEXT:   - Index:           [[#STACK_LOW+3]]
# GLOBALS-NEXT:     Type:            I32
# GLOBALS-NEXT:     Mutable:         false
# GLOBALS-NEXT:     InitExpr:
# GLOBALS-NEXT:       Opcode:          I32_CONST
# GLOBALS-NEXT:       Value:           7
## Confirm the block above is exactly the five externref boundary globals.
# GLOBALS:      GlobalNames:
# GLOBALS:        - Index:           [[#STACK_LOW-1]]
# GLOBALS-NEXT:     Name:            __externref_data_end
# GLOBALS-NEXT:   - Index:           [[#STACK_LOW]]
# GLOBALS-NEXT:     Name:            __externref_stack_low
# GLOBALS-NEXT:   - Index:           [[#STACK_LOW+1]]
# GLOBALS-NEXT:     Name:            __externref_stack_high
# GLOBALS-NEXT:   - Index:           [[#STACK_LOW+2]]
# GLOBALS-NEXT:     Name:            __externref_stack_pointer
# GLOBALS-NEXT:   - Index:           [[#STACK_LOW+3]]
# GLOBALS-NEXT:     Name:            __externref_heap_base
