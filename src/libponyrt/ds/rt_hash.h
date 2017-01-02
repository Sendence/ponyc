#ifndef ds_rt_hash_h
#define ds_rt_hash_h

#include "fun.h"
#include "../pony.h"
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <platform.h>

PONY_EXTERN_C_BEGIN

#define RT_HASHMAP_BEGIN ((size_t)-1)
#define RT_HASHMAP_UNKNOWN ((size_t)-1)

#ifdef PLATFORM_IS_ILP32
  typedef uint32_t bitmap_t;
  #define RT_HASHMAP_BITMAP_TYPE_SIZE 32
#else
  typedef uint64_t bitmap_t;
  #define RT_HASHMAP_BITMAP_TYPE_SIZE 64
#endif

/** Definition of a hash map entry for uintptr_t.
 */
typedef struct rt_hashmap_entry_t
{
  void* ptr;         /* pointer */
  uintptr_t data;    /* data */
} rt_hashmap_entry_t;

/** Definition of a quadratic probing hash map.
 *
 *  Do not access the fields of this type.
 */
typedef struct rt_hashmap_t
{
  size_t count;   /* number of elements in the map */
  size_t size;    /* size of the buckets array */
  size_t deleted_count;   /* number of deleted elements in the map */
  ssize_t optimize_deleted_shift;   /* shift amount for when to run optimize */
  bitmap_t* item_bitmap;   /* Item bitarray to keep track items for optimized scanning */
  rt_hashmap_entry_t* buckets;
} rt_hashmap_t;

/** Initializes a new hash map.
 *
 *  This is a quadratic probing hash map.
 */
void ponyint_rt_hashmap_init(rt_hashmap_t* map, size_t size, alloc_fn alloc);

/** Destroys a hash map.
 *
 */
void ponyint_rt_hashmap_destroy(rt_hashmap_t* map, free_size_fn fr,
  free_fn free_elem);

/** Checks whether the hashmap needs optimization or not.
 *
 */
bool ponyint_rt_hashmap_needs_optimize(rt_hashmap_t* map);

/** Handles bookkeeping for end of optimize.
 *
 */
void ponyint_rt_hashmap_finish_optimize(rt_hashmap_t* map, size_t num_optimized);

/** Optimize single item at specified index with given pointer/key by moving to an
 *  earlier bucket if it has a deleted entry.
 *
 *  NOTE: This function assumes the caller provided information is correct and does not
 *  validate that the given entry/key are actually present at the index provided.
 */
size_t ponyint_rt_hashmap_optimize_item(rt_hashmap_t* map, rt_hash_fn hash, alloc_fn alloc,
  free_size_fn fr, size_t old_index, void* entry, uintptr_t key);

/** Retrieve an element from a hash map.
 *
 *  Returns a pointer to the element, or NULL.
 */
void* ponyint_rt_hashmap_get(rt_hashmap_t* map, uintptr_t key, rt_hash_fn hash, size_t* index,
  alloc_fn alloc, free_size_fn fr);

/** Put a new element in a hash map.
 *
 *  If the element is already in the hash map, the old
 *  element is overwritten and returned to the caller.
 */
void* ponyint_rt_hashmap_put(rt_hashmap_t* map, void* entry, uintptr_t key, rt_hash_fn hash,
  alloc_fn alloc, free_size_fn fr);

/** Put a new element in a hash map at a specific index.
 *
 *  If an element is already in the hash map at that position, the old
 *  element is overwritten and returned to the caller.
 */
void* ponyint_rt_hashmap_putindex(rt_hashmap_t* map, void* entry, uintptr_t key, rt_hash_fn hash,
  alloc_fn alloc, free_size_fn fr, size_t index);

/** Removes a given entry from a hash map.
 *
 *  Returns the element removed (if any).
 */
void* ponyint_rt_hashmap_remove(rt_hashmap_t* map, uintptr_t key, rt_hash_fn hash,
  alloc_fn alloc, free_size_fn fr);

/** Removes a given entry from a hash map by index.
 *
 *  Returns the element removed (if any).
 */
void* ponyint_rt_hashmap_removeindex(rt_hashmap_t* map, size_t index);

/** Get the number of elements in the map.
 */
size_t ponyint_rt_hashmap_size(rt_hashmap_t* map);

/** Hashmap iterator.
 *
 *  Set i to RT_HASHMAP_BEGIN, then call until this returns NULL.
 */
