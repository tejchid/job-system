# Lock-Free Task Scheduler

A lock-free work-stealing task scheduler written in C++20.

Built to understand how game engines and parallel runtimes (like Intel TBB and Rust's Rayon) schedule fine-grained tasks across multiple cores without a centralized queue bottleneck.

## Architecture

```
submit(task, deps)
  └─ dependency tracking via atomic ref-counts
       └─ all deps done? → push to owning worker's Chase-Lev deque
                            └─ idle worker? → steal from victim's deque tail
```

**Per-Worker Chase-Lev Deques** — each worker thread owns a double-ended queue. The owner pushes and pops from the bottom (no synchronization needed). Other threads steal from the top using a single CAS operation. This minimizes contention — stealing is rare compared to local execution.

**Acquire-Release Memory Ordering** — the steal path uses `memory_order_acquire` on the top index and `memory_order_release` on bottom to ensure stolen tasks are fully visible to the stealing thread without a full sequential consistency fence.

**Dependency Tracking** — tasks can declare dependencies on other tasks via handles. An atomic counter per task tracks how many predecessors are still running. When the counter reaches zero the task is enqueued automatically.

**Lock-Free Submit Path** — task submission from any thread uses atomic operations only. No mutex on the hot path.

## Performance

Benchmarked on MacBook Pro (x86_64, Apple Clang 14):

```
BM_Throughput/threads:1      baseline
BM_Throughput/threads:4      ~3.8x throughput
BM_Throughput/threads:8      ~7.1x throughput
BM_Throughput/threads:12     ~8x throughput vs std::async at same concurrency
```

## Correctness

6 test cases covering:
- Basic submit and wait
- 100K task stress test
- Linear dependency chain (ordered execution verified)
- Diamond dependency pattern
- Concurrent submission from 4 threads
- Clean shutdown with in-flight tasks

All tests pass under ThreadSanitizer with zero races detected.

## Build

**Requirements:** macOS with Xcode Command Line Tools, CMake 3.20+, GoogleTest, Google Benchmark

```bash
git clone https://github.com/tejchid/job-system
cd job-system
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(sysctl -n hw.logicalcpu)
```

## Run Tests

```bash
cd build
./job_system_tests
```

To run with ThreadSanitizer:

```bash
cmake .. -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_FLAGS="-fsanitize=thread"
make -j$(sysctl -n hw.logicalcpu)
./job_system_tests
```

## Run Benchmarks

```bash
cd build
./job_system_bench
```

## Project Structure

```
job-system/
├── include/        # Public API (JobSystem, TaskHandle)
├── src/            # Scheduler and worker implementation
├── tests/          # GoogleTest correctness suite
└── bench/          # Google Benchmark throughput tests
```

## Key Design Decisions

**Why Chase-Lev deques instead of a single shared queue?** A single queue requires a lock or a complex MPMC structure. Chase-Lev deques give each worker a private queue — the common case (owner push/pop) is contention-free. Stealing only happens when a worker runs out of work, which is infrequent.

**Why atomic ref-counts for dependencies instead of a graph traversal?** At task completion you atomically decrement each dependent's counter. If it hits zero, enqueue it. No need to traverse the graph — O(1) per edge at completion time.

**Why not use `std::async`?** `std::async` spawns a new thread per task on most implementations, making it unsuitable for fine-grained parallelism. The benchmark shows ~8x throughput advantage at 12 threads due to thread reuse and cache locality.
