#include "benchmark/benchmark.h"
#include <platform.h>

#include <stdlib.h>
#include <stdint.h>

#include <ds/fun.h>
#include <ds/rt_hash.h>

#define INITIAL_SIZE 8
#define BELOW_HALF (INITIAL_SIZE + (2 - 1)) / 2

typedef struct hash_elem_t hash_elem_t;

DECLARE_RT_HASHMAP(testmap, testmap_t, hash_elem_t)

class HashMapTest: public ::benchmark::Fixture
{
  protected:
    testmap_t _map;

    virtual void SetUp(const ::benchmark::State& st);
    virtual void TearDown(const ::benchmark::State& st);

    hash_elem_t* get_element();
    void put_elements(size_t count);
    void delete_elements(size_t del_count, size_t count);

  public:
    static size_t hash_tst(uintptr_t p);
    static bool cmp_tst(hash_elem_t* a, hash_elem_t* b);
    static void free_elem(hash_elem_t* p);
    static void free_buckets(size_t size, void* p);
};

DEFINE_RT_HASHMAP(testmap, testmap_t, hash_elem_t, HashMapTest::hash_tst,
  malloc, HashMapTest::free_buckets,
  HashMapTest::free_elem)

struct hash_elem_t
{
  size_t key;
  size_t val;
};

void HashMapTest::SetUp(const ::benchmark::State& st)
{
  if (st.thread_index == 0) {
//    printf("setup; init: %d\n", st.range(0));
    testmap_init(&_map, st.range(0));
    put_elements(st.range(1));
    delete_elements(st.range(2), st.range(1));
    srand(635356);
  }
}

void HashMapTest::TearDown(const ::benchmark::State& st)
{
  if (st.thread_index == 0) {
    testmap_destroy(&_map);
  }
//  printf("teardown\n");
}

void HashMapTest::put_elements(size_t count)
{
//  printf("putelements; num: %lu\n", count);
  hash_elem_t* curr = NULL;

  for(size_t i = 0; i < count; i++)
  {
    curr = get_element();
    curr->key = i;

    testmap_put(&_map, curr, i);
  }
}

void HashMapTest::delete_elements(size_t del_pct, size_t count)
{
  size_t key = 0;
  size_t del_count = del_pct/100.0 * count;

//  for(size_t i = 0; i < del_count; i++)
  while(testmap_size(&_map) > count - del_count)
  {
    key = rand() % count;

    hash_elem_t* d = testmap_remove(&_map, key);
    if(d != NULL)
      free_elem(d);
  }
//  printf("deleteelements; pct: %lu, count: %lu, final_size: %lu\n", del_pct, del_count, testmap_size(&_map));
}

hash_elem_t* HashMapTest::get_element()
{
  return (hash_elem_t*) malloc(sizeof(hash_elem_t));
}

size_t HashMapTest::hash_tst(uintptr_t p)
{
  return ponyint_hash_size(p);
}

bool HashMapTest::cmp_tst(hash_elem_t* a, hash_elem_t* b)
{
  return a->key == b->key;
}

void HashMapTest::free_elem(hash_elem_t* p)
{
  free(p);
}

void HashMapTest::free_buckets(size_t len, void* p)
{
  (void)len;
  free(p);
}

BENCHMARK_DEFINE_F(HashMapTest, HashNext)(benchmark::State& st) {
  while (st.KeepRunning()) {
    size_t ind = RT_HASHMAP_UNKNOWN;
    for(size_t i = 0; i < testmap_size(&_map); i++) {
      hash_elem_t* n = testmap_next(&_map, &ind);
      if(n == NULL)
        printf("shouldn't happen\n");
    }
    hash_elem_t* n = testmap_next(&_map, &ind);
    if(n != NULL)
      printf("shouldn't happen\n");
    st.SetItemsProcessed(testmap_size(&_map) + 1);
  }
}

BENCHMARK_REGISTER_F(HashMapTest, HashNext)->RangeMultiplier(2)->Ranges({{1, 32<<10}, {1, 32}, {0, 0}, {0, 0}});
BENCHMARK_REGISTER_F(HashMapTest, HashNext)->RangeMultiplier(2)->Ranges({{1, 1}, {1, 32<<10}, {0, 0}, {0, 0}});

BENCHMARK_DEFINE_F(HashMapTest, HashPut)(benchmark::State& st) {
  hash_elem_t* curr = NULL;
  while (st.KeepRunning()) {
    st.PauseTiming();
    delete_elements(100);
    st.ResumeTiming();
    for(int i = 0; i < st.range(3); i++)
    {
      curr = get_element();
      curr->key = i;

      testmap_put(&_map, curr, i);
    }
    st.SetItemsProcessed(st.range(3));
  }
}

BENCHMARK_REGISTER_F(HashMapTest, HashPut)->RangeMultiplier(2)->Ranges({{32<<10, 32<<10}, {0, 0}, {0, 0}, {1<<10, 16<<10}});

