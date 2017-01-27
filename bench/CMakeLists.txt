# Enable the tests

find_package(Threads REQUIRED)
link_directories(${CMAKE_BINARY_DIR}/../)

# NOTE: These flags must be added after find_package(Threads REQUIRED) otherwise
# they will break the configuration check.
if (DEFINED BENCHMARK_CXX_LINKER_FLAGS)
  list(APPEND CMAKE_EXE_LINKER_FLAGS ${BENCHMARK_CXX_LINKER_FLAGS})
endif()

macro(compile_benchmark_test name file)
  add_executable(${name} ${file})
  target_link_libraries(${name} benchmark ponyrt ${CMAKE_THREAD_LIBS_INIT})
endmacro(compile_benchmark_test)

compile_benchmark_test(hash libponyrt/ds/hash.cc)
add_test(hash_benchmark hash --benchmark_min_time=0.05)

