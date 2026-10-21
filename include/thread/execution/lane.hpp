#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <new>

#include "thread/error.hpp"
#include "thread/job.hpp"
#include "thread/mpmc_queue.hpp"
#include "thread/runtime.hpp"

namespace thread
{
inline constexpr std::uint32_t lane_no_id = 0xFFFFFFFFu;
inline constexpr std::size_t lane_default_capacity = mpmc_default_capacity;

inline constexpr std::size_t manager_default_lane_capacity = 64;

inline constexpr std::uint32_t lane_default_drain_budget = 64;
inline constexpr std::uint32_t lane_owner_none = 0xFFFFFFFFu;

inline constexpr std::uint32_t manager_pump_owner = 0xFFFFFFFEu;

enum class lane_mode : std::uint8_t
{
    exclusive = 0,
    pinned = 1,
    shared = 2,
};

struct lane_config
{
    lane_mode mode{lane_mode::exclusive};
    std::uint32_t pinned_worker{0};
    std::size_t capacity{lane_default_capacity};
    std::uint32_t drain_budget{lane_default_drain_budget};

    static lane_config exclusive(std::size_t capacity = lane_default_capacity) noexcept
    {
        lane_config config{};
        config.mode = lane_mode::exclusive;
        config.capacity = capacity;
        return config;
    }

    static lane_config pinned(std::uint32_t worker,
                              std::size_t capacity = lane_default_capacity) noexcept
    {
        lane_config config{};
        config.mode = lane_mode::pinned;
        config.pinned_worker = worker;
        config.capacity = capacity;
        return config;
    }

    static lane_config shared(std::size_t capacity = lane_default_capacity) noexcept
    {
        lane_config config{};
        config.mode = lane_mode::shared;
        config.capacity = capacity;
        return config;
    }
};

struct lane_stats
{
    std::atomic<std::uint64_t> submitted_{0};
    std::atomic<std::uint64_t> executed_{0};
    std::atomic<std::uint64_t> owned_batches_{0};
    std::atomic<std::uint64_t> reclaims_{0};
    std::atomic<std::uint64_t> seq_breaks_{0};
};

struct lane
{
    lane() noexcept = default;

    lane(const lane&) = delete;
    lane& operator=(const lane&) = delete;

    ~lane()
    {
        queue_.destroy();
    }

    bool create(std::uint32_t id, lane_config config,
                runtime_context* runtime = current_runtime()) noexcept
    {
        if (created_)
        {
            return false;
        }
        id_ = id;
        config_ = config;
        rt_ = runtime != nullptr ? runtime : default_runtime();
        if (!queue_.create(config.capacity == 0 ? lane_default_capacity : config.capacity))
        {
            FRAMEWORK_REPORT(error_severity::error, errc::invariant_broken);
            return false;
        }
        owner_.store(lane_owner_none, std::memory_order_relaxed);
        next_seq_.store(1, std::memory_order_relaxed);
        last_executed_seq_.store(0, std::memory_order_relaxed);
        created_ = true;
        return true;
    }

    std::uint32_t id() const noexcept
    {
        return id_;
    }

    lane_mode mode() const noexcept
    {
        return config_.mode;
    }

    std::uint32_t pinned_worker() const noexcept
    {
        return config_.pinned_worker;
    }

    void bind_worker(std::uint32_t worker) noexcept
    {
        bound_worker_ = worker;
    }

    std::uint32_t bound_worker() const noexcept
    {
        return config_.mode == lane_mode::pinned ? config_.pinned_worker : bound_worker_;
    }

    std::uint32_t drain_budget() const noexcept
    {
        return config_.drain_budget;
    }

    lane_stats& stats() noexcept
    {
        return stats_;
    }

    // 多提交者并发提交时 seq 分配序与 push 序可能不一致，seq_breaks 有非乱序误报

    std::uint32_t submit(std::uint32_t slot_index) noexcept
    {
        if (slot_index == slot_index_none || rt_ == nullptr)
        {
            return slot_index_none;
        }
        job* target = rt_->jobs_.slot_of(slot_index);
        if (target != nullptr)
        {
            target->lane_id_ = id_;
            target->aux_ = next_seq_.fetch_add(1, std::memory_order_relaxed);
        }
        if (!queue_.try_push(slot_index))
        {
            return slot_index_none;
        }
        lane_mark_ready(rt_->lane_ready_, id_);
        stats_.submitted_.fetch_add(1, std::memory_order_relaxed);
        return slot_index;
    }

