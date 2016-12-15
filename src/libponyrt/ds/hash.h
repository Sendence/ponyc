#ifndef ds_hash_h
#define ds_hash_h

#include "fun.h"
#include "../pony.h"
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <platform.h>

PONY_EXTERN_C_BEGIN

#define HASHMAP_BEGIN ((size_t)-1)
#define HASHMAP_UNKNOWN ((size_t)-1)

#ifdef PLATFORM_IS_ILP32
  typedef uint32_t bitmap_t;
  #define HASHMAP_BITMAP_TYPE_SIZE 32
#else
  typedef uint64_t bitmap_t;
  #define HASHMAP_BITMAP_TYPE_SIZE 64
#endif

/** Definition of a quadratic probing hash map.
 *
 *  Do not access the fields of this type.
 */
typedef struct hashmap_t
{
  size_t count;   /* number of elements in the map */
  size_t size;    /* size of the buckets array */
  size_t deleted_count;   /* number of deleted elements in the map */
  ssize_t optimize_deleted_shift;   /* shift amount for when to run optimize */
  bitmap_t* item_bitmap;   /* Item bitarray to keep track items for optimized scanning */
  void** buckets;
} hashmap_t;

/** Initializes a new hash map.
 *
 *  This is a quadratic probing hash map.
 */
void ponyint_hashmap_init(hashmap_t* map, size_t size, alloc_fn alloc);

/** Destroys a hash map.
 *
 */
void ponyint_hashmap_destroy(hashmap_t* map, free_size_fn fr,
  free_fn free_elem);

/** Checks whether the hashmap needs optimization or not.
 *
 */
bool ponyint_hashmap_needs_optimize(hashmap_t* map);

/** Handles bookkeeping for end of optimize.
 *
 */
void ponyint_hashmap_finish_optimize(hashmap_t* map, size_t num_optimized);

/** Optimize single item at specified index with given pointer/key by moving to an
 *  earlier bucket if it has a deleted entry.
 *
 *  NOTE: This function assumes the caller provided information is correct and does not
 *  validate that the given entry/key are actually present at the index provided.
 */
size_t ponyint_hashmap_optimize_item(hashmap_t* map, hash_fn hash, alloc_fn alloc,
  free_size_fn fr, cmp_fn cmp, size_t old_index, void* entry);

/** Retrieve an element from a hash map.
 *
 *  Returns a pointer to the element, or NULL.
 */
void* ponyint_hashmap_get(hashmap_t* map, void* key, hash_fn hash, cmp_fn cmp, size_t* index,
  alloc_fn alloc, free_size_fn fr);

/** Put a new element in a hash map.
 *
 *  If the element (according to cmp_fn) is already in the hash map, the old
 *  element is overwritten and returned to the caller.
 */
void* ponyint_hashmap_put(hashmap_t* map, void* entry, hash_fn hash, cmp_fn cmp,
  alloc_fn alloc, free_size_fn fr);


/** Put a new element in a hash map at a specific index.
 *
 *  If an element is already in the hash map at that position, the old
 *  element is overwritten and returned to the caller.
 */
void* ponyint_hashmap_putindex(hashmap_t* map, void* entry, hash_fn hash, cmp_fn cmp,
  alloc_fn alloc, free_size_fn fr, size_t index);

/** Removes a given entry from a hash map.
 *
 *  Returns the element removed (if any).
 */
void* ponyint_hashmap_remove(hashmap_t* map, void* entry, hash_fn hash,
  cmp_fn cmp, alloc_fn alloc, free_size_fn fr);

/** Removes a given entry from a hash map by index.
 *
 *  Returns the element removed (if any).
 */
void* ponyint_hashmap_removeindex(hashmap_t* map, size_t index);

/** Get the number of elements in the map.
 */
size_t ponyint_hashmap_size(hashmap_t* map);

/** Hashmap iterator.
 *
 *  Set i to HASHMAP_BEGIN, then call until this returns NULL.
 */
void* ponyint_hashmap_next(hashmap_t* map, size_t* i);

