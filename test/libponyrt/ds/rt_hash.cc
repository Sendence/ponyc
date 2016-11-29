#include <platform.h>
#include <gtest/gtest.h>

#include <stdlib.h>
#include <stdint.h>

#include <ds/fun.h>
#include <ds/rt_hash.h>

#define INITIAL_SIZE 8
#define BELOW_HALF (INITIAL_SIZE + (2 - 1)) / 2

typedef struct rt_hash_elem_t rt_hash_elem_t;

DECLARE_RT_HASHMAP(rt_testmap, rt_testmap_t, rt_hash_elem_t);

class RTHashMapTest: public testing::Test
{
  protected:
    rt_testmap_t _map;

    virtual void SetUp();
    virtual void TearDown();

    rt_hash_elem_t* get_element();
    void put_elements(size_t count);

  public:
    static size_t hash_tst(uintptr_t key);
    static void free_elem(rt_hash_elem_t* p);
    static void free_buckets(size_t size, void* p);
};

DEFINE_RT_HASHMAP(rt_testmap, rt_testmap_t, rt_hash_elem_t, RTHashMapTest::hash_tst,
  malloc, RTHashMapTest::free_buckets,
  RTHashMapTest::free_elem);

struct rt_hash_elem_t
{
  size_t key;
  size_t val;
};

void RTHashMapTest::SetUp()
{
  rt_testmap_init(&_map, 1);
}

void RTHashMapTest::TearDown()
{
  rt_testmap_destroy(&_map);
}

void RTHashMapTest::put_elements(size_t count)
{
  rt_hash_elem_t* curr = NULL;

  for(size_t i = 0; i < count; i++)
  {
    curr = get_element();
    curr->key = i;

    rt_testmap_put(&_map, curr, (uintptr_t)curr->key);
  }
}

rt_hash_elem_t* RTHashMapTest::get_element()
{
  return (rt_hash_elem_t*) malloc(sizeof(rt_hash_elem_t));
}

size_t RTHashMapTest::hash_tst(uintptr_t key)
{
  return ponyint_hash_size(key);
}

void RTHashMapTest::free_elem(rt_hash_elem_t* p)
{
  free(p);
}

void RTHashMapTest::free_buckets(size_t len, void* p)
{
  (void)len;
  free(p);
}

/** The default size of a map is 0 or at least 8,
 *  i.e. a full cache line of void* on 64-bit systems.
 */
TEST_F(RTHashMapTest, InitialSizeCacheLine)
{
  ASSERT_EQ((size_t)INITIAL_SIZE, _map.contents.size);
}

/** The size of a list is the number of distinct elements
 *  that have been added to the list.
 */
TEST_F(RTHashMapTest, HashMapSize)
{
  put_elements(100);

  ASSERT_EQ((size_t)100, rt_testmap_size(&_map));
}

/** Hash maps are resized by (size << 3)
 *  once a size threshold of 0.5 is exceeded.
 */
TEST_F(RTHashMapTest, Resize)
{
  put_elements(BELOW_HALF);

  ASSERT_EQ((size_t)BELOW_HALF, rt_testmap_size(&_map));
  // the map was not resized yet.
  ASSERT_EQ((size_t)INITIAL_SIZE, _map.contents.size);

  rt_hash_elem_t* curr = get_element();
  curr->key = BELOW_HALF;

  rt_testmap_put(&_map, curr, (uintptr_t)curr->key);

  ASSERT_EQ((size_t)BELOW_HALF+1, rt_testmap_size(&_map));
  ASSERT_EQ((size_t)INITIAL_SIZE << 3, _map.contents.size);
}

/** After having put an element with a
 *  some key, it should be possible
 *  to retrieve that element using the key.
 */
TEST_F(RTHashMapTest, InsertAndRetrieve)
{
  rt_hash_elem_t* e = get_element();
  e->key = 1;
  e->val = 42;
  size_t index = RT_HASHMAP_UNKNOWN;

  rt_testmap_put(&_map, e, (uintptr_t)e->key);

  rt_hash_elem_t* n = rt_testmap_get(&_map, (uintptr_t)e->key, &index);

  ASSERT_EQ(e->val, n->val);
}

