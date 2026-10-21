#pragma once

#include <atomic>
#include <chrono>
#include <coroutine>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <new>

#include "thread/base/error.hpp"
#include "thread/base/platform.hpp"
#include "thread/task/task_context.hpp"
#include "thread/base/task_identity.hpp"

namespace thread
{

template <typename T> struct task;

struct task_node;
struct task_graph;

inline constexpr std::size_t task_name_size = 24;
inline constexpr std::size_t task_node_size = 128;
inline constexpr std::size_t task_node_align = 64;

union task_invoke
{
    void (*plain_)(task_context&, void*) noexcept;
    void (*plain_ctx_)(task_context&) noexcept;
    task<void> (*coro_)(task_context&, void*);
    task<void> (*coro_ctx_)(task_context&);
};

inline constexpr std::uint8_t task_body_plain = 0;
inline constexpr std::uint8_t task_body_coroutine = 1;
inline constexpr std::uint8_t task_body_plain_ctx = 2;
inline constexpr std::uint8_t task_body_coroutine_ctx = 3;

using task_body_destroy = void (*)(void* object) noexcept;

struct task_body
{
    task_invoke invoke_{};
    void* object_{nullptr};
    std::uint8_t kind_{task_body_plain};
    task_body_destroy destroy_{nullptr};
};

// 永久任务节点：64 字节对齐，热字段在前 64 字节
struct alignas(task_node_align) task_node
{
    std::atomic<std::uint32_t> state_{task_state_constructing};   // 0
    std::atomic<std::uint32_t> flags_{0};                         // 4
    std::atomic<std::uint32_t> dispatch_epoch_{0};                // 8
    std::atomic<std::uint32_t> heap_index_{0};                    // 12，域线程独占
    std::atomic<std::uint64_t> route_{0};                         // 16
    std::atomic<std::uint64_t> parent_{0};                        // 24
    std::atomic<std::uint64_t> order_key_{0};                     // 32
    std::atomic<std::uint32_t> pending_deps_{0};                  // 40
    std::atomic<std::uint16_t> pending_runs_{0};                  // 44
    std::uint8_t body_kind_{task_body_plain};                      // 46
    std::uint8_t reserved0_{0};                                    // 47
    task_invoke invoke_{};                                         // 48
    void* body_object_{nullptr};                                   // 56

    std::atomic<std::uint32_t> pending_children_{0};              // 64
    std::atomic<std::uint32_t> activation_count_{0};              // 68
    std::atomic<std::uint64_t> total_run_ns_{0};                  // 72
    std::atomic<std::uint64_t> heartbeat_ns_{0};                  // 80
    std::atomic<void*> coro_handle_{nullptr};                     // 88
    char name_[task_name_size]{};                                 // 96
    std::atomic<std::uint32_t> activation_policy_{0};             // 120，低 8 位策略 / 次 8 位重叠策略
    std::atomic<std::uint32_t> frame_period_{1};                  // 124，every_frames 的帧间隔

    task_node() noexcept = default;

    task_node(const task_node&) = delete;
    task_node& operator=(const task_node&) = delete;

    task_state state() const noexcept
    {
        const std::uint32_t raw = state_.load(std::memory_order_acquire);
        return raw == task_state_constructing ? task_state::dormant
                                              : static_cast<task_state>(raw);
    }

    bool constructing() const noexcept
    {
        return state_.load(std::memory_order_acquire) == task_state_constructing;
    }

    route_word route() const noexcept
    {
        return route_word{route_.load(std::memory_order_acquire)};
    }

    bool has_flag(std::uint32_t value) const noexcept
    {
        return (flags_.load(std::memory_order_acquire) & value) != 0;
    }

    activation_policy activation() const noexcept
    {
        return static_cast<activation_policy>(activation_policy_.load(std::memory_order_relaxed) &
                                              0xFFu);
    }

    activation_overlap overlap() const noexcept
    {
        return static_cast<activation_overlap>(
            (activation_policy_.load(std::memory_order_relaxed) >> 8) & 0xFFu);
    }

    void set_activation(activation_policy policy, activation_overlap overlap) noexcept
    {
        activation_policy_.store(static_cast<std::uint32_t>(policy) |
                                     (static_cast<std::uint32_t>(overlap) << 8),
                                 std::memory_order_relaxed);
    }

    std::uint32_t frame_period() const noexcept
    {
        return frame_period_.load(std::memory_order_relaxed);
    }

    void set_frame_period(std::uint32_t value) noexcept
    {
        frame_period_.store(value == 0 ? 1u : value, std::memory_order_relaxed);
    }

    void set_name(const char* name) noexcept
    {
        if (name == nullptr)
        {
            name_[0] = '\0';
            return;
        }
        std::size_t i = 0;
        for (; i + 1 < task_name_size && name[i] != '\0'; ++i)
        {
            name_[i] = name[i];
        }
        name_[i] = '\0';
    }
};

inline void task_context::heartbeat() noexcept
{
    if (node_ != nullptr)
    {
        node_->heartbeat_ns_.store(monotonic_now_ns(), std::memory_order_relaxed);
    }
}

inline bool task_context::cancelled() const noexcept
{
    return node_ != nullptr &&
           (node_->flags_.load(std::memory_order_relaxed) & flag_cancelled) != 0;
}

inline bool task_context::budget_exceeded() const noexcept
{
    return budget_ns_ != 0 && start_ns_ != 0 &&
           (monotonic_now_ns() - start_ns_) > budget_ns_;
}

inline void task_context::fail() noexcept
{
    if (node_ != nullptr)
    {
        node_->flags_.fetch_or(flag_body_failed, std::memory_order_acq_rel);
    }
}

inline task_id current_task() noexcept
{
    const task_context* ctx = current_task_slot();
    return ctx != nullptr ? ctx->id_ : invalid_task;
}

inline thread::role current_role() noexcept
{
    const task_context* ctx = current_task_slot();
    return ctx != nullptr ? ctx->role_ : thread::role::general;
}

inline context_id current_context_id() noexcept
{
    const task_context* ctx = current_task_slot();
    return ctx != nullptr ? ctx->context_ : invalid_context;
}

static_assert(sizeof(task_node) == task_node_size, "task_node must stay 128 bytes");
static_assert(alignof(task_node) == task_node_align, "task_node must be 64-byte aligned");
} // namespace thread
