#include "heap.h"
#include "pagemap.h"
#include "../ds/fun.h"
#include <string.h>
#include <assert.h>

#include <platform.h>
#include <dtrace.h>

#ifdef PLATFORM_IS_ILP32
#define DONT_USE_COMPACT_HEAP
#endif

#ifdef DONT_USE_COMPACT_HEAP
typedef struct large_chunk_t
{
  // immutable
  pony_actor_t* actor;
  size_t size;
  char* m;

  // mutable
  uint32_t slots;
  uint32_t shallow;
  uint32_t finalisers;

  struct large_chunk_t* next;
} large_chunk_t;

typedef struct small_chunk_t
{
  // immutable
  pony_actor_t* actor;
  size_t size;
  char* m;

  // mutable
  uint32_t slots;
  uint32_t shallow;
  uint32_t finalisers;

  struct small_chunk_t* next;
} small_chunk_t;

typedef struct chunk_t
{
  // immutable
  pony_actor_t* actor;
  size_t size;
} chunk_t;
#else

// low bits represent pointer; using 52 bits for a bit of safety
#define HEAP_LOW_BITS 0xFFFFFFFFFFFFF
#define HEAP_HIGH_BITS ~0xFFFFFFFFFFFFF
#define HEAP_HIGH_BITS_SHIFT 52
#define HEAP_CHUNK_TYPE_BIT 0x1000000000000000
#define HEAP_SMALL_CHUNK_SIZE_SHIFT 61
#define HEAP_SMALL_CHUNK_SIZE_BITS 0xE000000000000000
#define HEAP_SMALL_CHUNK_SHALLOW_SHIFT 32
#define HEAP_SMALL_CHUNK_SHALLOW_BITS 0xFFFFFFFF00000000
#define HEAP_SMALL_CHUNK_SLOT_BITS 0xFFFFFFFF
#define HEAP_LARGE_CHUNK_SLOT_BIT 0x2000000000000000
#define HEAP_LARGE_CHUNK_SLOT_SHIFT 61
#define HEAP_LARGE_CHUNK_SHALLOW_BIT 0x4000000000000000
#define HEAP_LARGE_CHUNK_SHALLOW_SHIFT 62
#define HEAP_LARGE_CHUNK_FINALISER_BIT 0x8000000000000000

typedef struct chunk_t
{
  // immutable
  uintptr_t actor;
  uintptr_t m;
} chunk_t;

// for a large chunk:
// large chunk will always have next high bits as 0 so use that
// as a test for whether a chunk is large or not
// encode slots/shallow/finaliser in high bits (61 - 63) of actor
// bit 61 = slot
// bit 62 = shallow
// bit 63 = finaliser
typedef struct large_chunk_t
{
  // immutable
  uintptr_t actor;
  uintptr_t m;

  // mutable
  uintptr_t next;
  size_t size;
} large_chunk_t;

// for a small chunk:
// encode finaliser in high bits of actor/m/next
// bits 0 - 11 go into highest 12 bits (52 - 63) of actor
// bits 12 - 23 go into highest 12 bits (52 - 63) of next
// bits 24 - 31 go into lower highest 8 bits (52 - 59) of m
// encode size in high bits of m
// bit 60 of m indicates a small chunk
// bits 61 - 63 of m
typedef struct small_chunk_t
{
  // immutable
  uintptr_t actor;
  uintptr_t m;

  // mutable
  uintptr_t next;
  uint32_t slots;
  uint32_t shallow;
} small_chunk_t;

#endif

typedef char block_t[POOL_ALIGN];
typedef void (*large_chunk_fn)(large_chunk_t* chunk, uint32_t mark);
typedef void (*small_chunk_fn)(small_chunk_t* chunk, uint32_t mark);

#define SIZECLASS_SIZE(sizeclass) (HEAP_MIN << (sizeclass))
#define SIZECLASS_MASK(sizeclass) (~(SIZECLASS_SIZE(sizeclass) - 1))

