#include "rt_hash.h"
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>

#define DELETED ((void*)1)

// Minimum RT_HASHMAP size allowed
#define MIN_RT_HASHMAP_SIZE 8

// Mazimum percent of deleted entries compared to valid entries allowed before optimization
#define MAX_RT_HASHMAP_DELETED_PCT 0.25

// Minimum RT_HASHMAP size for hashmap before optimization is considered
#define MIN_RT_HASHMAP_OPTIMIZE_SIZE 2048

static bool valid(void* entry)
{
  return ((uintptr_t)entry) > ((uintptr_t)DELETED);
}

static void* search(rt_hashmap_t* map, size_t* pos, uintptr_t key, rt_hash_fn hash)
{
  size_t index_del = map->size;
  size_t mask = index_del - 1;

  size_t h = hash(key);
  size_t index = h & mask;

  for(size_t i = 0; i <= mask; i++)
  {
    if(map->buckets[index].ptr == NULL)
    {
      if(index_del <= mask)
        *pos = index_del;
      else
        *pos = index;

      return NULL;
    } else if(map->buckets[index].ptr == DELETED) {
      /* some element was here, remember the first deleted slot */
      if(index_del > mask)
        index_del = index;
    } else if(key == map->buckets[index].data) {
      *pos = index;
      return map->buckets[index].ptr;
    }

    index = (h + ((i + (i * i)) >> 1)) & mask;
  }

  *pos = index_del;
  return NULL;
}

static void resize(rt_hashmap_t* map, rt_hash_fn hash, alloc_fn alloc,
  free_size_fn fr)
{
  size_t s = map->size;
//  size_t c = map->count;
  rt_hashmap_entry_t* b = map->buckets;

  map->count = 0;
  map->deleted_count = 0;
  map->size = (s < MIN_RT_HASHMAP_SIZE) ? MIN_RT_HASHMAP_SIZE : s << 3;
  map->buckets = (rt_hashmap_entry_t*)alloc(map->size * sizeof(rt_hashmap_entry_t));
  memset(map->buckets, 0, map->size * sizeof(rt_hashmap_entry_t));

  for(size_t i = 0; i < s; i++)
  {
    if(valid(b[i].ptr))
      ponyint_rt_hashmap_put(map, b[i].ptr, b[i].data, hash, alloc, fr);
  }

  if((fr != NULL) && (b != NULL))
    fr(s * sizeof(rt_hashmap_entry_t), b);

//  printf("resized.. old size: %lu, old count: %lu, new size: %lu, new count: %lu\n", s, c, map->size, map->count);
}

void ponyint_rt_hashmap_optimize(rt_hashmap_t* map, rt_hash_fn hash, alloc_fn alloc,
  free_size_fn fr)
{
  // Don't optimize if the hashmap is too small or if the # deleted items is not large enough
  // TODO: figure out right heuristic for when to optimize
  if((map->size < MIN_RT_HASHMAP_OPTIMIZE_SIZE) ||
    (((float)map->deleted_count)/((float)map->count) < MAX_RT_HASHMAP_DELETED_PCT))
    return;

  rt_hashmap_entry_t* b = map->buckets;

  map->count = 0;
  map->deleted_count = 0;
  map->buckets = (rt_hashmap_entry_t*)alloc(map->size * sizeof(rt_hashmap_entry_t));
  memset(map->buckets, 0, map->size * sizeof(rt_hashmap_entry_t));

  for(size_t i = 0; i < map->size; i++)
  {
    if(valid(b[i].ptr))
      ponyint_rt_hashmap_put(map, b[i].ptr, b[i].data, hash, alloc, fr);
  }

  if((fr != NULL) && (b != NULL))
    fr(map->size * sizeof(rt_hashmap_entry_t), b);
}

void ponyint_rt_hashmap_init(rt_hashmap_t* map, size_t size, alloc_fn alloc)
{
  if(size > 0)
  {
    // make sure we have room for this many elements without resizing
    size <<= 1;

    if(size < MIN_RT_HASHMAP_SIZE)
      size = MIN_RT_HASHMAP_SIZE;
    else
      size = ponyint_next_pow2(size);
  }

  map->count = 0;
  map->deleted_count = 0;
  map->size = size;

  if(size > 0)
  {
    map->buckets = (rt_hashmap_entry_t*)alloc(size * sizeof(rt_hashmap_entry_t));
    memset(map->buckets, 0, size * sizeof(rt_hashmap_entry_t));
  } else {
    map->buckets = NULL;
  }
}

