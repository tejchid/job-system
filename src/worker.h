#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <thread>

#include "deque.h"

namespace js {

class JobSystem;
struct Task;

class alignas(64) Worker {
public:
    Worker(JobSystem* js, std::size_t id);
    ~Worker();

    Worker(const Worker&) = delete;
    Worker& operator=(const Worker&) = delete;

    void start();
    void stop();
    void join();

    Task* steal() { return dq_.steal(); }
    std::size_t id() const { return id_; }

private:
    void run();

    JobSystem* js_;
    std::size_t id_;

    alignas(64) ChaseLevDeque<Task> dq_;
    alignas(64) std::atomic<bool> stop_{false};

    std::thread thr_;
    uint64_t rng_{0};
};

} // namespace js