cmake_minimum_required(VERSION 3.22)
project(hello_cmake CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Boost REQUIRED)
find_package(absl REQUIRED)

include(FetchContent)
FetchContent_Declare(
  GoogleBenchmark
  GIT_REPOSITORY https://github.com/google/benchmark.git
  GIT_TAG    v1.8.0
)
set(BENCHMARK_ENABLE_TESTING OFF)
FetchContent_MakeAvailable(GoogleBenchmark)

add_executable(hello_benchmark benchmark.cpp)
target_link_libraries(hello_benchmark PRIVATE
    benchmark::benchmark
    Boost::boost
    absl::flat_hash_map
)
