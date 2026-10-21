#pragma once

#include <atomic>
#include <coroutine>
#include <cstddef>
#include <cstdint>
#include <new>
#include <span>
#include <thread>

#include "part/index_pool.hpp"
#include "thread/base/error.hpp"
#include "thread/task/task_graph.hpp"

namespace thread
{

enum class io_status : std::uint8_t
{
    ok = 0,
    eof = 1,
    would_block = 2,
    partial = 3,
    error = 4,
    cancelled = 5,
};

struct io_result
{
    std::ptrdiff_t bytes_{0};
    io_status status_{io_status::ok};
    errc error_{errc::ok};

    bool ok() const noexcept
    {
        return status_ == io_status::ok || status_ == io_status::partial;
    }

    bool would_block() const noexcept
    {
        return status_ == io_status::would_block;
    }
};

// 字节数据源：核心不含任何 OS 网络/文件 API
struct data_source
{
    void* self_{nullptr};
    io_result (*read_)(void*, std::span<std::byte>) noexcept{nullptr};
    io_result (*write_)(void*, std::span<const std::byte>) noexcept{nullptr};
    errc (*close_)(void*) noexcept{nullptr};

    io_result read(std::span<std::byte> buffer) noexcept
    {
        return read_ != nullptr ? read_(self_, buffer) : io_result{0, io_status::error,
                                                                   errc::invalid_argument};
    }

    io_result write(std::span<const std::byte> buffer) noexcept
    {
        return write_ != nullptr ? write_(self_, buffer) : io_result{0, io_status::error,
                                                                     errc::invalid_argument};
    }
};

enum class io_request_state : std::uint8_t
{
    free = 0,
    submitted = 1,
    completing = 2,
};

struct io_request
{
    task_id task_{};
    std::uint32_t epoch_{0};
    data_source* source_{nullptr};
    std::byte* buffer_{nullptr};
    std::size_t capacity_{0};
    std::size_t offset_{0};
    std::uint32_t slot_{0xFFFFFFFFu};
    bool write_{false};
    io_result result_{};
    std::atomic<std::uint8_t> state_{static_cast<std::uint8_t>(io_request_state::free)};
    std::atomic<bool> cancel_requested_{false};
};

struct io_event
{
    io_request* request_{nullptr};
    io_result result_{};
};

struct io_backend
{
    void* self_{nullptr};
    bool (*submit_)(void*, io_request*) noexcept{nullptr};
    void (*cancel_)(void*, io_request*) noexcept{nullptr};
    int (*poll_)(void*, io_event*, int max_events, int timeout_ms) noexcept{nullptr};
};

struct io_metrics
{
    std::atomic<std::uint64_t> submitted_{0};
    std::atomic<std::uint64_t> completed_{0};
    std::atomic<std::uint64_t> rejected_{0};
    std::atomic<std::uint64_t> cancelled_{0};
    std::atomic<std::uint64_t> bytes_read_{0};
    std::atomic<std::uint64_t> bytes_written_{0};
    std::atomic<std::uint64_t> errors_{0};
    std::atomic<std::uint64_t> wake_failures_{0};
    std::atomic<std::uint32_t> wake_fail_state_{0};
};

// 完成事件线程：只轮询后端并唤醒等待任务，不执行任务体
struct io_domain
{
    struct slot
    {
        io_request request_{};
        std::atomic<bool> in_use_{false};
    };

    slot* slots_{nullptr};
    std::uint32_t capacity_{0};
    index_pool free_{};
    io_backend backend_{};
    task_graph* graph_{nullptr};
    io_metrics metrics_{};
    std::jthread thread_{};
    std::atomic<bool> running_{false};
    std::atomic<std::uint32_t> pending_{0};



    io_domain() noexcept = default;

    io_domain(const io_domain&) = delete;
    io_domain& operator=(const io_domain&) = delete;

    ~io_domain()
    {
        destroy();
    }

