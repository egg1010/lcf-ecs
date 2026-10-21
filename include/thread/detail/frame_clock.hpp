#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <thread>

#include "part/dense.hpp"

namespace thread
{

// 帧节拍：只负责按帧率停放，不依赖任务图
struct frame_clock
{
    dense<std::uint64_t> deadline_{};

    void start(std::size_t worker_count) noexcept
    {
        deadline_.resize_fill(worker_count, 0);
    }

    void clear() noexcept
    {
        deadline_.clear();
    }

    void pace(std::uint32_t worker, std::uint32_t rate) noexcept
    {
        if (worker >= deadline_.size())
        {
            return;
        }
        const auto period =
            std::chrono::nanoseconds(1'000'000'000ll / static_cast<long long>(rate == 0 ? 60u : rate));
        const auto now = std::chrono::steady_clock::now();
        const std::uint64_t stamp = deadline_[worker];
        auto target = std::chrono::steady_clock::time_point(std::chrono::nanoseconds(
            static_cast<long long>(stamp)));
        if (stamp == 0 || target < now)
        {
            target = now;
        }
        target += period;
        // Windows 默认 15.6 ms 定时器粒度：末段让步自旋，避免节拍被吞
        for (;;)
        {
            const auto current = std::chrono::steady_clock::now();
            if (current >= target)
            {
                break;
            }
            const auto remain = target - current;
            if (remain > std::chrono::milliseconds(16))
            {
                std::this_thread::sleep_until(target - std::chrono::milliseconds(4));
            }
            else
            {
                std::this_thread::yield();
            }
        }
        deadline_[worker] = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(target.time_since_epoch()).count());
    }
};

} // namespace thread
