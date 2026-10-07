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
// This file is part of the compiler builtins so that every language that
// targets WebAssembly (C, C++, Rust, ...) links the same allocator and shares
// one notion of the table heap.  It therefore has no dependency on a C
// library.  Its only need for linear memory is a small amount of metadata,
// obtained through __externref_metadata_alloc (below) and never freed.
//
// Externref values cannot live in linear memory, so only externrefs go in the
// table and all bookkeeping is kept beside it.  The design is deliberately
// simple:
//
//   * Fresh slots are handed out from a bump pointer.
//   * Freed blocks are pushed onto per-size-class free lists.  Sizes up to
//     kExactClasses have an exact class; larger requests are rounded up to a
//     power of two and the caller is allotted the rounded size.
//   * An open-addressing hash map from block start to size class lets a block
//     be freed without knowing its size (`__externref_table_free(p)`), which
//     is what `delete` and `delete[]` on an `__externref_t *` need since there
//     is nowhere in the table to keep an array cookie.  Its size is
//     proportional to the number of live blocks, not to the table.
//     `__externref_table_free_sized(p, n)` is available when the caller does
//     know the size.
//   * Freed slots are nulled with table.fill so the host GC can reclaim the
//     referenced objects.
//
// Threads: each WebAssembly thread has its own __externref_table (tables are
// not shared between instances), so all allocator state is thread-local and
// no locking is needed.  Without the atomics feature `_Thread_local` is just
// a plain global.
//
//===----------------------------------------------------------------------===//

#include <stddef.h>
#include <stdint.h>

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
// Linear memory for metadata.
//
// Linear memory on WebAssembly is owned by whoever implements malloc: libc
// allocators (emscripten, wasi-libc, Rust's dlmalloc) assume that nobody else
// calls memory.grow, so this file must not grow the memory behind their back.
// Instead it asks for metadata memory through a weak hook that a C library or
// language runtime overrides to route to its own allocator:
//
//   void *__externref_metadata_alloc(size_t n);
//
// It must return `n` bytes of 8-byte-aligned memory, or NULL.  The memory is
// never freed (the allocator recycles its own free-list nodes and map
// buffers), and it is only ever requested in chunks of a page or more.  The
// default implementation below is for modules with no memory owner at all:
// a static reserve (zero cost in the binary: it lives in .bss) followed by
// memory.grow.
//
// Requests are rare and large, so a simple bump arena over whatever the hook
// returns is enough.
//===----------------------------------------------------------------------===//

enum { kPageSize = 65536 };

static _Thread_local unsigned char static_reserve[kPageSize]
    __attribute__((aligned(8)));
static _Thread_local int static_reserve_used;

__attribute__((weak)) void *__externref_metadata_alloc(size_t n) {
  if (!static_reserve_used && n <= sizeof(static_reserve)) {
    static_reserve_used = 1;
    return static_reserve;
  }
  size_t pages = (n + kPageSize - 1) / kPageSize;
  uintptr_t old = (uintptr_t)__builtin_wasm_memory_grow(0, pages);
  if (old == (uintptr_t)-1)
    return NULL;
  return (void *)(old * kPageSize);
}

static _Thread_local struct {
  unsigned char *cur; // next free byte
  unsigned char *end; // end of the current chunk
} arena;

// Allocates `n` bytes, 8-byte aligned, or returns NULL if no memory is
// available.
static void *arena_alloc(size_t n) {
  n = (n + 7) & ~(size_t)7;
  if ((size_t)(arena.end - arena.cur) < n) {
    // Request at least a page, and geometrically more as the arena grows, so
    // the hook is called O(log n) times; what it returns is never contiguous
    // with the previous chunk in general, so the old tail is abandoned.
    size_t chunk = (size_t)(arena.end - arena.cur) * 2;
    if (chunk < kPageSize)
      chunk = kPageSize;
    if (chunk < n)
      chunk = n;
    unsigned char *p = (unsigned char *)__externref_metadata_alloc(chunk);
    if (!p && chunk > n) {
      chunk = (n + kPageSize - 1) & ~(size_t)(kPageSize - 1);
      p = (unsigned char *)__externref_metadata_alloc(chunk);
    }
    if (!p)
      return NULL;
    arena.cur = p;
    arena.end = p + chunk;
  }
  void *p = arena.cur;
  arena.cur += n;
  return p;
}

//===----------------------------------------------------------------------===//
// Size classes.
//===----------------------------------------------------------------------===//

// Requests of 1..kExactClasses slots get their own class.  Larger requests are
// rounded up to a power of two >= kExactClasses * 2.
enum { kExactClasses = 32 };
enum { kNumClasses = kExactClasses + 8 * sizeof(slot_t) };