#define EXTERNAL_PTR(p, sizeclass) \
  ((void*)((uintptr_t)p & SIZECLASS_MASK(sizeclass)))

#define FIND_SLOT(ext, base) \
  (1 << ((uintptr_t)((char*)(ext) - (char*)(base)) >> HEAP_MINBITS))

static const uint32_t sizeclass_empty[HEAP_SIZECLASSES] =
{
  0xFFFFFFFF,
  0x55555555,
  0x11111111,
  0x01010101,
  0x00010001
};

static const uint32_t sizeclass_init[HEAP_SIZECLASSES] =
{
  0xFFFFFFFE,
  0x55555554,
  0x11111110,
  0x01010100,
  0x00010000
};

static const uint32_t sizeclass_entries[HEAP_SIZECLASSES] =
{
  32,
  16,
  8,
  4,
  2
};

static const uint8_t sizeclass_table[HEAP_MAX / HEAP_MIN] =
{
  0, 1, 2, 2, 3, 3, 3, 3,
  4, 4, 4, 4, 4, 4, 4, 4
};

static size_t heap_initialgc = 1 << 14;
static double heap_nextgc_factor = 2.0;

static char* get_small_chunk_m(small_chunk_t* chunk)
{
#ifdef DONT_USE_COMPACT_HEAP
  return chunk->m;
#else
  // clear any high bits used to encode stuff
  return (char *)(chunk->m & HEAP_LOW_BITS);
#endif
}

static char* get_large_chunk_m(large_chunk_t* chunk)
{
#ifdef DONT_USE_COMPACT_HEAP
  return chunk->m;
#else
  return (char *)chunk->m;
#endif
}

static struct small_chunk_t* get_small_chunk_next(small_chunk_t* chunk)
{
#ifdef DONT_USE_COMPACT_HEAP
  return chunk->next;
#else
  // clear any high bits used to encode stuff
  return (struct small_chunk_t*)(chunk->next & HEAP_LOW_BITS);
#endif
}

static void set_small_chunk_next(small_chunk_t* chunk, small_chunk_t* next)
{
#ifdef DONT_USE_COMPACT_HEAP
  chunk->next = next;
#else
  // keep any high bits used to encode stuff
  chunk->next = (chunk->next & (size_t)HEAP_HIGH_BITS) | (uintptr_t)next;
#endif
}

static struct large_chunk_t* get_large_chunk_next(large_chunk_t* chunk)
{
#ifdef DONT_USE_COMPACT_HEAP
  return chunk->next;
#else
  return (struct large_chunk_t*)chunk->next;
#endif
}

static void large_pagemap(char* m, size_t size, large_chunk_t* chunk)
{
  char* end = m + size;

  while(m < end)
  {
    ponyint_pagemap_set(m, chunk);
    m += POOL_ALIGN;
  }
}

static bool chunk_is_large(chunk_t* chunk)
{
#ifdef DONT_USE_COMPACT_HEAP
  return chunk->size >= HEAP_SIZECLASSES;
#else
  return (chunk->m & HEAP_CHUNK_TYPE_BIT) == 0;
#endif
}

static size_t small_chunk_size(small_chunk_t* chunk)
{
#ifdef DONT_USE_COMPACT_HEAP
  return chunk->size;
#else
  return chunk->m >> HEAP_SMALL_CHUNK_SIZE_SHIFT;
#endif
}

static void set_small_chunk_size(small_chunk_t* chunk, size_t size)
{
#ifdef DONT_USE_COMPACT_HEAP
    chunk->size = size;
#else
    chunk->m = (chunk->m & ~HEAP_SMALL_CHUNK_SIZE_BITS) | (size << HEAP_SMALL_CHUNK_SIZE_SHIFT) | HEAP_CHUNK_TYPE_BIT;
#endif
}

