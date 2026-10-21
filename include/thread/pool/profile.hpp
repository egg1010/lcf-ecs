#pragma once

#include <cstddef>
#include <cstdint>
#include <thread>

#include "thread/base/job.hpp"
#include "thread/execution/lane.hpp"
#include "thread/pool/manager.hpp"
#include "thread/base/platform.hpp"
#include "thread/base/sync.hpp"

namespace thread
{

namespace profile
{

// 原生主线程参与帧循环，因此默认线程预算为硬件线程数减一，避免超订
inline std::size_t thread_budget() noexcept
{
    const unsigned hardware = std::thread::hardware_concurrency();
    const std::size_t total = hardware == 0 ? 1u : static_cast<std::size_t>(hardware);
    return total > 1 ? total - 1 : 1;
}

inline manager_config server(std::size_t worker_count = thread_budget()) noexcept
{
    manager_config config{};
    config.worker_count = worker_count;
    config.inject_capacity = mpmc_default_capacity;
    config.local_capacity = work_deque_default_capacity;
    config.steal_batch = 64;
    config.pin_workers = false;
    config.priority_levels = scheduler_default_levels;
    config.aging_threshold = worker_default_aging_threshold;
    config.lane_capacity = manager_default_lane_capacity;
    return config;
}

inline manager_config game(std::size_t worker_count = thread_budget()) noexcept
{
    manager_config config{};
    config.worker_count = worker_count;
    config.inject_capacity = mpmc_default_capacity;
    config.local_capacity = work_deque_default_capacity;
    config.steal_batch = 32;
    config.pin_workers = true;
    config.priority_levels = scheduler_default_levels;
    config.aging_threshold = worker_default_aging_threshold;
    config.lane_capacity = manager_default_lane_capacity;
    return config;
}

inline manager_config embedded() noexcept
{
    manager_config config{};
    config.worker_count = 0;
    config.inject_capacity = mpmc_default_capacity;
    config.local_capacity = work_deque_default_capacity;
    config.steal_batch = 64;
    config.pin_workers = false;
    config.priority_levels = scheduler_default_levels;
    config.aging_threshold = worker_default_aging_threshold;
    config.lane_capacity = manager_default_lane_capacity;
    return config;
}

inline bool is_embedded(const manager_config& config) noexcept
{
    return config.worker_count == 0;
}
} // namespace profile

struct frame_scope
{
    void begin() noexcept
    {
        counter_.init(0);
        submitted_ = 0;
    }

    void track() noexcept
    {
        counter_.add(1);
        ++submitted_;
    }

    std::uint32_t submitted() const noexcept
    {
        return submitted_;
    }

    void complete_one() noexcept
    {
        (void)counter_.complete_one();
    }

    bool wait(std::uint32_t timeout_ms = infinite_park_ms) noexcept
    {
        std::uint32_t spins = 0;
        while (!counter_.ready())
        {
            platform_yield();
            if (++spins > 2000000u)
            {
                return counter_.ready();
            }
        }
        (void)timeout_ms;
        return true;
    }

    bool done() const noexcept
    {
        return counter_.ready();
    }

    counter& counter_ref() noexcept
    {
        return counter_;
    }

  private:
    counter counter_{};
    std::uint32_t submitted_{0};
};
} // namespace thread
