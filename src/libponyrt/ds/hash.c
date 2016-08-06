#include "hash.h"
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>

#define DELETED ((void*)1)

static bool valid(void* entry)
{
  return ((uintptr_t)entry) > ((uintptr_t)DELETED);
}

static void* search(hashmap_t* map, size_t* pos, void* key, hash_fn hash,
  cmp_fn cmp, size_t from)
{
  size_t index_del = map->size;
  size_t mask = index_del - 1;

  size_t h = hash(key);
  size_t index = h & mask;
  void* elem;

  size_t collisions = 0;
  size_t delete_collisions = 0;

  for(size_t i = 0; i <= mask; i++)
  {
    elem = map->buckets[index];

    if(elem == NULL)
    {
      if(index_del <= mask)
        *pos = index_del;
      else
        *pos = index;

      //if ((delete_collisions + collisions)> 150)
      //  printf("%zu empty %p %p delete collisions: %zu other collisions: %zu size: %zu\n", from, &map, &key, delete_collisions, collisions, map->size);
 // if (i + 1 > 256) printf("%zu HASH INDEXES SEARCHED %zu size:%zu deletes: %zu\n", from, i + 1, map->size, delete_collisions);

      return NULL;
    } else if(elem == DELETED) {
      /* some element was here, remember the first deleted slot */
      delete_collisions++;
      if(index_del > mask)
        index_del = index;
    } else if(cmp(key, elem)) {
      *pos = index;

  //if ((delete_collisions + collisions)> 150)
  //printf("%zu delete %p %p delete collisions: %zu other collisions: %zu size: %zu\n", from, &map, &key, delete_collisions, collisions, map->size);
  //if (i + 1 > 256) printf("%zu HASH INDEXES SEARCHED %zu size:%zu delets: %zu\n", from, i + 1, map->size, delete_collisions);

      return elem;
    } else {
      collisions++;
    }

    //index = (h + i) & mask;
    index = (h + ((i + (i * i)) >> 1)) & mask;
  }

  //if ((delete_collisions + collisions)> 150)
  //printf("%zu end %p %p delete collisions: %zu other collisions: %zu size: %zu count: %zu\n", from, &map, &key, delete_collisions, collisions, map->size, map->count);
  //if (mask + 1 > 256) printf("%zu HASH INDEXES SEARCHED %zu size:%zu deletes: %zu\n", from, mask + 1, map->size, delete_collisions);

  *pos = index_del + (from - from);
  return NULL;
}

static void resize(hashmap_t* map, hash_fn hash, cmp_fn cmp, alloc_fn alloc,
  free_size_fn fr)
{
  size_t s = map->size;
  void** b = map->buckets;
  void* curr = NULL;

  if (s > 32768) {
    printf("before: %p : %zu %zu\n", &map, map->size, map->count);
    printf("needed to be %zu\n", map->count << 1);
  }
  map->count = 0;
  map->size = s << 1; // (s < 8) ? 8 : s << 3;
  map->buckets = (void**)alloc(map->size * sizeof(void*));
  memset(map->buckets, 0, map->size * sizeof(void*));

  if (s > 32768) {
    printf("after: %p : %zu\n", &map, map->size);
  }

  for(size_t i = 0; i < s; i++)
  {
    curr = b[i];

    if(valid(curr))
      ponyint_hashmap_put(map, curr, hash, cmp, alloc, fr);
  }

  if((fr != NULL) && (b != NULL))
    fr(s * sizeof(void*), b);
}

void ponyint_hashmap_init(hashmap_t* map, size_t size, alloc_fn alloc)
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
    map->buckets = (void**)alloc(size * sizeof(void*));
    memset(map->buckets, 0, size * sizeof(void*));
  } else {
    map->buckets = NULL;
  }
}

void ponyint_hashmap_destroy(hashmap_t* map, free_size_fn fr, free_fn free_elem)
{
  if(free_elem != NULL)
  {
    void* curr = NULL;

    for(size_t i = 0; i < map->size; i++)
    {
      curr = map->buckets[i];

      if(valid(curr))
        free_elem(curr);
    }
  }

  if((fr != NULL) && (map->size > 0))
    fr(map->size * sizeof(void*), map->buckets);

  map->count = 0;
  map->size = 0;
  map->buckets = NULL;
}

void* ponyint_hashmap_get(hashmap_t* map, void* key, hash_fn hash, cmp_fn cmp)
{
  if(map->count == 0)
    return NULL;

  size_t pos;
  return search(map, &pos, key, hash, cmp, 1);
}

void* ponyint_hashmap_put(hashmap_t* map, void* entry, hash_fn hash, cmp_fn cmp,
  alloc_fn alloc, free_size_fn fr)
{
  if(map->size == 0)
    ponyint_hashmap_init(map, 128, alloc);

  size_t pos;
  void* elem = search(map, &pos, entry, hash, cmp, 2);

  map->buckets[pos] = entry;

  if(elem == NULL)
  {
    map->count++;

    if((map->count << 1) > map->size) {
      resize(map, hash, cmp, alloc, fr);
    }
  }

  return elem;
}

void* ponyint_hashmap_remove(hashmap_t* map, void* entry, hash_fn hash,
  cmp_fn cmp)
{
  if(map->count == 0)
    return NULL;

  size_t pos;
  void* elem = search(map, &pos, entry, hash, cmp, 3);

  if(elem != NULL)
  {
    map->buckets[pos] = DELETED;
    map->count--;
  }

  return elem;
}

void* ponyint_hashmap_removeindex(hashmap_t* map, size_t index)
{
  if(map->size <= index)
    return NULL;

  void* elem = map->buckets[index];

  if(!valid(elem))
    return NULL;

  map->buckets[index] = DELETED;
  map->count--;
  return elem;
}

void* ponyint_hashmap_next(hashmap_t* map, size_t* i)
{
  if(map->count == 0)
    return NULL;

  void* elem = NULL;
  size_t index = *i + 1;

  while(index < map->size)
  {
    elem = map->buckets[index];

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

size_t ponyint_hashmap_size(hashmap_t* map)
{
  return map->count;
}
