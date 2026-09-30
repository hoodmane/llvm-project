// RUN: %clang_cc1 -fsyntax-only -verify=expected,conly -triple wasm32 -Wno-unused-value -target-feature +reference-types %s
// RUN: %clang_cc1 -x c++ -std=c++17 -fsyntax-only -verify=expected,cpp -triple wasm32 -Wno-unused-value -target-feature +reference-types %s

// Note: As WebAssembly references are sizeless types, we don't exhaustively
// test for cases covered by sizeless-1.c and similar tests.

// Unlike standard sizeless types, reftype globals are supported.
__externref_t r1;
extern __externref_t r2;
static __externref_t r3;
__externref_t r4 __attribute__((wasmtable));       // expected-error {{'wasmtable' attribute only applies to a zero-length array of WebAssembly reference types}}

__externref_t *t1;
__externref_t **t2;
__externref_t ******t3;
// Arrays of externref (including multi-dimensional ones) are allowed as
// globals.
static __externref_t t4[3];
static __externref_t t5[];       // conly-error {{array has sizeless element type '__externref_t'}} cpp-error {{definition of variable with array type needs an explicit size or an initializer}}
static __externref_t t6[] = {0}; // conly-error {{initializing '__externref_t' with an expression of incompatible type 'int'}} cpp-error {{cannot initialize an array element of type '__externref_t' with an rvalue of type 'int'}}
__externref_t t7[0];
static __externref_t t8[0][0];
// A pointer to an array of externref is allowed.
static __externref_t (*t9)[0];



static __externref_t t11[3] __attribute__((wasmtable));      // expected-error {{'wasmtable' attribute only applies to a zero-length array of WebAssembly reference types}}
static __externref_t t12[] __attribute__((wasmtable));       // expected-error {{'wasmtable' attribute only applies to a zero-length array of WebAssembly reference types}} conly-error {{array has sizeless element type '__externref_t'}} cpp-error {{definition of variable with array type needs an explicit size or an initializer}}
static __externref_t t13[] __attribute__((wasmtable)) = {0}; // expected-error {{'wasmtable' attribute only applies to a zero-length array of WebAssembly reference types}} conly-error {{initializing '__externref_t' with an expression of incompatible type 'int'}} cpp-error {{cannot initialize an array element of type '__externref_t' with an rvalue of type 'int'}}
__externref_t t14[0] __attribute__((wasmtable));             // expected-error {{WebAssembly table must be static}}
static __externref_t t15[0][0] __attribute__((wasmtable));   // expected-error {{'wasmtable' attribute only applies to a zero-length array of WebAssembly reference types}}
static __externref_t (*t16)[0] __attribute__((wasmtable));   // expected-error {{'wasmtable' attribute only applies to a zero-length array of WebAssembly reference types}}

static __externref_t table[0] __attribute__((wasmtable));
static __externref_t other_table[0] __attribute__((wasmtable)) = {};
static __externref_t another_table[] = {};

// The 'wasmtable' attribute is a type attribute and only forms a table when it
// appertains to the array via the declarator (trailing placement). Written in a
// leading or declaration-specifier position it attaches to the element type
// instead of the array, so it does not form a table and yields a single
// diagnostic.
__attribute__((wasmtable)) static __externref_t lead1[0]; // expected-error {{'wasmtable' attribute only applies to a zero-length array of WebAssembly reference types}}
static __attribute__((wasmtable)) __externref_t lead2[0]; // expected-error {{'wasmtable' attribute only applies to a zero-length array of WebAssembly reference types}}
static __externref_t __attribute__((wasmtable)) lead3[0]; // expected-error {{'wasmtable' attribute only applies to a zero-length array of WebAssembly reference types}}

// Arrays of externref are not allowed as struct or union members, but a
// pointer to such an array is fine.
struct s {
  __externref_t f1;       // expected-error {{field has sizeless type '__externref_t'}}
  __externref_t f2[0];    // expected-error {{arrays of WebAssembly reference types are not allowed in a struct}}
  __externref_t f3[];     // expected-error {{arrays of WebAssembly reference types are not allowed in a struct}}
  __externref_t f4[0][0]; // expected-error {{arrays of WebAssembly reference types are not allowed in a struct}}
  __externref_t *f5;
  __externref_t ****f6;
  __externref_t (*f7)[0];
};

