// RUN: %clang_cc1 -triple wasm32 -target-feature +reference-types -fsyntax-only -verify %s

typedef void (*__funcref fn_funcref)(void);

// Valid funcref table declaration (zero-length, static)
static fn_funcref valid_table[0] __attribute__((wasmtable)); // no error expected

// Invalid: non-zero length
static fn_funcref bad_table[1] __attribute__((wasmtable)); // expected-error {{'wasmtable' attribute only applies to a zero-length array of WebAssembly reference types}}

// Without the 'wasmtable' attribute, an array of funcref is not a table, and
// (unlike arrays of externref) arrays of funcref are not allowed at all.
static fn_funcref not_a_table[0];   // expected-error {{arrays of WebAssembly funcref types are not allowed}}
static fn_funcref not_a_table2[3];  // expected-error {{arrays of WebAssembly funcref types are not allowed}}
static fn_funcref not_a_table3[2][2]; // expected-error {{arrays of WebAssembly funcref types are not allowed}}
static fn_funcref (*ptr_to_array)[0]; // expected-error {{arrays of WebAssembly funcref types are not allowed}}

// Array subscript on funcref table should be rejected
void test_subscript(void) {
  (void)valid_table[0]; // expected-error {{cannot subscript a WebAssembly table}}
}

// Original reproducer from https://github.com/llvm/llvm-project/issues/140933
// The declaration should be rejected (not static, non-zero length)
extern fn_funcref issue_table[1] __attribute__((wasmtable)); // expected-error {{'wasmtable' attribute only applies to a zero-length array of WebAssembly reference types}}
