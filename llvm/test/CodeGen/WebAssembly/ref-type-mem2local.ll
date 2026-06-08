; RUN: llc < %s -mattr=+reference-types -stop-after=wasm-ref-type-mem2local | FileCheck %s
; RUN: llc < %s -stop-after=wasm-ref-type-mem2local | FileCheck %s --check-prefix=ATTR

target triple = "wasm32-unknown-unknown"

%externref = type ptr addrspace(10)
%funcref = type ptr addrspace(20)

declare %externref @get_externref()
declare %funcref @get_funcref()
declare i32 @get_i32()
declare void @take_externref(%externref)
declare void @take_funcref(%funcref)
declare void @take_i32(i32)

; Spilling an address-taken externref declares the linker-synthesized externref
; stack pointer and table (emitted at module scope).
; CHECK-DAG: @__externref_stack_pointer = external addrspace(1) global i32
; CHECK-DAG: @__externref_table = external addrspace(1) global [0 x ptr addrspace(10)]

; Reference type allocas should be moved to addrspace(1)
; CHECK-LABEL: @test_ref_type_mem2local
define void @test_ref_type_mem2local() {
entry:
  %alloc.externref = alloca %externref, align 1
  %eref = call %externref @get_externref()
  store %externref %eref, ptr %alloc.externref, align 1
  %eref.loaded = load %externref, ptr %alloc.externref, align 1
  call void @take_externref(%externref %eref.loaded)
  ; CHECK:      %alloc.externref.var = alloca ptr addrspace(10), align 1, addrspace(1)
  ; CHECK-NEXT: %eref = call ptr addrspace(10) @get_externref()
  ; CHECK-NEXT: store ptr addrspace(10) %eref, ptr addrspace(1) %alloc.externref.var, align 1
  ; CHECK-NEXT: %eref.loaded = load ptr addrspace(10), ptr addrspace(1) %alloc.externref.var, align 1
  ; CHECK-NEXT: call void @take_externref(ptr addrspace(10) %eref.loaded)

  %alloc.funcref = alloca %funcref, align 1
  %fref = call %funcref @get_funcref()
  store %funcref %fref, ptr %alloc.funcref, align 1
  %fref.loaded = load %funcref, ptr %alloc.funcref, align 1
  call void @take_funcref(%funcref %fref.loaded)
  ; CHECK-NEXT: %alloc.funcref.var = alloca ptr addrspace(20), align 1, addrspace(1)
  ; CHECK-NEXT: %fref = call ptr addrspace(20) @get_funcref()
  ; CHECK-NEXT: store ptr addrspace(20) %fref, ptr addrspace(1) %alloc.funcref.var, align 1
  ; CHECK-NEXT: %fref.loaded = load ptr addrspace(20), ptr addrspace(1) %alloc.funcref.var, align 1
  ; CHECK-NEXT: call void @take_funcref(ptr addrspace(20) %fref.loaded)

  ret void
}

; An address-taken externref cannot become a local. It is spilled to the
; externref table stack region: the function reserves a slot by subtracting
; from __externref_stack_pointer in the prologue (the stack grows down) and
; restores it before returning. The alloca's address becomes the slot index.
; Before returning, the used slots are cleared with table.fill ... ref.null so
; the references are not pinned by the GC after the frame is gone.
declare void @take_externref_ptr(ptr)
; CHECK-LABEL: @test_externref_addr_taken
define void @test_externref_addr_taken() {
entry:
  %slot = alloca %externref, align 1
  %eref = call %externref @get_externref()
  store %externref %eref, ptr %slot, align 1
  call void @take_externref_ptr(ptr %slot)
  ret void
  ; CHECK:      %externref.sp = load i32, ptr addrspace(1) @__externref_stack_pointer, align 4
  ; CHECK-NEXT: %externref.sp.new = sub i32 %externref.sp, 1
  ; CHECK-NEXT: store i32 %externref.sp.new, ptr addrspace(1) @__externref_stack_pointer, align 4
  ; CHECK-NEXT: %[[SLOT:[^ ]+]] = inttoptr i32 %externref.sp.new to ptr
  ; CHECK-NEXT: %eref = call ptr addrspace(10) @get_externref()
  ; CHECK-NEXT: store ptr addrspace(10) %eref, ptr %[[SLOT]], align 1
  ; CHECK-NEXT: call void @take_externref_ptr(ptr %[[SLOT]])
  ; CHECK-NEXT: store i32 %externref.sp, ptr addrspace(1) @__externref_stack_pointer, align 4
  ; CHECK-NEXT: %[[NULL:[^ ]+]] = call ptr addrspace(10) @llvm.wasm.ref.null.extern()
  ; CHECK-NEXT: call void @llvm.wasm.table.fill.externref(ptr addrspace(1) @__externref_table, i32 %externref.sp.new, ptr addrspace(10) %[[NULL]], i32 1)
  ; CHECK-NEXT: ret void
}

