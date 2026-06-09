; RUN: llc < %s --mtriple=wasm32-unknown-unknown -asm-verbose=false -mattr=+reference-types | FileCheck %s
; RUN: llc < %s --mtriple=wasm64-unknown-unknown -asm-verbose=false -mattr=+reference-types | FileCheck %s

; externref values flow through function arguments, a function return value, and
; phis that merge them across control flow. They cannot live in linear memory,
; so they are carried in wasm locals of externref type; a phi is resolved by
; reassigning the local on the incoming edge.
;
; Derived the following C code:
;
;   __externref_t foo(void);
;   void bar(__externref_t);
;   void test(int flag, __externref_t ref1, __externref_t ref2) {
;     if (flag) {
;       ref1 = foo();
;       ref2 = foo();
;     }
;     bar(ref1); bar(ref2);
;   }
%externref = type ptr addrspace(10) ;; addrspace 10 is nonintegral

declare %externref @foo()
declare void @bar(%externref)

; CHECK-LABEL: test:
; CHECK-NEXT:  .functype       test (i32, externref, externref) -> ()
; CHECK-NEXT:  block
; CHECK-NEXT:  local.get       0
; CHECK-NEXT:  i32.eqz
; CHECK-NEXT:  br_if           0
; CHECK-NEXT:  call            foo
; CHECK-NEXT:  local.set       1
; CHECK-NEXT:  call            foo
; CHECK-NEXT:  local.set       2
; CHECK:       end_block
; CHECK-NEXT:  local.get       1
; CHECK-NEXT:  call            bar
; CHECK-NEXT:  local.get       2
; CHECK-NEXT:  call            bar
; CHECK-NEXT:  end_function
define void @test(i32 %flag, %externref %ref1, %externref %ref2) {
entry:
  %c = icmp eq i32 %flag, 0
  br i1 %c, label %join, label %then

then:
  %a = tail call %externref @foo()
  %b = tail call %externref @foo()
  br label %join

join:
  %p1 = phi %externref [ %a, %then ], [ %ref1, %entry ]
  %p2 = phi %externref [ %b, %then ], [ %ref2, %entry ]
  tail call void @bar(%externref %p1)
  tail call void @bar(%externref %p2)
  ret void
}
