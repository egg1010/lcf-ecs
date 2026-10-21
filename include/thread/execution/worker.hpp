#pragma once

#include <atomic>
#include <coroutine>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <thread>

#include "part/dense.hpp"
#include "thread/base/error.hpp"
#include "thread/base/job.hpp"
#include "thread/execution/lane.hpp"
#include "thread/queue/mpmc_queue.hpp"
#include "thread/base/platform.hpp"
#include "thread/execution/runtime.hpp"
#include "thread/base/tls.hpp"
#include "thread/execution/scheduler.hpp"
#include "thread/base/task_identity.hpp"
#include "thread/queue/work_deque.hpp"

namespace thread
{
inline constexpr std::int32_t worker_no_owner = -1;
inline constexpr std::int32_t worker_no_group = -1;
inline constexpr std::uint32_t worker_default_aging_threshold = 64;

inline constexpr std::size_t worker_mailbox_batch = 16;

struct task_graph;

// 任务域步进钩子：worker 不直接依赖任务图实现，避免头文件循环
struct worker_task_hooks
{
    void* self_{nullptr};
    bool (*step_)(void* self, std::uint32_t worker) noexcept{nullptr};
    bool (*has_work_)(void* self, std::uint32_t worker) noexcept{nullptr};
};

struct worker_stats
{
    std::atomic<std::uint64_t> executed_{0};
    std::atomic<std::uint64_t> local_{0};
    std::atomic<std::uint64_t> injected_{0};
    std::atomic<std::uint64_t> stolen_{0};
    std::atomic<std::uint64_t> steal_attempts_{0};
    std::atomic<std::uint64_t> parks_{0};
};

struct worker_config
{
    std::size_t local_capacity{work_deque_default_capacity};
    std::size_t steal_batch{64};
    bool pin_to_core{false};
    std::int32_t core_index{0};
    std::int32_t group_index{worker_no_group};
    std::uint8_t priority_levels{scheduler_default_levels};
    std::uint32_t aging_threshold{worker_default_aging_threshold};
};

struct worker;

inline worker*& current_worker() noexcept
{
    return tls().worker_;
}

struct worker
{
    worker() noexcept = default;

    worker(const worker&) = delete;
    worker& operator=(const worker&) = delete;

    ~worker()
    {
        join();
        stack_.destroy();
        mailbox_.destroy();
    }

    bool create(std::int32_t id, worker_config config) noexcept
    {
        if (created_)
        {
            return false;
        }
        id_ = id;
        config_ = config;
        stack_.aging_threshold = config.aging_threshold;
        if (!stack_.create(config.priority_levels == 0 ? scheduler_default_levels
                                                       : config.priority_levels,
                           config.local_capacity))
        {
            FRAMEWORK_REPORT(error_severity::error, errc::invariant_broken);
            return false;
        }
        steal_scratch_.resize_fill(config.steal_batch == 0 ? 64 : config.steal_batch, nullptr);
        park_state_init(&park_);
        created_ = true;
        return true;
    }

    std::int32_t id() const noexcept
    {
        return id_;
    }

    priority_stack& stack() noexcept
    {
        return stack_;
    }

    const priority_stack& stack() const noexcept
    {
        return stack_;
    }

    worker_stats& stats() noexcept
    {
        return stats_;
    }

    mpmc_queue& mailbox() noexcept
    {
        return mailbox_;
    }

    bool create_mailbox(std::size_t capacity) noexcept
    {
        return mailbox_.create(capacity == 0 ? mpmc_default_capacity : capacity);
    }

    void start(worker** peers, std::size_t peer_count, mpmc_queue* inject_queue,
               std::atomic<bool>* shutdown_flag,
               std::atomic<std::uint64_t>* completed_counter,
               runtime_context* runtime = current_runtime(),
               const worker_task_hooks* hooks = nullptr, bool spawn = true) noexcept
    {
        if (!created_ || running_.load(std::memory_order_acquire))
        {
            return;
        }
        rt_ = runtime != nullptr ? runtime : default_runtime();
        peers_ = peers;
        peer_count_ = peer_count;
        inject_ = inject_queue;
        shutdown_ = shutdown_flag;
        completed_counter_ = completed_counter;
        hooks_ = hooks;
        running_.store(true, std::memory_order_release);
        if (spawn)
        {
            thread_ = std::thread(&worker::run_loop, this);
        }
    }

