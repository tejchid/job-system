#include "worker.h"
#include "job_system.h"

namespace js {

static uint64_t splitmix64(uint64_t& x) {
    uint64_t z = (x += 0x9e3779b97f4a7c15ull);
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
    return z ^ (z >> 31);
}

Worker::Worker(JobSystem* js, std::size_t id)
    : js_(js), id_(id), rng_(0xdeadbeef ^ (id * 0x9e3779b97f4a7c15ull)) {}

Worker::~Worker() { stop(); join(); }

void Worker::start() { thr_ = std::thread(&Worker::run, this); }
void Worker::stop()  { stop_.store(true, std::memory_order_release); }
void Worker::join()  { if (thr_.joinable()) thr_.join(); }

void Worker::run() {
    while (!stop_.load(std::memory_order_acquire) &&
           !js_->is_shutting_down()) {

        bool did_work = false;

        // 1) Pull one from global queue
        if (Task* t = js_->try_dequeue()) {
            // push into local deque so it can be stolen
            if (!dq_.push_bottom(t)) {
                js_->execute_task(t, id_);
                did_work = true;
            } else {
                // Drain local deque (batchy locality)
                while (Task* local = dq_.pop_bottom()) {
                    js_->execute_task(local, id_);
                    did_work = true;

                    // opportunistically pull more work
                    if (Task* more = js_->try_dequeue()) {
                        if (!dq_.push_bottom(more)) {
                            js_->execute_task(more, id_);
                        }
                    }
                }
            }
        }

        // 2) Steal
        if (!did_work) {
            std::size_t n = js_->worker_count();
            if (n > 1) {
                std::size_t victim = splitmix64(rng_) % n;
                if (victim == id_) victim = (victim + 1) % n;
                if (Task* t = js_->try_steal(victim)) {
                    js_->execute_task(t, id_);
                    did_work = true;
                }
            }
        }

        // 3) Park
        if (!did_work) {
            js_->park_until_work_or_stop();
        }
    }
}

} // namespace js