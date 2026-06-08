# RUN: llvm-mc -filetype=obj -triple=wasm32-unknown-unknown -mattr=+reference-types %s -o %t.o

## The externref region boundary globals are only provided when the
## reference-types feature is enabled (here via --extra-features).  Referencing
## them causes the linker to synthesize the __externref_table and lay it out
## into bss / stack / heap regions.  With -z externref-stack-size=16 and no bss
## slots.  Mirroring the linear-memory __stack_pointer, the
## externref stack pointer starts at the high end of the stack region and grows
## downward:
##   data_end   = 0
##   stack_low  = 0,  stack_high = 16,  stack_pointer = 16
##   heap_base  = 16
## and the table's minimum size is N + S = 16.
# RUN: wasm-ld --extra-features=reference-types --no-entry --export=_start \
# RUN:     -z externref-stack-size=16 -o %t.wasm %t.o
# RUN: obj2yaml %t.wasm | FileCheck %s

## Without an explicit -z externref-stack-size, a program that references
## __externref_stack_pointer (i.e. the compiler emitted spill code) still needs
## a real spill stack, so the linker reserves a positive default (1024 slots)
## rather than an empty region that would trap.  The stack pointer starts at the
## high end and grows down:
##   data_end   = 0
##   stack_low  = 0,  stack_high = 1024,  stack_pointer = 1024
##   heap_base  = 1024
## and the table's minimum size is the default 1024 (0x400).
# RUN: wasm-ld --extra-features=reference-types --no-entry --export=_start \
# RUN:     -o %t.default.wasm %t.o
# RUN: obj2yaml %t.default.wasm | FileCheck %s --check-prefix=DEFAULT

## An explicit -z externref-stack-size=0 opts back out of the default and leaves
## the stack region empty (table minimum size 0).
# RUN: wasm-ld --extra-features=reference-types --no-entry --export=_start \
# RUN:     -z externref-stack-size=0 -o %t.nostack.wasm %t.o
# RUN: obj2yaml %t.nostack.wasm | FileCheck %s --check-prefix=NOSTACK

## Without the reference-types feature the boundary globals are not provided, so
## the references remain undefined.
# RUN: not wasm-ld --no-entry --export=_start \
# RUN:     -o /dev/null %t.o 2>&1 | FileCheck %s --check-prefix=NOREFTYPES

## -z externref-stack-size requires the reference-types feature; otherwise no
## externref table or stack region can be laid out, so the link is rejected.
# RUN: not wasm-ld --no-entry --export=_start -z externref-stack-size=16 \
# RUN:     -o /dev/null %t.o 2>&1 | FileCheck %s --check-prefix=NOSTACKREFTYPES

## Under wasm64 the externref table is i64-indexed and the boundary globals
## are i64, mirroring the indirect function table.
# RUN: llvm-mc -filetype=obj -triple=wasm64-unknown-unknown -mattr=+reference-types %s -o %t.64.o
# RUN: wasm-ld -mwasm64 --extra-features=reference-types --no-entry \
# RUN:     --export=_start -z externref-stack-size=16 -o %t.64.wasm %t.64.o
# RUN: obj2yaml %t.64.wasm | FileCheck %s --check-prefix=WASM64

  .globaltype __externref_data_end, i32, immutable
  .globaltype __externref_stack_low, i32, immutable
  .globaltype __externref_stack_high, i32, immutable
  .globaltype __externref_stack_pointer, i32
  .globaltype __externref_heap_base, i32, immutable

  .globl  _start
_start:
  .functype _start () -> ()
  global.get __externref_data_end
  drop
  global.get __externref_stack_low
  drop
  global.get __externref_stack_high
  drop
  global.get __externref_stack_pointer
  global.set __externref_stack_pointer
  global.get __externref_heap_base
  drop
  end_function

