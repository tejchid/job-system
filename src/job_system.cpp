#include "job_system.h"
#include "worker.h"

#include <chrono>
#include <thread>

namespace js {

static std::size_t hw_threads() {
    unsigned n = std::thread::hardware_concurrency();
    return n ? n : 4;
}

JobSystem::JobSystem(std::size_t num_workers) {
    std::size_t n = (num_workers == 0) ? hw_threads() : num_workers;
    workers_.reserve(n);
    for (std::size_t i = 0; i < n; ++i) workers_.push_back(new Worker(this, i));
    for (auto* w : workers_) w->start();
}

JobSystem::~JobSystem() {
    shutdown();
    for (auto* w : workers_) delete w;
}

void JobSystem::shutdown() {
    bool expected = false;
    if (!shutting_down_.compare_exchange_strong(
            expected, true,
            std::memory_order_release,
            std::memory_order_relaxed)) {
        return;
    }

    // Wake everyone so they can observe shutdown
    submit_cv_.notify_all();

    for (auto* w : workers_) w->stop();
    for (auto* w : workers_) w->join();
}

std::size_t JobSystem::worker_count() const noexcept {
    return workers_.size();
}

Task* JobSystem::try_steal(std::size_t victim_id) {
    if (victim_id >= workers_.size()) return nullptr;
    return workers_[victim_id]->steal();
}

Task* JobSystem::try_dequeue() {
    std::unique_lock<std::mutex> lk(submit_m_, std::try_to_lock);
    if (!lk.owns_lock()) return nullptr;
    if (submit_q_.empty()) return nullptr;
    Task* t = submit_q_.front();
    submit_q_.pop();
    return t;
}

void JobSystem::execute_task(Task* t, std::size_t /*worker_id*/) {
    t->fn();
    on_complete(t);
}

void JobSystem::park_until_work_or_stop() {
    std::unique_lock<std::mutex> lk(submit_m_);
    submit_cv_.wait_for(lk, std::chrono::milliseconds(1), [&] {
        return shutting_down_.load(std::memory_order_acquire) || !submit_q_.empty();
    });
}

void JobSystem::enqueue_ready(Task* t) {
    {
        std::lock_guard<std::mutex> lk(submit_m_);
        submit_q_.push(t);
    }
    submit_cv_.notify_one();
}

TaskHandle JobSystem::submit_impl(std::function<void()> fn,
                                  const std::vector<Task*>& deps) {
    Task* t = new Task();
    t->fn = std::move(fn);
    t->deps_remaining.store(static_cast<int>(deps.size()), std::memory_order_relaxed);

    for (Task* d : deps) {
        if (!d) {
            t->deps_remaining.fetch_sub(1, std::memory_order_relaxed);
            continue;
        }

        std::unique_lock<std::mutex> lk(d->dep_m);
        if (d->done.load(std::memory_order_acquire)) {
            lk.unlock();
            t->deps_remaining.fetch_sub(1, std::memory_order_acq_rel);
        } else {
            d->dependents.push_back(t);
        }
    }

    if (t->deps_remaining.load(std::memory_order_acquire) == 0) {
        enqueue_ready(t);
    }

    return TaskHandle{t};
}

// IMPORTANT: done=true must be published only after dependents are processed,
// otherwise wait() can delete t while this function is still using it.
void JobSystem::on_complete(Task* t) {
    std::vector<Task*> children;
    {
        std::lock_guard<std::mutex> lk(t->dep_m);
        children.swap(t->dependents);
    }

    for (Task* child : children) {
        if (!child) continue;
        int prev = child->deps_remaining.fetch_sub(1, std::memory_order_acq_rel);
        if (prev == 1) enqueue_ready(child);
    }

    // Publish completion last.
    t->done.store(true, std::memory_order_release);

    // Wake waiters.
    {
        std::lock_guard<std::mutex> lk(t->wait_m);
        t->wait_cv.notify_all();
    }
}

void JobSystem::wait(TaskHandle h) {
    Task* t = h.t;
    if (!t) return;

    // Wait until done OR shutdown. If shutdown happens first, don't delete (avoid UAF).
    if (!t->done.load(std::memory_order_acquire)) {
        std::unique_lock<std::mutex> lk(t->wait_m);
        t->wait_cv.wait(lk, [&] {
            return t->done.load(std::memory_order_acquire) ||
                   shutting_down_.load(std::memory_order_acquire);
        });
    }

    if (!t->done.load(std::memory_order_acquire)) {
        // shutdown occurred before completion; safe leak > use-after-free
        return;
    }

    delete t;
}

} // namespace js