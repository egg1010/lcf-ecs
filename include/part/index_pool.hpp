#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <new>

// index_pool - 固定容量无锁索引池（线程安全，与 part 单线程容器区分）
// 64 位头打包 (next_index, generation)，防 ABA；allocate/release 均为一次 CAS

class index_pool
{
public:
    static constexpr std::uint32_t none = 0xFFFFFFFFu;

    static std::uint64_t pack(std::uint32_t next_index, std::uint32_t generation) noexcept
    {
        return (static_cast<std::uint64_t>(generation) << 32) |
               (static_cast<std::uint64_t>(next_index) + 1ull);
    }

    static std::uint32_t next_of(std::uint64_t head) noexcept
    {
        return static_cast<std::uint32_t>(head & 0xFFFFFFFFull) - 1u;
    }

    static std::uint32_t generation_of(std::uint64_t head) noexcept
    {
        return static_cast<std::uint32_t>(head >> 32);
    }

    index_pool() noexcept = default;

    index_pool(const index_pool&) = delete;
    index_pool& operator=(const index_pool&) = delete;

    ~index_pool()
    {
        destroy();
    }

    bool create(std::uint32_t capacity) noexcept
    {
        if (next_ != nullptr || capacity == 0)
        {
            return false;
        }
        next_ = static_cast<std::uint32_t*>(
            ::operator new(sizeof(std::uint32_t) * capacity, std::nothrow));
        if (next_ == nullptr)
        {
            return false;
        }
        for (std::uint32_t i = 0; i < capacity; ++i)
        {
            next_[i] = (i + 1u < capacity) ? i + 1u : none;
        }
        capacity_ = capacity;
        count_.store(static_cast<std::int64_t>(capacity), std::memory_order_release);
        head_.store(pack(0, 0), std::memory_order_release);
        return true;
    }

    void destroy() noexcept
    {
        std::uint32_t* memory = next_;
        next_ = nullptr;
        capacity_ = 0;
        count_.store(0, std::memory_order_relaxed);
        head_.store(pack(none, 0), std::memory_order_relaxed);
        if (memory != nullptr)
        {
            ::operator delete(static_cast<void*>(memory));
        }
    }

    bool created() const noexcept
    {
        return next_ != nullptr;
    }

    bool allocate(std::uint32_t& out_index) noexcept
    {
        std::uint64_t head = head_.load(std::memory_order_acquire);
        for (;;)
        {
            const std::uint32_t index = next_of(head);
            if (index == none || index >= capacity_)
            {
                return false;
            }
            const std::uint32_t next = next_[index];
            const std::uint64_t wanted = pack(next, generation_of(head) + 1u);
            if (!head_.compare_exchange_weak(head, wanted, std::memory_order_acq_rel,
                                             std::memory_order_acquire))
            {
                continue;
            }
            out_index = index;
            count_.fetch_sub(1, std::memory_order_relaxed);
            return true;
        }
    }

    void release(std::uint32_t index) noexcept
    {
        if (index >= capacity_)
        {
            return;
        }
        std::uint64_t head = head_.load(std::memory_order_acquire);
        for (;;)
        {
            const std::uint32_t generation = generation_of(head);
            next_[index] = next_of(head);
            const std::uint64_t wanted = pack(index, generation + 1u);
            if (head_.compare_exchange_weak(head, wanted, std::memory_order_acq_rel,
                                            std::memory_order_acquire))
            {
                count_.fetch_add(1, std::memory_order_relaxed);
                return;
            }
        }
    }

    std::int64_t available() const noexcept
    {
        return count_.load(std::memory_order_acquire);
    }

    std::uint32_t capacity() const noexcept
    {
        return capacity_;
    }

private:
    std::uint32_t* next_{nullptr};
    std::uint32_t capacity_{0};
    std::atomic<std::uint64_t> head_{pack(none, 0)};
    std::atomic<std::int64_t> count_{0};
};