static uint32_t large_chunk_slots(large_chunk_t* chunk)
{
#ifdef DONT_USE_COMPACT_HEAP
    return chunk->slots;
#else
    return (uint32_t)(chunk->actor & HEAP_LARGE_CHUNK_SLOT_BIT);
#endif
}

static void set_large_chunk_slots(large_chunk_t* chunk, uint32_t slots)
{
#ifdef DONT_USE_COMPACT_HEAP
    chunk->slots = slots;
#else
    assert(slots == 1 || slots == 0);
    chunk->actor |= ((size_t)slots << HEAP_LARGE_CHUNK_SLOT_SHIFT);
#endif
}

static uint32_t large_chunk_shallow(large_chunk_t* chunk)
{
#ifdef DONT_USE_COMPACT_HEAP
    return chunk->shallow;
#else
    return (uint32_t)(chunk->actor & HEAP_LARGE_CHUNK_SHALLOW_BIT);
#endif
}

static void set_large_chunk_shallow(large_chunk_t* chunk, uint32_t shallow)
{
#ifdef DONT_USE_COMPACT_HEAP
    chunk->shallow = shallow;
#else
    assert(shallow == 1 || shallow == 0);
    chunk->actor |= ((size_t)shallow << HEAP_LARGE_CHUNK_SHALLOW_SHIFT);
#endif
}

static uint32_t get_small_chunk_finaliser_bit(small_chunk_t* chunk, uint32_t bit)
{
    assert(bit <= 32);
#ifdef DONT_USE_COMPACT_HEAP
    return chunk->finalisers & (1 << bit);
#else
    if(bit < 12)
      return (uint32_t)(chunk->actor >> HEAP_HIGH_BITS_SHIFT) & (1 << bit);
    else if (bit < 24)
      return (uint32_t)(chunk->next >> HEAP_HIGH_BITS_SHIFT) & (1 << (bit - 12));
    else
      return (uint32_t)(chunk->m >> HEAP_HIGH_BITS_SHIFT) & (1 << (bit - 24));
#endif
}

static void set_small_chunk_finaliser_bit(small_chunk_t* chunk, uint32_t bit)
{
    assert(bit <= 32);
#ifdef DONT_USE_COMPACT_HEAP
    chunk->finalisers |= (1 << bit);
#else
    if(bit < 12)
      chunk->actor |= ((size_t)1 << ((size_t)bit + HEAP_HIGH_BITS_SHIFT));
    else if (bit < 24)
      chunk->next |= ((size_t)1 << ((size_t)bit - (size_t)12 + HEAP_HIGH_BITS_SHIFT));
    else
      chunk->m |= ((size_t)1 << ((size_t)bit - (size_t)24 + HEAP_HIGH_BITS_SHIFT));
#endif
}

static void clear_small_chunk_finaliser_bit(small_chunk_t* chunk, uint32_t bit)
{
    assert(bit <= 32);
#ifdef DONT_USE_COMPACT_HEAP
    chunk->finalisers &= ~(1 << bit);
#else
    if(bit < 12)
      chunk->actor &= ~((size_t)1 << ((size_t)bit + HEAP_HIGH_BITS_SHIFT));
    else if (bit < 24)
      chunk->next &= ~((size_t)1 << ((size_t)bit - (size_t)12 + HEAP_HIGH_BITS_SHIFT));
    else
      chunk->m &= ~((size_t)1 << ((size_t)bit - (size_t)24 + HEAP_HIGH_BITS_SHIFT));
#endif
}

static void clear_small_chunk(small_chunk_t* chunk, uint32_t mark)
{
  chunk->slots = mark;
  chunk->shallow = mark;
}

static void clear_large_chunk(large_chunk_t* chunk, uint32_t mark)
{
#ifdef DONT_USE_COMPACT_HEAP
  chunk->slots = mark;
  chunk->shallow = mark;
#else
  assert(mark == 1);
  set_large_chunk_slots(chunk, mark);
  set_large_chunk_shallow(chunk, mark);
#endif
}

