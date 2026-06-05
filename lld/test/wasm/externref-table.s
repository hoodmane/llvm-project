# RUN: llvm-mc -filetype=obj -triple=wasm32-unknown-unknown -mattr=+reference-types %s -o %t.o

## A reference to the reserved __externref_table symbol causes the linker to
## synthesize a defined externref table.
# RUN: wasm-ld --no-entry --export=_start -o %t.wasm %t.o
# RUN: obj2yaml %t.wasm | FileCheck %s --check-prefix=DEFINED

## With --import-externref-table the reserved table is imported instead.
# RUN: wasm-ld --no-entry --export=_start --import-externref-table -o %t.import.wasm %t.o
# RUN: obj2yaml %t.import.wasm | FileCheck %s --check-prefix=IMPORT

## --import-externref-table and --export-externref-table are mutually exclusive.
# RUN: not wasm-ld --no-entry --import-externref-table --export-externref-table \
# RUN:     -o /dev/null %t.o 2>&1 | FileCheck %s --check-prefix=ERROR

  .tabletype __externref_table, externref

  .globl  _start
_start:
  .functype _start () -> ()
  i32.const 0
  ref.null_extern
  table.set __externref_table
  end_function

# DEFINED:      - Type:            TABLE
# DEFINED-NEXT:   Tables:
# DEFINED-NEXT:     - Index:           0
# DEFINED-NEXT:       ElemType:        EXTERNREF
# DEFINED-NEXT:       Limits:
# DEFINED-NEXT:         Minimum:         0x0

# IMPORT:      - Type:            IMPORT
# IMPORT-NEXT:   Imports:
# IMPORT-NEXT:     - Module:          env
# IMPORT-NEXT:       Field:           __externref_table
# IMPORT-NEXT:       Kind:            TABLE
# IMPORT-NEXT:       Table:
# IMPORT-NEXT:         Index:           0
# IMPORT-NEXT:         ElemType:        EXTERNREF
# IMPORT-NEXT:         Limits:
# IMPORT-NEXT:           Minimum:         0x0

# ERROR: error: --import-externref-table and --export-externref-table may not be used together
