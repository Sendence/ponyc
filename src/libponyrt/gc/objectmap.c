#include "objectmap.h"
#include "gc.h"
#include "../ds/rt_hash.h"
#include "../ds/fun.h"
#include "../mem/pool.h"
#include "../mem/pagemap.h"
#include <assert.h>

static size_t object_hash(uintptr_t address)
{
  return ponyint_hash_ptr((void *)address);
}

static object_t* object_alloc(void* address, uint32_t mark)
{
  object_t* obj = (object_t*)POOL_ALLOC(object_t);
  obj->address = address;
  obj->final = NULL;
  obj->rc = 0;
  obj->immutable = false;

  // a new object is unmarked
  obj->mark = mark - 1;
  return obj;
}

static void object_free(object_t* obj)
{
  POOL_FREE(object_t, obj);
}

DEFINE_RT_HASHMAP(ponyint_objectmap, objectmap_t, object_t, object_hash,
  ponyint_pool_alloc_size, ponyint_pool_free_size, object_free);

object_t* ponyint_objectmap_getobject(objectmap_t* map, void* address, size_t* index)
{
  return ponyint_objectmap_get(map, (uintptr_t)address, index);
}

object_t* ponyint_objectmap_getorput(objectmap_t* map, void* address,
  uint32_t mark)
{
  size_t index = RT_HASHMAP_UNKNOWN;
  object_t* obj = ponyint_objectmap_getobject(map, address, &index);

  if(obj != NULL)
    return obj;

  obj = object_alloc(address, mark);
  ponyint_objectmap_putindex(map, obj, (uintptr_t)address, index);
  return obj;
}

object_t* ponyint_objectmap_register_final(objectmap_t* map, void* address,
  pony_final_fn final, uint32_t mark)
{
  object_t* obj = ponyint_objectmap_getorput(map, address, mark);
  obj->final = final;
  return obj;
}

void ponyint_objectmap_final(objectmap_t* map)
{
  size_t i = RT_HASHMAP_BEGIN;
  object_t* obj;

  while((obj = ponyint_objectmap_next(map, &i)) != NULL)
  {
    if(obj->final != NULL)
      obj->final(obj->address);
  }
}

size_t ponyint_objectmap_sweep(objectmap_t* map)
{
  size_t count = 0;
  size_t i = RT_HASHMAP_BEGIN;
  object_t* obj;

  while((obj = ponyint_objectmap_next(map, &i)) != NULL)
  {
    void* p = obj->address;

    if(obj->rc > 0)
    {
      chunk_t* chunk = (chunk_t*)ponyint_pagemap_get(p);
      ponyint_heap_mark_shallow(chunk, p);
    } else {
      if(obj->final != NULL)
      {
        // If we are not free in the heap, don't run the finaliser and don't
        // remove this entry from the object map.
        chunk_t* chunk = (chunk_t*)ponyint_pagemap_get(p);

        if(ponyint_heap_ismarked(chunk, p))
          continue;

        obj->final(p);
        count++;
      }

      ponyint_objectmap_removeindex(map, i);
      object_free(obj);
    }
  }

  // optimize map if too many deleted entries
  ponyint_objectmap_optimize(map);

  return count;
}
