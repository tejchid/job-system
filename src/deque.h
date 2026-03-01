#pragma once

#include <atomic>
#include <cstddef>

// Chase-Lev work-stealing deque.
// Owner thread calls push_bottom and pop_bottom.
// Thief threads call steal.

template <typename T, std::size_t kCapacity = 1u << 14>
class ChaseLevDeque {
    static_assert((kCapacity & (kCapacity - 1)) == 0, "capacity must be power of 2");
    static constexpr std::size_t kMask = kCapacity - 1;

public:
    ChaseLevDeque() : top_(0), bottom_(0) {
        for (std::size_t i = 0; i < kCapacity; ++i) buffer_[i] = nullptr;
    }

    // Owner only
    bool push_bottom(T* item) {
        std::size_t b = bottom_.load(std::memory_order_relaxed);
        std::size_t t = top_.load(std::memory_order_acquire);
        if (b - t >= kCapacity - 1) return false;
        buffer_[b & kMask] = item;
        std::atomic_thread_fence(std::memory_order_release);
        bottom_.store(b + 1, std::memory_order_relaxed);
        return true;
    }

    // Owner only
    T* pop_bottom() {
        std::size_t b = bottom_.load(std::memory_order_relaxed);
        if (b == 0) return nullptr;

        b -= 1;
        bottom_.store(b, std::memory_order_relaxed);
        std::atomic_thread_fence(std::memory_order_seq_cst);

        std::size_t t = top_.load(std::memory_order_relaxed);
        if (t <= b) {
            T* item = buffer_[b & kMask];
            if (t == b) {
                if (!top_.compare_exchange_strong(
                        t, t + 1,
                        std::memory_order_seq_cst,
                        std::memory_order_relaxed)) {
                    item = nullptr;
                }
                bottom_.store(b + 1, std::memory_order_relaxed);
            }
            return item;
        }

        bottom_.store(b + 1, std::memory_order_relaxed);
        return nullptr;
    }

    // Thief only
    T* steal() {
        std::size_t t = top_.load(std::memory_order_acquire);
        std::atomic_thread_fence(std::memory_order_seq_cst);
        std::size_t b = bottom_.load(std::memory_order_acquire);
        if (t >= b) return nullptr;

        T* item = buffer_[t & kMask];
        if (!top_.compare_exchange_strong(
                t, t + 1,
                std::memory_order_seq_cst,
                std::memory_order_relaxed)) {
            return nullptr;
        }
        return item;
    }

private:
    alignas(64) std::atomic<std::size_t> top_;
    alignas(64) std::atomic<std::size_t> bottom_;
    T* buffer_[kCapacity];
};