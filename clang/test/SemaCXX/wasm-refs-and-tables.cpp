// RUN: %clang_cc1 -std=c++11 -fcxx-exceptions -fexceptions -fsyntax-only -verify -triple wasm32 -Wno-unused-value -target-feature +reference-types %s
// RUN: %clang_cc1 -std=c++20 -fcxx-exceptions -fexceptions -fsyntax-only -verify -triple wasm32 -Wno-unused-value -target-feature +reference-types %s

// 
// Note: As WebAssembly references are sizeless types, we don't exhaustively
// test for cases covered by sizeless-1.c and similar tests.

// Using c++11 to test dynamic exception specifications (which are not 
// allowed in c++17).

// Unlike standard sizeless types, reftype globals are supported.
__externref_t r1;
static __externref_t table[0] __attribute__((wasmtable));

#if (_cplusplus == 201103L)
__externref_t func(__externref_t ref)  throw(__externref_t) { // expected-error {{WebAssembly reference type not allowed in exception specification}}
  return ref;
}
#endif

void *ret_void_ptr() {
  throw table;              // expected-error {{cannot throw a WebAssembly table}}
  throw r1;                 // expected-error {{cannot throw a WebAssembly reference type}}
  try {}
  catch (__externref_t T) { // expected-error {{cannot catch a WebAssembly reference type}}
    (void)0;
  }

  return table;             // expected-error {{cannot return a WebAssembly table}}
}

// new / delete of externref allocate and release __externref_table slots.
// Placement new cannot be honoured since the storage is never linear memory.
void *buf;
void new_delete(__externref_t v, int n) {
  __externref_t *a = new __externref_t;
  __externref_t *b = new __externref_t(v);
  __externref_t *c = new __externref_t();
  __externref_t *d = new __externref_t[n];
  __externref_t *e = new __externref_t[3]{v, v};
  __externref_t(*f)[3] = new __externref_t[n][3];
  __externref_t *g = ::new __externref_t;
  delete a;
  delete b;
  delete c;
  delete[] d;
  delete[] e;
  delete[] f;
  ::delete g;
  new (buf) __externref_t;    // expected-error {{placement new of WebAssembly reference type '__externref_t' is not allowed; references are always allocated in the externref table}}
  new (buf) __externref_t[n]; // expected-error {{placement new of WebAssembly reference type '__externref_t' is not allowed; references are always allocated in the externref table}}
  new (buf) __externref_t[2][3]; // expected-error {{placement new of WebAssembly reference type '__externref_t[3]' is not allowed; references are always allocated in the externref table}}
  new __externref_t *;         // pointers to externref live in linear memory as usual
  delete static_cast<__externref_t **>(nullptr);
}