static void final_small(small_chunk_t* chunk, uint32_t mark)
{
  // run any finalisers that need to be run
  void* p = NULL;

  // look through all finalisers
  for(uint32_t bit = 0; bit < sizeclass_entries[small_chunk_size(chunk)]; bit++)
  {
    // if there's a finaliser to run for a used slot
    if(get_small_chunk_finaliser_bit(chunk, bit) != 0)
    {
      p = get_small_chunk_m(chunk) + (bit << HEAP_MINBITS);

      // run finaliser
      assert((*(pony_type_t**)p)->final != NULL);
      (*(pony_type_t**)p)->final(p);

      // clear finaliser
      clear_small_chunk_finaliser_bit(chunk, bit);
    }
  }
  (void)mark;
}

static void final_small_freed(small_chunk_t* chunk)
{
  // run any finalisers that need to be run for any newly freed slots
  void* p = NULL;

  // look through all finalisers
  for(uint32_t bit = 0; bit < sizeclass_entries[small_chunk_size(chunk)]; bit++)
  {
    // if there's a finaliser to run for a free slot
    if((get_small_chunk_finaliser_bit(chunk, bit) != 0) && ((chunk->slots & (1 << bit)) != 0))
    {
      p = get_small_chunk_m(chunk) + (bit << HEAP_MINBITS);

      // run finaliser
      assert((*(pony_type_t**)p)->final != NULL);
      (*(pony_type_t**)p)->final(p);

      // clear finaliser
      clear_small_chunk_finaliser_bit(chunk, bit);
    }
  }
}

static void final_large(large_chunk_t* chunk, uint32_t mark)
{
#ifdef DONT_USE_COMPACT_HEAP
  if(chunk->finalisers == 1)
#else
  if((chunk->actor & HEAP_LARGE_CHUNK_FINALISER_BIT) == 1)
#endif
  {
    // run finaliser
    assert((*(pony_type_t**)get_large_chunk_m(chunk))->final != NULL);
    (*(pony_type_t**)get_large_chunk_m(chunk))->final(get_large_chunk_m(chunk));
#ifdef DONT_USE_COMPACT_HEAP
    chunk->finalisers = 0;
#else
    chunk->actor &= ~HEAP_LARGE_CHUNK_FINALISER_BIT;
#endif
  }
  (void)mark;
}

static void destroy_small(small_chunk_t* chunk, uint32_t mark)
{
  (void)mark;

  // run any finalisers that need running
  final_small(chunk, mark);

  ponyint_pagemap_set(get_small_chunk_m(chunk), NULL);
  POOL_FREE(block_t, get_small_chunk_m(chunk));
  POOL_FREE(small_chunk_t, chunk);
}

static void destroy_large(large_chunk_t* chunk, uint32_t mark)
{

  (void)mark;

  // run any finalisers that need running
  final_large(chunk, mark);

  large_pagemap(get_large_chunk_m(chunk), chunk->size, NULL);

  if(get_large_chunk_m(chunk) != NULL)
    ponyint_pool_free_size(chunk->size, get_large_chunk_m(chunk));

  POOL_FREE(large_chunk_t, chunk);
}

static size_t sweep_small(small_chunk_t* chunk, small_chunk_t** avail, small_chunk_t** full,
  uint32_t empty, size_t size)
{
  size_t used = 0;
  small_chunk_t* next;

  while(chunk != NULL)
  {
    next = get_small_chunk_next(chunk);
    chunk->slots &= chunk->shallow;

    if(chunk->slots == 0)
    {
      used += sizeof(block_t);
      set_small_chunk_next(chunk, *full);
      *full = chunk;
    } else if(chunk->slots == empty) {
      destroy_small(chunk, 0);
    } else {
      used += sizeof(block_t) -
        (__pony_popcount(chunk->slots) * size);
      set_small_chunk_next(chunk, *avail);
      *avail = chunk;

      // run finalisers for freed slots
      final_small_freed(chunk);
    }

    chunk = next;
  }

  return used;
}

