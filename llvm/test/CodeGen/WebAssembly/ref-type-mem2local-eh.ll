; RUN: llc < %s -wasm-enable-eh -wasm-use-legacy-eh=false -exception-model=wasm -mattr=+reference-types,+exception-handling -stop-after=wasm-ref-type-mem2local | FileCheck %s
; RUN: llc < %s -wasm-enable-eh -wasm-use-legacy-eh=false -exception-model=wasm -mattr=+reference-types,+exception-handling -verify-machineinstrs

target triple = "wasm32-unknown-unknown"

%externref = type ptr addrspace(10)

declare void @take_externref_ptr(ptr)
declare void @may_throw()
declare i32 @__gxx_wasm_personality_v0(...)

; CHECK-LABEL: @cleanupret_unwind_to_caller
define void @cleanupret_unwind_to_caller() personality ptr @__gxx_wasm_personality_v0 {
entry:
  %slot = alloca %externref, align 1
  call void @take_externref_ptr(ptr %slot)
  invoke void @may_throw()
          to label %ret unwind label %cleanup

ret:
  ret void

cleanup:
  %pad = cleanuppad within none []
  cleanupret from %pad unwind to caller

; CHECK:      %externref.sp = load i32, ptr addrspace(1) @__externref_stack_pointer, align 4
; CHECK-NEXT: %externref.sp.new = sub i32 %externref.sp, 1
; CHECK-NEXT: store i32 %externref.sp.new, ptr addrspace(1) @__externref_stack_pointer, align 4
; CHECK:      ret:
; CHECK-NEXT:   store i32 %externref.sp, ptr addrspace(1) @__externref_stack_pointer, align 4
; CHECK-NEXT:   %[[RET_NULL:[^ ]+]] = call ptr addrspace(10) @llvm.wasm.ref.null.extern()
; CHECK-NEXT:   call void @llvm.wasm.table.fill.externref(ptr addrspace(1) @__externref_table, i32 %externref.sp.new, ptr addrspace(10) %[[RET_NULL]], i32 1)
; CHECK-NEXT:   ret void
; CHECK:      cleanup:
; CHECK-NEXT:   %pad = cleanuppad within none []
; CHECK-NEXT:   store i32 %externref.sp, ptr addrspace(1) @__externref_stack_pointer, align 4
; CHECK-NEXT:   %[[CLEANUP_NULL:[^ ]+]] = call ptr addrspace(10) @llvm.wasm.ref.null.extern()
; CHECK-NEXT:   call void @llvm.wasm.table.fill.externref(ptr addrspace(1) @__externref_table, i32 %externref.sp.new, ptr addrspace(10) %[[CLEANUP_NULL]], i32 1)
; CHECK-NEXT:   cleanupret from %pad unwind to caller
}
