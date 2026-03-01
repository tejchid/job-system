#include <benchmark/benchmark.h>
#include <atomic>
#include <future>
#include <vector>
#include "job_system.h"

using namespace js;

// Throughput: how many tasks/sec across N threads
static void BM_Throughput(benchmark::State& state) {
    int threads = state.range(0);
    JobSystem js(threads);
    const int batch = 1000;

    for (auto _ : state) {
        std::atomic<int> counter{0};
        std::vector<TaskHandle> handles;
        handles.reserve(batch);
        for (int i = 0; i < batch; ++i)
            handles.push_back(js.submit([&] {
                counter.fetch_add(1, std::memory_order_relaxed);
            }));
        for (auto& h : handles) js.wait(h);
        benchmark::DoNotOptimize(counter.load());
    }

    state.SetItemsProcessed(state.iterations() * batch);
    js.shutdown();
}
BENCHMARK(BM_Throughput)->Arg(1)->Arg(4)->Arg(8)->Arg(12);

// Dependency chain: 1000 tasks in a linear chain
static void BM_DependencyChain(benchmark::State& state) {
    int threads = state.range(0);
    JobSystem js(threads);

    for (auto _ : state) {
        TaskHandle prev{};
        for (int i = 0; i < 1000; ++i) {
            if (prev) prev = js.submit([] {}, {prev});
            else      prev = js.submit([] {});
        }
        js.wait(prev);
    }

    state.SetItemsProcessed(state.iterations() * 1000);
    js.shutdown();
}
BENCHMARK(BM_DependencyChain)->Arg(1)->Arg(4)->Arg(8);

// vs std::async baseline
static void BM_StdAsync(benchmark::State& state) {
    const int batch = 1000;
    for (auto _ : state) {
        std::atomic<int> counter{0};
        std::vector<std::future<void>> futures;
        futures.reserve(batch);
        for (int i = 0; i < batch; ++i)
            futures.push_back(std::async(std::launch::async, [&] {
                counter.fetch_add(1, std::memory_order_relaxed);
            }));
        for (auto& f : futures) f.wait();
        benchmark::DoNotOptimize(counter.load());
    }
    state.SetItemsProcessed(state.iterations() * batch);
}
BENCHMARK(BM_StdAsync);

BENCHMARK_MAIN();