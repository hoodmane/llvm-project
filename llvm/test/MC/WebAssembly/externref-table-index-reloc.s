# RUN: llvm-mc -triple=wasm32-unknown-unknown -mattr=+reference-types %s | FileCheck --check-prefix=PRINT %s
# RUN: llvm-mc -triple=wasm32-unknown-unknown -mattr=+reference-types -filetype=obj %s -o - | llvm-readobj -r --expand-relocs - | FileCheck %s
# RUN: llvm-mc -triple=wasm32-unknown-unknown -mattr=+reference-types -filetype=obj %s -o - | obj2yaml | FileCheck --check-prefix=YAML %s

# Test the R_WASM_EXTERNREF_TABLE_INDEX_LEB relocation, which carries a symbol
# whose link-time value is its slot index in __externref_table.

get_slot:
  .functype get_slot () -> (i32)
  i32.const 0
  nop # 4 NOPs in addition to one zero in i32.const 0 for a canonical 5 byte ULEB.
  nop
  nop
  nop
  i32.const 0
  nop
  nop
  nop
  nop
  i32.add
  end_function

# PRINT: .reloc get_slot+2, R_WASM_EXTERNREF_TABLE_INDEX_LEB, externref_global
.reloc get_slot + 2, R_WASM_EXTERNREF_TABLE_INDEX_LEB, externref_global
# The _REL_ variant (emitted under PIC) carries the slot index relative to
# __externref_table_base.
# PRINT: .reloc get_slot+8, R_WASM_EXTERNREF_TABLE_INDEX_REL_LEB, externref_global
.reloc get_slot + 8, R_WASM_EXTERNREF_TABLE_INDEX_REL_LEB, externref_global

.section .data,"",@
externref_global:
  .int32 0
.size externref_global, 4

# CHECK:      Section ({{.*}}) CODE {
# CHECK-NEXT:   Relocation {
# CHECK-NEXT:     Type: R_WASM_EXTERNREF_TABLE_INDEX_LEB (27)
# CHECK-NEXT:     Offset: 0x4
# CHECK-NEXT:     Symbol: externref_global
# CHECK-NEXT:   }
# CHECK-NEXT:   Relocation {
# CHECK-NEXT:     Type: R_WASM_EXTERNREF_TABLE_INDEX_REL_LEB (28)
# CHECK-NEXT:     Offset: 0xA
# CHECK-NEXT:     Symbol: externref_global
# CHECK-NEXT:   }
# CHECK-NEXT: }

# YAML:      - Type:            R_WASM_EXTERNREF_TABLE_INDEX_LEB
# YAML-NEXT:        Index:           {{[0-9]+}}
# YAML-NEXT:        Offset:          0x4
# YAML-NEXT:      - Type:            R_WASM_EXTERNREF_TABLE_INDEX_REL_LEB
# YAML-NEXT:        Index:           {{[0-9]+}}
# YAML-NEXT:        Offset:          0xA