void* ponyint_rt_hashmap_next(rt_hashmap_t* map, size_t* i);

#define DECLARE_RT_HASHMAP(name, name_t, type) \
  typedef struct name_t { rt_hashmap_t contents; } name_t; \
  void name##_init(name_t* map, size_t size); \
  void name##_destroy(name_t* map); \
  void name##_optimize(name_t* map); \
  bool name##_needs_optimize(name_t* map); \
  void name##_finish_optimize(name_t* map, size_t num_optimized); \
  size_t name##_optimize_item(name_t* map, type* entry, uintptr_t key, size_t old_index); \
  type* name##_get(name_t* map, uintptr_t key, size_t* index); \
  type* name##_put(name_t* map, type* entry, uintptr_t key); \
  type* name##_putindex(name_t* map, type* entry, uintptr_t key, size_t index); \
  type* name##_remove(name_t* map, uintptr_t key); \
  type* name##_removeindex(name_t* map, size_t index); \
  size_t name##_size(name_t* map); \
  type* name##_next(name_t* map, size_t* i); \
  void name##_trace(void* map); \

#define DEFINE_RT_HASHMAP(name, name_t, type, hash, alloc, fr, free_elem) \
  typedef struct name_t name_t; \
  typedef size_t (*name##_hash_fn)(uintptr_t m); \
  typedef void (*name##_free_fn)(type* a); \
  typedef void (*name_trace_fn)(void* a); \
  \
  void name##_init(name_t* map, size_t size) \
  { \
    alloc_fn allocf = alloc; \
    ponyint_rt_hashmap_init((rt_hashmap_t*)map, size, allocf); \
  } \
  void name##_destroy(name_t* map) \
  { \
    name##_free_fn freef = free_elem; \
    ponyint_rt_hashmap_destroy((rt_hashmap_t*)map, fr, (free_fn)freef); \
  } \
  bool name##_needs_optimize(name_t* map) \
  { \
    return ponyint_rt_hashmap_needs_optimize((rt_hashmap_t*)map); \
  } \
  void name##_finish_optimize(name_t* map, size_t num_optimized) \
  { \
    ponyint_rt_hashmap_finish_optimize((rt_hashmap_t*)map, num_optimized); \
  } \
  size_t name##_optimize_item(name_t* map, type* entry, uintptr_t key, size_t old_index) \
  { \
    name##_hash_fn hashf = hash; \
    return ponyint_rt_hashmap_optimize_item((rt_hashmap_t*)map, \
      (rt_hash_fn)hashf, alloc, fr, old_index, (void*)entry, key); \
  } \
  type* name##_get(name_t* map, uintptr_t key, size_t* index) \
  { \
    name##_hash_fn hashf = hash; \
    return (type*)ponyint_rt_hashmap_get((rt_hashmap_t*)map, key, \
      (rt_hash_fn)hashf, index, alloc, fr); \
  } \
  type* name##_put(name_t* map, type* entry, uintptr_t key) \
  { \
    name##_hash_fn hashf = hash; \
    return (type*)ponyint_rt_hashmap_put((rt_hashmap_t*)map, (void*)entry, \
      key, (rt_hash_fn)hashf, alloc, fr); \
  } \
  type* name##_putindex(name_t* map, type* entry, uintptr_t key, size_t index) \
  { \
    name##_hash_fn hashf = hash; \
    return (type*)ponyint_rt_hashmap_putindex((rt_hashmap_t*)map, (void*)entry, \
      key, (rt_hash_fn)hashf, alloc, fr, index); \
  } \
  type* name##_remove(name_t* map, uintptr_t key) \
  { \
    name##_hash_fn hashf = hash; \
    return (type*)ponyint_rt_hashmap_remove((rt_hashmap_t*) map, key, \
      (rt_hash_fn)hashf, alloc, fr); \
  } \
  type* name##_removeindex(name_t* map, size_t index) \
  { \
    return (type*)ponyint_rt_hashmap_removeindex((rt_hashmap_t*) map, index); \
  } \
  size_t name##_size(name_t* map) \
  { \
    return ponyint_rt_hashmap_size((rt_hashmap_t*)map); \
  } \
  type* name##_next(name_t* map, size_t* i) \
  { \
    return (type*)ponyint_rt_hashmap_next((rt_hashmap_t*)map, i); \
  } \

#define RT_HASHMAP_INIT {0, 0, NULL}

PONY_EXTERN_C_END

#endif