    void attach_lanes(lane_table* table) noexcept
    {
        lane_registry_ = table;
    }

    void join() noexcept
    {
        if (thread_.joinable())
        {
            thread_.join();
        }
    }

    bool running() const noexcept
    {
        return running_.load(std::memory_order_acquire);
    }

    void unpark() noexcept
    {
        unpark_other(&park_, park_.word_.load(std::memory_order_relaxed) + 1u);
    }

    bool is_parked() const noexcept
    {
        return park_.parks_.load(std::memory_order_acquire) != 0;
    }

    park_state& park_state_ref() noexcept
    {
        return park_;
    }

    std::uint64_t queue_depth() const noexcept
    {
        return static_cast<std::uint64_t>(stack_.total_size());
    }

    runtime_context* runtime() const noexcept
    {
        return rt_;
    }

    bool scheduler_stopping() const noexcept
    {
        return shutdown_ != nullptr && shutdown_->load(std::memory_order_acquire);
    }

    bool reschedule(std::coroutine_handle<> handle) noexcept
    {
        if (rt_ == nullptr || handle == nullptr || handle.done())
        {
            return false;
        }
        std::uint32_t slot = slot_index_none;
        job* target = rt_->jobs_.allocate(slot);
        if (target == nullptr)
        {
            return false;
        }
        target->set_slot_backed(true);
        resume_payload payload{handle};
        fill_inline_job(target, [payload]() mutable noexcept
        {
            run_resume_payload(static_cast<void*>(&payload));
        });
        if (stack_.push(target))
        {
            return true;
        }
        // 本地栈满：退投全局 inject 队列，避免退化为内联续跑（深循环下会递归增栈）
        slot = rt_->jobs_.index_of(target);
        if (slot != slot_index_none && inject_ != nullptr && inject_->try_push(slot))
        {
            return true;
        }
        release_job(target);
        return false;
    }

  private:
    enum class job_source : std::uint8_t
    {
        none = 0,
        local,
        injected,
        stolen,
        lane,
    };

    void run_loop() noexcept
    {
        if (config_.pin_to_core)
        {
            pin_current_thread_to_core(static_cast<std::uint32_t>(config_.core_index));
        }
        char name_buf[32] = {};
        const int len = std::snprintf(name_buf, sizeof(name_buf), "fw-worker-%d", id_);
        if (len > 0)
        {
            set_current_thread_name(name_buf);
        }

        bind_runtime(rt_);
        current_worker() = this;

        while (!shutdown_->load(std::memory_order_acquire))
        {
            if (hooks_ != nullptr && hooks_->step_ != nullptr &&
                hooks_->step_(hooks_->self_, static_cast<std::uint32_t>(id_)))
            {
                continue;
            }
            job_source source = job_source::none;
            job* next = stack_.pop();
            if (next != nullptr)
            {
                source = job_source::local;
            }
            if (next == nullptr)
            {
                std::uint32_t batch[worker_mailbox_batch] = {};
                const std::size_t taken = mailbox_.try_pop_bulk(batch, worker_mailbox_batch);
                if (taken > 0)
                {
                    for (std::size_t i = 1; i < taken; ++i)
                    {
                        job* moved = slot_from_index(batch[i]);
                        if (moved != nullptr && !stack_.push(moved))
                        {
                            release_job(moved);
                            FRAMEWORK_REPORT(error_severity::error, errc::job_slot_exhausted);
                        }
                    }
                    next = slot_from_index(batch[0]);
                    if (next != nullptr)
                    {
                        source = job_source::injected;
                    }
                }
            }
            if (next == nullptr && inject_ != nullptr)
            {
                std::uint32_t index = mpmc_index_none;
                if (inject_->try_pop(index))
                {
                    next = slot_from_index(index);
                    source = job_source::injected;
                }
            }
            if (next == nullptr)
            {
                next = try_steal_round();
                if (next != nullptr)
                {
                    source = job_source::stolen;
                }
            }
            if (next == nullptr)
            {

                next = try_drain_lanes();
                if (next != nullptr)
                {
                    source = job_source::lane;
                }
            }
            if (next == nullptr)
            {
                idle_wait();
                continue;
            }
            run_job(next);
            release_job(next);
            stats_.executed_.fetch_add(1, std::memory_order_relaxed);
            if (source == job_source::local)
            {
                stats_.local_.fetch_add(1, std::memory_order_relaxed);
            }
            else if (source == job_source::injected)
            {
                stats_.injected_.fetch_add(1, std::memory_order_relaxed);
            }
            if (completed_counter_ != nullptr)
            {
                completed_counter_->fetch_add(1, std::memory_order_release);
            }

            wake_sibling();
        }
        current_worker() = nullptr;
        running_.store(false, std::memory_order_release);
    }

