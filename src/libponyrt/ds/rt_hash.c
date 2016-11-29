#include "rt_hash.h"
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>

#define DELETED ((void*)1)

static bool valid(void* entry)
{
  return ((uintptr_t)entry) > ((uintptr_t)DELETED);
}

// horrible horrible pointer math function to get element at position
static rt_hashmap_entry_t* get_element_at_position(rt_hashmap_entry_t* buckets, size_t pos)
{
  // horrible horrible pointer math
  return (buckets + pos);
//  rt_hashmap_entry_t* elem = (rt_hashmap_entry_t*)(buckets + pos);
//  printf("bucket: %lu, elem %lu, pos: %lu\n", (uintptr_t)buckets, (uintptr_t)elem, pos);
//  return elem;
}

static void* search(rt_hashmap_t* map, size_t* pos, uintptr_t key, rt_hash_fn hash)
{
  size_t index_del = map->size;
  size_t mask = index_del - 1;

  size_t h = hash(key);
  size_t index = h & mask;
  rt_hashmap_entry_t* elem;

  for(size_t i = 0; i <= mask; i++)
  {
    elem = get_element_at_position(map->buckets, index);

    if(elem->ptr == NULL)
    {
      if(index_del <= mask)
        *pos = index_del;
      else
        *pos = index;

      return NULL;
    } else if(elem->ptr == DELETED) {
      /* some element was here, remember the first deleted slot */
      if(index_del > mask)
        index_del = index;
    } else if(key == elem->data) {
      *pos = index;
      return elem->ptr;
    } else {
//      printf("@@@This shouldn't happen! pos: %lu, ptr: %lu, data: %lu, key: %lu\n", index, (uintptr_t)elem->ptr, elem->data, key);
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
  rt_hashmap_entry_t* curr = NULL;

  map->count = 0;
  map->size = (s < 8) ? 8 : s << 3;
  map->buckets = (rt_hashmap_entry_t*)alloc(map->size * sizeof(rt_hashmap_entry_t));
  memset(map->buckets, 0, map->size * sizeof(rt_hashmap_entry_t));

  for(size_t i = 0; i < s; i++)
  {
    curr = get_element_at_position(b, i);

    if(valid(curr->ptr))
      ponyint_rt_hashmap_put(map, curr->ptr, curr->data, hash, alloc, fr);
  }

  if((fr != NULL) && (b != NULL))
    fr(s * sizeof(rt_hashmap_entry_t), b);

//  printf("resized.. old size: %lu, old count: %lu, new size: %lu, new count: %lu\n", s, c, map->size, map->count);
}

void ponyint_rt_hashmap_init(rt_hashmap_t* map, size_t size, alloc_fn alloc)
{
  if(size > 0)
  {
    // make sure we have room for this many elements without resizing
    size <<= 1;

    if(size < 8)
      size = 8;
    else
      size = ponyint_next_pow2(size);
  }

  map->count = 0;
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
    rt_hashmap_entry_t* elem = NULL;

    for(size_t i = 0; i < map->size; i++)
    {
      elem = get_element_at_position(map->buckets, i);

      if(elem != NULL && valid(elem->ptr))
        free_elem(elem->ptr);
    }
  }

  if((fr != NULL) && (map->size > 0))
    fr(map->size * sizeof(rt_hashmap_entry_t), map->buckets);

  map->count = 0;
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

  rt_hashmap_entry_t* pos_entry = get_element_at_position(map->buckets, pos);

  pos_entry->ptr = entry;
  pos_entry->data = key;

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

  rt_hashmap_entry_t* pos_entry = get_element_at_position(map->buckets, pos);
  void* elem = pos_entry->ptr;

  pos_entry->ptr = entry;
  pos_entry->data = key;

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
    rt_hashmap_entry_t* pos_entry = get_element_at_position(map->buckets, pos);
    pos_entry->ptr = DELETED;
    map->count--;
  }

  return elem;
}

void* ponyint_rt_hashmap_removeindex(rt_hashmap_t* map, size_t index)
{
  if(map->size <= index)
    return NULL;

  rt_hashmap_entry_t* pos_entry = get_element_at_position(map->buckets, index);
  void* elem = pos_entry->ptr;

  if(!valid(elem))
    return NULL;

  pos_entry->ptr = DELETED;
  map->count--;
  return elem;
}

void* ponyint_rt_hashmap_next(rt_hashmap_t* map, size_t* i)
{
  if(map->count == 0)
    return NULL;

  void* elem = NULL;
  rt_hashmap_entry_t* pos_entry = NULL;
  size_t index = *i + 1;

  while(index < map->size)
  {
    pos_entry = get_element_at_position(map->buckets, index);
    elem = pos_entry->ptr;

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
