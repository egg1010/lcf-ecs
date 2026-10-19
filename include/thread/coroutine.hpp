// 红线：worker 内禁同步等待，挂起一律 co_await

#pragma once

#include <atomic>
#include <coroutine>
#include <cstddef>
#include <cstdint>
#include <new>
#include <utility>

#include "thread/error.hpp"
#include "thread/job.hpp"
#include "thread/platform.hpp"
#include "thread/runtime.hpp"
#include "thread/sync.hpp"
#include "thread/worker.hpp"

namespace thread
{

inline constexpr std::size_t coro_frame_tiers[] = {128, 256, 512};
inline constexpr std::size_t coro_frame_tier_count =
    sizeof(coro_frame_tiers) / sizeof(coro_frame_tiers[0]);

struct coroutine_stats
{
    std::atomic<std::uint64_t> frame_allocs_{0};
    std::atomic<std::uint64_t> frame_frees_{0};
    std::atomic<std::uint64_t> suspended_{0};
    std::atomic<std::uint64_t> resumed_{0};
    std::atomic<std::uint64_t> polled_{0};
};

inline coroutine_stats& coroutine_counters() noexcept
{
    static coroutine_stats stats{};
    return stats;
}

inline std::size_t coro_tier_size(std::size_t size) noexcept
{
    for (std::size_t i = 0; i < coro_frame_tier_count; ++i)
    {
        if (size <= coro_frame_tiers[i])
        {
            return coro_frame_tiers[i];
        }
    }
    return size;
}

struct cancel_token
{
    cancel_token() noexcept = default;

    explicit cancel_token(std::atomic<bool>* flag) noexcept : flag_(flag)
    {
    }

    bool cancelled() const noexcept
    {
        return flag_ != nullptr && flag_->load(std::memory_order_relaxed);
    }

    bool poll() const noexcept
    {
        const bool value = cancelled();
        if (value)
        {
            coroutine_counters().polled_.fetch_add(1, std::memory_order_relaxed);
        }
        return value;
    }

    bool await_ready() const noexcept
    {
        return true;
    }

    void await_suspend(std::coroutine_handle<>) const noexcept
    {
    }

    bool await_resume() const noexcept
    {
        return cancelled();
    }

  private:
    std::atomic<bool>* flag_{nullptr};
};

struct resume_waiter
{
    waiter_node node_{};
    std::coroutine_handle<> handle_{};
    std::atomic<bool> suspended_{false};
    std::atomic<bool> pending_resume_{false};
};

inline void resume_waiter_wake(waiter_node* self) noexcept
{
    auto* holder = reinterpret_cast<resume_waiter*>(self);
    if (holder->suspended_.exchange(false, std::memory_order_acq_rel))
    {
        if (holder->handle_ != nullptr && !holder->handle_.done())
        {
            holder->handle_.resume();
        }
        return;
    }

    holder->pending_resume_.store(true, std::memory_order_release);
}

inline void resume_waiter_init(resume_waiter* w) noexcept
{
    waiter_node_init(&w->node_, &resume_waiter_wake);
    w->handle_ = nullptr;
    w->suspended_.store(false, std::memory_order_relaxed);
    w->pending_resume_.store(false, std::memory_order_relaxed);
}

struct counter_awaiter
{
    counter* counter_{nullptr};
    resume_waiter* waiter_{nullptr};

    bool await_ready() const noexcept
    {
        return counter_ == nullptr || counter_->ready();
    }

    bool await_suspend(std::coroutine_handle<> handle) noexcept
    {

        if (counter_ == nullptr || counter_->ready())
        {
            return false;
        }
        waiter_->handle_ = handle;
        waiter_->pending_resume_.store(false, std::memory_order_relaxed);
        const bool armed = counter_->attach_waiter(&waiter_->node_);
        if (!armed)
        {
            waiter_->handle_ = nullptr;
            return false;
        }
        waiter_->suspended_.store(true, std::memory_order_release);
        if (waiter_->pending_resume_.load(std::memory_order_acquire))
        {

            waiter_->suspended_.store(false, std::memory_order_release);
            waiter_->handle_ = nullptr;
            return false;
        }
        coroutine_counters().suspended_.fetch_add(1, std::memory_order_relaxed);
        return true;
    }

