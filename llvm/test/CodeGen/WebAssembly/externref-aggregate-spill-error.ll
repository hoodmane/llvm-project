; RUN: not --crash llc < %s -mattr=+reference-types -stop-after=wasm-ref-type-mem2local 2>&1 | FileCheck %s

target triple = "wasm32-unknown-unknown"

%externref = type ptr addrspace(10)

declare void @take_ptr(ptr)

define void @array_of_externref() {
entry:
  %slot = alloca [2 x %externref], align 1
  %elem = getelementptr [2 x %externref], ptr %slot, i32 0, i32 0
  call void @take_ptr(ptr %elem)
  ret void
}

; CHECK: WebAssembly: cannot allocate an aggregate containing externref
