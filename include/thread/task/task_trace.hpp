#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

#include "thread/queue/mpmc_ring.hpp"
#include "thread/base/task_identity.hpp"
#include "thread/task/task_node.hpp"

namespace thread
{

enum class trace_kind : std::uint8_t
{
    enqueue = 0,
    start = 1,
    end = 2,
    wait_begin = 3,
    wait_end = 4,
    steal = 5,
    migrate = 6,
    cancel = 7,
    io_submit = 8,
    io_complete = 9,
};

struct trace_event
{
    std::uint64_t timestamp_ns_{0};
    std::uint64_t duration_ns_{0};
    task_id task_{};
    std::uint64_t trace_id_{0};
    std::uint32_t worker_{0};
    std::uint16_t role_{0};
    trace_kind kind_{trace_kind::start};
    std::uint16_t reserved_{0};
};

static_assert(sizeof(trace_event) <= 48, "trace event must stay compact");

using trace_sink = void (*)(const trace_event* events, std::size_t count, void* user) noexcept;

inline constexpr std::size_t trace_default_capacity = 256;

// 每个 worker 一个定长环；关闭时只读一个原子布尔
struct trace_recorder
{
    mpmc_ring<trace_event>* rings_{nullptr};
    std::uint32_t worker_count_{0};
    std::uint32_t capacity_{0};
    std::atomic<bool> enabled_{false};
    std::atomic<std::uint64_t> dropped_{0};
    std::atomic<std::uint64_t> recorded_{0};

    trace_recorder() noexcept = default;

    trace_recorder(const trace_recorder&) = delete;
    trace_recorder& operator=(const trace_recorder&) = delete;

    ~trace_recorder()
    {
        destroy();
    }

    bool create(std::uint32_t worker_count, std::uint32_t capacity) noexcept
    {
        if (rings_ != nullptr || worker_count == 0 || capacity == 0)
        {
            return false;
        }
        void* memory = ::operator new(sizeof(mpmc_ring<trace_event>) * worker_count,
                                      std::align_val_t(alignof(mpmc_ring<trace_event>)), std::nothrow);
        if (memory == nullptr)
        {
            return false;
        }
        rings_ = static_cast<mpmc_ring<trace_event>*>(memory);
        for (std::uint32_t i = 0; i < worker_count; ++i)
        {
            new (&rings_[i]) mpmc_ring<trace_event>{};
            if (!rings_[i].create(capacity))
            {
                destroy();
                return false;
            }
        }
        worker_count_ = worker_count;
        capacity_ = capacity;
        return true;
    }

    void destroy() noexcept
    {
        if (rings_ == nullptr)
        {
            return;
        }
        for (std::uint32_t i = 0; i < worker_count_; ++i)
        {
            rings_[i].destroy();
            rings_[i].~mpmc_ring<trace_event>();
        }
        ::operator delete(static_cast<void*>(rings_), std::align_val_t(alignof(mpmc_ring<trace_event>)));
        rings_ = nullptr;
        worker_count_ = 0;
        capacity_ = 0;
        enabled_.store(false, std::memory_order_release);
    }

    void set_enabled(bool value) noexcept
    {
        enabled_.store(value, std::memory_order_release);
    }

    bool enabled() const noexcept
    {
        return enabled_.load(std::memory_order_acquire);
    }

    void record(trace_kind kind, task_id id, std::uint32_t worker, thread::role role,
                std::uint64_t trace_id, std::uint64_t duration_ns = 0) noexcept
    {
        if (!enabled_.load(std::memory_order_relaxed) || rings_ == nullptr)
        {
            return;
        }
        // 未指定 worker 的事件（入队/迁移/取消）归到 0 号环
        const std::uint32_t slot = worker < worker_count_ ? worker : 0u;
        trace_event event{};
        event.timestamp_ns_ = monotonic_now_ns();
        event.duration_ns_ = duration_ns;
        event.task_ = id;
        event.trace_id_ = trace_id;
        event.worker_ = worker;
        event.role_ = static_cast<std::uint16_t>(role_index(role));
        event.kind_ = kind;
        if (!rings_[slot].try_push(event))
        {
            dropped_.fetch_add(1, std::memory_order_relaxed);
            return;
        }
        recorded_.fetch_add(1, std::memory_order_relaxed);
    }

    std::size_t drain(std::uint32_t worker, trace_sink sink, void* user,
                      std::size_t max_events = trace_default_capacity) noexcept
    {
        if (rings_ == nullptr || worker >= worker_count_ || sink == nullptr)
        {
            return 0;
        }
        trace_event batch[trace_default_capacity];
        const std::size_t limit = max_events < trace_default_capacity ? max_events
                                                                     : trace_default_capacity;
        const std::size_t count = rings_[worker].try_pop_bulk(batch, limit);
        if (count != 0)
        {
            sink(batch, count, user);
        }
        return count;
    }

    std::uint32_t queue_depth(std::uint32_t worker) const noexcept
    {
        if (rings_ == nullptr || worker >= worker_count_)
        {
            return 0;
        }
        const std::int64_t depth = rings_[worker].size();
        return depth > 0 ? static_cast<std::uint32_t>(depth) : 0;
    }
};
} // namespace thread