; Multiple address-taken externrefs share a single prologue/epilogue and get
; consecutive slots; the epilogue clears all of them with a single table.fill.
; CHECK-LABEL: @test_externref_addr_taken_multi
define void @test_externref_addr_taken_multi() {
entry:
  %slot0 = alloca %externref, align 1
  %slot1 = alloca %externref, align 1
  call void @take_externref_ptr(ptr %slot0)
  call void @take_externref_ptr(ptr %slot1)
  ret void
  ; CHECK:      %externref.sp = load i32, ptr addrspace(1) @__externref_stack_pointer, align 4
  ; CHECK-NEXT: %externref.sp.new = sub i32 %externref.sp, 2
  ; CHECK-NEXT: store i32 %externref.sp.new, ptr addrspace(1) @__externref_stack_pointer, align 4
  ; CHECK-NEXT: %[[S0:[^ ]+]] = inttoptr i32 %externref.sp.new to ptr
  ; CHECK-NEXT: %[[S1IDX:[^ ]+]] = add i32 %externref.sp.new, 1
  ; CHECK-NEXT: %[[S1:[^ ]+]] = inttoptr i32 %[[S1IDX]] to ptr
  ; CHECK-NEXT: call void @take_externref_ptr(ptr %[[S0]])
  ; CHECK-NEXT: call void @take_externref_ptr(ptr %[[S1]])
  ; CHECK-NEXT: store i32 %externref.sp, ptr addrspace(1) @__externref_stack_pointer, align 4
  ; CHECK-NEXT: %[[NULL:[^ ]+]] = call ptr addrspace(10) @llvm.wasm.ref.null.extern()
  ; CHECK-NEXT: call void @llvm.wasm.table.fill.externref(ptr addrspace(1) @__externref_table, i32 %externref.sp.new, ptr addrspace(10) %[[NULL]], i32 2)
  ; CHECK-NEXT: ret void
}

; POD type allocas should stay the same
; CHECK-LABEL: @test_pod_type
define void @test_pod_type() {
entry:
  %alloc.i32 = alloca i32
  %i32 = call i32 @get_i32()
  store i32 %i32, ptr %alloc.i32
  %i32.loaded = load i32, ptr %alloc.i32
  call void @take_i32(i32 %i32.loaded)
  ; CHECK: %alloc.i32 = alloca i32, align 4{{$}}
  ; CHECK-NOT: alloca i32 {{.*}} addrspace(1)

  ret void
}

; The same function as test_ref_type_mem2local, but here +reference-types is
; given in the function attribute.
; Reference type allocas should be moved to addrspace(1)
; ATTR-LABEL: @test_ref_type_mem2local_func_attr
define void @test_ref_type_mem2local_func_attr() #0 {
entry:
  %alloc.externref = alloca %externref, align 1
  %eref = call %externref @get_externref()
  store %externref %eref, ptr %alloc.externref, align 1
  %eref.loaded = load %externref, ptr %alloc.externref, align 1
  call void @take_externref(%externref %eref.loaded)
  ; ATTR:      %alloc.externref.var = alloca ptr addrspace(10), align 1, addrspace(1)
  ; ATTR-NEXT: %eref = call ptr addrspace(10) @get_externref()
  ; ATTR-NEXT: store ptr addrspace(10) %eref, ptr addrspace(1) %alloc.externref.var, align 1
  ; ATTR-NEXT: %eref.loaded = load ptr addrspace(10), ptr addrspace(1) %alloc.externref.var, align 1
  ; ATTR-NEXT: call void @take_externref(ptr addrspace(10) %eref.loaded)

  %alloc.funcref = alloca %funcref, align 1
  %fref = call %funcref @get_funcref()
  store %funcref %fref, ptr %alloc.funcref, align 1
  %fref.loaded = load %funcref, ptr %alloc.funcref, align 1
  call void @take_funcref(%funcref %fref.loaded)
  ; ATTR-NEXT: %alloc.funcref.var = alloca ptr addrspace(20), align 1, addrspace(1)
  ; ATTR-NEXT: %fref = call ptr addrspace(20) @get_funcref()
  ; ATTR-NEXT: store ptr addrspace(20) %fref, ptr addrspace(1) %alloc.funcref.var, align 1
  ; ATTR-NEXT: %fref.loaded = load ptr addrspace(20), ptr addrspace(1) %alloc.funcref.var, align 1
  ; ATTR-NEXT: call void @take_funcref(ptr addrspace(20) %fref.loaded)

  ret void
}

attributes #0 = { "target-features"="+reference-types" }
