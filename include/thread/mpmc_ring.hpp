#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <new>
#include <type_traits>

namespace thread
{

// ---------------------------------------------------------------------------
// 有界 MPMC 环（Vyukov 算法）：元素要求平凡可拷贝，用于票据与命令交接
// ---------------------------------------------------------------------------

inline constexpr std::size_t ring_default_capacity = 1024;
inline constexpr std::size_t ring_max_capacity = 1u << 22;

inline std::size_t ring_round_up(std::size_t value) noexcept
{
    const std::size_t rounded = round_up_power_of_two(value);
    return rounded < ring_max_capacity ? rounded : ring_max_capacity;
}

template <typename T>
struct mpmc_ring
{
    static_assert(std::is_trivially_copyable_v<T>, "mpmc_ring element must be trivially copyable");

    struct cell
    {
        std::atomic<std::uint64_t> sequence_{0};
        T value_{};
    };

    mpmc_ring() noexcept = default;

    mpmc_ring(const mpmc_ring&) = delete;
    mpmc_ring& operator=(const mpmc_ring&) = delete;

    ~mpmc_ring()
    {
        destroy();
    }

    bool create(std::size_t capacity_hint) noexcept
    {
        if (cells_ != nullptr)
        {
            return false;
        }
        const std::size_t capacity = ring_round_up(capacity_hint == 0 ? ring_default_capacity
                                                                      : capacity_hint);
        if (capacity < 2)
        {
            return false;
        }
        auto* memory = static_cast<cell*>(::operator new(sizeof(cell) * capacity, std::nothrow));
        if (memory == nullptr)
        {
            return false;
        }
        for (std::size_t i = 0; i < capacity; ++i)
        {
            new (&memory[i]) cell{};
            memory[i].sequence_.store(static_cast<std::uint64_t>(i), std::memory_order_relaxed);
        }
        cells_ = memory;
        capacity_ = capacity;
        mask_ = capacity - 1;
        head_.store(0, std::memory_order_relaxed);
        tail_.store(0, std::memory_order_relaxed);
        return true;
    }

    void destroy() noexcept
    {
        cell* cells = cells_;
        cells_ = nullptr;
        capacity_ = 0;
        mask_ = 0;
        head_.store(0, std::memory_order_relaxed);
        tail_.store(0, std::memory_order_relaxed);
        if (cells != nullptr)
        {
            ::operator delete(static_cast<void*>(cells));
        }
    }

    std::size_t capacity() const noexcept
    {
        return capacity_;
    }

    std::int64_t size() const noexcept
    {
        const std::uint64_t head = head_.load(std::memory_order_acquire);
        const std::uint64_t tail = tail_.load(std::memory_order_acquire);
        return tail <= head ? 0 : static_cast<std::int64_t>(tail - head);
    }

    bool empty() const noexcept
    {
        return size() == 0;
    }

    bool try_push(const T& value) noexcept
    {
        if (cells_ == nullptr)
        {
            return false;
        }
        std::uint64_t tail = tail_.load(std::memory_order_relaxed);
        for (;;)
        {
            cell& target = cells_[static_cast<std::size_t>(tail) & mask_];
            const std::uint64_t sequence = target.sequence_.load(std::memory_order_acquire);
            const std::int64_t diff =
                static_cast<std::int64_t>(sequence) - static_cast<std::int64_t>(tail);
            if (diff == 0)
            {
                if (tail_.compare_exchange_weak(tail, tail + 1, std::memory_order_relaxed,
                                                std::memory_order_relaxed))
                {
                    target.value_ = value;
                    target.sequence_.store(tail + 1, std::memory_order_release);
                    return true;
                }
                continue;
            }
            if (diff < 0)
            {
                return false;
            }
            tail = tail_.load(std::memory_order_relaxed);
        }
    }

    bool try_pop(T& out) noexcept
    {
        if (cells_ == nullptr)
        {
            return false;
        }
        std::uint64_t head = head_.load(std::memory_order_relaxed);
        for (;;)
        {
            cell& target = cells_[static_cast<std::size_t>(head) & mask_];
            const std::uint64_t sequence = target.sequence_.load(std::memory_order_acquire);
            const std::int64_t diff =
                static_cast<std::int64_t>(sequence) - static_cast<std::int64_t>(head + 1);
            if (diff == 0)
            {
                if (head_.compare_exchange_weak(head, head + 1, std::memory_order_relaxed,
                                                std::memory_order_relaxed))
                {
                    out = target.value_;
                    target.sequence_.store(head + capacity_, std::memory_order_release);
                    return true;
                }
                continue;
            }
            if (diff < 0)
            {
                return false;
            }
            head = head_.load(std::memory_order_relaxed);
        }
    }

    std::size_t try_pop_bulk(T* out, std::size_t max_count) noexcept
    {
        std::size_t count = 0;
        while (count < max_count)
        {
            if (!try_pop(out[count]))
            {
                break;
            }
            ++count;
        }
        return count;
    }

  private:
    cell* cells_{nullptr};
    std::size_t capacity_{0};
    std::size_t mask_{0};
    std::atomic<std::uint64_t> head_{0};
    [[maybe_unused]] std::uint64_t head_pad_[7]{};
    std::atomic<std::uint64_t> tail_{0};
    [[maybe_unused]] std::uint64_t tail_pad_[7]{};
};

} // namespace thread
