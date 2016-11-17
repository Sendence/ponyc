#ifndef ds_hashalt_h
#define ds_hashalt_h

#include "fun.h"
#include "../pony.h"
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <platform.h>

PONY_EXTERN_C_BEGIN

#define HASHMAPALT_BEGIN ((size_t)-1)

/** Definition of a quadratic probing hash map.
 *
 *  Do not access the fields of this type.
 */
typedef struct hashmapalt_t
{
  size_t count;   /* number of elements in the map */
  size_t size;    /* size of the buckets array */
  void** buckets;
} hashmapalt_t;

/** Initializes a new hash map.
 *
 *  This is a quadratic probing hash map.
 */
void ponyint_hashmapalt_init(hashmapalt_t* map, size_t size, alloc_fn alloc);

/** Destroys a hash map.
 *
 */
void ponyint_hashmapalt_destroy(hashmapalt_t* map, free_size_fn fr,
  free_fn free_elem);

/** Retrieve an element from a hash map.
 *
 *  Returns a pointer to the element, or NULL.
 */
void* ponyint_hashmapalt_get(hashmapalt_t* map, void* key, hash_fn hash, cmp_fn cmp);

/** Put a new element in a hash map.
 *
 *  If the element (according to cmp_fn) is already in the hash map, the old
 *  element is overwritten and returned to the caller.
 */
void* ponyint_hashmapalt_put(hashmapalt_t* map, void* entry, hash_fn hash, cmp_fn cmp,
  alloc_fn alloc, free_size_fn fr);

/** Get or put a new element in a hash map.
 *
 *  If the element (according to cmp_fn) is already in the hash map, the old
 *  element is returned to the caller else the new element is inserted and returned.
 */
void* ponyint_hashmapalt_get_or_put(hashmapalt_t* map, void* key, void* p, uint32_t s, hash_fn hash, cmp_fn cmp,
  alloc_fn alloc, free_size_fn fr, oalloc_fn oalloc);

/** Removes a given entry from a hash map.
 *
 *  Returns the element removed (if any).
 */
void* ponyint_hashmapalt_remove(hashmapalt_t* map, void* entry, hash_fn hash,
  cmp_fn cmp);

/** Removes a given entry from a hash map by index.
 *
 *  Returns the element removed (if any).
 */
void* ponyint_hashmapalt_removeindex(hashmapalt_t* map, size_t index);

/** Get the number of elements in the map.
 */
size_t ponyint_hashmapalt_size(hashmapalt_t* map);

/** Hashmap iterator.
 *
 *  Set i to HASHMAPALT_BEGIN, then call until this returns NULL.
 */
void* ponyint_hashmapalt_next(hashmapalt_t* map, size_t* i);

#define DECLARE_HASHMAPALT(name, name_t, type) \
  typedef struct name_t { hashmapalt_t contents; } name_t; \
  void name##_init(name_t* map, size_t size); \
  void name##_destroy(name_t* map); \
  type* name##_get(name_t* map, type* key); \
  type* name##_put(name_t* map, type* entry); \
  type* name##_get_or_put(name_t* map, void* key, void* p, uint32_t s); \
  type* name##_remove(name_t* map, type* entry); \
  type* name##_removeindex(name_t* map, size_t index); \
  size_t name##_size(name_t* map); \
  type* name##_next(name_t* map, size_t* i); \
  void name##_trace(void* map); \

#define DEFINE_HASHMAPALT(name, name_t, type, hash, cmp, alloc, fr, free_elem, oalloc) \
  typedef struct name_t name_t; \
  typedef size_t (*name##_hash_fn)(type* m); \
  typedef void (*name##_oalloc_fn)(void* p, uint32_t s); \
  typedef bool (*name##_cmp_fn)(type* a, type* b); \
  typedef void (*name##_free_fn)(type* a); \
  typedef void (*name_trace_fn)(void* a); \
  \
  void name##_init(name_t* map, size_t size) \
  { \
    alloc_fn allocf = alloc; \
    ponyint_hashmapalt_init((hashmapalt_t*)map, size, allocf); \
  } \
  void name##_destroy(name_t* map) \
  { \
    name##_free_fn freef = free_elem; \
    ponyint_hashmapalt_destroy((hashmapalt_t*)map, fr, (free_fn)freef); \
  } \
  type* name##_get(name_t* map, type* key) \
  { \
    name##_hash_fn hashf = hash; \
    name##_cmp_fn cmpf = cmp; \
    return (type*)ponyint_hashmapalt_get((hashmapalt_t*)map, (void*)key, \
      (hash_fn)hashf, (cmp_fn)cmpf); \
  } \
  type* name##_put(name_t* map, type* entry) \
  { \
    name##_hash_fn hashf = hash; \
    name##_cmp_fn cmpf = cmp; \
    return (type*)ponyint_hashmapalt_put((hashmapalt_t*)map, (void*)entry, \
      (hash_fn)hashf, (cmp_fn)cmpf, alloc, fr); \
  } \
  type* name##_get_or_put(name_t* map, void* key, void* p, uint32_t s) \
  { \
    name##_hash_fn hashf = hash; \
    name##_cmp_fn cmpf = cmp; \
    return (type*)ponyint_hashmapalt_get_or_put((hashmapalt_t*)map, key, p, s, \
      (hash_fn)hashf, (cmp_fn)cmpf, alloc, fr, oalloc); \
  } \
  type* name##_remove(name_t* map, type* entry) \
  { \
    name##_hash_fn hashf = hash; \
    name##_cmp_fn cmpf = cmp; \
    return (type*)ponyint_hashmapalt_remove((hashmapalt_t*) map, (void*)entry, \
      (hash_fn)hashf, (cmp_fn)cmpf); \
  } \
  type* name##_removeindex(name_t* map, size_t index) \
  { \
    return (type*)ponyint_hashmapalt_removeindex((hashmapalt_t*) map, index); \
  } \
  size_t name##_size(name_t* map) \
  { \
    return ponyint_hashmapalt_size((hashmapalt_t*)map); \
  } \
  type* name##_next(name_t* map, size_t* i) \
  { \
    return (type*)ponyint_hashmapalt_next((hashmapalt_t*)map, i); \
  } \

#define HASHMAPALT_INIT {0, 0, NULL}

PONY_EXTERN_C_END

#endif
