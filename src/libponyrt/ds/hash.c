#include "hash.h"
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#define DELETED ((void*)1)

// Minimum HASHMAP size allowed
#define MIN_HASHMAP_SIZE 8

// Maximum percent of deleted entries compared to valid entries allowed before initial optimization
// The shift value is the multiplier before a comparison is done against the count
// Positive == left shift; negative == right shift
// A shift of 4 effectively equals a maximum percentage of 6.25%
// A shift of 3 effectively equals a maximum percentage of 12.5%
// A shift of 2 effectively equals a maximum percentage of 25%
// A shift of 1 effectively equals a maximum percentage of 50%
// A shift of 0 effectively equals a maximum percentage of 100%
// A shift of -1 effectively equals a maximum percentage of 200%
// A shift of -2 effectively equals a maximum percentage of 400%
// A shift of -3 effectively equals a maximum percentage of 800%
// A shift of -4 effectively equals a maximum percentage of 1600%
// NOTE: A shift is used to avoid floating point math as a performance optimization
#define MAX_HASHMAP_DELETED_SHIFT_INITIAL 2

// Minimum percent of entries optimized compared to valid entries by an optimize before
// or else we back off on how often we optimize by modulating the MAX_HASHMAP_DELETED_SHIFT_INITIAL
// shift
// The shift value is the multiplier before a comparison is done against the count
// A shift of 6 effectively equals a minimum percentage of 1.5625%
// A shift of 5 effectively equals a minimum percentage of 3.125%
// A shift of 4 effectively equals a minimum percentage of 6.25%
// A shift of 3 effectively equals a minimum percentage of 12.5%
// A shift of 2 effectively equals a minimum percentage of 25%
// A shift of 1 effectively equals a minimum percentage of 50%
// A shift of 0 effectively equals a minimum percentage of 100%
// NOTE: A shift is used to avoid floating point math as a performance optimization
#define MIN_HASHMAP_OPTIMIZATION_SHIFT 4

// Maximum percent of entries optimized compared to valid entries by an optimize
// so we increase how often we optimize by modulating the MAX_HASHMAP_DELETED_SHIFT_INITIAL
// shift
// The shift value is the multiplier before a comparison is done against the count
// A shift of 6 effectively equals a minimum percentage of 1.5625%
// A shift of 5 effectively equals a minimum percentage of 3.125%
// A shift of 4 effectively equals a minimum percentage of 6.25%
// A shift of 3 effectively equals a minimum percentage of 12.5%
// A shift of 2 effectively equals a minimum percentage of 25%
// A shift of 1 effectively equals a minimum percentage of 50%
// A shift of 0 effectively equals a minimum percentage of 100%
// NOTE: A shift is used to avoid floating point math as a performance optimization
#define MAX_HASHMAP_OPTIMIZATION_SHIFT 3

// Minimum HASHMAP size for hashmap before optimization is considered
#define MIN_HASHMAP_OPTIMIZE_SIZE 2048

static bool valid(void* entry)
{
  return ((uintptr_t)entry) > ((uintptr_t)DELETED);
}

static void* search(hashmap_t* map, size_t* pos, void* key, hash_fn hash,
  cmp_fn cmp)
{
  size_t index_del = map->size;
  size_t mask = index_del - 1;

  size_t h = hash(key);
  size_t index = h & mask;
  void* elem;

  for(size_t i = 1; i <= mask; i++)
  {
    elem = map->buckets[index];

    if(elem == NULL)
    {
      if(index_del <= mask)
        *pos = index_del;
      else
        *pos = index;

      return NULL;
    } else if(elem == DELETED) {
      /* some element was here, remember the first deleted slot */
      if(index_del > mask)
        index_del = index;
    } else if(cmp(key, elem)) {
      *pos = index;
      return elem;
    }

    index = (h + ((i + (i * i)) >> 1)) & mask;
  }

  *pos = index_del;
  return NULL;
}