static large_chunk_t* sweep_large(large_chunk_t* chunk, size_t* used)
{
  large_chunk_t* list = NULL;
  large_chunk_t* next;

  while(chunk != NULL)
  {
    next = get_large_chunk_next(chunk);
    set_large_chunk_slots(chunk, large_chunk_slots(chunk) & large_chunk_shallow(chunk));

    if(large_chunk_slots(chunk) == 0)
    {
#ifdef DONT_USE_COMPACT_HEAP
      chunk->next = list;
#else
      chunk->next = (uintptr_t)list;
#endif
      list = chunk;
      *used += chunk->size;
    } else {
      destroy_large(chunk, 0);
    }

    chunk = next;
  }

  return list;
}

static void chunk_list_large(large_chunk_fn f, large_chunk_t* current, uint32_t mark)
{
  large_chunk_t* next;

  while(current != NULL)
  {
    next = get_large_chunk_next(current);
    f(current, mark);
    current = next;
  }
}

static void chunk_list_small(small_chunk_fn f, small_chunk_t* current, uint32_t mark)
{
  small_chunk_t* next;

  while(current != NULL)
  {
    next = get_small_chunk_next(current);
    f(current, mark);
    current = next;
  }
}

uint32_t ponyint_heap_index(size_t size)
{
  // size is in range 1..HEAP_MAX
  // change to 0..((HEAP_MAX / HEAP_MIN) - 1) and look up in table
  return sizeclass_table[(size - 1) >> HEAP_MINBITS];
}

void ponyint_heap_setinitialgc(size_t size)
{
  heap_initialgc = (size_t)1 << size;
}

void ponyint_heap_setnextgcfactor(double factor)
{
  if(factor < 1.0)
    factor = 1.0;

  DTRACE1(GC_THRESHOLD, factor);
  heap_nextgc_factor = factor;
}

void ponyint_heap_init(heap_t* heap)
{
  memset(heap, 0, sizeof(heap_t));
  heap->next_gc = heap_initialgc;
}

void ponyint_heap_destroy(heap_t* heap)
{
  chunk_list_large(destroy_large, heap->large, 0);

  for(int i = 0; i < HEAP_SIZECLASSES; i++)
  {
    chunk_list_small(destroy_small, heap->small_free[i], 0);
    chunk_list_small(destroy_small, heap->small_full[i], 0);
  }
}

void ponyint_heap_final(heap_t* heap)
{
  chunk_list_large(final_large, heap->large, 0);

  for(int i = 0; i < HEAP_SIZECLASSES; i++)
  {
    chunk_list_small(final_small, heap->small_free[i], 0);
    chunk_list_small(final_small, heap->small_full[i], 0);
  }
}

void* ponyint_heap_alloc(pony_actor_t* actor, heap_t* heap, size_t size,
  bool has_finaliser)
{
  if(size == 0)
  {
    return NULL;
  } else if(size <= HEAP_MAX) {
    return ponyint_heap_alloc_small(actor, heap, ponyint_heap_index(size),
      has_finaliser);
  } else {
    return ponyint_heap_alloc_large(actor, heap, size, has_finaliser);
  }
}

