# RUN: llvm-mc -filetype=obj -triple=wasm32-unknown-unknown -mattr=+reference-types %s -o %t.o

## An undefined-weak data symbol targeted by R_WASM_EXTERNREF_TABLE_INDEX_LEB is
## not a statically-allocated slot in this module's __externref_table, so it is
## not assigned a bss slot (mirroring the R_WASM_MEMORY_ADDR_* handling of
## undefined-weak symbols).  Here only the undefined-weak g_weak is referenced,
## so no slot is allocated and the table is not synthesized at all; the
## relocation resolves to the null/tombstone value 0.
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
  end_function

  .weak g_weak
.reloc _start+2, R_WASM_EXTERNREF_TABLE_INDEX_LEB, g_weak

## No externref slot is allocated, so no defined externref table is emitted.
# CHECK-NOT: ElemType:        EXTERNREF

## The undefined-weak reference resolves to the null/tombstone value 0.
# DIS: i32.const 0
