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
  new __externref_t *;         // pointers to externref live in linear memory as usual
  delete static_cast<__externref_t **>(nullptr);
}

// The reserved placement form constructs into an existing table slot (this is
// what std::construct_at uses); placement new with any other allocation
// arguments cannot be honoured.
typedef __SIZE_TYPE__ size_t;
void *operator new(size_t, void *) noexcept;
void *operator new[](size_t, void *) noexcept;
struct Arena {};
void *operator new(size_t, Arena &);
void *operator new[](size_t, Arena &);
void placement_new(__externref_t *slot, __externref_t v, int n, Arena &arena) {
  new (slot) __externref_t;
  new (slot) __externref_t(v);
  new (slot) __externref_t();
  ::new (static_cast<void *>(slot)) __externref_t(v);
  new (slot) __externref_t[n];
  new (slot) __externref_t[2]{v, v};
  new (arena) __externref_t;    // expected-error {{placement new of WebAssembly reference type '__externref_t' with custom allocation arguments is not allowed; references live in the externref table, so only placement new into an existing slot ('new (slot) T') is supported}}
  new (arena) __externref_t[n]; // expected-error {{placement new of WebAssembly reference type '__externref_t' with custom allocation arguments is not allowed}}
  new (buf) int;                // unrelated types are unaffected
}

// Lambdas may capture an externref by reference (the closure then holds an
// __externref_t&, i.e. a table slot index) but not by value (that would need
// an externref field in the closure, which cannot live in linear memory).
void use(__externref_t);
void lambda_captures(__externref_t v, __externref_t &vr) {
  [&] { use(v); }();
  [&v] { use(v); }();
  [&] { use(vr); }();
  [&vr, &v] { vr = v; }();
  [&v]() -> __externref_t & { return v; }();
  [=] { use(v); }();  // expected-error {{cannot capture WebAssembly reference}}
  [v] { use(v); }();  // expected-error {{cannot capture WebAssembly reference}}
  [vr] { use(vr); }(); // expected-error {{cannot capture WebAssembly reference}}
#if __cplusplus >= 201402L
  [x = v] { use(x); }(); // expected-error {{field has sizeless type '__externref_t'}}
  [&x = v] { use(x); }();
#endif
}

// Pseudo-destructor calls on an externref are accepted (and are no-ops), as
// for any trivial type; std::destroy_at relies on this.
typedef __externref_t externref_t;
template <class T> void destroy_at(T *p) { p->~T(); }
void pseudo_dtor(__externref_t *p, __externref_t &r) {
  p->~__externref_t();
  r.~__externref_t();
  p->~externref_t();
  destroy_at(p);
  p->externref_t::~externref_t();
}
