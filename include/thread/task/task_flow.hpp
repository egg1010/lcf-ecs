#pragma once

#include <chrono>
#include <coroutine>
#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>

#include "thread/execution/coroutine.hpp"
#include "thread/base/job.hpp"
#include "thread/base/platform.hpp"
#include "thread/base/sync.hpp"
#include "thread/task/task_graph.hpp"

namespace thread
{

inline task_graph* current_graph() noexcept
{
    task_context* ctx = current_task_context();
    return ctx != nullptr ? ctx->graph_ : nullptr;
}

// ---------------------------------------------------------------------------
// switch_to：改路由并在目标域恢复
// ---------------------------------------------------------------------------

struct switch_to_awaitable
{
    thread::role target_{thread::role::general};
    uint32_t result_{0};

    bool await_ready() const noexcept
    {
        return current_graph() == nullptr;
    }

    bool await_suspend(std::coroutine_handle<>) noexcept
    {
        task_graph* graph = current_graph();
        task_context* ctx = current_task_context();
        if (graph == nullptr || ctx == nullptr)
        {
            result_ = om_err_internal_invariant.code;
            return false;
        }
        if (ctx->cancelled())
        {
            result_ = om_err_cancelled.code;
            return false;
        }
        operating_message error;
        if (!graph->prepare_switch_impl(ctx->id_, target_, error))
        {
            result_ = om_err_invalid_argument.code;
            return false;
        }
        return true;
    }

    uint32_t await_resume() const noexcept
    {
        const task_context* ctx = current_task_context();
        if (ctx != nullptr && ctx->cancelled())
        {
            return om_err_cancelled.code;
        }
        return result_;
    }
};

inline switch_to_awaitable switch_to(thread::role target) noexcept
{
    return switch_to_awaitable{target, 0};
}

// ---------------------------------------------------------------------------
// next_frame / frame_sync：等待帧边界
// ---------------------------------------------------------------------------

struct next_frame_awaitable
{
    thread::role target_{thread::role::general};
    uint32_t result_{0};

    bool await_ready() const noexcept
    {
        return current_graph() == nullptr;
    }

    bool await_suspend(std::coroutine_handle<>) noexcept
    {
        task_graph* graph = current_graph();
        task_context* ctx = current_task_context();
        if (graph == nullptr || ctx == nullptr)
        {
            result_ = om_err_internal_invariant.code;
            return false;
        }
        if (ctx->cancelled())
        {
            result_ = om_err_cancelled.code;
            return false;
        }
        operating_message error;
        if (!graph->prepare_frame_wait_impl(ctx->id_, target_, error))
        {
            result_ = om_err_invalid_argument.code;
            return false;
        }
        return true;
    }

    uint32_t await_resume() const noexcept
    {
        const task_context* ctx = current_task_context();
        if (ctx != nullptr && ctx->cancelled())
        {
            return om_err_cancelled.code;
        }
        return result_;
    }
};

inline next_frame_awaitable next_frame(thread::role target) noexcept
{
    return next_frame_awaitable{target, 0};
}

struct frame_sync_awaitable
{
    thread::role first_{thread::role::general};
    thread::role second_{thread::role::general};
    uint32_t result_{0};

    bool await_ready() const noexcept
    {
        return current_graph() == nullptr;
    }

    bool await_suspend(std::coroutine_handle<>) noexcept
    {
        task_graph* graph = current_graph();
        task_context* ctx = current_task_context();
        if (graph == nullptr || ctx == nullptr)
        {
            result_ = om_err_internal_invariant.code;
            return false;
        }
        operating_message error;
        if (!graph->prepare_frame_pair_impl(ctx->id_, first_, second_, error))
        {
            result_ = om_err_invalid_argument.code;
            return false;
        }
        return true;
    }

    uint32_t await_resume() const noexcept
    {
        return result_;
    }
};

inline frame_sync_awaitable frame_sync(thread::role first, thread::role second) noexcept
{
    return frame_sync_awaitable{first, second, 0};
}

// ---------------------------------------------------------------------------
// join / when_all：等待子任务
// ---------------------------------------------------------------------------

struct join_awaitable
{
    task_id child_{};
    uint32_t result_{0};

    bool await_ready() const noexcept
    {
        return false;
    }

    bool await_suspend(std::coroutine_handle<>) noexcept
    {
        task_graph* graph = current_graph();
        task_context* ctx = current_task_context();
        if (graph == nullptr || ctx == nullptr)
        {
            result_ = om_err_internal_invariant.code;
            return false;
        }
        operating_message error;
        if (!graph->prepare_join_impl(ctx->id_, child_, error))
        {
            result_ = om_err_invalid_argument.code;
            return false;
        }
        return true;
    }

