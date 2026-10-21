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
        if (target.window_.load(std::memory_order_relaxed) != window)
        {
            target.window_.store(window, std::memory_order_relaxed);
            target.key_.store(key, std::memory_order_relaxed);
            target.count_.store(1, std::memory_order_relaxed);
            return true;
        }
        if (target.key_.load(std::memory_order_relaxed) != key)
        {
            target.key_.store(key, std::memory_order_relaxed);
            target.count_.store(1, std::memory_order_relaxed);
            return true;
        }
        return target.count_.fetch_add(1, std::memory_order_relaxed) < limit;
    }

    void clear() noexcept
    {
        for (std::size_t i = 0; i < SlotCount; ++i)
        {
            slots_[i].key_.store(0, std::memory_order_relaxed);
            slots_[i].window_.store(0, std::memory_order_relaxed);
            slots_[i].count_.store(0, std::memory_order_relaxed);
        }
    }

private:
    struct slot
    {
        std::atomic<std::uint64_t> key_{0};
        std::atomic<std::uint64_t> window_{0};
        std::atomic<std::uint32_t> count_{0};
    };

    slot slots_[SlotCount]{};
};
