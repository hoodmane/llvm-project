//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___MEMORY_VOIDIFY_H
#define _LIBCPP___MEMORY_VOIDIFY_H

#include <__config>
#include <__cstddef/size_t.h>
#include <__type_traits/integral_constant.h>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_BEGIN_NAMESPACE_STD

// Returns the pointer to pass to placement new so that the reserved
// non-allocating global `operator new` is selected regardless of any
// class-specific operator new: `static_cast<void*>(__p)`.
//
// On WebAssembly a pointer to an externref is an externref table slot index
// and cannot be converted to void*; the compiler instead provides an implicit
// non-allocating `operator new(size_t, __externref_t*)` that constructs into
// the slot, so the pointer is passed through unchanged.
template <class _Tp>
_LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR void* __voidify(_Tp* __p) _NOEXCEPT {
  return static_cast<void*>(__p);
}
template <class _Tp>
_LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR const void* __voidify(const _Tp* __p) _NOEXCEPT {
  return static_cast<const void*>(__p);
}

// Whether _Ptr is a pointer to a WebAssembly externref.
template <class _Ptr>
struct __libcpp_is_wasm_externref_pointer : false_type {};

#if defined(__wasm_reference_types__)
_LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR inline __externref_t* __voidify(__externref_t* __p) _NOEXCEPT { return __p; }
// The sanitizer container annotations take const void*; a null pointer means
// no annotation, which is right for slots that are not in linear memory.
_LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR inline const void* __voidify(const __externref_t*) _NOEXCEPT {
  return nullptr;
}
template <>
struct __libcpp_is_wasm_externref_pointer<__externref_t*> : true_type {};
template <>
struct __libcpp_is_wasm_externref_pointer<const __externref_t*> : true_type {};
#endif

// Bytewise relocation of a range of trivially relocatable objects; the
// externref overload exists only so that the (never taken) trivially
// relocatable branch still compiles for externref, which cannot be copied
// bytewise and does not convert to void*.
template <class _Tp>
_LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX14 void
__relocate_bytes(_Tp* __result, _Tp* __first, size_t __count) _NOEXCEPT {
  // Casting to void* to suppress clang complaining that this is technically UB.
  __builtin_memcpy(static_cast<void*>(__result), __first, sizeof(_Tp) * __count);
}

#if defined(__wasm_reference_types__)
_LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX14 inline void
__relocate_bytes(__externref_t* __result, __externref_t* __first, size_t __count) _NOEXCEPT {
  __builtin_wasm_externref_copy(__result, __first, __count);
}
#endif

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP___MEMORY_VOIDIFY_H
