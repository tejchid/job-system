#pragma once

#include <atomic>
#include <cstddef>
#include <cassert>

template <typename T, std::size_t kCapacity = 1 << 17>
class ChaseLevDeque
{
public:
    ChaseLevDeque()
        : top_(0),
          bottom_(0)
    {}

    bool is_empty() const
    {
        const std::size_t b = bottom_.load(std::memory_order_relaxed);
        const std::size_t t = top_.load(std::memory_order_acquire);
        return t >= b;
    }

    bool is_full() const
    {
        const std::size_t b = bottom_.load(std::memory_order_relaxed);
        const std::size_t t = top_.load(std::memory_order_acquire);
        return (b - t) >= (kCapacity - 1);
    }

void push_bottom(T* item)
{
    std::size_t b, t;
    do {
        b = bottom_.load(std::memory_order_relaxed);
        t = top_.load(std::memory_order_acquire);
        if ((b - t) < (kCapacity - 1)) break;
        std::this_thread::yield();
    } while (true);

    buffer_[b % kCapacity] = item;
    bottom_.store(b + 1, std::memory_order_release);
}

    T* pop_bottom()
    {
        std::size_t b = bottom_.load(std::memory_order_relaxed);
        if (b == 0)
            return nullptr;

        b -= 1;
        bottom_.store(b, std::memory_order_relaxed);

        std::atomic_thread_fence(std::memory_order_seq_cst);

        std::size_t t = top_.load(std::memory_order_relaxed);

        if (t <= b)
        {
            T* item = buffer_[b % kCapacity];
            if (t == b)
            {
                if (!top_.compare_exchange_strong(t, t + 1,
                                                  std::memory_order_seq_cst,
                                                  std::memory_order_relaxed))
                {
                    item = nullptr;
                }
                bottom_.store(b + 1, std::memory_order_relaxed);
            }
            return item;
        }
        else
        {
            bottom_.store(b + 1, std::memory_order_relaxed);
            return nullptr;
        }
    }

    T* steal()
    {
        std::size_t t = top_.load(std::memory_order_acquire);
        std::atomic_thread_fence(std::memory_order_seq_cst);
        std::size_t b = bottom_.load(std::memory_order_acquire);

        if (t < b)
        {
            T* item = buffer_[t % kCapacity];
            if (top_.compare_exchange_strong(t, t + 1,
                                             std::memory_order_seq_cst,
                                             std::memory_order_relaxed))
            {
                return item;
            }
        }
        return nullptr;
    }

private:
    T* buffer_[kCapacity]{};

    alignas(64) std::atomic<std::size_t> top_;
    alignas(64) std::atomic<std::size_t> bottom_;
};