//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___NEW_ALLOCATE_H
#define _LIBCPP___NEW_ALLOCATE_H

#include <__config>
#include <__cstddef/max_align_t.h>
#include <__cstddef/size_t.h>
#include <__new/align_val_t.h>
#include <__type_traits/type_identity.h>
#include <__utility/element_count.h>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_BEGIN_NAMESPACE_STD

_LIBCPP_CONSTEXPR inline _LIBCPP_HIDE_FROM_ABI bool __is_overaligned_for_new(size_t __align) _NOEXCEPT {
#ifdef __STDCPP_DEFAULT_NEW_ALIGNMENT__
  return __align > __STDCPP_DEFAULT_NEW_ALIGNMENT__;
#else
  return __align > _LIBCPP_ALIGNOF(max_align_t);
#endif
}

template <class _Tp>
inline _LIBCPP_HIDE_FROM_ABI _LIBCPP_NO_CFI _Tp*
__libcpp_allocate(__element_count __n, [[__maybe_unused__]] size_t __align = _LIBCPP_ALIGNOF(_Tp)) {
  size_t __size = static_cast<size_t>(__n) * sizeof(_Tp);
#if _LIBCPP_HAS_ALIGNED_ALLOCATION
  if (__is_overaligned_for_new(__align))
    return static_cast<_Tp*>(__builtin_operator_new(__size, static_cast<align_val_t>(__align)));
#endif

  return static_cast<_Tp*>(__builtin_operator_new(__size));
}

#if defined(__cpp_sized_deallocation) && __cpp_sized_deallocation >= 201309L
#  define _LIBCPP_ONLY_IF_SIZED_DEALLOCATION(...) __VA_ARGS__
#else
#  define _LIBCPP_ONLY_IF_SIZED_DEALLOCATION(...) /* nothing */
#endif

template <class _Tp>
inline _LIBCPP_HIDE_FROM_ABI void
__libcpp_deallocate(__type_identity_t<_Tp>* __ptr,
                    __element_count __n,
                    [[__maybe_unused__]] size_t __align = _LIBCPP_ALIGNOF(_Tp)) _NOEXCEPT {
  [[__maybe_unused__]] size_t __size = static_cast<size_t>(__n) * sizeof(_Tp);
#if _LIBCPP_HAS_ALIGNED_ALLOCATION
  if (__is_overaligned_for_new(__align))
    return __builtin_operator_delete(
        __ptr _LIBCPP_ONLY_IF_SIZED_DEALLOCATION(, __size), static_cast<align_val_t>(__align));
#endif
  return __builtin_operator_delete(__ptr _LIBCPP_ONLY_IF_SIZED_DEALLOCATION(, __size));
}

#undef _LIBCPP_ONLY_IF_SIZED_DEALLOCATION

template <class _Tp>
inline _LIBCPP_HIDE_FROM_ABI void __libcpp_deallocate_unsized(
    __type_identity_t<_Tp>* __ptr, [[__maybe_unused__]] size_t __align = _LIBCPP_ALIGNOF(_Tp)) _NOEXCEPT {
#if _LIBCPP_HAS_ALIGNED_ALLOCATION
  if (__is_overaligned_for_new(__align))
    return __builtin_operator_delete(__ptr, static_cast<align_val_t>(__align));
#endif
  return __builtin_operator_delete(__ptr);
}

// Allocation during constant evaluation, where only the replaceable global
// operator new/delete may be used.
template <class _Tp>
_LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX20 _Tp* __constexpr_allocate(size_t __n) {
  return static_cast<_Tp*>(::operator new(__n * sizeof(_Tp)));
}
template <class _Tp>
_LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX20 void __constexpr_deallocate(_Tp* __p) _NOEXCEPT {
  ::operator delete(__p);
}

#if defined(__wasm_reference_types__)
// A WebAssembly externref cannot live in linear memory: an `__externref_t*` is
// an index into the externref table, and storage for externrefs is obtained
// from the compiler-rt table allocator (the same functions `new __externref_t`
// and `delete` lower to) rather than from operator new. Route the typed
// allocation primitives there so that std::allocator<__externref_t>, and thus
// the standard containers, work with externrefs.
extern "C" __externref_t* __externref_table_alloc(size_t __nrefs);
extern "C" void __externref_table_free(__externref_t* __p);

template <>
inline _LIBCPP_HIDE_FROM_ABI _LIBCPP_NO_CFI __externref_t*
__libcpp_allocate<__externref_t>(__element_count __n, size_t) {
  return __externref_table_alloc(static_cast<size_t>(__n));
}

template <>
inline _LIBCPP_HIDE_FROM_ABI void
__libcpp_deallocate<__externref_t>(__externref_t* __ptr, __element_count, size_t) _NOEXCEPT {
  __externref_table_free(__ptr);
}

template <>
inline _LIBCPP_HIDE_FROM_ABI void __libcpp_deallocate_unsized<__externref_t>(__externref_t* __ptr, size_t) _NOEXCEPT {
  __externref_table_free(__ptr);
}

// Externrefs cannot be constant-evaluated; these only exist so that
// std::allocator<__externref_t> instantiates (an externref pointer does not
// convert to or from void*).
template <>
inline _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX20 __externref_t* __constexpr_allocate<__externref_t>(size_t __n) {
  return __externref_table_alloc(__n);
}
template <>
inline _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX20 void
__constexpr_deallocate<__externref_t>(__externref_t* __p) _NOEXCEPT {
  __externref_table_free(__p);
}
#endif // __wasm_reference_types__

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP___NEW_ALLOCATE_H
