; RUN: llc < %s --mtriple=wasm32-unknown-unknown -asm-verbose=false -mattr=+reference-types | FileCheck %s --check-prefixes=CHECK
; RUN: llc < %s --mtriple=wasm64-unknown-unknown -asm-verbose=false -mattr=+reference-types | FileCheck %s --check-prefixes=CHECK,WASM64

; An externref cannot live in linear memory, so when its address is taken it
; cannot be promoted to a WebAssembly local either. Instead it is spilled to the
; externref stack region of the linker-synthesized __externref_table: the
; function reserves a run of table slots in its prologue by decrementing the
; mutable __externref_stack_pointer global (the stack grows down), and the
; address of the spilled externref is that slot's index. Loads and stores of the
; externref through such a pointer become table.get / table.set
; __externref_table. Before returning, the stack pointer is restored and the
; used slots are cleared with table.fill ... ref.null so the references are not
; pinned by the GC after the frame is gone.
;
; The externref table is i32-indexed even under wasm64, so the stack pointer
; global and all slot arithmetic are i32; the index is only zero-extended to
; pointer width where it is used as a `ptr` (here: passed to @use).

%externref = type target("wasm.externref")

; CHECK: .tabletype __externref_table, externref

declare void @use(ptr)

; CHECK-LABEL: spill_one:
; CHECK:       .functype spill_one (externref) -> (externref)
; The prologue reads __externref_stack_pointer and subtracts one slot.
; CHECK:       global.get __externref_stack_pointer
; CHECK:       i32.const -1
; CHECK:       i32.add
; The incoming value is stored into the reserved slot.
; CHECK:       table.set __externref_table
; The decremented stack pointer is written back, then the slot index (== the
; pointer, zero-extended to pointer width on wasm64) is passed to the callee.
; CHECK:       global.set __externref_stack_pointer
; WASM64:      i64.extend_i32_u
; CHECK:       call use
; The stack pointer is restored, the spilled value reloaded, and the slot
; cleared with ref.null before returning.
; CHECK:       global.set __externref_stack_pointer
; CHECK:       table.get __externref_table
; CHECK:       ref.null_extern
; CHECK:       i32.const 1
; CHECK:       table.fill __externref_table
; CHECK:       end_function
define %externref @spill_one(%externref %v) {
  %slot = alloca %externref, align 1
  store %externref %v, ptr %slot, align 1
  call void @use(ptr %slot)
  %r = load %externref, ptr %slot, align 1
  ret %externref %r
}

; Two address-taken externrefs share one prologue/epilogue and reserve two
; consecutive slots; the epilogue clears both with a single table.fill of two.
; CHECK-LABEL: spill_two:
; CHECK:       i32.const -2
; CHECK:       global.set __externref_stack_pointer
; CHECK:       call use
; CHECK:       call use
; CHECK:       global.set __externref_stack_pointer
; CHECK:       ref.null_extern
; CHECK:       i32.const 2
; CHECK:       table.fill __externref_table
; CHECK:       end_function
define void @spill_two(%externref %a, %externref %b) {
  %p = alloca %externref, align 1
  %q = alloca %externref, align 1
  store %externref %a, ptr %p, align 1
  store %externref %b, ptr %q, align 1
  call void @use(ptr %p)
  call void @use(ptr %q)
  ret void
}

; An externref whose address does not escape is still promoted to a local and
; needs neither the stack pointer nor table.fill.
; CHECK-LABEL: no_spill:
; CHECK-NOT:   __externref_stack_pointer
; CHECK-NOT:   table.fill
; CHECK:       end_function
define %externref @no_spill(%externref %v) {
  %slot = alloca %externref, align 1
  store %externref %v, ptr %slot, align 1
  %r = load %externref, ptr %slot, align 1
  ret %externref %r
}

; The externref spill stack pointer is a mutable i32 global regardless of
; pointer width, because the externref table is always i32-indexed.
; CHECK: .globaltype __externref_stack_pointer, i32{{$}}
