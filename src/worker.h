#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <thread>

#include "deque.h"

namespace js {

struct Task;
class cppJobSystem;

class alignas(64) Worker {
public:
    Worker(cppJobSystem* js, std::size_t id);
    ~Worker();

    Worker(const Worker&) = delete;
    Worker& operator=(const Worker&) = delete;

    void start();
    void stop();
    void join();

    void push(Task* t);
    Task* pop();
    Task* steal();
    bool is_full() const { return dq_.is_full(); }

private:
    void run();

    cppJobSystem* js_{nullptr};
    std::size_t   id_{0};

    alignas(64) std::atomic<bool> stop_{false};
    alignas(64) ChaseLevDeque<Task> dq_;

    std::thread thr_;
    uint64_t    rng_state_{0};
};

} // namespace js