void ponyint_rt_hashmap_destroy(rt_hashmap_t* map, free_size_fn fr, free_fn free_elem)
{
  if(free_elem != NULL)
  {
    for(size_t i = 0; i < map->size; i++)
    {
      if(valid(map->buckets[i].ptr))
        free_elem(map->buckets[i].ptr);
    }
  }

  if((fr != NULL) && (map->size > 0))
    fr(map->size * sizeof(rt_hashmap_entry_t), map->buckets);

  map->count = 0;
  map->deleted_count = 0;
  map->size = 0;
  map->buckets = NULL;
}

void* ponyint_rt_hashmap_get(rt_hashmap_t* map, uintptr_t key, rt_hash_fn hash, size_t* pos)
{
  if(map->count == 0)
    return NULL;

  return search(map, pos, key, hash);
}

void* ponyint_rt_hashmap_put(rt_hashmap_t* map, void* entry, uintptr_t key, rt_hash_fn hash,
  alloc_fn alloc, free_size_fn fr)
{
  if(map->size == 0)
    ponyint_rt_hashmap_init(map, 4, alloc);

  size_t pos;
  void* elem = search(map, &pos, key, hash);

  map->buckets[pos].ptr = entry;
  map->buckets[pos].data = key;

  if(elem == NULL)
  {
    map->count++;
//    printf("successfully put new element bucket: %lu, elem %lu, pos: %lu\n", (uintptr_t)map->buckets, (uintptr_t)pos_entry, pos);
//    printf("requested ptr: %lu, data: %lu\n", (uintptr_t)(entry), key);
//    printf("actual ptr: %lu, data: %lu\n", (uintptr_t)(pos_entry->ptr), pos_entry->data);

    if((map->count << 1) > map->size)
      resize(map, hash, alloc, fr);
  }

  return elem;
}

void* ponyint_rt_hashmap_putindex(rt_hashmap_t* map, void* entry, uintptr_t key, rt_hash_fn hash,
  alloc_fn alloc, free_size_fn fr, size_t pos)
{
  if(pos == RT_HASHMAP_UNKNOWN)
    return ponyint_rt_hashmap_put(map, entry, key, hash, alloc, fr);

  if(map->size == 0)
    ponyint_rt_hashmap_init(map, 4, alloc);

  assert(pos <= map->size);

  void* elem = map->buckets[pos].ptr;

  map->buckets[pos].ptr = entry;
  map->buckets[pos].data = key;

  if(elem == DELETED || elem == 0)
  {
    map->count++;

    if((map->count << 1) > map->size)
      resize(map, hash, alloc, fr);

    return entry;
  }

  return elem;
}

void* ponyint_rt_hashmap_remove(rt_hashmap_t* map, uintptr_t key, rt_hash_fn hash)
{
  if(map->count == 0)
    return NULL;

  size_t pos;
  void* elem = search(map, &pos, key, hash);

  if(elem != NULL)
  {
    map->buckets[pos].ptr = DELETED;
    map->deleted_count++;
    map->count--;
  }

  return elem;
}

void* ponyint_rt_hashmap_removeindex(rt_hashmap_t* map, size_t index)
{
  if(map->size <= index)
    return NULL;

  void* elem = map->buckets[index].ptr;

  if(!valid(elem))
    return NULL;

  map->buckets[index].ptr = DELETED;
  map->deleted_count++;
  map->count--;
  return elem;
}

void* ponyint_rt_hashmap_next(rt_hashmap_t* map, size_t* i)
{
  if(map->count == 0)
    return NULL;

  void* elem = NULL;
  size_t index = *i + 1;

  while(index < map->size)
  {
    elem = map->buckets[index].ptr;

    if(valid(elem))
    {
      *i = index;
      return elem;
    }

    index++;
  }

  *i = index;
  return NULL;
}

size_t ponyint_rt_hashmap_size(rt_hashmap_t* map)
{
  return map->count;
}