// Returns the size class for a request of `n` slots and stores the number of
// slots actually reserved for that class in *reserved.
static unsigned size_class(slot_t n, slot_t *reserved) {
  if (n <= kExactClasses) {
    *reserved = n;
    return (unsigned)(n - 1);
  }
  unsigned cls = kExactClasses;
  slot_t p = (slot_t)kExactClasses * 2;
  while (p < n) {
    p <<= 1;
    ++cls;
  }
  *reserved = p;
  return cls;
}

// Number of slots reserved for a block of class `cls`.
static slot_t class_size(unsigned cls) {
  if (cls < kExactClasses)
    return cls + 1;
  return (slot_t)kExactClasses << (cls - kExactClasses + 1);
}

//===----------------------------------------------------------------------===//
// Allocator state (all in linear memory, all thread-local).
//===----------------------------------------------------------------------===//

// Free lists are singly linked through nodes carved from the arena; nodes are
// recycled through `free_nodes` so the arena only ever grows to the peak
// number of simultaneously free blocks.
typedef struct free_node {
  struct free_node *next;
  slot_t idx;
} free_node_t;

// Live-block map: open addressing with linear probing, keyed by block start
// slot (never 0, since slot 0 is the null externref).  Values are 1 + the
// size class.  Deleted entries are tombstoned so probe chains stay intact;
// the map is rebuilt when it fills up.
typedef struct {
  slot_t key;        // 0 = empty, kTombstone = deleted
  unsigned char cls; // 1 + size class
} map_entry_t;

enum { kTombstone = (slot_t)-1 };

static _Thread_local struct {
  int initialized;
  int failed;  // set if the initial map could not be allocated
  slot_t base; // first heap slot
  slot_t top;  // next never-allocated slot
  slot_t end;  // current table size (exclusive)
  free_node_t *free_lists[kNumClasses];
  free_node_t *free_nodes;
  map_entry_t *map;
  size_t map_cap;  // power of two
  size_t map_used; // live + tombstones
  size_t map_live;
  // The previous map buffer, reused for the next rehash when it is big
  // enough (which it always is for a same-size tombstone purge), so that
  // rehashing does not leak arena memory in the common case.
  map_entry_t *spare;
  size_t spare_cap;
} state;

static size_t map_hash(slot_t key, size_t cap) {
  // Fibonacci hashing; block starts are often regularly spaced.
  return (size_t)((uint32_t)key * 2654435769u) & (cap - 1);
}

static map_entry_t *map_find(slot_t key) {
  size_t i = map_hash(key, state.map_cap);
  for (size_t n = 0; n < state.map_cap; ++n) {
    map_entry_t *e = &state.map[i];
    if (e->key == key)
      return e;
    if (e->key == 0)
      return NULL;
    i = (i + 1) & (state.map_cap - 1);
  }
  return NULL;
}

// Inserts into `m` (of capacity `cap`, no tombstones) without resizing.
static void map_insert_raw(map_entry_t *m, size_t cap, slot_t key,
                           unsigned char cls) {
  size_t i = map_hash(key, cap);
  while (m[i].key != 0)
    i = (i + 1) & (cap - 1);
  m[i].key = key;
  m[i].cls = cls;
}

// Rebuilds the map with capacity `cap`, dropping tombstones.  Returns 0 on
// failure (the old map is kept).
static int map_rehash(size_t cap) {
  map_entry_t *m;
  size_t m_cap;
  if (state.spare && state.spare_cap >= cap) {
    m = state.spare;
    m_cap = state.spare_cap;
    cap = m_cap;
  } else {
    m = (map_entry_t *)arena_alloc(cap * sizeof(map_entry_t));
    if (!m)
      return 0;
    m_cap = cap;
  }
  for (size_t i = 0; i < cap; ++i) {
    m[i].key = 0;
    m[i].cls = 0;
  }
  for (size_t i = 0; i < state.map_cap; ++i) {
    map_entry_t *e = &state.map[i];
    if (e->key != 0 && e->key != kTombstone)
      map_insert_raw(m, cap, e->key, e->cls);
  }
  // Keep the old buffer as the spare for the next rehash (a same-size
  // tombstone purge reuses it; a doubling allocates once and then the two
  // buffers alternate).
  state.spare = state.map;
  state.spare_cap = state.map_cap;
  state.map = m;
  state.map_cap = m_cap;
  state.map_used = state.map_live;
  return 1;
}

// Records a live block.  Returns 0 if the map is full and cannot grow.
static int map_insert(slot_t key, unsigned char cls) {
  // Keep the load factor (including tombstones) under 3/4.
  if ((state.map_used + 1) * 4 > state.map_cap * 3) {
    size_t cap = state.map_cap;
    // Grow only if the live entries alone would exceed half; otherwise a
    // same-size rehash to purge tombstones suffices.
    if ((state.map_live + 1) * 2 > cap)
      cap *= 2;
    if (!map_rehash(cap))
      return 0;
  }
  map_insert_raw(state.map, state.map_cap, key, cls);
  ++state.map_used;
  ++state.map_live;
  return 1;
}

