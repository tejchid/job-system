#pragma once

#include <atomic>
#include <cstddef>
#include <condition_variable>
#include <functional>
#include <initializer_list>
#include <mutex>
#include <span>
#include <utility>
#include <vector>

namespace js {

class Worker;
struct Task;

struct TaskHandle {
    Task* t{nullptr};
    constexpr TaskHandle() = default;
    constexpr explicit TaskHandle(Task* p) : t(p) {}
    constexpr explicit operator bool() const noexcept { return t != nullptr; }
};

struct alignas(64) Task {
    std::function<void()> fn;
    std::atomic<int> deps_remaining{0};
    std::mutex dep_m;
    std::vector<Task*> dependents;
    std::atomic<bool> done{false};
    std::mutex wait_m;
    std::condition_variable wait_cv;
    char pad[64]{};
};

class cppJobSystem {
public:
    cppJobSystem();
    explicit cppJobSystem(std::size_t num_workers);
    ~cppJobSystem();

    cppJobSystem(const cppJobSystem&) = delete;
    cppJobSystem& operator=(const cppJobSystem&) = delete;

    template <typename F>
    TaskHandle submit(F&& f) {
        return submit_impl(std::function<void()>(std::forward<F>(f)),
                           std::span<Task* const>{});
    }

    template <typename F>
    TaskHandle submit(F&& f, std::span<TaskHandle const> deps) {
        std::vector<Task*> raw;
        raw.reserve(deps.size());
        for (auto const& h : deps) raw.push_back(h.t);
        return submit_impl(std::function<void()>(std::forward<F>(f)),
                           std::span<Task* const>(raw.data(), raw.size()));
    }

    template <typename F>
    TaskHandle submit(F&& f, std::initializer_list<TaskHandle> deps) {
        std::vector<Task*> raw;
        raw.reserve(deps.size());
        for (auto const& h : deps) raw.push_back(h.t);
        return submit_impl(std::function<void()>(std::forward<F>(f)),
                           std::span<Task* const>(raw.data(), raw.size()));
    }

    void wait(TaskHandle h);
    void shutdown();

    bool is_shutting_down() const noexcept {
        return shutting_down_.load(std::memory_order_acquire);
    }

    std::size_t worker_count() const noexcept;
    Task* worker_steal(std::size_t victim);
    void execute_task(Task* t, std::size_t worker_id);
    void park_until_work_or_stop();
    void notify_one_worker();

private:
    friend class Worker;

    TaskHandle submit_impl(std::function<void()> fn, std::span<Task* const> deps);
    void enqueue_ready(Task* t, std::size_t preferred_worker);
    void on_task_complete(Task* t, std::size_t completing_worker);

    std::atomic<bool> shutting_down_{false};
    std::atomic<int>  runnable_{0};
    std::vector<Worker*> workers_;

    struct alignas(64) ParkState {
        std::mutex m;
        std::condition_variable cv;
        char pad[64]{};
    };
    ParkState park_;
};

} // namespace js