    bool create(task_graph& graph, io_backend backend, std::uint32_t capacity) noexcept
    {
        if (slots_ != nullptr || capacity == 0)
        {
            return false;
        }
        void* memory = ::operator new(sizeof(slot) * capacity, std::align_val_t(64), std::nothrow);
        if (memory == nullptr)
        {
            return false;
        }
        slots_ = static_cast<slot*>(memory);
        for (std::uint32_t i = 0; i < capacity; ++i)
        {
            new (&slots_[i]) slot{};
        }
        if (!free_.create(capacity))
        {
            for (std::uint32_t i = 0; i < capacity; ++i)
            {
                slots_[i].~slot();
            }
            ::operator delete(static_cast<void*>(slots_), std::align_val_t(64));
            slots_ = nullptr;
            return false;
        }
        capacity_ = capacity;
        backend_ = backend;
        graph_ = &graph;
        pending_.store(0, std::memory_order_release);
        reset_metrics();
        return true;
    }

    void destroy() noexcept
    {
        stop();
        if (slots_ != nullptr)
        {
            for (std::uint32_t i = 0; i < capacity_; ++i)
            {
                slots_[i].~slot();
            }
            ::operator delete(static_cast<void*>(slots_), std::align_val_t(64));
            slots_ = nullptr;
        }
        capacity_ = 0;
        free_.destroy();
        graph_ = nullptr;
        pending_.store(0, std::memory_order_release);
        reset_metrics();
    }

    bool start() noexcept
    {
        if (slots_ == nullptr || running_.load(std::memory_order_acquire))
        {
            return false;
        }
        running_.store(true, std::memory_order_release);
        thread_ = std::jthread([this](std::stop_token token) { run_loop(token); });
        return true;
    }

    void stop() noexcept
    {
        if (!running_.exchange(false, std::memory_order_acq_rel))
        {
            return;
        }
        thread_.request_stop();
        if (thread_.joinable())
        {
            thread_.join();
        }
    }

    bool running() const noexcept
    {
        return running_.load(std::memory_order_acquire);
    }

    io_request* acquire() noexcept
    {
        std::uint32_t index = index_pool::none;
        if (!free_.allocate(index))
        {
            metrics_.rejected_.fetch_add(1, std::memory_order_relaxed);
            return nullptr;
        }
        slots_[index].in_use_.store(true, std::memory_order_release);
        io_request& request = slots_[index].request_;
        request.task_ = invalid_task;
        request.slot_ = index;
        request.source_ = nullptr;
        request.buffer_ = nullptr;
        request.capacity_ = 0;
        request.offset_ = 0;
        request.write_ = false;
        request.result_ = io_result{};
        request.cancel_requested_.store(false, std::memory_order_relaxed);
        request.state_.store(static_cast<std::uint8_t>(io_request_state::submitted),
                             std::memory_order_release);
        return &request;
    }

    void release(io_request* request) noexcept
    {
        if (request == nullptr)
        {
            return;
        }
        const std::uint32_t index = request->slot_;
        if (index >= capacity_)
        {
            return;
        }
        request->state_.store(static_cast<std::uint8_t>(io_request_state::free),
                              std::memory_order_release);
        slots_[index].in_use_.store(false, std::memory_order_release);
        free_.release(index);
    }

    bool submit(io_request* request) noexcept
    {
        if (backend_.submit_ == nullptr || request == nullptr)
        {
            return false;
        }
        if (!backend_.submit_(backend_.self_, request))
        {
            metrics_.rejected_.fetch_add(1, std::memory_order_relaxed);
            return false;
        }
        pending_.fetch_add(1, std::memory_order_acq_rel);
        metrics_.submitted_.fetch_add(1, std::memory_order_relaxed);
        if (graph_ != nullptr)
        {
            if (trace_recorder* tracer = graph_->trace(); tracer != nullptr)
            {
                tracer->record(trace_kind::io_submit, request->task_, task_worker_none,
                               thread::role::general, 0);
            }
        }
        return true;
    }

    void cancel(io_request* request) noexcept
    {
        if (request == nullptr)
        {
            return;
        }
        request->cancel_requested_.store(true, std::memory_order_release);
        if (backend_.cancel_ != nullptr)
        {
            backend_.cancel_(backend_.self_, request);
        }
        metrics_.cancelled_.fetch_add(1, std::memory_order_relaxed);
    }

