// RUN: %clang_cc1 -triple wasm32-unknown-unknown -target-feature +reference-types -target-feature +simd128 -target-feature +relaxed-simd -target-feature +nontrapping-fptoint -target-feature +exception-handling -target-feature +bulk-memory -target-feature +atomics -target-feature +fp16 -flax-vector-conversions=none -O3 -emit-llvm -o - %s | FileCheck %s -check-prefixes WEBASSEMBLY

// SIMD convenience types
typedef signed char i8x16 __attribute((vector_size(16)));
typedef short i16x8 __attribute((vector_size(16)));
typedef int i32x4 __attribute((vector_size(16)));
typedef long long i64x2 __attribute((vector_size(16)));
typedef unsigned char u8x16 __attribute((vector_size(16)));
typedef unsigned short u16x8 __attribute((vector_size(16)));
typedef unsigned int u32x4 __attribute((vector_size(16)));
typedef unsigned long long u64x2 __attribute((vector_size(16)));
typedef __fp16 f16x8 __attribute((vector_size(16)));
typedef float f32x4 __attribute((vector_size(16)));
typedef double f64x2 __attribute((vector_size(16)));

typedef int (*Fpointers)(void*, void*, void*);

void use(int);

int test_function_pointer_signature_pointers(Fpointers func, void* a, void* b, void* c) {
  int success;
  // WEBASSEMBLY:    %0 = tail call i32 (ptr, ...) @llvm.wasm.ref.test.func(ptr %func, i32 0, token poison, ptr null, ptr null, ptr null)
  // WEBASSEMBLY:    %1 = icmp eq i32 %0, 0
  // WEBASSEMBLY:    br i1 %1, label %return, label %if.then
  // WEBASSEMBLY:  if.then:
  // WEBASSEMBLY:    %2 = tail call i32 %func(ptr %a, ptr %b, ptr %c) #2
  // WEBASSEMBLY:    br label %return
  // WEBASSEMBLY:  return:                                           ; preds = %entry, %if.then
  // WEBASSEMBLY:    %3 = phi i32 [ %2, %if.then ], [ 0, %entry ]
  // WEBASSEMBLY:    ret i32 %3

  return __builtin_wasm_nontrapping_call(&success, func, a, b, c);
}
