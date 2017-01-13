In order to run hashmap benchmarks do the following:

* Starting from root of ponyc directory
* `make config=release` # to build ponyc
* `cd lib/gbenchmark`
* `mkdir build`
* `cd build`
* `cmake -DCMAKE_BUILD_TYPE=Release ..` # configure benchmark build
* `make` # compile benchmarks
* `./test/hash_test` # run hashmap benchmarks