    job* drain_one_lane(lane* l) noexcept
    {
        if (l == nullptr || l->empty())
        {
            return nullptr;
        }
        const std::uint32_t self = static_cast<std::uint32_t>(id_);
        if (l->mode() == lane_mode::shared)
        {
            if (!l->try_acquire(self))
            {
                return nullptr;
            }
            std::uint32_t index = mpmc_index_none;
            job* taken = nullptr;
            if (l->try_pop(index))
            {
                taken = slot_from_index(index);
            }
            l->release_ownership(self);
            return taken;
        }

        // exclusive / pinned：仅绑定者抽取（FIFO 顺序的前提）
        if (l->bound_worker() != self)
        {
            return nullptr;
        }
        std::uint32_t index = mpmc_index_none;
        if (l->try_pop(index))
        {
            job* taken = slot_from_index(index);
            if (taken != nullptr)
            {
                return taken;
            }
        }
        return nullptr;
    }

    job* try_drain_lanes() noexcept
    {
        if (lane_registry_ == nullptr || rt_ == nullptr)
        {
            return nullptr;
        }
        const std::size_t lane_count = lane_registry_->size();
        if (lane_count == 0)
        {
            return nullptr;
        }

        std::uint64_t bits = rt_->lane_ready_.load(std::memory_order_acquire);
        while (bits != 0)
        {
            const std::uint32_t index = count_trailing_zeros(bits);
            bits &= (bits - 1);
            if (index >= lane_count)
            {
                break;
            }
            job* taken = drain_one_lane(lane_registry_->at(index));
            if (taken != nullptr)
            {
                return taken;
            }
        }

        // 位图仅覆盖前 64 个 lane；默认配置 lane_capacity=64，此分支通常不触发
        for (std::size_t i = lane_bitmap_bits; i < lane_count; ++i)
        {
            job* taken = drain_one_lane(lane_registry_->at(i));
            if (taken != nullptr)
            {
                return taken;
            }
        }
        return nullptr;
    }

    void wake_sibling() noexcept
    {
        if (peers_ == nullptr || peer_count_ < 2)
        {
            return;
        }
        const std::size_t start =
            static_cast<std::size_t>(mix_bits(static_cast<std::uint64_t>(id_) + 1u) % peer_count_);
        for (std::size_t k = 0; k < peer_count_; ++k)
        {
            worker* peer = peers_[(start + k) % peer_count_];
            if (peer == nullptr || peer == this)
            {
                continue;
            }
            if (peer->is_parked())
            {
                peer->unpark();
                return;
            }
        }
    }

    void idle_wait() noexcept
    {
        for (int i = 0; i < 32; ++i)
        {
            if (has_work())
            {
                return;
            }
            platform_relax_pause();
        }
        for (int i = 0; i < 16; ++i)
        {
            if (has_work())
            {
                return;
            }
            platform_yield();
        }
        for (;;)
        {
            if (shutdown_->load(std::memory_order_acquire))
            {
                return;
            }
            // 先取唤醒字再查谓词：避免“检查之后、装载之前”的唤醒丢失
            const std::uint32_t expected = park_.word_.load(std::memory_order_acquire);
            if (has_work())
            {
                return;
            }
            stats_.parks_.fetch_add(1, std::memory_order_relaxed);
            (void)park_once(&park_, expected, default_park_ms);
            return;
        }
    }

