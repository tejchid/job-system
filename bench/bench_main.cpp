#include <benchmark/benchmark.h>
#include <atomic>
#include "job_system.h"

using namespace js;

static void BM_Throughput(benchmark::State& state) {
    int threads = state.range(0);
    cppJobSystem js(threads);
    const int batch = 1000;

    for (auto _ : state) {
        std::atomic<int> counter{0};
        for (int i = 0; i < 10000; i += batch) {
            std::vector<TaskHandle> handles;
            handles.reserve(batch);
            for (int j = 0; j < batch; ++j) {
                handles.push_back(js.submit([&] {
                    counter.fetch_add(1, std::memory_order_relaxed);
                }));
            }
            for (auto& h : handles) js.wait(h);
        }
        benchmark::DoNotOptimize(counter.load());
    }

    js.shutdown();
}
BENCHMARK(BM_Throughput)->Arg(1)->Arg(4)->Arg(8);

static void BM_DependencyChain(benchmark::State& state) {
    int threads = state.range(0);
    cppJobSystem js(threads);

    for (auto _ : state) {
        TaskHandle prev{};
        for (int i = 0; i < 1000; ++i) {
            if (prev) prev = js.submit([] {}, {prev});
            else      prev = js.submit([] {});
        }
        js.wait(prev);
    }

    js.shutdown();
}
BENCHMARK(BM_DependencyChain)->Arg(1)->Arg(4)->Arg(8);

BENCHMARK_MAIN();