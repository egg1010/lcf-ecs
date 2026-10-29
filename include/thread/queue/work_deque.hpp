#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <new>

#include "part/bit_ops.hpp"
#include "part/operating_message.hpp"
#include "thread/base/job.hpp"

namespace thread
{
inline constexpr std::size_t work_deque_default_capacity = 1024;
inline constexpr std::size_t work_deque_max_capacity = 1u << 20;

struct work_deque_buffer
{
    std::atomic<std::int64_t> capacity_{0};
    std::atomic<work_deque_buffer*> retired_next_{nullptr};
    std::atomic<job*> slots_[1];
};

struct work_deque_stats
{
    std::atomic<std::uint64_t> pushes_{0};
    std::atomic<std::uint64_t> pops_{0};
    std::atomic<std::uint64_t> steals_{0};
    std::atomic<std::uint64_t> steal_attempts_{0};
    std::atomic<std::uint64_t> grow_events_{0};
    std::atomic<std::uint64_t> stolen_from_{0};
    std::atomic<std::uint64_t> steal_aborts_{0};
    std::atomic<std::uint64_t> drops_{0};
};


inline work_deque_buffer* work_deque_make_buffer(std::size_t capacity)
{
    const std::size_t bytes =
        sizeof(work_deque_buffer) + sizeof(std::atomic<job*>) * (capacity - 1);
    auto* memory = static_cast<unsigned char*>(::operator new(bytes, std::nothrow));
    if (memory == nullptr)
    {
        return nullptr;
    }
    auto* buffer = new (memory) work_deque_buffer{};
    buffer->capacity_.store(static_cast<std::int64_t>(capacity), std::memory_order_relaxed);
    for (std::size_t i = 0; i < capacity; ++i)
    {
        new (&buffer->slots_[i]) std::atomic<job*>(nullptr);
    }
    return buffer;
}

inline void work_deque_free_buffer(work_deque_buffer* buffer) noexcept
{
    ::operator delete(static_cast<void*>(buffer));
}

struct work_deque
{
    std::atomic<std::int64_t> top_{0};
    std::atomic<std::int64_t> bottom_{0};
    std::atomic<work_deque_buffer*> buffer_{nullptr};
    std::size_t capacity_{0};
    std::size_t mask_{0};
    work_deque_stats stats_{};

    work_deque() noexcept = default;

    work_deque(const work_deque&) = delete;
    work_deque& operator=(const work_deque&) = delete;

    work_deque(work_deque&& other) noexcept
    {
        steal_state_from(other);
    }

    work_deque& operator=(work_deque&& other) noexcept
    {
        if (this != &other)
        {
            destroy();
            steal_state_from(other);
        }
        return *this;
    }

    ~work_deque()
    {
        destroy();
    }

    bool create(std::size_t capacity_hint) noexcept
    {
        if (buffer_.load(std::memory_order_relaxed) != nullptr)
        {
            return false;
        }
        const std::size_t capacity =
            round_up_power_of_two(capacity_hint == 0 ? work_deque_default_capacity : capacity_hint);
        if (capacity < 2)
        {
            return false;
        }
        work_deque_buffer* buffer = work_deque_make_buffer(capacity);
        if (buffer == nullptr)
        {
            return false;
        }
        capacity_ = capacity;
        mask_ = capacity - 1;
        top_.store(0, std::memory_order_relaxed);
        bottom_.store(0, std::memory_order_relaxed);
        buffer_.store(buffer, std::memory_order_release);
        return true;
    }

    void destroy() noexcept
    {
        work_deque_buffer* buffer = buffer_.exchange(nullptr, std::memory_order_acq_rel);
        if (buffer != nullptr)
        {
            work_deque_free_buffer(buffer);
        }
        work_deque_buffer* retired = retired_.exchange(nullptr, std::memory_order_acq_rel);
        while (retired != nullptr)
        {
            work_deque_buffer* next = retired->retired_next_.load(std::memory_order_relaxed);
            work_deque_free_buffer(retired);
            retired = next;
        }
        capacity_ = 0;
        mask_ = 0;
        top_.store(0, std::memory_order_relaxed);
        bottom_.store(0, std::memory_order_relaxed);
    }

