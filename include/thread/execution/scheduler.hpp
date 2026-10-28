#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <new>

#include "part/dense.hpp"
#include "thread/base/job.hpp"
#include "thread/queue/work_deque.hpp"

namespace thread
{

inline constexpr std::uint8_t scheduler_default_levels = default_priority_levels;

inline constexpr std::uint32_t scheduler_default_aging_threshold = 64;

struct priority_stack
{
    bool create(std::uint8_t levels, std::size_t capacity_per_level) noexcept
    {
        if (levels == 0 || levels > max_priority_levels)
        {
            return false;
        }
        levels_ = levels;
        deques_.increase_capacity(levels);
        for (std::uint8_t i = 0; i < levels; ++i)
        {
            deques_.emplace_back();
            if (!deques_.back().create(capacity_per_level))
            {
                destroy();
                return false;
            }
        }
        popped_since_lowest_.store(0, std::memory_order_relaxed);
        return true;
    }

    void destroy() noexcept
    {
        for (work_deque& d : deques_)
        {
            d.destroy();
        }
        deques_.clear();
        levels_ = 0;
    }

    std::size_t levels() const noexcept
    {
        return levels_;
    }

    std::size_t total_size() const noexcept
    {
        std::size_t total = 0;
        for (const work_deque& d : deques_)
        {
            total += d.size();
        }
        return total;
    }

    std::uint64_t drops() const noexcept
    {
        std::uint64_t total = 0;
        for (const work_deque& d : deques_)
        {
            total += d.stats_.drops_.load(std::memory_order_relaxed);
        }
        return total;
    }

    std::uint8_t clamp_level(std::uint8_t level) const noexcept
    {
        if (levels_ == 0)
        {
            return 0;
        }
        if (level >= levels_)
        {
            return static_cast<std::uint8_t>(levels_ - 1);
        }
        return level;
    }

    bool push(job* target) noexcept
    {
        if (levels_ == 0 || target == nullptr)
        {
            return false;
        }
        return deques_[clamp_level(target->priority_)].push(target);
    }

    job* pop() noexcept
    {
        if (levels_ == 0)
        {
            return nullptr;
        }
        const std::uint8_t lowest = 0;

        // 老化配额：定期优先取最低优先级，防止其饥饿
        if (levels_ > 1 && popped_since_lowest_.load(std::memory_order_relaxed) >= aging_threshold)
        {
            popped_since_lowest_.store(0, std::memory_order_relaxed);
            job* aged = deques_[lowest].pop();
            if (aged != nullptr)
            {
                return aged;
            }
        }

        // 每轮都从最高优先级开始，不能沿用上次命中的层级
        for (std::uint8_t level = static_cast<std::uint8_t>(levels_); level > 0; --level)
        {
            job* taken = deques_[level - 1].pop();
            if (taken == nullptr)
            {
                continue;
            }
            if (level - 1 == lowest)
            {
                popped_since_lowest_.store(0, std::memory_order_relaxed);
            }
            else
            {
                popped_since_lowest_.fetch_add(1, std::memory_order_relaxed);
            }
            return taken;
        }
        return nullptr;
    }

    std::size_t steal(job** out, std::size_t max_count) noexcept
    {
        for (std::uint8_t level = static_cast<std::uint8_t>(levels_); level > 0; --level)
        {
            const std::size_t got = deques_[level - 1].steal(out, max_count);
            if (got > 0)
            {
                return got;
            }
        }
        return 0;
    }

    std::uint32_t aging_threshold{scheduler_default_aging_threshold};

  private:
    dense<work_deque> deques_{};
    std::uint8_t levels_{0};
    std::atomic<std::uint32_t> popped_since_lowest_{0};
};

struct dispatch_index
{
    ~dispatch_index()
    {
        release_loads();
    }

    dispatch_index() noexcept = default;

    dispatch_index(const dispatch_index&) = delete;
    dispatch_index& operator=(const dispatch_index&) = delete;

    void init(std::size_t worker_count) noexcept
    {
        release_loads();
        count_ = worker_count;
        if (worker_count == 0)
        {
            return;
        }
        loads_ = new (std::nothrow) std::atomic<std::uint64_t>[worker_count];
        if (loads_ == nullptr)
        {
            count_ = 0;
            return;
        }
        for (std::size_t i = 0; i < worker_count; ++i)
        {
            loads_[i].store(0, std::memory_order_relaxed);
        }
        cursor_.store(0, std::memory_order_relaxed);
    }

    std::size_t worker_count() const noexcept
    {
        return count_;
    }

    std::size_t pick_lightest() noexcept
    {
        if (count_ == 0)
        {
            return 0;
        }
        const std::size_t start = cursor_.fetch_add(1, std::memory_order_relaxed) % count_;
        std::size_t best = start;
        std::uint64_t best_load = loads_[best].load(std::memory_order_relaxed);
        for (std::size_t k = 1; k < count_; ++k)
        {
            const std::size_t index = (start + k) % count_;
            const std::uint64_t load = loads_[index].load(std::memory_order_relaxed);
            if (load < best_load)
            {
                best = index;
                best_load = load;
            }
        }
        return best;
    }

    void set_load(std::size_t index, std::uint64_t value) noexcept
    {
        if (index < count_)
        {
            loads_[index].store(value, std::memory_order_relaxed);
        }
    }

  private:
    void release_loads() noexcept
    {
        delete[] loads_;
        loads_ = nullptr;
        count_ = 0;
    }

    std::atomic<std::uint64_t>* loads_{nullptr};
    std::size_t count_{0};
    std::atomic<std::size_t> cursor_{0};
};
} // namespace thread
