cmake_minimum_required(VERSION 3.20)
project(job_system LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(BUILD_BENCH "Build benchmarks" ON)
option(BUILD_TESTS "Build tests" ON)

find_package(Threads REQUIRED)

add_library(job_system
    src/job_system.cpp
    src/worker.cpp
)

target_include_directories(job_system
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(job_system
    PUBLIC Threads::Threads
)

if (MSVC)
    target_compile_options(job_system PRIVATE /W4)
else()
    target_compile_options(job_system PRIVATE
        -Wall -Wextra -Wpedantic -Wshadow -Wconversion
    )
endif()

# ================= BENCH =================

if (BUILD_BENCH)
    include(FetchContent)

    FetchContent_Declare(
        benchmark
        URL https://github.com/google/benchmark/archive/refs/tags/v1.8.3.zip
    )

    set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(benchmark)

    add_executable(job_system_bench
        bench/bench_main.cpp
    )

    target_link_libraries(job_system_bench
        PRIVATE
            job_system
            benchmark::benchmark
            benchmark::benchmark_main
    )
endif()

# ================= TESTS =================

if (BUILD_TESTS)

    # Use Homebrew-installed GoogleTest
    find_package(GTest REQUIRED)

    enable_testing()

    add_executable(job_system_tests
        tests/test_basic.cpp
        tests/test_dependencies.cpp
        tests/test_stress.cpp
    )

    target_link_libraries(job_system_tests
        PRIVATE
            job_system
            GTest::GTest
            GTest::Main
            Threads::Threads
    )

    add_test(NAME job_system_tests
             COMMAND job_system_tests)
endif()