    bool has_work() noexcept
    {
        if (hooks_ != nullptr && hooks_->has_work_ != nullptr &&
            hooks_->has_work_(hooks_->self_, static_cast<std::uint32_t>(id_)))
        {
            return true;
        }
        if (stack_.total_size() != 0)
        {
            return true;
        }
        if (mailbox_.size() != 0)
        {
            return true;
        }
        if (inject_ != nullptr && inject_->size() != 0)
        {
            return true;
        }
        if (lane_registry_ != nullptr && rt_ != nullptr)
        {
            const std::size_t lane_count = lane_registry_->size();
            std::uint64_t bits = rt_->lane_ready_.load(std::memory_order_acquire);
            while (bits != 0)
            {
                const std::uint32_t index = count_trailing_zeros(bits);
                bits &= (bits - 1);
                if (index >= lane_count)
                {
                    break;
                }
                lane* l = lane_registry_->at(index);
                if (l == nullptr || l->empty())
                {
                    continue;
                }
                if (l->mode() == lane_mode::shared ||
                    l->bound_worker() == static_cast<std::uint32_t>(id_))
                {
                    return true;
                }
            }
            for (std::size_t i = lane_bitmap_bits; i < lane_count; ++i)
            {
                lane* l = lane_registry_->at(i);
                if (l == nullptr || l->empty())
                {
                    continue;
                }
                if (l->mode() == lane_mode::shared ||
                    l->bound_worker() == static_cast<std::uint32_t>(id_))
                {
                    return true;
                }
            }
        }
        return false;
    }

    job* slot_from_index(std::uint32_t index) noexcept
    {
        runtime_context* const context = rt_ != nullptr ? rt_ : current_runtime();
        return context->jobs_.slot_of(index);
    }

    job* try_steal_round() noexcept
    {
        if (peers_ == nullptr || peer_count_ < 2 || steal_scratch_.empty())
        {
            return nullptr;
        }

        const std::size_t start =
            static_cast<std::size_t>(mix_bits(static_cast<std::uint64_t>(id_)) % peer_count_);
        for (std::size_t k = 0; k < peer_count_; ++k)
        {
            const std::size_t slot = (start + k) % peer_count_;
            worker* victim = peers_[slot];
            if (victim == nullptr || victim == this)
            {
                continue;
            }
            stats_.steal_attempts_.fetch_add(1, std::memory_order_relaxed);
            const std::size_t got =
                victim->stack_.steal(steal_scratch_.data(), steal_scratch_.size());
            if (got > 0)
            {
                stats_.stolen_.fetch_add(got, std::memory_order_relaxed);

                for (std::size_t i = 1; i < got; ++i)
                {
                    if (!stack_.push(steal_scratch_[i]))
                    {
                        release_job(steal_scratch_[i]);
                        FRAMEWORK_REPORT(error_severity::error, errc::job_slot_exhausted);
                    }
                }
                return steal_scratch_[0];
            }
        }
        return nullptr;
    }

    std::int32_t id_{worker_no_owner};
    worker_config config_{};
    priority_stack stack_{};
    std::atomic<bool> running_{false};
    std::thread thread_{};
    worker** peers_{nullptr};
    std::size_t peer_count_{0};
    mpmc_queue mailbox_{};
    mpmc_queue* inject_{nullptr};
    std::atomic<bool>* shutdown_{nullptr};
    std::atomic<std::uint64_t>* completed_counter_{nullptr};
    dense<job*> steal_scratch_{};
    lane_table* lane_registry_{nullptr};
    runtime_context* rt_{nullptr};
    const worker_task_hooks* hooks_{nullptr};
    worker_stats stats_{};
    park_state park_{};
    bool created_{false};
};
} // namespace thread
