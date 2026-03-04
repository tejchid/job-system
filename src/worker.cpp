#include "worker.h"
#include "job_system.h"

namespace js {

static inline uint64_t splitmix64(uint64_t& x) {
    uint64_t z = (x += 0x9e3779b97f4a7c15ull);
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
    return z ^ (z >> 31);
}

Worker::Worker(cppJobSystem* js, std::size_t id)
    : js_(js),
      id_(id),
      rng_state_(0x123456789abcdef0ull ^ (id * 0x9e3779b97f4a7c15ull)) {}

Worker::~Worker() {
    stop();
    join();
}

void Worker::start() { thr_ = std::thread(&Worker::run, this); }

void Worker::stop() { stop_.store(true, std::memory_order_release); }

void Worker::join() {
    if (thr_.joinable()) thr_.join();
}

void Worker::push(Task* t)   { dq_.push_bottom(t); }
Task* Worker::pop()          { return dq_.pop_bottom(); }
Task* Worker::steal()        { return dq_.steal(); }

void Worker::run() {
    while (true) {
        if (stop_.load(std::memory_order_acquire)) break;
        if (js_->is_shutting_down()) break;

        // 1) local work
        if (Task* t = pop()) {
            js_->execute_task(t, id_);
            continue;
        }

        // 2) steal from random victims (bounded attempts)
        std::size_t n = js_->worker_count();
        if (n > 1) {
            for (std::size_t tries = 0; tries < n - 1; ++tries) {
                uint64_t r = splitmix64(rng_state_);
                std::size_t victim = static_cast<std::size_t>(r % n);
                if (victim == id_) victim = (victim + 1) % n;

                if (Task* t = js_->worker_steal(victim)) {
                    js_->execute_task(t, id_);
                    goto next_iter;
                }
            }
        }

        // 3) park (no busy spin)
        js_->park_until_work_or_stop();

    next_iter:
        continue;
    }
}

} // namespace js