static void resize(hashmap_t* map, hash_fn hash, cmp_fn cmp, alloc_fn alloc,
  free_size_fn fr)
{
  size_t s = map->size;
  void** b = map->buckets;
  bitmap_t* old_item_bitmap = map->item_bitmap;
  void* curr = NULL;

  map->count = 0;
  map->size = (s < MIN_HASHMAP_SIZE) ? MIN_HASHMAP_SIZE : s << 3;

  // use a single memory allocation to exploit spatial memory/cache locality
  size_t bitmap_size = map->size/HASHMAP_BITMAP_TYPE_SIZE + (map->size%HASHMAP_BITMAP_TYPE_SIZE==0?0:1);
  void* mem_alloc = alloc((bitmap_size * sizeof(bitmap_t)) + (map->size * sizeof(void*)));
  memset(mem_alloc, 0, (bitmap_size * sizeof(bitmap_t)) + (map->size * sizeof(void*)));
  map->item_bitmap = (bitmap_t*)mem_alloc;
  map->buckets = (void**)(mem_alloc + (bitmap_size * sizeof(bitmap_t)));
//  printf("map: ib: %lu, ib_size: %lu, b: %lu, b_size: %lu\n", (uintptr_t)map->item_bitmap, bitmap_size * sizeof(bitmap_t), (uintptr_t)map->buckets, map->size * sizeof(void*));

  for(size_t i = 0; i < s; i++)
  {
    curr = b[i];

    if(valid(curr))
      ponyint_hashmap_put(map, curr, hash, cmp, alloc, fr);
  }

  if((fr != NULL) && (b != NULL))
  {
    size_t old_bitmap_size = s/HASHMAP_BITMAP_TYPE_SIZE + (s%HASHMAP_BITMAP_TYPE_SIZE==0?0:1);
    fr((old_bitmap_size * sizeof(bitmap_t)) + (s * sizeof(void*)), old_item_bitmap);
  }

}

bool ponyint_hashmap_needs_optimize(hashmap_t* map)
{
  // Don't optimize if the hashmap is too small or if the # deleted items is not large enough
  // TODO: figure out right heuristic for when to optimize
  if((map->size < MIN_HASHMAP_OPTIMIZE_SIZE) ||
    (((map->deleted_count << 1) < map->size) &&
    (((map->optimize_deleted_shift >= 0) ?
       map->deleted_count << map->optimize_deleted_shift :
       map->deleted_count >> map->optimize_deleted_shift) < (map->count))))
    return false;

  return true;
}

void ponyint_hashmap_finish_optimize(hashmap_t* map, size_t num_optimized)
{
//  printf("finish optimize.. size: %lu, count: %lu, deleted_count: %lu, optimize_deleted_shift: %ld, num_optimized: %lu\n", map->size, map->count, map->deleted_count, map->optimize_deleted_shift, num_optimized);

  // reset deleted count to 0 since we only care about new deletions since the last optimize
  // this is because the deleted elements will accumulate in the hashmap as time goes on
  // and entries are added and removed
  map->deleted_count = 0;


  // The hashmap is likely to reach steady state where optimize is barely moving any
  // items because new items get deleted and long lived items are already in optimal
  // position. The following is a way to identify that and not run optimize as often
  // in that case to not pay the penalty of hashing the keys all the time

  // back off on when next optimize will occur because we didn't optimize enough entries
  // during this optimize run to avoid wasting cpu cycles hashing entries that don't move
  if((num_optimized << MIN_HASHMAP_OPTIMIZATION_SHIFT) < map->count)
    // only back off to a maximum amount only to ensure that we would eventually run optimize again
    // TODO: Figure out better heuristic
    if(map->optimize_deleted_shift >= 0 || (map->optimize_deleted_shift < 0 &&
      (map->size >> map->optimize_deleted_shift) > 128))
      map->optimize_deleted_shift--;

  // increase frequency of when next optimize will occur because we optimized too many  entries
  // during this optimize run to avoid wasting cpu cycles via unnecessary probing
  if((num_optimized << MAX_HASHMAP_OPTIMIZATION_SHIFT) > map->count)
    // only increase frequency to a maximum # deleted of 6.25% of count
    // TODO: Figure out better heuristic
    if(map->optimize_deleted_shift < 4)
      map->optimize_deleted_shift++;
}

