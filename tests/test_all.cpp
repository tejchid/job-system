#include <gtest/gtest.h>

#include <atomic>
#include <mutex>
#include <thread>   // REQUIRED
#include <vector>

#include "job_system.h"

using namespace js;

TEST(JobSystem, BasicSubmitWait) {
    JobSystem js(4);
    std::atomic<int> x{0};
    auto h = js.submit([&] { x.store(42, std::memory_order_relaxed); });
    js.wait(h);
    EXPECT_EQ(x.load(), 42);
}

TEST(JobSystem, HundredKTasks) {
    JobSystem js(4);
    std::atomic<int> counter{0};
    const int N = 100000;
    const int batch = 2000;

    for (int i = 0; i < N; i += batch) {
        std::vector<TaskHandle> handles;
        handles.reserve(batch);
        for (int j = 0; j < batch; ++j)
            handles.push_back(js.submit([&] {
                counter.fetch_add(1, std::memory_order_relaxed);
            }));
        for (auto& h : handles) js.wait(h);
    }

    EXPECT_EQ(counter.load(), N);
}

TEST(JobSystem, LinearDependencyChain) {
    JobSystem js(4);
    std::vector<int> order;
    std::mutex mu;

    TaskHandle prev{};
    for (int i = 0; i < 100; ++i) {
        int val = i;
        if (prev)
            prev = js.submit([&, val] {
                std::lock_guard<std::mutex> lk(mu);
                order.push_back(val);
            }, {prev});
        else
            prev = js.submit([&, val] {
                std::lock_guard<std::mutex> lk(mu);
                order.push_back(val);
            });
    }
    js.wait(prev);

    ASSERT_EQ(static_cast<int>(order.size()), 100);
    for (int i = 0; i < 100; ++i)
        EXPECT_EQ(order[i], i);
}

TEST(JobSystem, DiamondDependency) {
    JobSystem js(4);
    std::atomic<int> result{0};

    auto a = js.submit([&] { result.fetch_add(1, std::memory_order_relaxed); });
    auto b = js.submit([&] { result.fetch_add(2, std::memory_order_relaxed); }, {a});
    auto c = js.submit([&] { result.fetch_add(4, std::memory_order_relaxed); }, {a});
    auto d = js.submit([&] { result.fetch_add(8, std::memory_order_relaxed); }, {b, c});

    js.wait(d);
    EXPECT_EQ(result.load(), 15);
}

TEST(JobSystem, ConcurrentSubmit) {
    JobSystem js(8);
    std::atomic<int> counter{0};
    const int T = 4;
    const int N = 1000;

    std::vector<std::thread> threads;
    threads.reserve(T);

    for (int t = 0; t < T; ++t) {
        threads.emplace_back([&] {
            std::vector<TaskHandle> handles;
            handles.reserve(N);
            for (int i = 0; i < N; ++i)
                handles.push_back(js.submit([&] {
                    counter.fetch_add(1, std::memory_order_relaxed);
                }));
            for (auto& h : handles) js.wait(h);
        });
    }

    for (auto& th : threads) th.join();
    EXPECT_EQ(counter.load(), T * N);
}

TEST(JobSystem, ShutdownClean) {
    JobSystem js(4);
    for (int i = 0; i < 100; ++i) js.submit([]{});
    js.shutdown();
}