    void steal_state_from(work_deque& other) noexcept
    {
        capacity_ = other.capacity_;
        mask_ = other.mask_;
        other.capacity_ = 0;
        other.mask_ = 0;
        top_.store(other.top_.load(std::memory_order_relaxed), std::memory_order_relaxed);
        bottom_.store(other.bottom_.load(std::memory_order_relaxed), std::memory_order_relaxed);
        buffer_.store(other.buffer_.exchange(nullptr, std::memory_order_acq_rel),
                      std::memory_order_relaxed);
        retired_.store(other.retired_.exchange(nullptr, std::memory_order_acq_rel),
                       std::memory_order_relaxed);
        other.top_.store(0, std::memory_order_relaxed);
        other.bottom_.store(0, std::memory_order_relaxed);
    }

    bool empty() const noexcept
    {
        return size() <= 0;
    }

    std::int64_t size() const noexcept
    {
        const std::int64_t bottom = bottom_.load(std::memory_order_acquire);
        const std::int64_t top = top_.load(std::memory_order_acquire);
        return bottom - top;
    }

    std::size_t capacity() const noexcept
    {
        return capacity_;
    }

    work_deque_stats& stats() noexcept
    {
        return stats_;
    }

    const work_deque_stats& stats() const noexcept
    {
        return stats_;
    }

    bool grow() noexcept
    {
        work_deque_buffer* old_buffer = buffer_.load(std::memory_order_acquire);
        if (old_buffer == nullptr || capacity_ >= work_deque_max_capacity)
        {
            return false;
        }
        const std::size_t new_capacity = capacity_ * 2;
        work_deque_buffer* new_buffer = work_deque_make_buffer(new_capacity);
        if (new_buffer == nullptr)
        {
            return false;
        }

        const std::int64_t top = top_.load(std::memory_order_acquire);
        const std::int64_t bottom = bottom_.load(std::memory_order_acquire);
        const std::size_t new_mask = new_capacity - 1;
        for (std::int64_t i = top; i < bottom; ++i)
        {
            job* const element = old_buffer->slots_[static_cast<std::size_t>(i) & mask_].load(
                std::memory_order_relaxed);
            new_buffer->slots_[static_cast<std::size_t>(i) & new_mask].store(
                element, std::memory_order_relaxed);
        }

        capacity_ = new_capacity;
        mask_ = new_mask;
        buffer_.store(new_buffer, std::memory_order_release);
        stats_.grow_events_.fetch_add(1, std::memory_order_relaxed);
        retire(old_buffer);
        return true;
    }

    bool push(job* element) noexcept
    {
        if (element == nullptr)
        {
            return false;
        }
        std::int64_t bottom = bottom_.load(std::memory_order_relaxed);
        std::int64_t top = top_.load(std::memory_order_acquire);
        // 至少保留一个空槽：窃取者先拷贝后 CAS，所有者不得在拷贝窗口内绕回被窃取区间
        while (static_cast<std::size_t>(bottom - top) + 1 >= capacity_)
        {
            if (!grow())
            {
                stats_.drops_.fetch_add(1, std::memory_order_relaxed);
                return false;
            }
            top = top_.load(std::memory_order_acquire);
        }
        work_deque_buffer* const buffer = buffer_.load(std::memory_order_relaxed);
        buffer->slots_[static_cast<std::size_t>(bottom) & mask_].store(element,
                                                                       std::memory_order_relaxed);
        bottom_.store(bottom + 1, std::memory_order_release);
        stats_.pushes_.fetch_add(1, std::memory_order_relaxed);
        return true;
    }

    std::size_t room() const noexcept
    {
        const std::int64_t bottom = bottom_.load(std::memory_order_acquire);
        const std::int64_t top = top_.load(std::memory_order_acquire);
        const std::int64_t used = bottom - top;
        return used >= static_cast<std::int64_t>(capacity_) ? 0
                                                           : capacity_ - static_cast<std::size_t>(used);
    }