    uint32_t await_resume() const noexcept
    {
        task_graph* graph = current_graph();
        const task_context* ctx = current_task_context();
        if (ctx != nullptr && ctx->cancelled())
        {
            return om_err_cancelled.code;
        }
        if (graph == nullptr || !child_.valid())
        {
            return result_;
        }
        const task_state state = graph->state_of(child_);
        if (state == task_state::faulted)
        {
            return om_err_task_body_faulted.code;
        }
        if (state == task_state::cancelled)
        {
            return om_err_cancelled.code;
        }
        return result_;
    }
};

inline join_awaitable join(task_id child) noexcept
{
    return join_awaitable{child, 0};
}

struct when_all_awaitable
{
    std::span<const task_id> children_{};
    std::span<uint32_t> errors_{};
    uint32_t result_{0};
    std::size_t armed_{0};

    bool await_ready() const noexcept
    {
        return children_.empty() || current_graph() == nullptr;
    }

    bool await_suspend(std::coroutine_handle<>) noexcept
    {
        task_graph* graph = current_graph();
        task_context* ctx = current_task_context();
        if (graph == nullptr || ctx == nullptr)
        {
            result_ = om_err_internal_invariant.code;
            return false;
        }
        for (std::size_t i = 0; i < children_.size(); ++i)
        {
            operating_message error;
            if (!graph->prepare_join_impl(ctx->id_, children_[i], error))
            {
                result_ = om_err_invalid_argument.code;
                break;
            }
            ++armed_;
        }
        return armed_ != 0;
    }

    uint32_t await_resume() noexcept
    {
        task_graph* graph = current_graph();
        for (std::size_t i = 0; i < children_.size() && i < errors_.size(); ++i)
        {
            uint32_t value = 0;
            if (graph != nullptr)
            {
                const task_state state = graph->state_of(children_[i]);
                if (state == task_state::faulted)
                {
                    value = om_err_task_body_faulted.code;
                }
                else if (state == task_state::cancelled)
                {
                    value = om_err_cancelled.code;
                }
                else if (state != task_state::completed)
                {
                    value = om_err_invalid_argument.code;
                }
            }
            errors_[i] = value;
        }
        return result_;
    }
};

inline when_all_awaitable when_all(std::span<const task_id> children,
                                   std::span<uint32_t> errors) noexcept
{
    return when_all_awaitable{children, errors, 0, 0};
}

struct when_any_awaitable
{
    std::span<const task_id> children_{};
    uint32_t result_{0};
    task_id completed_{};

    bool await_ready() const noexcept
    {
        return children_.empty() || current_graph() == nullptr;
    }

    bool await_suspend(std::coroutine_handle<>) noexcept
    {
        task_graph* graph = current_graph();
        task_context* ctx = current_task_context();
        if (graph == nullptr || ctx == nullptr)
        {
            result_ = om_err_internal_invariant.code;
            return false;
        }
        operating_message error;
        if (!graph->prepare_join_any_impl(ctx->id_, children_, error))
        {
            result_ = om_err_invalid_argument.code;
            return false;
        }
        return true;
    }

    task_id await_resume() noexcept
    {
        task_graph* graph = current_graph();
        const task_context* ctx = current_task_context();
        if (graph == nullptr)
        {
            return invalid_task;
        }
        if (ctx != nullptr)
        {
            graph->clear_join_any(ctx->id_);
        }
        for (std::size_t i = 0; i < children_.size(); ++i)
        {
            const task_state state = graph->state_of(children_[i]);
            if (state == task_state::completed || state == task_state::cancelled ||
                state == task_state::faulted)
            {
                completed_ = children_[i];
                break;
            }
        }
        return completed_;
    }
};

inline when_any_awaitable when_any(std::span<const task_id> children) noexcept
{
    return when_any_awaitable{children, 0, invalid_task};
}

// ---------------------------------------------------------------------------
// sleep_for / sleep_until：由看门狗扫描唤醒
// ---------------------------------------------------------------------------

struct sleep_awaitable
{
    std::uint64_t delay_ns_{0};
    uint32_t result_{0};

    bool await_ready() const noexcept
    {
        return delay_ns_ == 0 || current_graph() == nullptr;
    }

    bool await_suspend(std::coroutine_handle<>) noexcept
    {
        task_graph* graph = current_graph();
        task_context* ctx = current_task_context();
        if (graph == nullptr || ctx == nullptr)
        {
            result_ = om_err_internal_invariant.code;
            return false;
        }
        if (ctx->cancelled())
        {
            result_ = om_err_cancelled.code;
            return false;
        }
        operating_message error;
        if (!graph->prepare_timed_impl(ctx->id_, delay_ns_, error))
        {
            result_ = om_err_internal_invariant.code;
            return false;
        }
        return true;
    }

