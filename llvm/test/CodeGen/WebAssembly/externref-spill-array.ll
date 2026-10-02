; RUN: llc < %s --mtriple=wasm32-unknown-unknown -asm-verbose=false -mattr=+reference-types | FileCheck %s
; RUN: llc < %s --mtriple=wasm64-unknown-unknown -asm-verbose=false -mattr=+reference-types | FileCheck %s
; RUN: llc < %s --mtriple=wasm32-unknown-unknown -mattr=+reference-types -stop-after=wasm-ref-type-mem2local | FileCheck %s --check-prefix=IR

; A local array of externref cannot be a wasm local, so it is always spilled to
; a run of consecutive __externref_table slots in the externref stack region,
; one slot per element (externref is laid out one byte wide, so element GEPs
; into the alloca are already slot offsets off the base). Several spilled
; allocas in one function share a single prologue/epilogue and are laid out
; back to back.

%externref = type target("wasm.externref")

declare void @use(ptr addrspace(2))

; A 4-element array reserves 4 slots.
; CHECK-LABEL: array4:
; CHECK:       global.get __externref_stack_pointer
; CHECK:       i32.const -4
; CHECK:       i32.add
; CHECK:       global.set __externref_stack_pointer
; CHECK:       call use
; CHECK:       global.set __externref_stack_pointer
; CHECK:       ref.null_extern
; CHECK:       i32.const 4
; CHECK:       table.fill __externref_table
;
; IR-LABEL: define void @array4(
; IR:         %externref.sp.new = sub i32 %externref.sp, 4
; IR:         %arr{{.*}} = inttoptr i32 %externref.sp.new to ptr
; IR-NOT:     alloca
; IR:         call void @llvm.wasm.table.fill.externref(ptr addrspace(1) @__externref_table, i32 %externref.sp.new, target("wasm.externref") %{{.*}}, i32 4)
define void @array4() {
  %arr = alloca [4 x %externref], align 1
  %p = addrspacecast ptr %arr to ptr addrspace(2)
  call void @use(ptr addrspace(2) %p)
  ret void
}

; A nested array is flattened; a scalar and an array in the same function get
; consecutive runs (scalar at +0, array at +1..+6) and one epilogue clearing 7.
; CHECK-LABEL: mixed:
; CHECK:       i32.const -7
; CHECK:       i32.const 7
; CHECK:       table.fill __externref_table
;
; IR-LABEL: define void @mixed(
; IR:         %externref.sp.new = sub i32 %externref.sp, 7
; IR:         %s{{.*}} = inttoptr i32 %externref.sp.new to ptr
; IR:         %m.slot = add i32 %externref.sp.new, 1
; IR:         %m{{.*}} = inttoptr i32 %m.slot to ptr
define void @mixed() {
  %s = alloca %externref, align 1
  %m = alloca [2 x [3 x %externref]], align 1
  %ps = addrspacecast ptr %s to ptr addrspace(2)
  %pm = addrspacecast ptr %m to ptr addrspace(2)
  call void @use(ptr addrspace(2) %ps)
  call void @use(ptr addrspace(2) %pm)
  ret void
}

; Element accesses are table.get/table.set at base + offset (here the store to
; element 0 at sp - 4 and the load of element 3 folded to sp - 1).
; CHECK-LABEL: elem:
; CHECK:       global.get __externref_stack_pointer
; CHECK:       local.tee [[SP:[0-9]+]]
; CHECK:       i32.const -4
; CHECK:       i32.add
; CHECK:       local.tee [[BASE:[0-9]+]]
; CHECK:       table.set __externref_table
; CHECK:       local.get [[SP]]
; CHECK:       i32.const -1
; CHECK:       i32.add
; CHECK:       table.get __externref_table
define %externref @elem(%externref %v) {
  %arr = alloca [4 x %externref], align 1
  %p = addrspacecast ptr %arr to ptr addrspace(2)
  store %externref %v, ptr addrspace(2) %p, align 1
  %e3 = getelementptr inbounds [4 x %externref], ptr addrspace(2) %p, i32 0, i32 3
  %r = load %externref, ptr addrspace(2) %e3, align 1
  call void @use(ptr addrspace(2) %p)
  ret %externref %r
}