void* ponyint_heap_alloc_small(pony_actor_t* actor, heap_t* heap,
  uint32_t sizeclass, bool has_finaliser)
{
  small_chunk_t* chunk = heap->small_free[sizeclass];
  void* m;

  // If there are none in this size class, get a new one.
  if(chunk != NULL)
  {
    // Clear and use the first available slot.
    uint32_t slots = chunk->slots;
    uint32_t bit = __pony_ffs(slots) - 1;
    slots &= ~(1 << bit);

    m = (void*)(get_small_chunk_m(chunk) + (bit << HEAP_MINBITS));
    chunk->slots = slots;

    // note that a finaliser needs to run
    if(has_finaliser)
      set_small_chunk_finaliser_bit(chunk, bit);

    if(slots == 0)
    {
      heap->small_free[sizeclass] = get_small_chunk_next(chunk);
      set_small_chunk_next(chunk, heap->small_full[sizeclass]);
      heap->small_full[sizeclass] = chunk;
    }
  } else {
    small_chunk_t* n = (small_chunk_t*) POOL_ALLOC(small_chunk_t);
#ifdef DONT_USE_COMPACT_HEAP
    n->actor = actor;
    n->m = (char*) POOL_ALLOC(block_t);
    n->next = NULL;
#else
    n->actor = (uintptr_t)actor;
    n->m = (uintptr_t) POOL_ALLOC(block_t);
    n->next = (uintptr_t)NULL;
#endif
    set_small_chunk_size(n, sizeclass);

    // note that a finaliser needs to run
#ifdef DONT_USE_COMPACT_HEAP
    if(has_finaliser)
      n->finalisers = 1;
    else
      n->finalisers = 0;
#else
    if(has_finaliser)
      set_small_chunk_finaliser_bit(n, 0);
#endif

    // Clear the first bit.
    n->shallow = n->slots = sizeclass_init[sizeclass];

    ponyint_pagemap_set(get_small_chunk_m(n), n);

    heap->small_free[sizeclass] = n;
    chunk = n;

    // Use the first slot.
    m = get_small_chunk_m(chunk);
  }

  heap->used += SIZECLASS_SIZE(sizeclass);
  return m;
}

void* ponyint_heap_alloc_large(pony_actor_t* actor, heap_t* heap, size_t size,
  bool has_finaliser)
{
  size = ponyint_pool_adjust_size(size);

  large_chunk_t* chunk = (large_chunk_t*) POOL_ALLOC(large_chunk_t);
#ifdef DONT_USE_COMPACT_HEAP
  chunk->actor = actor;
  chunk->m = (char*) ponyint_pool_alloc_size(size);
#else
  chunk->actor = (uintptr_t)actor;
  chunk->m = (uintptr_t) ponyint_pool_alloc_size(size);
#endif
  chunk->size = size;

#ifdef DONT_USE_COMPACT_HEAP
  chunk->slots = 0;
  chunk->shallow = 0;

  // note that a finaliser needs to run
  if(has_finaliser)
    chunk->finalisers = 1;
  else
    chunk->finalisers = 0;
#else
  // note that a finaliser needs to run
  if(has_finaliser)
    chunk->actor |= HEAP_LARGE_CHUNK_FINALISER_BIT;
#endif

  large_pagemap(get_large_chunk_m(chunk), size, chunk);

#ifdef DONT_USE_COMPACT_HEAP
  chunk->next = heap->large;
#else
  chunk->next = (uintptr_t)heap->large;
#endif
  heap->large = chunk;
  heap->used += chunk->size;

  return get_large_chunk_m(chunk);
}

