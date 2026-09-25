//===-- externref_table_alloc.c - __externref_table heap allocator --------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// A simple allocator for slots in the linker-synthesized __externref_table.
//
// An `__externref_t *` on WebAssembly is an index into __externref_table.
// The table is always i32-indexed, even under wasm64, so the pointer holds a
// zero-extended 32-bit slot index.  The linker lays the table out as
// [bss | spill stack | heap) and publishes the start of the heap region as the
// immutable i32 global __externref_heap_base.  Everything from there to the
// current table size belongs to this allocator, which may extend it with
// table.grow.
//
// Externref values cannot live in linear memory, so all allocator metadata is
// kept in linear memory (obtained from malloc) and only externrefs go in the
// table.  The design is deliberately simple:
//
//   * Fresh slots are handed out from a bump pointer.
//   * Freed blocks are pushed onto per-size-class free stacks.  Sizes up to
//     kExactClasses have an exact class; larger requests are rounded up to a
//     power of two and the caller is allotted the rounded size.
//   * Freed slots are nulled with table.fill so the host GC can reclaim the
//     referenced objects.
//   * A one-byte side table in linear memory records the size class of each
//     block at its first slot, so blocks can be freed without knowing their
//     size (`__externref_table_free(p)`), which is what `delete` and
//     `delete[]` on an `__externref_t *` need since there is nowhere in the
//     table to keep an array cookie.  `__externref_table_free_sized(p, n)` is
//     available when the caller does know the size.
//
// This allocator is not thread-safe.
//
//===----------------------------------------------------------------------===//

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#include "../int_lib.h"

// Slot indices are i32 regardless of pointer width.
typedef uint32_t slot_t;

// The table.* instructions and ref.null_extern require the reference-types
// feature.  Enable it per function so this file builds with the default
// compiler-rt flags.
#define REFTYPES __attribute__((target("reference-types")))

//===----------------------------------------------------------------------===//
// Thin wrappers over the wasm table instructions.
//===----------------------------------------------------------------------===//

REFTYPES static slot_t externref_heap_base(void) {
  slot_t r;
  __asm__(".globaltype __externref_heap_base, i32, immutable\n"
          "global.get __externref_heap_base\n"
          "local.set %0"
          : "=r"(r));
  return r;
}

REFTYPES static slot_t externref_table_size(void) {
  slot_t r;
  __asm__ volatile(".tabletype __externref_table, externref\n"
                   "table.size __externref_table\n"
                   "local.set %0"
                   : "=r"(r));
  return r;
}

// Grows the table by `delta` null slots.  Returns the previous size, or
// (slot_t)-1 on failure.
REFTYPES static slot_t externref_table_grow(slot_t delta) {
  slot_t r;
  __asm__ volatile(".tabletype __externref_table, externref\n"
                   "ref.null_extern\n"
                   "local.get %1\n"
                   "table.grow __externref_table\n"
                   "local.set %0"
                   : "=r"(r)
                   : "r"(delta));
  return r;
}

// Sets slots [idx, idx + n) to null.
REFTYPES static void externref_table_clear(slot_t idx, slot_t n) {
  __asm__ volatile(".tabletype __externref_table, externref\n"
                   "local.get %0\n"
                   "ref.null_extern\n"
                   "local.get %1\n"
                   "table.fill __externref_table"
                   :
                   : "r"(idx), "r"(n)
                   : "memory");
}

//===----------------------------------------------------------------------===//
// Size classes.
//===----------------------------------------------------------------------===//

// Requests of 1..kExactClasses slots get their own class.  Larger requests are
// rounded up to a power of two >= kExactClasses * 2.
enum { kExactClasses = 32 };
enum { kNumClasses = kExactClasses + 8 * sizeof(slot_t) };

// Rounds `n` (> kExactClasses) up to a power of two.
static slot_t round_up_pow2(slot_t n) {
  slot_t p = (slot_t)kExactClasses * 2;
  while (p < n)
    p <<= 1;
  return p;
}

// Returns the size class for a request of `n` slots and stores the number of
// slots actually reserved for that class in *reserved.
static unsigned size_class(slot_t n, slot_t *reserved) {
  if (n <= kExactClasses) {
    *reserved = n;
    return (unsigned)(n - 1);
  }
  slot_t p = round_up_pow2(n);
  *reserved = p;
  // log2(p) - log2(kExactClasses * 2) + kExactClasses
  unsigned cls = kExactClasses;
  for (slot_t q = (slot_t)kExactClasses * 2; q < p; q <<= 1)
    ++cls;
  return cls;
}

//===----------------------------------------------------------------------===//
// Allocator state (all in linear memory).
//===----------------------------------------------------------------------===//

typedef struct {
  slot_t *items;
  size_t len;
  size_t cap;
} free_stack_t;

static struct {
  int initialized;
  slot_t base; // first heap slot
  slot_t top;  // next never-allocated slot
  slot_t end;  // current table size (exclusive)
  free_stack_t free_lists[kNumClasses];
  // classes[slot - base] is 1 + the size class of the live block starting at
  // `slot`, or 0 if no live block starts there.  Covers [base, end).
  uint8_t *classes;
} state;

