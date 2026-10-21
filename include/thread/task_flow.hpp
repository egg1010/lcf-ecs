#pragma once

#include <chrono>
#include <coroutine>
#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>

#include "thread/coroutine.hpp"
#include "thread/job.hpp"
#include "thread/platform.hpp"
#include "thread/sync.hpp"
#include "thread/task_graph.hpp"

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
    errc result_{errc::ok};

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
            result_ = errc::invariant_broken;
            return false;
        }
        if (ctx->cancelled())
        {
            result_ = errc::cancelled;
            return false;
        }
        if (!graph->prepare_switch(ctx->id_, target_))
        {
            result_ = errc::affinity_violation;
            return false;
        }
        return true;
    }

    errc await_resume() const noexcept
    {
        const task_context* ctx = current_task_context();
        if (ctx != nullptr && ctx->cancelled())
        {
            return errc::cancelled;
        }
        return result_;
    }
};

inline switch_to_awaitable switch_to(thread::role target) noexcept
{
    return switch_to_awaitable{target, errc::ok};
}

// ---------------------------------------------------------------------------
// next_frame / frame_sync：等待帧边界
// ---------------------------------------------------------------------------

struct next_frame_awaitable
{
    thread::role target_{thread::role::general};
    errc result_{errc::ok};

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
            result_ = errc::invariant_broken;
            return false;
        }
        if (ctx->cancelled())
        {
            result_ = errc::cancelled;
            return false;
        }
        if (!graph->prepare_frame_wait(ctx->id_, target_))
        {
            result_ = errc::role_unbound;
            return false;
        }
        return true;
    }

    errc await_resume() const noexcept
    {
        const task_context* ctx = current_task_context();
        if (ctx != nullptr && ctx->cancelled())
        {
            return errc::cancelled;
        }
        return result_;
    }
};

inline next_frame_awaitable next_frame(thread::role target) noexcept
{
    return next_frame_awaitable{target, errc::ok};
}

struct frame_sync_awaitable
{
    thread::role first_{thread::role::general};
    thread::role second_{thread::role::general};
    errc result_{errc::ok};

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
            result_ = errc::invariant_broken;
            return false;
        }
        if (!graph->prepare_frame_pair(ctx->id_, first_, second_))
        {
            result_ = errc::invalid_argument;
            return false;
        }
        return true;
    }

    errc await_resume() const noexcept
    {
        return result_;
    }
};

inline frame_sync_awaitable frame_sync(thread::role first, thread::role second) noexcept
{
    return frame_sync_awaitable{first, second, errc::ok};
}

// ---------------------------------------------------------------------------
// join / when_all：等待子任务
// ---------------------------------------------------------------------------

struct join_awaitable
{
    task_id child_{};
    errc result_{errc::ok};

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
            result_ = errc::invariant_broken;
            return false;
        }
        if (!graph->prepare_join(ctx->id_, child_))
        {
            result_ = errc::task_identity_invalid;
            return false;
        }
        return true;
    }

    errc await_resume() const noexcept
    {
        task_graph* graph = current_graph();
        const task_context* ctx = current_task_context();
        if (ctx != nullptr && ctx->cancelled())
        {
            return errc::cancelled;
        }
        if (graph == nullptr || !child_.valid())
        {
            return result_;
        }
        const task_state state = graph->state_of(child_);
        if (state == task_state::faulted)
        {
            return errc::task_body_faulted;
        }
        if (state == task_state::cancelled)
        {
            return errc::cancelled;
        }
        return result_;
    }
};

inline join_awaitable join(task_id child) noexcept
{
    return join_awaitable{child, errc::ok};
}

struct when_all_awaitable
{
    std::span<const task_id> children_{};
    std::span<errc> errors_{};
    errc result_{errc::ok};
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
            result_ = errc::invariant_broken;
            return false;
        }
        for (std::size_t i = 0; i < children_.size(); ++i)
        {
            if (!graph->prepare_join(ctx->id_, children_[i]))
            {
                result_ = errc::task_identity_invalid;
                break;
            }
            ++armed_;
        }
        return armed_ != 0;
    }

    errc await_resume() noexcept
    {
        task_graph* graph = current_graph();
        for (std::size_t i = 0; i < children_.size() && i < errors_.size(); ++i)
        {
            errc value = errc::ok;
            if (graph != nullptr)
            {
                const task_state state = graph->state_of(children_[i]);
                if (state == task_state::faulted)
                {
                    value = errc::task_body_faulted;
                }
                else if (state == task_state::cancelled)
                {
                    value = errc::cancelled;
                }
                else if (state != task_state::completed)
                {
                    value = errc::task_state_conflict;
                }
            }
            errors_[i] = value;
        }
        return result_;
    }
};

inline when_all_awaitable when_all(std::span<const task_id> children,
                                   std::span<errc> errors) noexcept
{
    return when_all_awaitable{children, errors, errc::ok, 0};
}

struct when_any_awaitable
{
    std::span<const task_id> children_{};
    errc result_{errc::ok};
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
            result_ = errc::invariant_broken;
            return false;
        }
        if (!graph->prepare_join_any(ctx->id_, children_))
        {
            result_ = errc::task_identity_invalid;
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
    return when_any_awaitable{children, errc::ok, invalid_task};
}

// ---------------------------------------------------------------------------
// sleep_for / sleep_until：由看门狗扫描唤醒
// ---------------------------------------------------------------------------

struct sleep_awaitable
{
    std::uint64_t delay_ns_{0};
    errc result_{errc::ok};

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
            result_ = errc::invariant_broken;
            return false;
        }
        if (ctx->cancelled())
        {
            result_ = errc::cancelled;
            return false;
        }
        if (!graph->prepare_timed(ctx->id_, delay_ns_))
        {
            result_ = errc::invariant_broken;
            return false;
        }
        return true;
    }

    errc await_resume() const noexcept
    {
        const task_context* ctx = current_task_context();
        if (ctx != nullptr && ctx->cancelled())
        {
            return errc::cancelled;
        }
        return result_;
    }
};

inline sleep_awaitable sleep_for(std::chrono::nanoseconds duration) noexcept
{
    const auto count = std::chrono::duration_cast<std::chrono::nanoseconds>(duration).count();
    return sleep_awaitable{count > 0 ? static_cast<std::uint64_t>(count) : 0ull, errc::ok};
}

inline sleep_awaitable sleep_until(std::chrono::steady_clock::time_point deadline) noexcept
{
    const std::uint64_t now = monotonic_now_ns();
    const std::uint64_t target = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(deadline.time_since_epoch()).count());
    return sleep_awaitable{target > now ? target - now : 0ull, errc::ok};
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
            (void)holder->graph_->wake_waiting(holder->task_);
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
    errc result_{errc::ok};

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
            if (!graph->submit_work(target))
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
        if (!graph->prepare_external_wait(ctx->id_))
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

    errc await_resume() noexcept
    {
        const task_context* ctx = current_task_context();
        if (ctx != nullptr && ctx->cancelled())
        {
            return errc::cancelled;
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
    return parallel_for_awaitable<Body>{begin, end, std::move(body), {}, {}, errc::ok};
}
} // namespace thread