void* ponyint_heap_realloc(pony_actor_t* actor, heap_t* heap, void* p,
  size_t size, bool has_finaliser)
{
  if(p == NULL)
    return ponyint_heap_alloc(actor, heap, size, has_finaliser);

  chunk_t* chunk = (chunk_t*)ponyint_pagemap_get(p);

  if(chunk == NULL)
  {
    // Get new memory and copy from the old memory.
    void* q = ponyint_heap_alloc(actor, heap, size, has_finaliser);
    memcpy(q, p, size);
    return q;
  }

  size_t oldsize;

  if(!chunk_is_large(chunk))
  {
    small_chunk_t* small_chunk = (small_chunk_t*)chunk;

    // Previous allocation was a ponyint_heap_alloc_small.
    void* ext = EXTERNAL_PTR(p, small_chunk_size(small_chunk));

    // If the new allocation is a ponyint_heap_alloc_small and the pointer is
    // not an internal pointer, we may be able to reuse this memory. If it is
    // an internal pointer, we know where the old allocation begins but not
    // where it ends, so we cannot reuse this memory.
    if((size <= HEAP_MAX) && (p == ext))
    {
      uint32_t sizeclass = ponyint_heap_index(size);

      // If the new allocation is the same size or smaller, return the old
      // one.
      if(sizeclass <= small_chunk_size(small_chunk))
        return p;
    }

    oldsize = SIZECLASS_SIZE(small_chunk_size(small_chunk)) - ((uintptr_t)p - (uintptr_t)ext);
  } else {
    large_chunk_t* large_chunk = (large_chunk_t*)chunk;

    // Previous allocation was a ponyint_heap_alloc_large.
    if((size <= large_chunk->size) && (p == get_large_chunk_m(large_chunk)))
    {
      // If the new allocation is the same size or smaller, and this is not an
      // internal pointer, return the old one. We can't reuse internal
      // pointers in large allocs for the same reason as small ones.
      return p;
    }

    oldsize = large_chunk->size - ((uintptr_t)p - (uintptr_t)get_large_chunk_m(large_chunk));
  }

  // Determine how much memory to copy.
  if(oldsize > size)
    oldsize = size;

  // Get new memory and copy from the old memory.
  void* q = ponyint_heap_alloc(actor, heap, size, has_finaliser);
  memcpy(q, p, oldsize);
  return q;
}

void ponyint_heap_used(heap_t* heap, size_t size)
{
  heap->used += size;
}

bool ponyint_heap_startgc(heap_t* heap)
{
  if(heap->used <= heap->next_gc)
    return false;

  for(int i = 0; i < HEAP_SIZECLASSES; i++)
  {
    uint32_t mark = sizeclass_empty[i];
    chunk_list_small(clear_small_chunk, heap->small_free[i], mark);
    chunk_list_small(clear_small_chunk, heap->small_full[i], mark);
  }

  chunk_list_large(clear_large_chunk, heap->large, 1);

  // reset used to zero
  heap->used = 0;
  return true;
}

bool ponyint_heap_mark(chunk_t* chunk, void* p)
{
  // If it's an internal pointer, we shallow mark it instead. This will
  // preserve the external pointer, but allow us to mark and recurse the
  // external pointer in the same pass.
  bool marked;

  if(chunk_is_large(chunk))
  {
    large_chunk_t* large_chunk = (large_chunk_t*)chunk;
    marked = large_chunk_slots(large_chunk) == 0;

    if(p == get_large_chunk_m(large_chunk))
      set_large_chunk_slots(large_chunk, 0);
    else
      set_large_chunk_shallow(large_chunk, 0);
  } else {
    small_chunk_t* small_chunk = (small_chunk_t*)chunk;

    // Calculate the external pointer.
    void* ext = EXTERNAL_PTR(p, small_chunk_size(small_chunk));

    // Shift to account for smallest allocation size.
    uint32_t slot = FIND_SLOT(ext, get_small_chunk_m(small_chunk));

    // Check if it was already marked.
    marked = (small_chunk->slots & slot) == 0;

    // A clear bit is in-use, a set bit is available.
    if(p == ext)
      small_chunk->slots &= ~slot;
    else
      small_chunk->shallow &= ~slot;
  }

  return marked;
}

void ponyint_heap_mark_shallow(chunk_t* chunk, void* p)
{
  if(chunk_is_large(chunk))
  {
    large_chunk_t* large_chunk = (large_chunk_t*)chunk;
    set_large_chunk_shallow(large_chunk, 0);
  } else {
    small_chunk_t* small_chunk = (small_chunk_t*)chunk;

    // Calculate the external pointer.
    void* ext = EXTERNAL_PTR(p, small_chunk_size(small_chunk));

    // Shift to account for smallest allocation size.
    uint32_t slot = FIND_SLOT(ext, get_small_chunk_m(small_chunk));

    // A clear bit is in-use, a set bit is available.
    small_chunk->shallow &= ~slot;
  }
}

