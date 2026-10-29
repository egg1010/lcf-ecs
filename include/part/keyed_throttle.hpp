#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

// keyed_throttle - 固定槽键控限流器（线程安全，无分配）
// 每个槽记录 (key, window, count)；窗口或键变化时重置，计数超过上限返回 false

template <std::size_t SlotCount = 64>
class keyed_throttle
{
    static_assert(SlotCount > 0, "keyed_throttle needs at least one slot");

public:
    bool allow(std::uint64_t key, std::uint64_t window, std::uint32_t limit) noexcept
    {
        if (limit == 0)
        {
            return false;
        }
        slot& target = slots_[key % SlotCount];
        while (target.lock_.test_and_set(std::memory_order_acquire))
        {
        }
        bool allowed = false;
        if (target.window_.load(std::memory_order_relaxed) != window)
        {
            target.window_.store(window, std::memory_order_relaxed);
            target.key_.store(key, std::memory_order_relaxed);
            target.count_.store(1, std::memory_order_relaxed);
            allowed = true;
        }
        else if (target.key_.load(std::memory_order_relaxed) != key)
        {
            target.key_.store(key, std::memory_order_relaxed);
            target.count_.store(1, std::memory_order_relaxed);
            allowed = true;
        }
        else
        {
            allowed = target.count_.fetch_add(1, std::memory_order_relaxed) < limit;
        }
        target.lock_.clear(std::memory_order_release);
        return allowed;
    }

    void clear() noexcept
    {
        for (std::size_t i = 0; i < SlotCount; ++i)
        {
            slot& target = slots_[i];
            while (target.lock_.test_and_set(std::memory_order_acquire))
            {
            }
            target.key_.store(0, std::memory_order_relaxed);
            target.window_.store(0, std::memory_order_relaxed);
            target.count_.store(0, std::memory_order_relaxed);
            target.lock_.clear(std::memory_order_release);
        }
    }

private:
    struct slot
    {
        std::atomic<std::uint64_t> key_{0};
        std::atomic<std::uint64_t> window_{0};
        std::atomic<std::uint32_t> count_{0};
        std::atomic_flag lock_{};
    };

    slot slots_[SlotCount]{};
};
