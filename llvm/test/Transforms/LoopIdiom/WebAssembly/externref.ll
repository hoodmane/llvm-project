; RUN: opt -S -passes=loop-idiom < %s | FileCheck %s --check-prefixes=CHECK,LIR
; RUN: opt -S -passes=memcpyopt < %s | FileCheck %s

; An externref is a target extension type laid out as a pointer in the
; non-integral address space 10. Its values have no byte representation (they
; live in the externref table and a `ptr` to one is a table slot index), so
; loops of externref loads/stores must not be turned into memset / memcpy /
; memmove, exactly as for stores of unstable-representation pointers.

target datalayout = "e-m:e-p:32:32-p10:8:8-p20:8:8-i64:64-i128:128-n32:64-S128-ni:1:10:20"
target triple = "wasm32-unknown-unknown"

; CHECK-LABEL: @clear(
; CHECK-NOT:     memset
; CHECK:         store target("wasm.externref") zeroinitializer, ptr
; CHECK-NOT:     memset
define void @clear(ptr %p, i32 %n) {
entry:
  br label %loop
loop:
  %i = phi i32 [ 0, %entry ], [ %inc, %loop ]
  %a = getelementptr inbounds target("wasm.externref"), ptr %p, i32 %i
  store target("wasm.externref") zeroinitializer, ptr %a, align 1
  %inc = add nuw i32 %i, 1
  %c = icmp eq i32 %inc, %n
  br i1 %c, label %exit, label %loop
exit:
  ret void
}

; CHECK-LABEL: @copy(
; CHECK-NOT:     memcpy
; CHECK-NOT:     memmove
; CHECK:         load target("wasm.externref"), ptr
; CHECK:         store target("wasm.externref") %{{.*}}, ptr
; CHECK-NOT:     memcpy
; CHECK-NOT:     memmove
define void @copy(ptr noalias %d, ptr noalias %s, i32 %n) {
entry:
  br label %loop
loop:
  %i = phi i32 [ 0, %entry ], [ %inc, %loop ]
  %sa = getelementptr inbounds target("wasm.externref"), ptr %s, i32 %i
  %v = load target("wasm.externref"), ptr %sa, align 1
  %da = getelementptr inbounds target("wasm.externref"), ptr %d, i32 %i
  store target("wasm.externref") %v, ptr %da, align 1
  %inc = add nuw i32 %i, 1
  %c = icmp eq i32 %inc, %n
  br i1 %c, label %exit, label %loop
exit:
  ret void
}

; Consecutive externref stores must not be merged into a memset either.
; CHECK-LABEL: @clear4(
; CHECK-NOT:     memset
; CHECK-COUNT-4: store target("wasm.externref") zeroinitializer, ptr
; CHECK-NOT:     memset
define void @clear4(ptr %p) {
  store target("wasm.externref") zeroinitializer, ptr %p, align 1
  %a1 = getelementptr inbounds target("wasm.externref"), ptr %p, i32 1
  store target("wasm.externref") zeroinitializer, ptr %a1, align 1
  %a2 = getelementptr inbounds target("wasm.externref"), ptr %p, i32 2
  store target("wasm.externref") zeroinitializer, ptr %a2, align 1
  %a3 = getelementptr inbounds target("wasm.externref"), ptr %p, i32 3
  store target("wasm.externref") zeroinitializer, ptr %a3, align 1
  ret void
}

; Loops over ordinary integer types are still recognised by loop-idiom.
; CHECK-LABEL: @clear_i8(
; LIR:           call void @llvm.memset
define void @clear_i8(ptr %p, i32 %n) {
entry:
  br label %loop
loop:
  %i = phi i32 [ 0, %entry ], [ %inc, %loop ]
  %a = getelementptr inbounds i8, ptr %p, i32 %i
  store i8 0, ptr %a, align 1
  %inc = add nuw i32 %i, 1
  %c = icmp eq i32 %inc, %n
  br i1 %c, label %exit, label %loop
exit:
  ret void
}