size_t ponyint_hashmap_optimize_item(hashmap_t* map, hash_fn hash, alloc_fn alloc,
  free_size_fn fr, cmp_fn cmp, size_t old_index, void* entry)
{

  size_t mask = map->size - 1;

  size_t h = hash(entry);
  size_t index = h & mask;

  for(size_t i = 1; i <= mask; i++)
  {
    // if next bucket index is current position, item is already in optimal spot
    if(index == old_index)
      break;

    // found an earlier deleted bucket so move item
    if(map->buckets[index] == DELETED)
    {
      ponyint_hashmap_removeindex(map, old_index);
      ponyint_hashmap_putindex(map, entry, hash, cmp, alloc, fr, index);
      return 1;
    }

    // find next bucket index
    index = (h + ((i + (i * i)) >> 1)) & mask;
  }

  return 0;
}

void ponyint_hashmap_init(hashmap_t* map, size_t size, alloc_fn alloc)
{
  if(size > 0)
  {
    // make sure we have room for this many elements without resizing
    size <<= 1;

    if(size < MIN_HASHMAP_SIZE)
      size = MIN_HASHMAP_SIZE;
    else
      size = ponyint_next_pow2(size);
  }

  map->count = 0;
  map->size = size;

  if(size > 0)
  {
    // use a single memory allocation to exploit spatial memory/cache locality
    size_t bitmap_size = size/HASHMAP_BITMAP_TYPE_SIZE + (size%HASHMAP_BITMAP_TYPE_SIZE==0?0:1);
    void* mem_alloc = alloc((bitmap_size * sizeof(bitmap_t)) + (size * sizeof(void*)));
    memset(mem_alloc, 0, (bitmap_size * sizeof(bitmap_t)) + (size * sizeof(void*)));
    map->item_bitmap = (bitmap_t*)mem_alloc;
    map->buckets = (void**)(mem_alloc + (bitmap_size * sizeof(bitmap_t)));
//    printf("map: ib: %lu, ib_size: %lu, b: %lu, b_size: %lu\n", (uintptr_t)map->item_bitmap, bitmap_size * sizeof(bitmap_t), (uintptr_t)map->buckets, size * sizeof(void*));
  } else {
    map->buckets = NULL;
    map->item_bitmap = NULL;
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
  {
    size_t bitmap_size = map->size/HASHMAP_BITMAP_TYPE_SIZE + (map->size%HASHMAP_BITMAP_TYPE_SIZE==0?0:1);
    fr((bitmap_size * sizeof(bitmap_t)) + (map->size * sizeof(void*)), map->item_bitmap);
  }

  map->count = 0;
  map->size = 0;
  map->buckets = NULL;
  map->item_bitmap = NULL;
}

void* ponyint_hashmap_get(hashmap_t* map, void* key, hash_fn hash, cmp_fn cmp, size_t* pos)
{
  if(map->count == 0)
    return NULL;

  return search(map, pos, key, hash, cmp);
}

void* ponyint_hashmap_put(hashmap_t* map, void* entry, hash_fn hash, cmp_fn cmp,
  alloc_fn alloc, free_size_fn fr)
{
  if(map->size == 0)
    ponyint_hashmap_init(map, 4, alloc);

  size_t pos;
  void* elem = search(map, &pos, entry, hash, cmp);

  map->buckets[pos] = entry;

  if(elem == NULL)
  {
    map->count++;

    // update item bitmap
    size_t ib_index = pos/HASHMAP_BITMAP_TYPE_SIZE;
    size_t ib_offset = pos%HASHMAP_BITMAP_TYPE_SIZE;
    map->item_bitmap[ib_index] |= ((bitmap_t)1 << ib_offset);

    if((map->count << 1) > map->size)
      resize(map, hash, cmp, alloc, fr);
  }

  return elem;
}

void* ponyint_hashmap_putindex(hashmap_t* map, void* entry, hash_fn hash, cmp_fn cmp,
  alloc_fn alloc, free_size_fn fr, size_t pos)
{
  if(pos == HASHMAP_UNKNOWN)
    return ponyint_hashmap_put(map, entry, hash, cmp, alloc, fr);

  if(map->size == 0)
    ponyint_hashmap_init(map, 4, alloc);

  assert(pos <= map->size);
  void* elem = map->buckets[pos];

  map->buckets[pos] = entry;

  if(elem == DELETED || elem == 0)
  {
    map->count++;

    // update item bitmap
    size_t ib_index = pos/HASHMAP_BITMAP_TYPE_SIZE;
    size_t ib_offset = pos%HASHMAP_BITMAP_TYPE_SIZE;
    map->item_bitmap[ib_index] |= ((bitmap_t)1 << ib_offset);


    if((map->count << 1) > map->size)
      resize(map, hash, cmp, alloc, fr);

    return entry;
  }

  return elem;
}

void* ponyint_hashmap_remove(hashmap_t* map, void* entry, hash_fn hash,
  cmp_fn cmp)
{
  if(map->count == 0)
    return NULL;

  size_t pos;
  void* elem = search(map, &pos, entry, hash, cmp);

  if(elem != NULL)
  {
    map->buckets[pos] = DELETED;
    map->count--;

    // update item bitmap
    size_t ib_index = pos/HASHMAP_BITMAP_TYPE_SIZE;
    size_t ib_offset = pos%HASHMAP_BITMAP_TYPE_SIZE;
    map->item_bitmap[ib_index] &= ~((bitmap_t)1 << ib_offset);
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

  // update item bitmap
  size_t ib_index = index/HASHMAP_BITMAP_TYPE_SIZE;
  size_t ib_offset = index%HASHMAP_BITMAP_TYPE_SIZE;
  map->item_bitmap[ib_index] &= ~((bitmap_t)1 << ib_offset);

  return elem;
}

void* ponyint_hashmap_next(hashmap_t* map, size_t* i)
{
  if(map->count == 0)
    return NULL;

  void* elem = NULL;
  size_t index = *i + 1;
  size_t ib_index = 0;
  size_t ib_offset = 0;

  while(index < map->size)
  {
    ib_index = index/HASHMAP_BITMAP_TYPE_SIZE;
    ib_offset = index%HASHMAP_BITMAP_TYPE_SIZE;

    // if we're at the beginning of a new array entry in the item bitmap
    if(ib_offset == 0)
    {
      // find first set bit using ffs
#ifdef PLATFORM_IS_ILP32
      ib_offset = __pony_ffs(map->item_bitmap[ib_index]);
#else
      ib_offset = __pony_ffsl(map->item_bitmap[ib_index]);
#endif

      // if no bits set; increment by size of item bitmap type and continue
      if(ib_offset == 0)
      {
        index += HASHMAP_BITMAP_TYPE_SIZE;
        continue;
      } else {
        // found a set bit for valid element
        index += (ib_offset - 1);
        elem = map->buckets[index];

        // no need to check if valid element because item bitmap keeps track of that
        *i = index;
        return elem;
      }
    } else {

      // in the middle of an item bitmap array entry (can't use ffs optimization
      // to find first valid element in bitmap araay entry)
      do
        if((map->item_bitmap[ib_index] & ((bitmap_t)1 << ib_offset)) != 0)
        {
          // found an element
          elem = map->buckets[index];

          // no need to check if valid element because item bitmap keeps track of that
          *i = index;
          return elem;
        } else {
          index++;
          ib_offset++;
        }
      while(ib_offset < HASHMAP_BITMAP_TYPE_SIZE);
    }
  }

  // searched through bitmap and didn't find any more valid elements.
  // due to ffs index can be greater than size so set i to size instead
  // to preserve old behavior
  *i = map->size;
  return NULL;
}

size_t ponyint_hashmap_size(hashmap_t* map)
{
  return map->count;
}
