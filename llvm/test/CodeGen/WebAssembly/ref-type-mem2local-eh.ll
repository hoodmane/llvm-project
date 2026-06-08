; RUN: llc < %s -wasm-enable-eh -wasm-use-legacy-eh=false -exception-model=wasm -mattr=+reference-types,+exception-handling -stop-after=wasm-ref-type-mem2local | FileCheck %s
; RUN: llc < %s -wasm-enable-eh -wasm-use-legacy-eh=false -exception-model=wasm -mattr=+reference-types,+exception-handling -verify-machineinstrs

target triple = "wasm32-unknown-unknown"

%externref = type ptr addrspace(10)

declare void @take_externref_ptr(ptr)
declare void @may_throw()
declare i32 @__gxx_wasm_personality_v0(...)
@_ZTIi = external constant ptr

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

; A catchswitch that unwinds to the caller (an exception matching none of the
; handlers leaves the function) is not an instruction insertion point, so the
; epilogue is injected by rerouting that edge through a synthesized cleanup pad
; that restores the spill stack and then unwinds to the caller.
; CHECK-LABEL: @catchswitch_unwind_to_caller
define void @catchswitch_unwind_to_caller() personality ptr @__gxx_wasm_personality_v0 {
entry:
  %slot = alloca %externref, align 1
  call void @take_externref_ptr(ptr %slot)
  invoke void @may_throw()
          to label %ret unwind label %cs

ret:
  ret void

cs:
  %tok = catchswitch within none [label %catch] unwind to caller

catch:
  %cp = catchpad within %tok [ptr @_ZTIi]
  catchret from %cp to label %ret

; CHECK:      cs:
; CHECK-NEXT:   %tok = catchswitch within none [label %catch] unwind label %externref.cleanup
; CHECK:      externref.cleanup:
; CHECK-NEXT:   %[[CP:[^ ]+]] = cleanuppad within none []
; CHECK-NEXT:   store i32 %externref.sp, ptr addrspace(1) @__externref_stack_pointer, align 4
; CHECK-NEXT:   %[[CS_NULL:[^ ]+]] = call ptr addrspace(10) @llvm.wasm.ref.null.extern()
; CHECK-NEXT:   call void @llvm.wasm.table.fill.externref(ptr addrspace(1) @__externref_table, i32 %externref.sp.new, ptr addrspace(10) %[[CS_NULL]], i32 1)
; CHECK-NEXT:   cleanupret from %[[CP]] unwind to caller
}

; Two nested catchswitches that both unwind to the caller must share a single
; cleanup destination to satisfy EH structural rules (all unwind edges leaving a
; funclet share one destination).
; CHECK-LABEL: @nested_catchswitch_unwind_to_caller
define void @nested_catchswitch_unwind_to_caller() personality ptr @__gxx_wasm_personality_v0 {
entry:
  %slot = alloca %externref, align 1
  call void @take_externref_ptr(ptr %slot)
  invoke void @may_throw()
          to label %ret unwind label %cs1

ret:
  ret void

cs1:
  %tok1 = catchswitch within none [label %catch1] unwind to caller

catch1:
  %cp1 = catchpad within %tok1 [ptr @_ZTIi]
  invoke void @may_throw() [ "funclet"(token %cp1) ]
          to label %catchret1 unwind label %cs2

catchret1:
  catchret from %cp1 to label %ret

cs2:
  %tok2 = catchswitch within %cp1 [label %catch2] unwind to caller

catch2:
  %cp2 = catchpad within %tok2 [ptr @_ZTIi]
  catchret from %cp2 to label %catchret1

; CHECK:      %tok1 = catchswitch within none [label %catch1] unwind label %externref.cleanup
; CHECK:      %tok2 = catchswitch within %cp1 [label %catch2] unwind label %externref.cleanup
; CHECK:      externref.cleanup:
; CHECK-NEXT:   %[[NCP:[^ ]+]] = cleanuppad within none []
; CHECK-NEXT:   store i32 %externref.sp, ptr addrspace(1) @__externref_stack_pointer, align 4
; CHECK:        cleanupret from %[[NCP]] unwind to caller
}