static void map_erase(map_entry_t *e) {
  e->key = kTombstone;
  e->cls = 0;
  --state.map_live;
}

static void ensure_initialized(void) {
  if (state.initialized)
    return;
  state.initialized = 1;
  // The linker always reserves slot 0 as the null externref (in an executable
  // it is part of the bss region; under PIC it is below __externref_table_base
  // and owned by the loader), so __externref_heap_base is never 0 and a slot
  // index of 0 is never handed out here: it is the null `__externref_t *`.
  state.top = externref_heap_base();
  state.end = externref_table_size();
  if (state.end < state.top)
    state.end = state.top;
  state.base = state.top;
  if (!map_rehash(256))
    state.failed = 1;
}

static int free_list_push(unsigned cls, slot_t idx) {
  free_node_t *node = state.free_nodes;
  if (node) {
    state.free_nodes = node->next;
  } else {
    node = (free_node_t *)arena_alloc(sizeof(free_node_t));
    if (!node)
      return 0;
  }
  node->idx = idx;
  node->next = state.free_lists[cls];
  state.free_lists[cls] = node;
  return 1;
}

static int free_list_pop(unsigned cls, slot_t *idx) {
  free_node_t *node = state.free_lists[cls];
  if (!node)
    return 0;
  state.free_lists[cls] = node->next;
  *idx = node->idx;
  node->next = state.free_nodes;
  state.free_nodes = node;
  return 1;
}

// Makes room for at least `n` more slots past state.top, growing the table
// (and the side table) if necessary.  Returns 0 on failure.
static int reserve(slot_t n) {
  if (state.end - state.top >= n)
    return 1;
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
  if (old != state.end)
    state.top = old;
  state.end = old + want;
  return 1;
}

// Frees the block of `reserved` slots in class `cls` starting at `idx`.
static void release(slot_t idx, unsigned cls, slot_t reserved) {
  externref_table_clear(idx, reserved);
  // If pushing onto the free list fails we simply leak the slots; they are
  // already nulled so no host objects are retained.
  (void)free_list_push(cls, idx);
}

//===----------------------------------------------------------------------===//
// Public API.
//===----------------------------------------------------------------------===//

// Allocates `nrefs` contiguous null slots in __externref_table and returns a
// pointer to the first one, or NULL on failure.  A request of 0 is treated as
// a request of 1 so that the result is a unique non-null pointer.
COMPILER_RT_ABI __externref_t *__externref_table_alloc(size_t nrefs) {
  ensure_initialized();
  if (state.failed)
    return NULL;
  if (nrefs == 0)
    nrefs = 1;
  // The index space is 32 bits regardless of size_t's width.
  if (nrefs > (slot_t)-1 / 2)
    return NULL;

  slot_t reserved;
  unsigned cls = size_class((slot_t)nrefs, &reserved);

  slot_t idx;
  int from_free_list = free_list_pop(cls, &idx);
  if (!from_free_list) {
    if (!reserve(reserved))
      return NULL;
    idx = state.top;
  }
  if (!map_insert(idx, (unsigned char)(cls + 1))) {
    // Out of metadata memory; put the block back and fail the allocation.
    if (from_free_list)
      (void)free_list_push(cls, idx);
    return NULL;
  }
  if (!from_free_list)
    state.top += reserved;
  return (__externref_t *)(uintptr_t)idx;
}

// Releases a block previously returned by __externref_table_alloc.  The slots
// are nulled so the host can collect the referenced objects.  Freeing NULL is
// a no-op; freeing a pointer that is not the start of a live block is ignored.
COMPILER_RT_ABI void __externref_table_free(__externref_t *p) {
  if (!p || !state.initialized || state.failed)
    return;
  slot_t idx = (slot_t)(uintptr_t)p;
  map_entry_t *e = map_find(idx);
  if (!e || e->key == kTombstone)
    return;
  unsigned cls = e->cls - 1;
  map_erase(e);
  release(idx, cls, class_size(cls));
}

// Like __externref_table_free, but `nrefs` must equal the size passed to the
// matching __externref_table_alloc call; this skips the side-table lookup.
COMPILER_RT_ABI void __externref_table_free_sized(__externref_t *p,
                                                  size_t nrefs) {
  if (!p || !state.initialized || state.failed)
    return;
  if (nrefs == 0)
    nrefs = 1;
  slot_t reserved;
  unsigned cls = size_class((slot_t)nrefs, &reserved);
  slot_t idx = (slot_t)(uintptr_t)p;
  map_entry_t *e = map_find(idx);
  if (e && e->key != kTombstone)
    map_erase(e);
  release(idx, cls, reserved);
}