    job* pop() noexcept
    {
        std::int64_t bottom = bottom_.load(std::memory_order_acquire);
        work_deque_buffer* buffer = buffer_.load(std::memory_order_relaxed);
        bottom_.store(bottom - 1, std::memory_order_relaxed);
        std::atomic_thread_fence(std::memory_order_seq_cst);
        const std::int64_t top = top_.load(std::memory_order_relaxed);
        bottom = bottom - 1;

        if (top <= bottom)
        {
            const std::size_t index = static_cast<std::size_t>(bottom) & mask_;
            job* const element = buffer->slots_[index].load(std::memory_order_relaxed);
            if (top < bottom)
            {
                stats_.pops_.fetch_add(1, std::memory_order_relaxed);
                return element;
            }

            std::int64_t expected = top;
            if (!top_.compare_exchange_strong(expected, expected + 1, std::memory_order_seq_cst,
                                              std::memory_order_relaxed))
            {
                bottom_.store(bottom + 1, std::memory_order_relaxed);
                return nullptr;
            }
            bottom_.store(bottom + 1, std::memory_order_relaxed);
            stats_.pops_.fetch_add(1, std::memory_order_relaxed);
            return element;
        }

        bottom_.store(bottom + 1, std::memory_order_relaxed);
        if (top > bottom)
        {
            return nullptr;
        }
        std::int64_t expected = top;
        top_.compare_exchange_strong(expected, expected + 1, std::memory_order_seq_cst,
                                     std::memory_order_relaxed);
        return nullptr;
    }

    std::size_t steal(job** out, std::size_t max_count) noexcept
    {
        stats_.steal_attempts_.fetch_add(1, std::memory_order_relaxed);
        if (max_count == 0 || out == nullptr)
        {
            return 0;
        }

        std::int64_t top = top_.load(std::memory_order_acquire);
        std::atomic_thread_fence(std::memory_order_seq_cst);
        const std::int64_t bottom = bottom_.load(std::memory_order_acquire);

        if (top >= bottom)
        {
            return 0;
        }

        work_deque_buffer* const buffer = buffer_.load(std::memory_order_acquire);
        if (buffer == nullptr)
        {
            return 0;
        }
        const std::size_t buffer_mask =
            static_cast<std::size_t>(buffer->capacity_.load(std::memory_order_relaxed) - 1);

        const std::int64_t available = bottom - top;
        std::int64_t wanted = available / 2;
        const std::int64_t quarter = static_cast<std::int64_t>((buffer_mask + 1) / 4);
        if (quarter > 0 && wanted > quarter)
        {
            wanted = quarter;
        }
        if (wanted > static_cast<std::int64_t>(max_count))
        {
            wanted = static_cast<std::int64_t>(max_count);
        }
        if (wanted < 1)
        {
            wanted = 1;
        }

        // 先拷贝再竞争所有权：CAS 成功即宣告槽位可复用，拷贝必须在其之前完成
        for (std::int64_t i = 0; i < wanted; ++i)
        {
            const std::size_t index = static_cast<std::size_t>(top + i) & buffer_mask;
            out[i] = buffer->slots_[index].load(std::memory_order_relaxed);
        }
        if (!top_.compare_exchange_strong(top, top + wanted, std::memory_order_acq_rel,
                                          std::memory_order_relaxed))
        {
            stats_.steal_aborts_.fetch_add(1, std::memory_order_relaxed);
            return 0;
        }
        stats_.steals_.fetch_add(static_cast<std::uint64_t>(wanted), std::memory_order_relaxed);
        stats_.stolen_from_.fetch_add(1, std::memory_order_relaxed);
        return static_cast<std::size_t>(wanted);
    }

  private:
    void retire(work_deque_buffer* buffer) noexcept
    {
        work_deque_buffer* head = retired_.load(std::memory_order_relaxed);
        for (;;)
        {
            buffer->retired_next_.store(head, std::memory_order_relaxed);
            if (retired_.compare_exchange_weak(head, buffer, std::memory_order_acq_rel,
                                               std::memory_order_relaxed))
            {
                return;
            }
        }
    }

    std::atomic<work_deque_buffer*> retired_{nullptr};
};
} // namespace thread
