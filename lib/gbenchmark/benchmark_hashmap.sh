#!/bin/bash -vx

cd ../..
BRANCH=dh_hashmap_optimization_base
git checkout ${BRANCH}
make clean
make config=release
cd lib/gbenchmark
rm -rf build
mkdir build
cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
make
./test/hash_test --benchmark_out=../../../${BRANCH}.json --benchmark_out_format=json

cd ../../..
BRANCH=dh_hashmap_optimization
git checkout ${BRANCH}
make clean
make config=release
cd lib/gbenchmark
rm -rf build
mkdir build
cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
make
./test/hash_test --benchmark_out=../../../${BRANCH}.json --benchmark_out_format=json

cd ../../..
BRANCH=dh_hashmap_optimization_alt
git checkout ${BRANCH}
make clean
make config=release
cd lib/gbenchmark
rm -rf build
mkdir build
cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
make
./test/hash_test --benchmark_out=../../../${BRANCH}.json --benchmark_out_format=json

cd ../../..
BRANCH=dh_hashmap_optimization_alt2
git checkout ${BRANCH}
make clean
make config=release
cd lib/gbenchmark
rm -rf build
mkdir build
cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
make
./test/hash_test --benchmark_out=../../../${BRANCH}.json --benchmark_out_format=json

cd ../../..
BRANCH=dh_hashmap_optimization_alt3
git checkout ${BRANCH}
make clean
make config=release
cd lib/gbenchmark
rm -rf build
mkdir build
cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
make
./test/hash_test --benchmark_out=../../../${BRANCH}.json --benchmark_out_format=json
