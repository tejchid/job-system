#pragma once

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <functional>
#include <initializer_list>
#include <mutex>
#include <queue>
#include <vector>

namespace js {

class Worker;

struct alignas(64) Task {
    std::function<void()> fn;

    // Dependency tracking
    std::atomic<int> deps_remaining{0};
    std::mutex       dep_m;
    std::vector<Task*> dependents;

    // Completion tracking
    std::atomic<bool> done{false};
    std::mutex        wait_m;
    std::condition_variable wait_cv;
};

struct TaskHandle {
    Task* t{nullptr};
    constexpr TaskHandle() = default;
    constexpr explicit TaskHandle(Task* p) : t(p) {}
    constexpr explicit operator bool() const noexcept { return t != nullptr; }
};

class JobSystem {
public:
    explicit JobSystem(std::size_t num_workers = 0);
    ~JobSystem();

    JobSystem(const JobSystem&) = delete;
    JobSystem& operator=(const JobSystem&) = delete;

    template <typename F>
    TaskHandle submit(F&& f) {
        return submit_impl(std::function<void()>(std::forward<F>(f)), {});
    }

    template <typename F>
    TaskHandle submit(F&& f, std::initializer_list<TaskHandle> deps) {
        std::vector<Task*> raw;
        raw.reserve(deps.size());
        for (auto const& h : deps) raw.push_back(h.t);
        return submit_impl(std::function<void()>(std::forward<F>(f)), raw);
    }

    // Blocks until task is done. Frees the task (single-owner handle semantics).
    // If shutdown occurs before completion, it returns WITHOUT deleting the task (safe leak > UAF).
    void wait(TaskHandle h);

    void shutdown();
    bool is_shutting_down() const noexcept {
        return shutting_down_.load(std::memory_order_acquire);
    }

    std::size_t worker_count() const noexcept;

    // Called by workers
    Task* try_steal(std::size_t victim_id);
    Task* try_dequeue();

    void execute_task(Task* t, std::size_t worker_id);
    void park_until_work_or_stop();

private:
    TaskHandle submit_impl(std::function<void()> fn, const std::vector<Task*>& deps);
    void enqueue_ready(Task* t);
    void on_complete(Task* t);

    std::atomic<bool> shutting_down_{false};
    std::vector<Worker*> workers_;

    // Shared submission queue
    alignas(64) std::mutex submit_m_;
    std::queue<Task*> submit_q_;
    std::condition_variable submit_cv_;
};

} // namespace js