    void reset_metrics() noexcept
    {
        metrics_.submitted_.store(0, std::memory_order_relaxed);
        metrics_.completed_.store(0, std::memory_order_relaxed);
        metrics_.rejected_.store(0, std::memory_order_relaxed);
        metrics_.cancelled_.store(0, std::memory_order_relaxed);
        metrics_.bytes_read_.store(0, std::memory_order_relaxed);
        metrics_.bytes_written_.store(0, std::memory_order_relaxed);
        metrics_.errors_.store(0, std::memory_order_relaxed);
        metrics_.wake_failures_.store(0, std::memory_order_relaxed);
        metrics_.wake_fail_state_.store(0, std::memory_order_relaxed);
    }

    std::uint32_t queue_depth() const noexcept
    {
        const std::int64_t free_slots = free_.available();
        const std::int64_t used = static_cast<std::int64_t>(capacity_) - free_slots;
        return used > 0 ? static_cast<std::uint32_t>(used) : 0;
    }

  private:
    void complete(io_request* request, const io_result& result) noexcept
    {
        if (request == nullptr)
        {
            return;
        }
        request->result_ = result;
        if (graph_ != nullptr)
        {
            if (trace_recorder* tracer = graph_->trace(); tracer != nullptr)
            {
                tracer->record(trace_kind::io_complete, request->task_, task_worker_none,
                               thread::role::general, 0);
            }
        }
        request->state_.store(static_cast<std::uint8_t>(io_request_state::completing),
                              std::memory_order_release);
        if (pending_.load(std::memory_order_acquire) > 0)
        {
            pending_.fetch_sub(1, std::memory_order_acq_rel);
        }
        metrics_.completed_.fetch_add(1, std::memory_order_relaxed);
        if (result.bytes_ > 0)
        {
            if (request->write_)
            {
                metrics_.bytes_written_.fetch_add(static_cast<std::uint64_t>(result.bytes_),
                                                  std::memory_order_relaxed);
            }
            else
            {
                metrics_.bytes_read_.fetch_add(static_cast<std::uint64_t>(result.bytes_),
                                                std::memory_order_relaxed);
            }
        }
        if (result.status_ == io_status::error)
        {
            metrics_.errors_.fetch_add(1, std::memory_order_relaxed);
        }
        if (request->write_ && result.status_ == io_status::partial && result.bytes_ > 0 &&
            request->offset_ + static_cast<std::size_t>(result.bytes_) < request->capacity_)
        {
            // 短写：推进偏移后重新提交，调用方仍只看到一次 await
            request->offset_ += static_cast<std::size_t>(result.bytes_);
            request->buffer_ += result.bytes_;
            request->capacity_ -= static_cast<std::size_t>(result.bytes_);
            request->result_ = io_result{};
            request->state_.store(static_cast<std::uint8_t>(io_request_state::submitted),
                                  std::memory_order_release);
            if (submit(request))
            {
                return;
            }
        }
        if (graph_ != nullptr && request->task_.valid())
        {
            if (!graph_->wake_waiting(request->task_))
            {
                metrics_.wake_failures_.fetch_add(1, std::memory_order_relaxed);
            }
        }
    }

    // 取消作用域命中的在途请求：调用后端撤销并立即完成
    void cancel_cancelled() noexcept
    {
        if (graph_ == nullptr || slots_ == nullptr)
        {
            return;
        }
        for (std::uint32_t i = 0; i < capacity_; ++i)
        {
            if (!slots_[i].in_use_.load(std::memory_order_acquire))
            {
                continue;
            }
            io_request& request = slots_[i].request_;
            if (request.state_.load(std::memory_order_acquire) !=
                static_cast<std::uint8_t>(io_request_state::submitted))
            {
                continue;
            }
            if (!request.task_.valid() || !graph_->cancelled(request.task_))
            {
                continue;
            }
            if (backend_.cancel_ != nullptr)
            {
                backend_.cancel_(backend_.self_, &request);
            }
            metrics_.cancelled_.fetch_add(1, std::memory_order_relaxed);
            complete(&request, io_result{0, io_status::cancelled, errc::cancelled});
        }
    }