struct t {
  __externref_t f2[0] __attribute__((wasmtable));    // expected-error {{field has sizeless type '__externref_t'}}
  __externref_t f3[] __attribute__((wasmtable));     // expected-error {{'wasmtable' attribute only applies to a zero-length array of WebAssembly reference types}} expected-error {{arrays of WebAssembly reference types are not allowed in a struct}}
  __externref_t f4[0][0] __attribute__((wasmtable)); // expected-error {{'wasmtable' attribute only applies to a zero-length array of WebAssembly reference types}} expected-error {{arrays of WebAssembly reference types are not allowed in a struct}}
  __externref_t *f5 __attribute__((wasmtable));      // expected-error {{'wasmtable' attribute only applies to a zero-length array of WebAssembly reference types}}
  __externref_t ****f6 __attribute__((wasmtable));   // expected-error {{'wasmtable' attribute only applies to a zero-length array of WebAssembly reference types}}
  __externref_t (*f7)[0] __attribute__((wasmtable)); // expected-error {{'wasmtable' attribute only applies to a zero-length array of WebAssembly reference types}}
};

union u {
  __externref_t f1;       // expected-error {{field has sizeless type '__externref_t'}}
  __externref_t f2[0];    // expected-error {{arrays of WebAssembly reference types are not allowed in a union}}
  __externref_t f3[];     // expected-error {{arrays of WebAssembly reference types are not allowed in a union}}
  __externref_t f4[0][0]; // expected-error {{arrays of WebAssembly reference types are not allowed in a union}}
  __externref_t *f5;
  __externref_t ****f6;
  __externref_t (*f7)[0];
};

// Arrays of externref are allowed as function parameters (they decay to a
// pointer), as is a pointer to an array of externref.
void argument_1(__externref_t table[]);
void argument_2(__externref_t table[0][0]);
void argument_3(__externref_t *table);
void argument_4(__externref_t ***table);
void argument_5(__externref_t (*table)[0]);
void argument_6(__externref_t table[0]);
void illegal_argument_7(__externref_t table[] __attribute__((wasmtable)));     // expected-error {{'wasmtable' attribute only applies to a zero-length array of WebAssembly reference types}}

__externref_t *return_1();
__externref_t ***return_2();
__externref_t (*return_3())[0];

void varargs(int, ...);
typedef void (*__funcref funcref_t)();
typedef void (*__funcref __funcref funcref_fail_t)(); // expected-warning {{attribute '__funcref' is already applied}}

// Unlike externref, pointers to and address-of funcrefs remain disallowed.
funcref_t *fr_ptr1;  // expected-error {{pointer to WebAssembly reference type is not allowed}}
funcref_t **fr_ptr2; // expected-error {{pointer to WebAssembly reference type is not allowed}}

void funcref_uses(funcref_t fr) {
  &fr; // expected-error {{cannot take address of WebAssembly reference}}
}

// A pointer to an externref is an externref table slot index, not a
// linear-memory address, so it never converts to or from another pointer type
// (void * included), in either direction, implicitly or by cast. Null pointer
// constants and cv changes are fine.
void takes_void(void *);          // conly-note {{passing argument to parameter here}} cpp-note {{candidate function not viable}}
void takes_ref(__externref_t *);  // conly-note {{passing argument to parameter here}} cpp-note {{candidate function not viable}}
void takes_const_ref(const __externref_t *);
void pointer_conversions(__externref_t *p, void *vp, int *ip, char *cp,
                         __externref_t (*ap)[3], __externref_t **pp) {
  vp = p;                       // expected-error {{cannot convert from a pointer to WebAssembly reference type '__externref_t'}}
  p = vp;                       // expected-error {{cannot convert to a pointer to WebAssembly reference type '__externref_t'}}
  ip = (int *)p;                // expected-error {{cannot convert from a pointer to WebAssembly reference type '__externref_t'}}
  p = (__externref_t *)ip;      // expected-error {{cannot convert to a pointer to WebAssembly reference type '__externref_t'}}
  cp = (char *)ap;              // expected-error {{cannot convert from a pointer to WebAssembly reference type '__externref_t[3]'}}
  takes_void(p);                // conly-error {{cannot convert from a pointer to WebAssembly reference type '__externref_t'}} cpp-error {{no matching function for call to 'takes_void'}}
  takes_ref(vp);                // conly-error {{cannot convert to a pointer to WebAssembly reference type '__externref_t'}} cpp-error {{no matching function for call to 'takes_ref'}}
  takes_ref(0);
#ifndef __cplusplus
  takes_ref((void *)0); // a null pointer constant in C
#endif
  takes_const_ref(p);
  takes_ref(*ap);
  vp = pp;                      // a pointer to an externref pointer lives in linear memory
  pp = (__externref_t **)vp;
  p = (__externref_t *)(const __externref_t *)p;
}

// Pointers to externref behave like ordinary pointers in unary and binary
// expressions (unlike tables, which are rejected below in func()).
int pointer_exprs(__externref_t *p, __externref_t *q) {
  // Arithmetic steps one table slot per element.
  __externref_t *r = p + 1;
  r = r - 1;
  r++;
  --r;
  r += 3;
  (void)(q - p);
  (void)r[2];
  (void)&p[2];
  if (!p)
    return 0;
  if (p == 0 || q != 0)
    return 1;
  if (p == q || p != q || p < q || p >= q)
    return 2;
  if (p && q)
    return 3;
  return p ? 4 : 5;
}