static void ensure_initialized(void) {
  if (state.initialized)
    return;
  // The linker always reserves slot 0 as the null externref (in an executable
  // it is part of the bss region; under PIC it is below __externref_table_base
  // and owned by the loader), so __externref_heap_base is never 0 and a slot
  // index of 0 is never handed out here: it is the null `__externref_t *`.
  state.top = externref_heap_base();
  state.end = externref_table_size();
  if (state.end < state.top)
    state.end = state.top;
  state.base = state.top;
  // Allocate at least one byte so an initially empty heap region does not get
  // a NULL side table (calloc(0, 1) may return NULL).
  size_t n = state.end - state.base;
  state.classes = (uint8_t *)calloc(n ? n : 1, 1);
  if (!state.classes) {
    // Without the side table nothing can be allocated; make every request
    // fail by leaving no room and forbidding growth (see reserve()).
    state.end = state.top;
  }
  state.initialized = 1;
}

static int free_stack_push(free_stack_t *s, slot_t idx) {
  if (s->len == s->cap) {
    size_t new_cap = s->cap ? s->cap * 2 : 16;
    slot_t *items = (slot_t *)realloc(s->items, new_cap * sizeof(*items));
    if (!items)
      return 0;
    s->items = items;
    s->cap = new_cap;
  }
  s->items[s->len++] = idx;
  return 1;
}

static int free_stack_pop(free_stack_t *s, slot_t *idx) {
  if (s->len == 0)
    return 0;
  *idx = s->items[--s->len];
  return 1;
}

// Makes room for at least `n` more slots past state.top, growing the table
// (and the side table) if necessary.  Returns 0 on failure.
static int reserve(slot_t n) {
  if (state.end - state.top >= n)
    return 1;
  if (!state.classes)
    return 0;
  slot_t need = n - (state.end - state.top);
  // Grow geometrically to amortize table.grow calls, but never by less than
  // the shortfall.  Guard against overflowing the 32-bit index space.
  slot_t want = state.end / 2;
  if (want < 64)
    want = 64;
  if (want < need)
    want = need;
  if ((slot_t)-1 - state.end < want)
    want = need;
  if ((slot_t)-1 - state.end < want)
    return 0;

  // Grow the side table first so a table.grow is never left unrecorded.
  slot_t new_end = state.end + want;
  uint8_t *classes = (uint8_t *)realloc(state.classes, new_end - state.base);
  if (!classes) {
    new_end = state.end + need;
    classes = (uint8_t *)realloc(state.classes, new_end - state.base);
    if (!classes)
      return 0;
    want = need;
  }
  state.classes = classes;
  for (slot_t i = state.end; i < new_end; ++i)
    state.classes[i - state.base] = 0;

  slot_t old = externref_table_grow(want);
  if (old == (slot_t)-1) {
    // Fall back to the minimum.
    old = externref_table_grow(need);
    if (old == (slot_t)-1)
      return 0;
    want = need;
  }
  // `old` is the previous table size; normally state.end, but someone else may
  // have grown the table behind our back.  Only [old, old + want) is ours.
  state.end = old + want;
  return 1;
}

// Frees the block of `reserved` slots in class `cls` starting at `idx`.
static void release(slot_t idx, unsigned cls, slot_t reserved) {
  externref_table_clear(idx, reserved);
  state.classes[idx - state.base] = 0;
  // If pushing onto the free list fails we simply leak the slots; they are
  // already nulled so no host objects are retained.
  (void)free_stack_push(&state.free_lists[cls], idx);
}

//===----------------------------------------------------------------------===//
// Public API.
//===----------------------------------------------------------------------===//

// Allocates `nrefs` contiguous null slots in __externref_table and returns a
// pointer to the first one, or NULL on failure.  A request of 0 is treated as
// a request of 1 so that the result is a unique non-null pointer.
COMPILER_RT_ABI __externref_t *__externref_table_alloc(size_t nrefs) {
  ensure_initialized();
  if (nrefs == 0)
    nrefs = 1;
  // The index space is 32 bits regardless of size_t's width.
  if (nrefs > (slot_t)-1 / 2)
    return NULL;

  slot_t reserved;
  unsigned cls = size_class((slot_t)nrefs, &reserved);

  slot_t idx;
  if (!free_stack_pop(&state.free_lists[cls], &idx)) {
    if (!reserve(reserved))
      return NULL;
    idx = state.top;
    state.top += reserved;
  }
  state.classes[idx - state.base] = (uint8_t)(cls + 1);
  return (__externref_t *)(uintptr_t)idx;
}

// Releases a block previously returned by __externref_table_alloc.  The slots
// are nulled so the host can collect the referenced objects.  Freeing NULL is
// a no-op; freeing a pointer that is not the start of a live block is ignored.
COMPILER_RT_ABI void __externref_table_free(__externref_t *p) {
  if (!p || !state.initialized)
    return;
  slot_t idx = (slot_t)(uintptr_t)p;
  if (idx < state.base || idx >= state.end)
    return;
  uint8_t tag = state.classes[idx - state.base];
  if (tag == 0)
    return;
  unsigned cls = tag - 1;
  slot_t reserved = cls < kExactClasses ? cls + 1
                                        : (slot_t)kExactClasses
                                              << (cls - kExactClasses + 1);
  release(idx, cls, reserved);
}

// Like __externref_table_free, but `nrefs` must equal the size passed to the
// matching __externref_table_alloc call; this skips the side-table lookup.
COMPILER_RT_ABI void __externref_table_free_sized(__externref_t *p,
                                                  size_t nrefs) {
  if (!p || !state.initialized)
    return;
  if (nrefs == 0)
    nrefs = 1;
  slot_t reserved;
  unsigned cls = size_class((slot_t)nrefs, &reserved);
  release((slot_t)(uintptr_t)p, cls, reserved);
}
