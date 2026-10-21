#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <thread>

#include "thread/error.hpp"
#include "thread/task_graph.hpp"

namespace thread
{

struct blocking_op
{
    void (*run_)(void* arg) noexcept{nullptr};
    void* arg_{nullptr};
    task_id task_{};
};

inline constexpr std::uint32_t blocking_max_threads = 8;

// 独立阻塞卸载池：只执行阻塞调用，不执行任务体；完成票据回投原域
struct blocking_domain
{
    mpmc_ring<blocking_op> queue_{};
    std::jthread threads_[blocking_max_threads]{};
    task_graph* graph_{nullptr};
    std::atomic<std::uint32_t> thread_count_{0};
    std::atomic<std::uint64_t> completed_{0};
    std::atomic<std::uint64_t> rejected_{0};
    std::atomic<bool> running_{false};

    blocking_domain() noexcept = default;

    blocking_domain(const blocking_domain&) = delete;
    blocking_domain& operator=(const blocking_domain&) = delete;

    ~blocking_domain()
    {
        destroy();
    }

    bool create(task_graph& graph, std::uint32_t thread_count, std::uint32_t queue_capacity) noexcept
    {
        if (running_.load(std::memory_order_acquire) || thread_count == 0 ||
            thread_count > blocking_max_threads)
        {
            return false;
        }
        graph_ = &graph;
        if (!queue_.create(queue_capacity))
        {
            return false;
        }
        running_.store(true, std::memory_order_release);
        thread_count_.store(thread_count, std::memory_order_release);
        for (std::uint32_t i = 0; i < thread_count; ++i)
        {
            threads_[i] = std::jthread([this](std::stop_token token) { run_loop(token); });
        }
        return true;
    }

    void destroy() noexcept
    {
        if (!running_.exchange(false, std::memory_order_acq_rel))
        {
            queue_.destroy();
            return;
        }
        for (std::uint32_t i = 0; i < thread_count_.load(std::memory_order_acquire); ++i)
        {
            threads_[i].request_stop();
        }
        for (std::uint32_t i = 0; i < thread_count_.load(std::memory_order_acquire); ++i)
        {
            if (threads_[i].joinable())
            {
                threads_[i].join();
            }
        }
        queue_.destroy();
        graph_ = nullptr;
        thread_count_.store(0, std::memory_order_release);
    }

    void stop() noexcept
    {
        destroy();
    }

    bool submit(const blocking_op& op) noexcept
    {
        if (!running_.load(std::memory_order_acquire) || !queue_.try_push(op))
        {
            rejected_.fetch_add(1, std::memory_order_relaxed);
            return false;
        }
        return true;
    }

    std::uint32_t queue_depth() const noexcept
    {
        const std::int64_t depth = queue_.size();
        return depth > 0 ? static_cast<std::uint32_t>(depth) : 0;
    }

  private:
    void run_loop(std::stop_token token) noexcept
    {
        int idle = 0;
        while (!token.stop_requested())
        {
            blocking_op op{};
            if (!queue_.try_pop(op))
            {
                if (++idle < 256)
                {
                    std::this_thread::yield();
                    continue;
                }
                idle = 0;
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                continue;
            }
            idle = 0;
            if (op.run_ != nullptr)
            {
                op.run_(op.arg_);
            }
            completed_.fetch_add(1, std::memory_order_relaxed);
            if (graph_ != nullptr && op.task_.valid())
            {
                (void)graph_->wake_waiting(op.task_);
            }
        }
    }
};

struct blocking_awaitable
{
    blocking_domain* domain_{nullptr};
    void (*run_)(void* arg) noexcept{nullptr};
    void* arg_{nullptr};
    errc result_{errc::ok};

    bool await_ready() const noexcept
    {
        return domain_ == nullptr || run_ == nullptr;
    }

    bool await_suspend(std::coroutine_handle<>) noexcept
    {
        task_context* ctx = current_task_context();
        if (ctx == nullptr || ctx->cancelled())
        {
            result_ = errc::cancelled;
            return false;
        }
        (void)ctx->graph_->prepare_external_wait(ctx->id_);
        blocking_op op{run_, arg_, ctx->id_};
        if (!domain_->submit(op))
        {
            result_ = errc::queue_full;
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

inline blocking_awaitable blocking_call(blocking_domain& domain,
                                        void (*run)(void* arg) noexcept,
                                        void* arg = nullptr) noexcept
{
    return blocking_awaitable{&domain, run, arg, errc::ok};
}
} // namespace thread