    uint32_t await_resume() const noexcept
    {
        const task_context* ctx = current_task_context();
        if (ctx != nullptr && ctx->cancelled())
        {
            return om_err_cancelled.code;
        }
        return result_;
    }
};

inline sleep_awaitable sleep_for(std::chrono::nanoseconds duration) noexcept
{
    const auto count = std::chrono::duration_cast<std::chrono::nanoseconds>(duration).count();
    return sleep_awaitable{count > 0 ? static_cast<std::uint64_t>(count) : 0ull, 0};
}

inline sleep_awaitable sleep_until(std::chrono::steady_clock::time_point deadline) noexcept
{
    const std::uint64_t now = monotonic_now_ns();
    const std::uint64_t target = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(deadline.time_since_epoch()).count());
    return sleep_awaitable{target > now ? target - now : 0ull, 0};
}

// ---------------------------------------------------------------------------
// parallel_for：工作项分片并行，父任务等待
// ---------------------------------------------------------------------------

inline constexpr std::size_t parallel_for_max_chunks = 32;

// 并行分片汇合：最后一个分片只负责把父任务重新入队，
// 恢复由图在任务上下文内完成（保留 current_task_slot 与状态机路径）
struct parallel_join_waiter
{
    waiter_node node_{};
    task_graph* graph_{nullptr};
    task_id task_{};

    static void wake(waiter_node* self) noexcept
    {
        auto* holder = reinterpret_cast<parallel_join_waiter*>(self);
        if (holder->graph_ != nullptr)
        {
            operating_message error;
            (void)holder->graph_->wake_waiting_impl(holder->task_, error);
        }
    }

    void init(task_graph* graph, task_id task) noexcept
    {
        waiter_node_init(&node_, &parallel_join_waiter::wake);
        graph_ = graph;
        task_ = task;
    }
};

template <typename Body>
struct parallel_for_awaitable
{
    std::size_t begin_{0};
    std::size_t end_{0};
    Body body_{};
    counter counter_{};
    parallel_join_waiter waiter_{};
    uint32_t result_{0};

    bool await_ready() const noexcept
    {
        return end_ <= begin_;
    }

    bool await_suspend(std::coroutine_handle<> handle) noexcept
    {
        (void)handle;
        const std::size_t total = end_ - begin_;
        std::size_t chunks = total;
        if (chunks > parallel_for_max_chunks)
        {
            chunks = parallel_for_max_chunks;
        }
        if (chunks == 0)
        {
            return false;
        }
        if (chunks > total)
        {
            chunks = total;
        }
        counter_.init(static_cast<std::uint32_t>(chunks));

        task_graph* graph = current_graph();
        const std::size_t step = (total + chunks - 1) / chunks;
        for (std::size_t index = 0; index < chunks; ++index)
        {
            const std::size_t first = begin_ + index * step;
            if (first >= end_)
            {
                break;
            }
            const std::size_t last = (first + step < end_) ? first + step : end_;
            job* target = take_slot_job_checked();
            if (target == nullptr || graph == nullptr)
            {
                for (std::size_t i = first; i < last; ++i)
                {
                    body_(i);
                }
                counter_.complete_one();
                continue;
            }
            fill_inline_job(target, [this, first, last]() noexcept
            {
                for (std::size_t i = first; i < last; ++i)
                {
                    body_(i);
                }
                counter_.complete_one();
            });
            operating_message error;
            if (!graph->submit_work_impl(target, error))
            {
                // 无提交通道：就地执行，保证语义
                for (std::size_t i = first; i < last; ++i)
                {
                    body_(i);
                }
                counter_.complete_one();
                release_job(target);
                continue;
            }
        }
        if (counter_.ready())
        {
            return false;
        }
        task_context* ctx = current_task_context();
        if (graph == nullptr || ctx == nullptr || !ctx->id_.valid())
        {
            while (!counter_.ready())
            {
                platform_yield();
            }
            return false;
        }
        operating_message error;
        if (!graph->prepare_external_wait_impl(ctx->id_, error))
        {
            while (!counter_.ready())
            {
                platform_yield();
            }
            return false;
        }
        waiter_.init(graph, ctx->id_);
        if (!counter_.attach_waiter(&waiter_.node_))
        {
            return false;
        }
        return true;
    }

    uint32_t await_resume() noexcept
    {
        const task_context* ctx = current_task_context();
        if (ctx != nullptr && ctx->cancelled())
        {
            return om_err_cancelled.code;
        }
        return result_;
    }

  private:
    static job* take_slot_job_checked() noexcept
    {
        std::uint32_t slot = slot_index_none;
        return take_slot_job(slot);
    }
};

template <typename Body>
parallel_for_awaitable<Body> parallel_for(std::size_t begin, std::size_t end, Body body) noexcept
{
    return parallel_for_awaitable<Body>{begin, end, std::move(body), {}, {}, 0};
}
} // namespace thread