# CHECK:        - Type:            TABLE
# CHECK-NEXT:     Tables:
# CHECK-NEXT:       - Index:           0
# CHECK-NEXT:         ElemType:        EXTERNREF
# CHECK-NEXT:         Limits:
# CHECK-NEXT:           Minimum:         0x10
# CHECK:        - Type:            GLOBAL
# CHECK-NEXT:     Globals:
# CHECK-NEXT:       - Index:           0
# CHECK-NEXT:         Type:            I32
# CHECK-NEXT:         Mutable:         false
# CHECK-NEXT:         InitExpr:
# CHECK-NEXT:           Opcode:          I32_CONST
# CHECK-NEXT:           Value:           0
# CHECK-NEXT:       - Index:           1
# CHECK-NEXT:         Type:            I32
# CHECK-NEXT:         Mutable:         false
# CHECK-NEXT:         InitExpr:
# CHECK-NEXT:           Opcode:          I32_CONST
# CHECK-NEXT:           Value:           0
# CHECK-NEXT:       - Index:           2
# CHECK-NEXT:         Type:            I32
# CHECK-NEXT:         Mutable:         false
# CHECK-NEXT:         InitExpr:
# CHECK-NEXT:           Opcode:          I32_CONST
# CHECK-NEXT:           Value:           16
# CHECK-NEXT:       - Index:           3
# CHECK-NEXT:         Type:            I32
# CHECK-NEXT:         Mutable:         true
# CHECK-NEXT:         InitExpr:
# CHECK-NEXT:           Opcode:          I32_CONST
# CHECK-NEXT:           Value:           16
# CHECK-NEXT:       - Index:           4
# CHECK-NEXT:         Type:            I32
# CHECK-NEXT:         Mutable:         false
# CHECK-NEXT:         InitExpr:
# CHECK-NEXT:           Opcode:          I32_CONST
# CHECK-NEXT:           Value:           16
# CHECK:      GlobalNames:
# CHECK-NEXT:   - Index:           0
# CHECK-NEXT:     Name:            __externref_data_end
# CHECK-NEXT:   - Index:           1
# CHECK-NEXT:     Name:            __externref_stack_low
# CHECK-NEXT:   - Index:           2
# CHECK-NEXT:     Name:            __externref_stack_high
# CHECK-NEXT:   - Index:           3
# CHECK-NEXT:     Name:            __externref_stack_pointer
# CHECK-NEXT:   - Index:           4
# CHECK-NEXT:     Name:            __externref_heap_base

# DEFAULT:      - Type:            TABLE
# DEFAULT-NEXT:   Tables:
# DEFAULT-NEXT:     - Index:           0
# DEFAULT-NEXT:       ElemType:        EXTERNREF
# DEFAULT-NEXT:       Limits:
# DEFAULT-NEXT:         Minimum:         0x400
# DEFAULT:        - Type:            GLOBAL
# DEFAULT-NEXT:     Globals:
## __externref_data_end = 0
# DEFAULT-NEXT:       - Index:           0
# DEFAULT-NEXT:         Type:            I32
# DEFAULT-NEXT:         Mutable:         false
# DEFAULT-NEXT:         InitExpr:
# DEFAULT-NEXT:           Opcode:          I32_CONST
# DEFAULT-NEXT:           Value:           0
## __externref_stack_low = 0
# DEFAULT-NEXT:       - Index:           1
# DEFAULT-NEXT:         Type:            I32
# DEFAULT-NEXT:         Mutable:         false
# DEFAULT-NEXT:         InitExpr:
# DEFAULT-NEXT:           Opcode:          I32_CONST
# DEFAULT-NEXT:           Value:           0
## __externref_stack_high = 1024
# DEFAULT-NEXT:       - Index:           2
# DEFAULT-NEXT:         Type:            I32
# DEFAULT-NEXT:         Mutable:         false
# DEFAULT-NEXT:         InitExpr:
# DEFAULT-NEXT:           Opcode:          I32_CONST
# DEFAULT-NEXT:           Value:           1024
## __externref_stack_pointer = 1024 (mutable, grows down)
# DEFAULT-NEXT:       - Index:           3
# DEFAULT-NEXT:         Type:            I32
# DEFAULT-NEXT:         Mutable:         true
# DEFAULT-NEXT:         InitExpr:
# DEFAULT-NEXT:           Opcode:          I32_CONST
# DEFAULT-NEXT:           Value:           1024
## __externref_heap_base = 1024
# DEFAULT-NEXT:       - Index:           4
# DEFAULT-NEXT:         Type:            I32
# DEFAULT-NEXT:         Mutable:         false
# DEFAULT-NEXT:         InitExpr:
# DEFAULT-NEXT:           Opcode:          I32_CONST
# DEFAULT-NEXT:           Value:           1024

# NOSTACK:      - Type:            TABLE
# NOSTACK-NEXT:   Tables:
# NOSTACK-NEXT:     - Index:           0
# NOSTACK-NEXT:       ElemType:        EXTERNREF
# NOSTACK-NEXT:       Limits:
# NOSTACK-NEXT:         Minimum:         0x0

# NOREFTYPES: undefined symbol: __externref_data_end

# NOSTACKREFTYPES: -z externref-stack-size requires the reference-types feature

# WASM64:      - Type:            TABLE
# WASM64-NEXT:   Tables:
# WASM64-NEXT:     - Index:           0
# WASM64-NEXT:       ElemType:        EXTERNREF
# WASM64-NEXT:       Limits:
# WASM64-NEXT:         Flags:           [ IS_64 ]
# WASM64-NEXT:         Minimum:         0x10
# WASM64:      - Type:            GLOBAL
# WASM64-NEXT:   Globals:
# WASM64-NEXT:     - Index:           0
# WASM64-NEXT:       Type:            I64