BENCHMARK_DEFINE_F(HashMapTest, HashPutIndex)(benchmark::State& st) {
  hash_elem_t* curr = NULL;
  while (st.KeepRunning()) {
    st.PauseTiming();
    delete_elements(100);
    st.ResumeTiming();
    for(int i = 0; i < st.range(3); i++)
    {
      curr = get_element();
      curr->key = i;

      testmap_putindex(&_map, curr, i, i);
    }
    st.SetItemsProcessed(st.range(3));
  }
}

BENCHMARK_REGISTER_F(HashMapTest, HashPutIndex)->RangeMultiplier(2)->Ranges({{32<<10, 32<<10}, {0, 0}, {0, 0}, {1<<10, 16<<10}});

BENCHMARK_DEFINE_F(HashMapTest, HashRemove)(benchmark::State& st) {
  while (st.KeepRunning()) {
    st.PauseTiming();
    put_elements(st.range(3));
    st.ResumeTiming();
    for(int i = 0; i < st.range(3); i++)
    {
      hash_elem_t* n2 = testmap_remove(&_map, i);
      if(n2 != NULL) {
        st.PauseTiming();
        free_elem(n2);
        st.ResumeTiming();
      } else {
        printf("shouldn't happen; %d not found\n", i);
      }
    }
    st.SetItemsProcessed(st.range(3));
  }
}

BENCHMARK_REGISTER_F(HashMapTest, HashRemove)->RangeMultiplier(2)->Ranges({{1, 1}, {0, 0}, {0, 0}, {1<<10, 32<<10}});

BENCHMARK_DEFINE_F(HashMapTest, HashRemoveIndex)(benchmark::State& st) {
  while (st.KeepRunning()) {
    st.PauseTiming();
    put_elements(st.range(3));
    st.ResumeTiming();
    for(size_t i = 0; i < testmap_size(&_map); i++)
    {
      hash_elem_t* n2 = testmap_removeindex(&_map, i);
      if(n2 != NULL) {
        st.PauseTiming();
        free_elem(n2);
        st.ResumeTiming();
      }
    }
    st.SetItemsProcessed(st.range(3));
  }
}

BENCHMARK_REGISTER_F(HashMapTest, HashRemoveIndex)->RangeMultiplier(2)->Ranges({{1, 1}, {0, 0}, {0, 0}, {1<<10, 32<<10}});

BENCHMARK_DEFINE_F(HashMapTest, HashSearch)(benchmark::State& st) {
  size_t key = 0;
  while (st.KeepRunning()) {
    for(int i = 0; i < st.range(3); i++) {
      st.PauseTiming();
      key = rand() % st.range(1);
      st.ResumeTiming();
      size_t index = RT_HASHMAP_UNKNOWN;
      hash_elem_t* n2 = testmap_get(&_map, key, &index);
      if(n2 == NULL)
        printf("shouldn't happen\n");
    }
    st.SetItemsProcessed(st.range(3));
  }
}

BENCHMARK_REGISTER_F(HashMapTest, HashSearch)->RangeMultiplier(2)->Ranges({{1, 1}, {1<<10, 32<<10}, {0, 0}, {64, 1024}});

BENCHMARK_DEFINE_F(HashMapTest, HashSearchDeletes)(benchmark::State& st) {
  size_t key = 0;
  while (st.KeepRunning()) {
    st.PauseTiming();
    size_t *a = new size_t[testmap_size(&_map)];
    size_t ind = RT_HASHMAP_UNKNOWN;
    size_t ind2 = RT_HASHMAP_UNKNOWN;
    size_t num_optimized = 0;

    for(size_t i = 0; i < testmap_size(&_map); i++) {
      hash_elem_t* n = testmap_next(&_map, &ind);
      if(n != NULL) {
        num_optimized += testmap_optimize_item(&_map, n, n->key, ind);
        a[i] = n->key;
        ind2 = RT_HASHMAP_UNKNOWN;
        hash_elem_t* n2 = testmap_get(&_map, n->key, &ind2);
        if(n2 == NULL)
          printf("shouldn't happen\n");
      } else {
        printf("shouldn't happen\n");
      }
    }
    testmap_finish_optimize(&_map, num_optimized);
    st.ResumeTiming();

    for(int i = 0; i < st.range(3); i++) {
      st.PauseTiming();
      key = a[rand() % testmap_size(&_map)];
      st.ResumeTiming();
      size_t index = RT_HASHMAP_UNKNOWN;
      hash_elem_t* n2 = testmap_get(&_map, key, &index);
      if(n2 == NULL)
        printf("shouldn't happen\n");
    }
    st.PauseTiming();
    delete[] a;
    st.ResumeTiming();
    st.SetItemsProcessed(st.range(3));
  }
}

BENCHMARK_REGISTER_F(HashMapTest, HashSearchDeletes)->RangeMultiplier(2)->Ranges({{1, 1}, {1<<10, 32<<10}, {64, 90}, {64, 1024}});

BENCHMARK_MAIN()