    // 自愈补唤醒：已完成但等待方尚未取走的请求，每次轮询补一次唤醒
    void retry_wakes() noexcept
    {
        if (graph_ == nullptr || slots_ == nullptr)
        {
            return;
        }
        for (std::uint32_t i = 0; i < capacity_; ++i)
        {
            if (!slots_[i].in_use_.load(std::memory_order_acquire))
            {
                continue;
            }
            io_request& request = slots_[i].request_;
            if (request.state_.load(std::memory_order_acquire) !=
                static_cast<std::uint8_t>(io_request_state::completing))
            {
                continue;
            }
            if (request.task_.valid())
            {
                // 只补唤醒仍在等待的任务：运行中的任务已由 complete 记录延迟唤醒，
                // 此时再唤醒会污染下一次等待
                if (graph_->waiting(request.task_))
                {
                    (void)graph_->wake_waiting(request.task_);
                }
            }
        }
    }

    void run_loop(std::stop_token token) noexcept
    {
        io_event events[16];
        int idle = 0;
        while (!token.stop_requested())
        {
            const int count = backend_.poll_ != nullptr
                                  ? backend_.poll_(backend_.self_, events, 16, 5)
                                  : 0;
            for (int i = 0; i < count; ++i)
            {
                complete(events[i].request_, events[i].result_);
            }
            // 每轮都执行：取消命中与完成补唤醒不能走自旋快路被跳过
            cancel_cancelled();
            retry_wakes();
            if (count == 0)
            {
                // 有在途请求时持续自旋，避免完成事件被 1ms 停放粒度拖慢
                if (pending_.load(std::memory_order_acquire) > 0 || ++idle < 256)
                {
                    std::this_thread::yield();
                    continue;
                }
                idle = 0;
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            else
            {
                idle = 0;
            }
        }
        running_.store(false, std::memory_order_release);
    }
};

// ---------------------------------------------------------------------------
// co_await read_some / write_all
// ---------------------------------------------------------------------------

struct io_awaitable
{
    io_domain* domain_{nullptr};
    data_source* source_{nullptr};
    std::span<std::byte> buffer_{};
    bool write_{false};
    io_result result_{};

    io_awaitable() noexcept = default;

    io_awaitable(io_domain* domain, data_source* source, std::span<std::byte> buffer,
                 bool write) noexcept
        : domain_(domain), source_(source), buffer_(buffer), write_(write)
    {
    }

    bool await_ready() const noexcept
    {
        return domain_ == nullptr || source_ == nullptr || buffer_.empty();
    }

    bool await_suspend(std::coroutine_handle<>) noexcept
    {
        task_context* ctx = current_task_context();
        if (ctx == nullptr || ctx->cancelled())
        {
            result_ = io_result{0, io_status::cancelled, errc::cancelled};
            return false;
        }
        io_request* request = domain_->acquire();
        if (request == nullptr)
        {
            result_ = io_result{0, io_status::error, errc::queue_full};
            return false;
        }
        (void)ctx->graph_->prepare_external_wait(ctx->id_);
        request->task_ = ctx->id_;
        request->source_ = source_;
        request->buffer_ = buffer_.data();
        request->capacity_ = buffer_.size();
        request->write_ = write_;
        if (!domain_->submit(request))
        {
            domain_->release(request);
            result_ = io_result{0, io_status::error, errc::queue_full};
            return false;
        }
        pending_ = request;
        return true;
    }

    io_result await_resume() noexcept
    {
        if (pending_ == nullptr)
        {
            return result_;
        }
        io_request* request = pending_;
        pending_ = nullptr;
        const task_context* ctx = current_task_context();
        if (ctx != nullptr && ctx->cancelled())
        {
            domain_->cancel(request);
        }
        io_result result = request->result_;
        if (result.status_ == io_status::error && result.error_ == errc::ok)
        {
            result.error_ = errc::invariant_broken;
        }
        domain_->release(request);
        return result;
    }

  private:
    io_request* pending_{nullptr};
};

inline io_awaitable read_some(io_domain& domain, data_source& source,
                              std::span<std::byte> buffer) noexcept
{
    return io_awaitable{&domain, &source, buffer, false};
}

inline io_awaitable write_all(io_domain& domain, data_source& source,
                              std::span<const std::byte> buffer) noexcept
{
    return io_awaitable{&domain, &source,
                        std::span<std::byte>{const_cast<std::byte*>(buffer.data()),
                                             buffer.size()},
                        true};
}
} // namespace thread
