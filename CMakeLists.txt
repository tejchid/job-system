cmake_minimum_required(VERSION 3.20)
project(job_system CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Threads REQUIRED)

add_library(job_system STATIC
    src/job_system.cpp
    src/worker.cpp
)
target_include_directories(job_system
    PUBLIC  include
    PRIVATE src
)
target_link_libraries(job_system PUBLIC Threads::Threads)
target_compile_options(job_system PRIVATE -Wall -Wextra)

# Tests
find_package(GTest REQUIRED)
enable_testing()
add_executable(job_system_tests tests/test_all.cpp)
target_include_directories(job_system_tests PRIVATE src)
target_link_libraries(job_system_tests PRIVATE job_system GTest::gtest GTest::gtest_main)
add_test(NAME job_system_tests COMMAND job_system_tests)

# Benchmarks
find_package(benchmark QUIET)
if(benchmark_FOUND)
    add_executable(job_system_bench bench/bench_main.cpp)
    target_include_directories(job_system_bench PRIVATE src)
    target_link_libraries(job_system_bench PRIVATE job_system benchmark::benchmark benchmark::benchmark_main)
else()
    include(FetchContent)
    FetchContent_Declare(
        benchmark
        URL https://github.com/google/benchmark/archive/refs/tags/v1.8.3.zip
        DOWNLOAD_EXTRACT_TIMESTAMP true
    )
    set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(benchmark)
    add_executable(job_system_bench bench/bench_main.cpp)
    target_include_directories(job_system_bench PRIVATE src)
    target_link_libraries(job_system_bench PRIVATE job_system benchmark::benchmark benchmark::benchmark_main)
endif()