    void await_resume() const noexcept
    {
        coroutine_counters().resumed_.fetch_add(1, std::memory_order_relaxed);
    }
};

struct event_awaiter
{
    event* event_{nullptr};
    resume_waiter* waiter_{nullptr};

    bool await_ready() const noexcept
    {
        return event_ == nullptr || event_->triggered();
    }

    bool await_suspend(std::coroutine_handle<> handle) noexcept
    {
        if (event_ == nullptr || event_->triggered())
        {
            return false;
        }
        waiter_->handle_ = handle;
        waiter_->pending_resume_.store(false, std::memory_order_relaxed);
        const bool armed = event_->attach_waiter(&waiter_->node_);
        if (!armed)
        {
            waiter_->handle_ = nullptr;
            return false;
        }
        waiter_->suspended_.store(true, std::memory_order_release);
        if (waiter_->pending_resume_.load(std::memory_order_acquire))
        {
            waiter_->suspended_.store(false, std::memory_order_release);
            waiter_->handle_ = nullptr;
            return false;
        }
        coroutine_counters().suspended_.fetch_add(1, std::memory_order_relaxed);
        return true;
    }

    void await_resume() const noexcept
    {
        coroutine_counters().resumed_.fetch_add(1, std::memory_order_relaxed);
    }
};

struct coroutine_resume
{
    std::coroutine_handle<> handle_{};
};

inline void run_coroutine_resume(void* ctx) noexcept
{
    auto* self = reinterpret_cast<coroutine_resume*>(ctx);
    if (self->handle_ != nullptr && !self->handle_.done())
    {
        coroutine_counters().resumed_.fetch_add(1, std::memory_order_relaxed);
        self->handle_.resume();
    }
}

template <typename T> struct task
{
    struct promise_type
    {
        T value_{};
        std::coroutine_handle<> continuation_{};

        static void* operator new(std::size_t size) noexcept
        {
            const std::size_t tier = coro_tier_size(size);
            void* memory = current_runtime()->frames_.allocate(tier, frame_arena_alignment);
            if (memory != nullptr)
            {
                coroutine_counters().frame_allocs_.fetch_add(1, std::memory_order_relaxed);
            }
            return memory;
        }

        static void operator delete(void* memory) noexcept
        {
            if (memory != nullptr)
            {
                coroutine_counters().frame_frees_.fetch_add(1, std::memory_order_relaxed);
                current_runtime()->frames_.deallocate(memory);
            }
        }

        static void operator delete(void* memory, std::size_t) noexcept
        {
            operator delete(memory);
        }

        static void* operator new(std::size_t size, std::align_val_t alignment) noexcept
        {
            const std::size_t tier = coro_tier_size(size);
            void* memory = current_runtime()->frames_.allocate(
                tier, static_cast<std::size_t>(alignment));
            if (memory != nullptr)
            {
                coroutine_counters().frame_allocs_.fetch_add(1, std::memory_order_relaxed);
            }
            return memory;
        }

        static void operator delete(void* memory, std::align_val_t alignment) noexcept
        {
            if (memory != nullptr)
            {
                coroutine_counters().frame_frees_.fetch_add(1, std::memory_order_relaxed);
                current_runtime()->frames_.deallocate_aligned(
                    memory, static_cast<std::size_t>(alignment));
            }
        }

        task get_return_object() noexcept
        {
            return task{std::coroutine_handle<promise_type>::from_promise(*this)};
        }

        static task get_return_object_on_allocation_failure() noexcept
        {
            return task{};
        }

        std::suspend_always initial_suspend() noexcept
        {
            return {};
        }

        struct final_awaiter
        {
            bool await_ready() const noexcept
            {
                return false;
            }

            std::coroutine_handle<>
            await_suspend(std::coroutine_handle<promise_type> handle) noexcept
            {
                std::coroutine_handle<> continuation = handle.promise().continuation_;
                if (continuation != nullptr && !continuation.done())
                {
                    return continuation;
                }
                return std::noop_coroutine();
            }

            void await_resume() const noexcept
            {
            }
        };

        final_awaiter final_suspend() noexcept
        {
            return {};
        }

        void return_value(T value) noexcept
        {
            value_ = value;
        }

        void unhandled_exception() noexcept
        {
            FRAMEWORK_REPORT(error_severity::fatal, errc::handler_faulted);
        }
    };

    using handle_type = std::coroutine_handle<promise_type>;

    task() noexcept = default;

    explicit task(handle_type handle) noexcept : handle_(handle)
    {
    }

    task(const task&) = delete;
    task& operator=(const task&) = delete;

    task(task&& other) noexcept : handle_(other.handle_)
    {
        other.handle_ = nullptr;
    }

    task& operator=(task&& other) noexcept
    {
        if (this != &other)
        {
            destroy();
            handle_ = other.handle_;
            other.handle_ = nullptr;
        }
        return *this;
    }

    ~task()
    {
        destroy();
    }

    void destroy() noexcept
    {
        if (handle_ != nullptr)
        {
            handle_.destroy();
            handle_ = nullptr;
        }
    }

    handle_type release() noexcept
    {
        handle_type const handle = handle_;
        handle_ = nullptr;
        return handle;
    }

    bool valid() const noexcept
    {
        return handle_ != nullptr;
    }

    void start() noexcept
    {
        if (handle_ != nullptr && !handle_.done())
        {
            handle_.resume();
        }
    }

    bool done() const noexcept
    {
        return handle_ == nullptr || handle_.done();
    }

    T value() const noexcept
    {
        return handle_ != nullptr ? handle_.promise().value_ : T{};
    }

    handle_type handle() const noexcept
    {
        return handle_;
    }

    struct awaiter
    {
        handle_type handle_{};

        bool await_ready() const noexcept
        {
            return handle_ == nullptr || handle_.done();
        }

        std::coroutine_handle<> await_suspend(std::coroutine_handle<> continuation) noexcept
        {
            handle_.promise().continuation_ = continuation;
            return handle_;
        }

        T await_resume() const noexcept
        {
            return handle_.promise().value_;
        }
    };

    awaiter operator co_await() const noexcept
    {
        return awaiter{handle_};
    }

  private:
    handle_type handle_{nullptr};
};

template <> struct task<void>
{
    struct promise_type
    {
        std::coroutine_handle<> continuation_{};

        static void* operator new(std::size_t size) noexcept
        {
            const std::size_t tier = coro_tier_size(size);
            void* memory = current_runtime()->frames_.allocate(tier, frame_arena_alignment);
            if (memory != nullptr)
            {
                coroutine_counters().frame_allocs_.fetch_add(1, std::memory_order_relaxed);
            }
            return memory;
        }

        static void operator delete(void* memory) noexcept
        {
            if (memory != nullptr)
            {
                coroutine_counters().frame_frees_.fetch_add(1, std::memory_order_relaxed);
                current_runtime()->frames_.deallocate(memory);
            }
        }

        static void operator delete(void* memory, std::size_t) noexcept
        {
            operator delete(memory);
        }

        static void* operator new(std::size_t size, std::align_val_t alignment) noexcept
        {
            const std::size_t tier = coro_tier_size(size);
            void* memory = current_runtime()->frames_.allocate(
                tier, static_cast<std::size_t>(alignment));
            if (memory != nullptr)
            {
                coroutine_counters().frame_allocs_.fetch_add(1, std::memory_order_relaxed);
            }
            return memory;
        }

        static void operator delete(void* memory, std::align_val_t alignment) noexcept
        {
            if (memory != nullptr)
            {
                coroutine_counters().frame_frees_.fetch_add(1, std::memory_order_relaxed);
                current_runtime()->frames_.deallocate_aligned(
                    memory, static_cast<std::size_t>(alignment));
            }
        }

        task get_return_object() noexcept
        {
            return task{std::coroutine_handle<promise_type>::from_promise(*this)};
        }

        static task get_return_object_on_allocation_failure() noexcept
        {
            return task{};
        }

        std::suspend_always initial_suspend() noexcept
        {
            return {};
        }

        struct final_awaiter
        {
            bool await_ready() const noexcept
            {
                return false;
            }

            std::coroutine_handle<>
            await_suspend(std::coroutine_handle<promise_type> handle) noexcept
            {
                std::coroutine_handle<> continuation = handle.promise().continuation_;
                if (continuation != nullptr && !continuation.done())
                {
                    return continuation;
                }
                return std::noop_coroutine();
            }

            void await_resume() const noexcept
            {
            }
        };

        final_awaiter final_suspend() noexcept
        {
            return {};
        }

        void return_void() noexcept
        {
        }

        void unhandled_exception() noexcept
        {
            FRAMEWORK_REPORT(error_severity::fatal, errc::handler_faulted);
        }
    };

    using handle_type = std::coroutine_handle<promise_type>;

    task() noexcept = default;

    explicit task(handle_type handle) noexcept : handle_(handle)
    {
    }

    task(const task&) = delete;
    task& operator=(const task&) = delete;

    task(task&& other) noexcept : handle_(other.handle_)
    {
        other.handle_ = nullptr;
    }

    task& operator=(task&& other) noexcept
    {
        if (this != &other)
        {
            destroy();
            handle_ = other.handle_;
            other.handle_ = nullptr;
        }
        return *this;
    }

    ~task()
    {
        destroy();
    }

    void destroy() noexcept
    {
        if (handle_ != nullptr)
        {
            handle_.destroy();
            handle_ = nullptr;
        }
    }

    handle_type release() noexcept
    {
        handle_type const handle = handle_;
        handle_ = nullptr;
        return handle;
    }

    bool valid() const noexcept
    {
        return handle_ != nullptr;
    }

    void start() noexcept
    {
        if (handle_ != nullptr && !handle_.done())
        {
            handle_.resume();
        }
    }

    bool done() const noexcept
    {
        return handle_ == nullptr || handle_.done();
    }

    handle_type handle() const noexcept
    {
        return handle_;
    }

    struct awaiter
    {
        handle_type handle_{};

        bool await_ready() const noexcept
        {
            return handle_ == nullptr || handle_.done();
        }

        std::coroutine_handle<> await_suspend(std::coroutine_handle<> continuation) noexcept
        {
            handle_.promise().continuation_ = continuation;
            return handle_;
        }

        void await_resume() const noexcept
        {
        }
    };

    awaiter operator co_await() const noexcept
    {
        return awaiter{handle_};
    }

  private:
    handle_type handle_{nullptr};
};

struct yield_awaitable
{
    bool await_ready() const noexcept
    {
        return current_worker() == nullptr;
    }

    void await_suspend(std::coroutine_handle<> handle) noexcept
    {
        worker* const self = current_worker();
        if (self == nullptr || self->scheduler_stopping() || !self->reschedule(handle))
        {
            coroutine_counters().resumed_.fetch_add(1, std::memory_order_relaxed);
            if (!handle.done())
            {
                handle.resume();
            }
            return;
        }
        coroutine_counters().suspended_.fetch_add(1, std::memory_order_relaxed);
    }

    void await_resume() const noexcept
    {
        coroutine_counters().resumed_.fetch_add(1, std::memory_order_relaxed);
    }
};

inline yield_awaitable co_yield_now() noexcept
{
    return {};
}
} // namespace thread