    bool try_pop(std::uint32_t& out) noexcept
    {
        if (!queue_.try_pop(out))
        {
            return false;
        }
        // 抽空即清位。残留位最坏引发一次空探测，正确性不依赖位图精确性。
        if (rt_ != nullptr && queue_.size() == 0)
        {
            lane_clear_ready(rt_->lane_ready_, id_);
        }
        note_executed(out);
        return true;
    }

    bool try_acquire(std::uint32_t worker_id) noexcept
    {
        std::uint32_t expected = lane_owner_none;
        if (!owner_.compare_exchange_strong(expected, worker_id, std::memory_order_acq_rel,
                                            std::memory_order_relaxed))
        {
            return false;
        }
        stats_.owned_batches_.fetch_add(1, std::memory_order_relaxed);
        return true;
    }

    bool owns(std::uint32_t worker_id) const noexcept
    {
        return owner_.load(std::memory_order_acquire) == worker_id;
    }

    void release_ownership(std::uint32_t worker_id) noexcept
    {
        std::uint32_t expected = worker_id;
        if (owner_.compare_exchange_strong(expected, lane_owner_none, std::memory_order_acq_rel,
                                           std::memory_order_relaxed))
        {
            stats_.reclaims_.fetch_add(1, std::memory_order_relaxed);
        }
    }

    bool empty() const noexcept
    {
        return queue_.size() == 0;
    }

    std::int64_t size() const noexcept
    {
        return queue_.size();
    }

    std::uint32_t last_executed_seq() const noexcept
    {
        return last_executed_seq_.load(std::memory_order_relaxed);
    }

  private:
    void note_executed(std::uint32_t slot_index) noexcept
    {
        job* target = rt_ != nullptr ? rt_->jobs_.slot_of(slot_index) : nullptr;
        if (target != nullptr)
        {
            const std::uint32_t seq = target->aux_;
            const std::uint32_t previous = last_executed_seq_.load(std::memory_order_relaxed);
            if (seq <= previous)
            {
                stats_.seq_breaks_.fetch_add(1, std::memory_order_relaxed);
            }
            last_executed_seq_.store(seq, std::memory_order_relaxed);
        }
        stats_.executed_.fetch_add(1, std::memory_order_relaxed);
    }

    std::uint32_t id_{lane_no_id};
    lane_config config_{};
    mpmc_queue queue_{};
    std::atomic<std::uint32_t> owner_{lane_owner_none};
    std::atomic<std::uint32_t> next_seq_{1};
    std::atomic<std::uint32_t> last_executed_seq_{0};
    std::uint32_t bound_worker_{0};
    lane_stats stats_{};
    runtime_context* rt_{nullptr};
    bool created_{false};
};

struct lane_table
{
    void init(std::size_t capacity) noexcept
    {
        capacity_ = capacity == 0 ? manager_default_lane_capacity : capacity;
        slots_ = new (std::nothrow) std::atomic<lane*>[capacity_];
        if (slots_ == nullptr)
        {
            capacity_ = 0;
            return;
        }
        for (std::size_t i = 0; i < capacity_; ++i)
        {
            slots_[i].store(nullptr, std::memory_order_relaxed);
        }
        count_.store(0, std::memory_order_relaxed);
    }

    void release() noexcept
    {
        delete[] slots_;
        slots_ = nullptr;
        capacity_ = 0;
        count_.store(0, std::memory_order_relaxed);
    }

    bool push(lane* l) noexcept
    {
        if (slots_ == nullptr)
        {
            return false;
        }
        std::size_t index = count_.load(std::memory_order_relaxed);
        for (;;)
        {
            if (index >= capacity_)
            {
                return false;
            }
            if (count_.compare_exchange_weak(index, index + 1, std::memory_order_acq_rel,
                                             std::memory_order_relaxed))
            {
                slots_[index].store(l, std::memory_order_release);
                return true;
            }
        }
    }

    std::size_t size() const noexcept
    {
        return count_.load(std::memory_order_acquire);
    }

    std::size_t capacity() const noexcept
    {
        return capacity_;
    }

    lane* at(std::size_t index) const noexcept
    {
        return index < count_.load(std::memory_order_acquire)
                   ? slots_[index].load(std::memory_order_acquire)
                   : nullptr;
    }

  private:
    std::atomic<lane*>* slots_{nullptr};
    std::size_t capacity_{0};
    std::atomic<std::size_t> count_{0};
};
} // namespace thread