/** Getting an element which is not in the HashMap
 *  should result in NULL.
 */
TEST_F(RTHashMapTest, TryGetNonExistent)
{
  rt_hash_elem_t* e1 = get_element();
  rt_hash_elem_t* e2 = get_element();
  size_t index = RT_HASHMAP_UNKNOWN;

  e1->key = 1;
  e2->key = 2;

  rt_testmap_put(&_map, e1, (uintptr_t)e1->key);

  rt_hash_elem_t* n = rt_testmap_get(&_map, (uintptr_t)e2->key, &index);

  ASSERT_EQ(NULL, n);
}

/** Replacing elements with equivalent keys
 *  returns the previous one.
 */
TEST_F(RTHashMapTest, ReplacingElementReturnsReplaced)
{
  rt_hash_elem_t* e1 = get_element();
  rt_hash_elem_t* e2 = get_element();
  size_t index = RT_HASHMAP_UNKNOWN;

  e1->key = 1;
  e2->key = 1;

  rt_testmap_put(&_map, e1, (uintptr_t)e1->key);

  rt_hash_elem_t* n = rt_testmap_put(&_map, e2, (uintptr_t)e2->key);
  ASSERT_EQ(n, e1);

  rt_hash_elem_t* m = rt_testmap_get(&_map, (uintptr_t)e2->key, &index);
  ASSERT_EQ(m, e2);
}

/** Deleting an element in a hash map returns it.
 *  The element cannot be retrieved anymore after that.
 *
 *  All other elements remain within the map.
 */
TEST_F(RTHashMapTest, DeleteElement)
{
  rt_hash_elem_t* e1 = get_element();
  rt_hash_elem_t* e2 = get_element();

  e1->key = 1;
  e2->key = 2;
  size_t index = RT_HASHMAP_UNKNOWN;

  rt_testmap_put(&_map, e1, (uintptr_t)e1->key);
  rt_testmap_put(&_map, e2, (uintptr_t)e2->key);

  size_t l = rt_testmap_size(&_map);

  ASSERT_EQ(l, (size_t)2);

  rt_hash_elem_t* n1 = rt_testmap_remove(&_map, (uintptr_t)e1->key);

  l = rt_testmap_size(&_map);

  ASSERT_EQ(n1, e1);
  ASSERT_EQ(l, (size_t)1);

  rt_hash_elem_t* n2 = rt_testmap_get(&_map, (uintptr_t)e2->key, &index);

  ASSERT_EQ(n2, e2);
}

/** Iterating over a hash map returns every element
 *  in it.
 */
TEST_F(RTHashMapTest, MapIterator)
{
  rt_hash_elem_t* curr = NULL;
  size_t expect = 0;

  for(uint32_t i = 0; i < 100; i++)
  {
    expect += i;
    curr = get_element();

    curr->key = i;
    curr->val = i;

    rt_testmap_put(&_map, curr, (uintptr_t)curr->key);
  }

  size_t s = RT_HASHMAP_BEGIN;
  size_t l = rt_testmap_size(&_map);
  size_t c = 0; //executions
  size_t e = 0; //sum

  ASSERT_EQ(l, (size_t)100);

  while((curr = rt_testmap_next(&_map, &s)) != NULL)
  {
    c++;
    e += curr->val;
  }

  ASSERT_EQ(e, expect);
  ASSERT_EQ(c, l);
}

/** An element removed by index cannot be retrieved
 *  after being removed.
 */
TEST_F(RTHashMapTest, RemoveByIndex)
{
  rt_hash_elem_t* curr = NULL;
  put_elements(100);

  size_t i = RT_HASHMAP_BEGIN;
  size_t index = RT_HASHMAP_UNKNOWN;
  rt_hash_elem_t* p = NULL;

  while((curr = rt_testmap_next(&_map, &i)) != NULL)
  {
    if(curr->key == 20)
    {
      p = curr;
      break;
    }
  }

  rt_hash_elem_t* n = rt_testmap_removeindex(&_map, i);

  ASSERT_EQ(n, p);
  ASSERT_EQ(NULL, rt_testmap_get(&_map, (uintptr_t)p->key, &index));
}