bool ponyint_heap_ismarked(chunk_t* chunk, void* p)
{
  if(chunk_is_large(chunk))
  {
    large_chunk_t* large_chunk = (large_chunk_t*)chunk;
    return (large_chunk_slots(large_chunk) & large_chunk_shallow(large_chunk)) == 0;
  }

  small_chunk_t* small_chunk = (small_chunk_t*)chunk;

  // Shift to account for smallest allocation size.
  uint32_t slot = FIND_SLOT(p, get_small_chunk_m(small_chunk));

  // Check if the slot is marked or shallow marked.
  return (small_chunk->slots & small_chunk->shallow & slot) == 0;
}

void ponyint_heap_free(chunk_t* chunk, void* p)
{
  if(chunk_is_large(chunk))
  {
    large_chunk_t* large_chunk = (large_chunk_t*)chunk;
    if(p == get_large_chunk_m(large_chunk))
    {
      // run finaliser if needed
      final_large(large_chunk, 0);

      ponyint_pool_free_size(large_chunk->size, get_large_chunk_m(large_chunk));
#ifdef DONT_USE_COMPACT_HEAP
      large_chunk->m = NULL;
#else
      large_chunk->m = (uintptr_t)NULL;
#endif
      set_large_chunk_slots(large_chunk, 1);
    }
    return;
  }

  small_chunk_t* small_chunk = (small_chunk_t*)chunk;

  // Calculate the external pointer.
  void* ext = EXTERNAL_PTR(p, small_chunk_size(small_chunk));

  if(p == ext)
  {
    // Shift to account for smallest allocation size.
    uint32_t slot = FIND_SLOT(ext, get_small_chunk_m(small_chunk));

    // check if there's a finaliser to run
    if(get_small_chunk_finaliser_bit(small_chunk, slot) != 0)
    {
      // run finaliser
      (*(pony_type_t**)p)->final(p);

      // clear finaliser
      clear_small_chunk_finaliser_bit(small_chunk, slot);
    }

    // free slot
    small_chunk->slots |= slot;
  }
}

void ponyint_heap_endgc(heap_t* heap)
{
  size_t used = 0;

  for(int i = 0; i < HEAP_SIZECLASSES; i++)
  {
    small_chunk_t* list1 = heap->small_free[i];
    small_chunk_t* list2 = heap->small_full[i];

    heap->small_free[i] = NULL;
    heap->small_full[i] = NULL;

    small_chunk_t** avail = &heap->small_free[i];
    small_chunk_t** full = &heap->small_full[i];

    size_t size = SIZECLASS_SIZE(i);
    uint32_t empty = sizeclass_empty[i];

    used += sweep_small(list1, avail, full, empty, size);
    used += sweep_small(list2, avail, full, empty, size);
  }

  heap->large = sweep_large(heap->large, &used);

  // Foreign object sizes will have been added to heap->used already. Here we
  // add local object sizes as well and set the next gc point for when memory
  // usage has increased.
  heap->used += used;
  heap->next_gc = (size_t)((double)heap->used * heap_nextgc_factor);

  if(heap->next_gc < heap_initialgc)
    heap->next_gc = heap_initialgc;
}

pony_actor_t* ponyint_heap_owner(chunk_t* chunk)
{
  // FIX: false sharing
  // reading from something that will never be written
  // but is on a cache line that will often be written
  // called during tracing
  // actual chunk only needed for GC tracing
  // all other tracing only needs the owner
  // so the owner needs the chunk and everyone else just needs the owner
#ifdef DONT_USE_COMPACT_HEAP
  return chunk->actor;
#else
  // clear any high bits used to encode stuff
  return (pony_actor_t*)(chunk->actor & HEAP_LOW_BITS);
#endif
}

size_t ponyint_heap_size(chunk_t* chunk)
{
  if(chunk_is_large(chunk))
    return ((large_chunk_t*)chunk)->size;

  return SIZECLASS_SIZE(small_chunk_size((small_chunk_t*)chunk));
}
