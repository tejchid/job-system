#include "job_system.h"
#include "worker.h"
#include <thread>

namespace js {

static std::size_t default_thread_count() {
    unsigned hw = std::thread::hardware_concurrency();
    return hw ? static_cast<std::size_t>(hw) : 4;
}

cppJobSystem::cppJobSystem() : cppJobSystem(default_thread_count()) {}

cppJobSystem::cppJobSystem(std::size_t threads) {
    std::size_t n = (threads == 0) ? default_thread_count() : threads;
    workers_.reserve(n);
    for (std::size_t i = 0; i < n; ++i)
        workers_.push_back(new Worker(this, i));
    for (auto* w : workers_) w->start();
}

cppJobSystem::~cppJobSystem() {
    shutdown();
    for (auto* w : workers_) delete w;
    workers_.clear();
}

void cppJobSystem::shutdown() {
    bool expected = false;
    if (!shutting_down_.compare_exchange_strong(expected, true,
            std::memory_order_release, std::memory_order_relaxed))
        return;
    { std::lock_guard<std::mutex> lk(park_.m); park_.cv.notify_all(); }
    for (auto* w : workers_) w->stop();
    for (auto* w : workers_) w->join();
}

std::size_t cppJobSystem::worker_count() const noexcept {
    return workers_.size();
}

Task* cppJobSystem::worker_steal(std::size_t victim) {
    if (victim >= workers_.size()) return nullptr;
    return workers_[victim]->steal();
}

void cppJobSystem::execute_task(Task* t, std::size_t worker_id) {
    t->fn();
    runnable_.fetch_sub(1, std::memory_order_release);
    on_task_complete(t, worker_id);
}

void cppJobSystem::park_until_work_or_stop() {
    if (shutting_down_.load(std::memory_order_acquire)) return;
    std::unique_lock<std::mutex> lk(park_.m);
    park_.cv.wait(lk, [&] {
        return shutting_down_.load(std::memory_order_acquire) ||
               runnable_.load(std::memory_order_acquire) > 0;
    });
}

void cppJobSystem::notify_one_worker() {
    std::lock_guard<std::mutex> lk(park_.m);
    park_.cv.notify_one();
}

void cppJobSystem::enqueue_ready(Task* t, std::size_t preferred_worker) {
    std::size_t n = workers_.size();
    if (n == 0) return;

    static std::atomic<std::size_t> rr{0};
    std::size_t start = rr.fetch_add(1, std::memory_order_relaxed) % n;

    for (;;) {
        for (std::size_t i = 0; i < n; ++i) {
            std::size_t idx = (start + i) % n;
            if (!workers_[idx]->is_full()) {
                workers_[idx]->push(t);
                runnable_.fetch_add(1, std::memory_order_release);
                notify_one_worker();
                return;
            }
        }
        std::this_thread::yield();
    }
}

TaskHandle cppJobSystem::submit_impl(std::function<void()> fn,
                                      std::span<Task* const> deps) {
    Task* t = new Task();
    t->fn = std::move(fn);
    t->deps_remaining.store(static_cast<int>(deps.size()),
                             std::memory_order_relaxed);

    for (Task* d : deps) {
        if (!d) { t->deps_remaining.fetch_sub(1, std::memory_order_relaxed); continue; }
        std::unique_lock<std::mutex> lk(d->dep_m);
        if (d->done.load(std::memory_order_acquire)) {
            lk.unlock();
            t->deps_remaining.fetch_sub(1, std::memory_order_acq_rel);
        } else {
            d->dependents.push_back(t);
        }
    }

    if (t->deps_remaining.load(std::memory_order_acquire) == 0)
        enqueue_ready(t, 0);

    return TaskHandle{t};
}

void cppJobSystem::on_task_complete(Task* t, std::size_t completing_worker) {
    std::vector<Task*> children;
    {
        std::lock_guard<std::mutex> lk(t->dep_m);
        t->done.store(true, std::memory_order_release);
        children.swap(t->dependents);
    }
    for (Task* child : children) {
        if (!child) continue;
        int prev = child->deps_remaining.fetch_sub(1, std::memory_order_acq_rel);
        if (prev == 1) enqueue_ready(child, completing_worker);
    }
    { std::lock_guard<std::mutex> lk(t->wait_m); t->wait_cv.notify_all(); }
}

void cppJobSystem::wait(TaskHandle h) {
    Task* t = h.t;
    if (!t) return;
    if (!t->done.load(std::memory_order_acquire)) {
        std::unique_lock<std::mutex> lk(t->wait_m);
        t->wait_cv.wait(lk, [&] {
            return shutting_down_.load(std::memory_order_acquire) ||
                   t->done.load(std::memory_order_acquire);
        });
    }
    delete t;
}

} // namespace js