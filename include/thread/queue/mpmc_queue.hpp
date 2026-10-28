#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <new>

#include "part/bit_ops.hpp"
#include "part/operating_message.hpp"

namespace thread
{
inline constexpr std::size_t mpmc_default_capacity = 4096;
inline constexpr std::size_t mpmc_max_capacity = 1u << 22;
inline constexpr std::uint32_t mpmc_index_none = 0xFFFFFFFFu;

struct mpmc_stats
{
    std::atomic<std::uint64_t> pushes_{0};
    std::atomic<std::uint64_t> pops_{0};
    std::atomic<std::uint64_t> push_full_{0};
    std::atomic<std::uint64_t> contended_{0};
};


struct mpmc_queue
{
    struct slot
    {
        std::atomic<std::uint64_t> sequence_{0};
        std::uint32_t value_{mpmc_index_none};
        std::uint32_t reserved_{0};
    };

    mpmc_queue() noexcept = default;

    mpmc_queue(const mpmc_queue&) = delete;
    mpmc_queue& operator=(const mpmc_queue&) = delete;

    ~mpmc_queue()
    {
        destroy();
    }

    bool create(std::size_t capacity_hint) noexcept
    {
        if (slots_ != nullptr)
        {
            return false;
        }
        const std::size_t capacity =
            round_up_power_of_two(capacity_hint == 0 ? mpmc_default_capacity : capacity_hint);
        if (capacity < 2)
        {
            return false;
        }
        auto* memory = static_cast<slot*>(::operator new(sizeof(slot) * capacity, std::nothrow));
        if (memory == nullptr)
        {
            return false;
        }
        for (std::size_t i = 0; i < capacity; ++i)
        {
            new (&memory[i]) slot{};
            memory[i].sequence_.store(static_cast<std::uint64_t>(i), std::memory_order_relaxed);
        }
        capacity_ = capacity;
        mask_ = capacity - 1;
        head_.store(0, std::memory_order_relaxed);
        tail_.store(0, std::memory_order_relaxed);
        slots_ = memory;
        return true;
    }

    void destroy() noexcept
    {
        slot* slots = slots_;
        slots_ = nullptr;
        capacity_ = 0;
        mask_ = 0;
        head_.store(0, std::memory_order_relaxed);
        tail_.store(0, std::memory_order_relaxed);
        if (slots != nullptr)
        {
            ::operator delete(static_cast<void*>(slots));
        }
    }

    bool created() const noexcept
    {
        return slots_ != nullptr;
    }

    std::size_t capacity() const noexcept
    {
        return capacity_;
    }

    std::int64_t size() const noexcept
    {
        const std::uint64_t head = head_.load(std::memory_order_acquire);
        const std::uint64_t tail = tail_.load(std::memory_order_acquire);
        if (tail <= head)
        {
            return 0;
        }
        return static_cast<std::int64_t>(tail - head);
    }

    bool empty() const noexcept
    {
        return head_.load(std::memory_order_acquire) == tail_.load(std::memory_order_acquire);
    }

    bool try_push(std::uint32_t value) noexcept
    {
        if (value == mpmc_index_none)
        {
            return false;
        }
        std::uint64_t tail = tail_.load(std::memory_order_relaxed);
        for (;;)
        {
            slot& cell = slots_[static_cast<std::size_t>(tail) & mask_];
            const std::uint64_t sequence = cell.sequence_.load(std::memory_order_acquire);
            const std::int64_t diff =
                static_cast<std::int64_t>(sequence) - static_cast<std::int64_t>(tail);
            if (diff == 0)
            {
                if (tail_.compare_exchange_weak(tail, tail + 1, std::memory_order_relaxed,
                                                std::memory_order_relaxed))
                {
                    cell.value_ = value;
                    cell.sequence_.store(tail + 1, std::memory_order_release);
                    stats_.pushes_.fetch_add(1, std::memory_order_relaxed);
                    return true;
                }
                stats_.contended_.fetch_add(1, std::memory_order_relaxed);
                continue;
            }
            if (diff < 0)
            {
                stats_.push_full_.fetch_add(1, std::memory_order_relaxed);
                return false;
            }
            tail = tail_.load(std::memory_order_relaxed);
        }
    }

    bool try_pop(std::uint32_t& out) noexcept
    {
        std::uint64_t head = head_.load(std::memory_order_relaxed);
        for (;;)
        {
            slot& cell = slots_[static_cast<std::size_t>(head) & mask_];
            const std::uint64_t sequence = cell.sequence_.load(std::memory_order_acquire);
            const std::int64_t diff =
                static_cast<std::int64_t>(sequence) - static_cast<std::int64_t>(head + 1);
            if (diff == 0)
            {
                if (head_.compare_exchange_weak(head, head + 1, std::memory_order_relaxed,
                                                std::memory_order_relaxed))
                {
                    out = cell.value_;
                    cell.sequence_.store(head + capacity_, std::memory_order_release);
                    stats_.pops_.fetch_add(1, std::memory_order_relaxed);
                    return true;
                }
                stats_.contended_.fetch_add(1, std::memory_order_relaxed);
                continue;
            }
            if (diff < 0)
            {
                return false;
            }
            head = head_.load(std::memory_order_relaxed);
        }
    }

    std::size_t try_pop_bulk(std::uint32_t* out, std::size_t max_count) noexcept
    {
        std::size_t count = 0;
        while (count < max_count)
        {
            std::uint32_t value = mpmc_index_none;
            if (!try_pop(value))
            {
                break;
            }
            out[count] = value;
            ++count;
        }
        return count;
    }

    mpmc_stats& stats() noexcept
    {
        return stats_;
    }

  private:
    static_assert(sizeof(std::atomic<std::uint64_t>) == 8, "atomic u64 must be 8 bytes");

    slot* slots_{nullptr};
    std::size_t capacity_{0};
    std::size_t mask_{0};
    std::atomic<std::uint64_t> head_{0};
    [[maybe_unused]] std::uint64_t head_pad_[7]{};
    std::atomic<std::uint64_t> tail_{0};
    [[maybe_unused]] std::uint64_t tail_pad_[7]{};
    mpmc_stats stats_{};
};
} // namespace thread