#define DECLARE_HASHMAP(name, name_t, type) \
  typedef struct name_t { hashmap_t contents; } name_t; \
  void name##_init(name_t* map, size_t size); \
  void name##_destroy(name_t* map); \
  bool name##_needs_optimize(name_t* map); \
  void name##_finish_optimize(name_t* map, size_t num_optimized); \
  size_t name##_optimize_item(name_t* map, type* entry, size_t old_index); \
  type* name##_get(name_t* map, type* key, size_t* index); \
  type* name##_put(name_t* map, type* entry); \
  type* name##_putindex(name_t* map, type* entry, size_t index); \
  type* name##_remove(name_t* map, type* entry); \
  type* name##_removeindex(name_t* map, size_t index); \
  size_t name##_size(name_t* map); \
  type* name##_next(name_t* map, size_t* i); \
  void name##_trace(void* map); \

#define DEFINE_HASHMAP(name, name_t, type, hash, cmp, alloc, fr, free_elem) \
  typedef struct name_t name_t; \
  typedef size_t (*name##_hash_fn)(type* m); \
  typedef bool (*name##_cmp_fn)(type* a, type* b); \
  typedef void (*name##_free_fn)(type* a); \
  typedef void (*name_trace_fn)(void* a); \
  \
  void name##_init(name_t* map, size_t size) \
  { \
    alloc_fn allocf = alloc; \
    ponyint_hashmap_init((hashmap_t*)map, size, allocf); \
  } \
  void name##_destroy(name_t* map) \
  { \
    name##_free_fn freef = free_elem; \
    ponyint_hashmap_destroy((hashmap_t*)map, fr, (free_fn)freef); \
  } \
  bool name##_needs_optimize(name_t* map) \
  { \
    return ponyint_hashmap_needs_optimize((hashmap_t*)map); \
  } \
  void name##_finish_optimize(name_t* map, size_t num_optimized) \
  { \
    ponyint_hashmap_finish_optimize((hashmap_t*)map, num_optimized); \
  } \
  size_t name##_optimize_item(name_t* map, type* entry, size_t old_index) \
  { \
    name##_hash_fn hashf = hash; \
    name##_cmp_fn cmpf = cmp; \
    return ponyint_hashmap_optimize_item((hashmap_t*)map, \
      (hash_fn)hashf, alloc, fr, (cmp_fn)cmpf, old_index, (void*)entry); \
  } \
  type* name##_get(name_t* map, type* key, size_t* index) \
  { \
    name##_hash_fn hashf = hash; \
    name##_cmp_fn cmpf = cmp; \
    return (type*)ponyint_hashmap_get((hashmap_t*)map, (void*)key, \
      (hash_fn)hashf, (cmp_fn)cmpf, index, alloc, fr); \
  } \
  type* name##_put(name_t* map, type* entry) \
  { \
    name##_hash_fn hashf = hash; \
    name##_cmp_fn cmpf = cmp; \
    return (type*)ponyint_hashmap_put((hashmap_t*)map, (void*)entry, \
      (hash_fn)hashf, (cmp_fn)cmpf, alloc, fr); \
  } \
  type* name##_putindex(name_t* map, type* entry, size_t index) \
  { \
    name##_hash_fn hashf = hash; \
    name##_cmp_fn cmpf = cmp; \
    return (type*)ponyint_hashmap_putindex((hashmap_t*)map, (void*)entry, \
      (hash_fn)hashf, (cmp_fn)cmpf, alloc, fr, index); \
  } \
  type* name##_remove(name_t* map, type* entry) \
  { \
    name##_hash_fn hashf = hash; \
    name##_cmp_fn cmpf = cmp; \
    return (type*)ponyint_hashmap_remove((hashmap_t*) map, (void*)entry, \
      (hash_fn)hashf, (cmp_fn)cmpf, alloc, fr); \
  } \
  type* name##_removeindex(name_t* map, size_t index) \
  { \
    return (type*)ponyint_hashmap_removeindex((hashmap_t*) map, index); \
  } \
  size_t name##_size(name_t* map) \
  { \
    return ponyint_hashmap_size((hashmap_t*)map); \
  } \
  type* name##_next(name_t* map, size_t* i) \
  { \
    return (type*)ponyint_hashmap_next((hashmap_t*)map, i); \
  } \

#define HASHMAP_INIT {0, 0, NULL}

PONY_EXTERN_C_END

#endif
