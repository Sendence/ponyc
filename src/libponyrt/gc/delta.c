#include "delta.h"
#include "../mem/pool.h"

typedef struct delta_t
{
  pony_actor_t* actor;
  size_t rc;
} delta_t;

static size_t delta_hash(uintptr_t actor)
{
  return ponyint_hash_ptr((void *)actor);
}

static void delta_free(delta_t* delta)
{
  POOL_FREE(delta_t, delta);
}

pony_actor_t* ponyint_delta_actor(delta_t* delta)
{
  return delta->actor;
}

size_t ponyint_delta_rc(delta_t* delta)
{
  return delta->rc;
}

DEFINE_RT_HASHMAP(ponyint_deltamap, deltamap_t, delta_t, delta_hash,
  ponyint_pool_alloc_size, ponyint_pool_free_size, delta_free);

deltamap_t* ponyint_deltamap_update(deltamap_t* map, pony_actor_t* actor,
  size_t rc)
{
  size_t index = RT_HASHMAP_UNKNOWN;

  if(map == NULL)
  {
    // allocate a new map with space for at least one element
    map = (deltamap_t*)POOL_ALLOC(deltamap_t);
    ponyint_deltamap_init(map, 1);
  } else {
    delta_t* delta = ponyint_deltamap_get(map, (uintptr_t)actor, &index);

    if(delta != NULL)
    {
      delta->rc = rc;
      return map;
    }
  }

  delta_t* delta = (delta_t*)POOL_ALLOC(delta_t);
  delta->actor = actor;
  delta->rc = rc;

  if(index == RT_HASHMAP_UNKNOWN)
  {
    // new map
    ponyint_deltamap_put(map, delta, (uintptr_t)actor);
  } else {
    // didn't find it in the map but index is where we can put the
    // new one without another search
    ponyint_deltamap_putindex(map, delta, (uintptr_t)actor, index);
  }

  return map;
}

void ponyint_deltamap_free(deltamap_t* map)
{
  ponyint_deltamap_destroy(map);
  POOL_FREE(deltamap_t, map);
}
