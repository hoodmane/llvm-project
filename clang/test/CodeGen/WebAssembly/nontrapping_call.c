// RUN: %clang_cc1 -triple wasm32-unknown-unknown -target-feature +reference-types -target-feature +simd128 -target-feature +relaxed-simd -target-feature +nontrapping-fptoint -target-feature +exception-handling -target-feature +bulk-memory -target-feature +atomics -target-feature +fp16 -flax-vector-conversions=none -O3 -emit-llvm -o - %s | FileCheck %s -check-prefixes WEBASSEMBLY

typedef int (*F)(void*, void*);

int test_function_pointer_signature_pointers(int* success, F func, void* a, void* b) {
// WEBASSEMBLY:  entry:
// WEBASSEMBLY:    store i32 1, ptr %success, align 4
// WEBASSEMBLY:    %0 = tail call i32 (ptr, ...) @llvm.wasm.ref.test.func(ptr %func, i32 0, token poison, ptr null, ptr null)
// WEBASSEMBLY:    %.not = icmp eq i32 %0, 0
// WEBASSEMBLY:    br i1 %.not, label %test1, label %call2
//
// WEBASSEMBLY:  call0:                                            ; preds = %test0
// WEBASSEMBLY:    %1 = tail call i32 %func() #2
// WEBASSEMBLY:    br label %return
//
// WEBASSEMBLY:  call1:                                            ; preds = %test1
// WEBASSEMBLY:    %2 = tail call i32 %func(ptr %a) #2
// WEBASSEMBLY:    br label %return
//
// WEBASSEMBLY:  call2:                                            ; preds = %entry
// WEBASSEMBLY:    %3 = tail call i32 %func(ptr %a, ptr %b) #2
// WEBASSEMBLY:    br label %return
//
// WEBASSEMBLY:  test1:                                            ; preds = %entry
// WEBASSEMBLY:    %4 = tail call i32 (ptr, ...) @llvm.wasm.ref.test.func(ptr %func, i32 0, token poison, ptr null)
// WEBASSEMBLY:    %.not2 = icmp eq i32 %4, 0
// WEBASSEMBLY:    br i1 %.not2, label %test0, label %call1
//
// WEBASSEMBLY:  test0:                                            ; preds = %test1
// WEBASSEMBLY:    %5 = tail call i32 (ptr, ...) @llvm.wasm.ref.test.func(ptr %func, i32 0, token poison)
// WEBASSEMBLY:    %.not3 = icmp eq i32 %5, 0
// WEBASSEMBLY:    br i1 %.not3, label %fail, label %call0
//
// WEBASSEMBLY:  fail:                                             ; preds = %test0
// WEBASSEMBLY:    store i32 0, ptr %success, align 4
// WEBASSEMBLY:    br label %return
//
// WEBASSEMBLY:  return:                                           ; preds = %fail, %call2, %call1, %call0
// WEBASSEMBLY:    %6 = phi i32 [ %1, %call0 ], [ %2, %call1 ], [ %3, %call2 ], [ 0, %fail ]
// WEBASSEMBLY:    ret i32 %6
  return __builtin_wasm_nontrapping_call(success, func, a, b);
}