__externref_t func(__externref_t ref) {
  &ref;
  int foo = 40;
  (__externref_t *)(&foo);    // expected-error {{cannot convert to a pointer to WebAssembly reference type '__externref_t'; a pointer to a reference type is an externref table slot index, not a linear-memory address}}
  (__externref_t ****)(&foo); // a pointer to a pointer is an ordinary pointer
  // An externref pointer is an externref table slot index with one slot per
  // element, so sizeof and alignof are both 1 (and arrays scale by count).
  _Static_assert(sizeof(ref) == 1, "");
  _Static_assert(sizeof(__externref_t) == 1, "");
  _Static_assert(sizeof(__externref_t[0]) == 0, "");
  _Static_assert(sizeof(__externref_t[4]) == 4, "");
  _Static_assert(sizeof(__externref_t[2][3]) == 6, "");
  sizeof(table);               // expected-error {{invalid application of 'sizeof' to WebAssembly table}}
  sizeof(__externref_t[0][0]);
  sizeof(__externref_t *);
  sizeof(__externref_t ***);
  // expected-warning@+1 {{'_Alignof' applied to an expression is a GNU extension}}
  _Static_assert(_Alignof(ref) == 1, "");
  _Static_assert(_Alignof(__externref_t) == 1, "");
  _Static_assert(_Alignof(__externref_t[]) == 1, "");
  _Static_assert(_Alignof(__externref_t[0]) == 1, "");
  _Alignof(table);               // expected-warning {{'_Alignof' applied to an expression is a GNU extension}} expected-error {{invalid application of 'alignof' to WebAssembly table}}
  _Static_assert(_Alignof(__externref_t[0][0]) == 1, "");
  _Alignof(__externref_t *);
  _Alignof(__externref_t ***);
  varargs(1, ref);               // expected-error {{cannot pass expression of type '__externref_t' to variadic function}}

  __externref_t lt1[0];
  static __externref_t lt2[0];
  static __externref_t lt3[0][0];
  static __externref_t(*lt4)[0];
  // conly-error@+2 {{cannot use WebAssembly table as a function parameter}}
  // cpp-error@+1 {{no matching function for call to 'argument_1'}}
  argument_1(table);
  varargs(1, table);              // expected-error {{cannot use WebAssembly table as a function parameter}}
  table == 1;                     // expected-error {{invalid operands to binary expression ('__attribute__((address_space(1))) __externref_t[0]' and 'int')}}
  1 >= table;                     // expected-error {{invalid operands to binary expression ('int' and '__attribute__((address_space(1))) __externref_t[0]')}}
  table == other_table;           // expected-error {{invalid operands to binary expression ('__attribute__((address_space(1))) __externref_t[0]' and '__attribute__((address_space(1))) __externref_t[0]')}}
  table !=- table;                // expected-error {{invalid argument type '__externref_t[0]' to unary expression}}
  !table;                         // expected-error {{invalid argument type '__externref_t[0]' to unary expression}}
  1 && table;                     // expected-error {{invalid operands to binary expression ('int' and '__attribute__((address_space(1))) __externref_t[0]')}}
  table || 1;                     // expected-error {{invalid operands to binary expression ('__attribute__((address_space(1))) __externref_t[0]' and 'int')}}
  1 ? table : table;              // expected-error {{cannot use a WebAssembly table within a branch of a conditional expression}}
  table ? : other_table;          // expected-error {{cannot use a WebAssembly table within a branch of a conditional expression}}
  (void *)table;                  // expected-error {{cannot cast from a WebAssembly table}}
  void *u;
  u = table;                      // expected-error {{cannot assign a WebAssembly table}}
  void *v = table;                // expected-error {{cannot assign a WebAssembly table}}
  &table;                         // expected-error {{cannot form a reference to a WebAssembly table}}
  (void)table;

  table[0];                       // expected-error {{cannot subscript a WebAssembly table}}
  table[0] = ref;                 // expected-error {{cannot subscript a WebAssembly table}}

  int i = 0;                      // cpp-note {{declared here}}
  __externref_t oh_no_vlas[i];    // cpp-warning {{variable length arrays in C++ are a Clang extension}} \
                                     cpp-note {{read of non-const variable 'i' is not allowed in a constant expression}}

  return ref;
}

void foo() {
  static __externref_t t1[0];
  static __externref_t t2[0] __attribute__((wasmtable)); // expected-error {{WebAssembly table cannot be declared within a function}}
  {
    static __externref_t t3[0];
    for (;;) {
      static __externref_t t4[0];
    }
  }
  int i = ({
    static __externref_t t5[0];
    1;
  });
}

void *ret_void_ptr() {
  return table; // expected-error {{cannot return a WebAssembly table}}
}
