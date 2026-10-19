#pragma once

#include <atomic>
#include <coroutine>
#include <cstddef>
#include <cstdint>
#include <new>

#include "part/dense.hpp"
#include "thread/error.hpp"
#include "thread/job.hpp"
#include "thread/lane.hpp"
#include "thread/mpmc_queue.hpp"
#include "thread/platform.hpp"
#include "thread/runtime.hpp"
#include "thread/scheduler.hpp"
#include "thread/worker.hpp"

namespace thread
{

enum class stop_mode : std::uint8_t
{
    drain = 0,
    abort = 1,
};

enum class destroy_policy : std::uint8_t
{
    soft = 0,
    hard = 1,
};

enum class shutdown_phase : std::uint8_t
{
    running = 0,
    draining = 1,
    aborting = 2,
    stopped = 3,
};

inline constexpr std::size_t manager_default_suspended_capacity = 4096;

struct manager_config
{
    std::size_t worker_count{0};
    std::size_t inject_capacity{mpmc_default_capacity};
    std::size_t local_capacity{work_deque_default_capacity};
    std::size_t steal_batch{64};
    bool pin_workers{false};
    std::uint8_t priority_levels{scheduler_default_levels};
    std::uint32_t aging_threshold{worker_default_aging_threshold};
    std::size_t lane_capacity{manager_default_lane_capacity};
    std::size_t mailbox_capacity{mpmc_default_capacity};
    std::size_t job_slot_capacity{runtime_default_job_slots};
    std::size_t frame_pool_blocks{frame_default_blocks_per_tier};
};

struct manager_stats
{
    std::atomic<std::uint64_t> submitted_{0};
    std::atomic<std::uint64_t> rejected_{0};
    std::atomic<std::uint64_t> wakes_{0};
};

struct manager
{
    manager() noexcept = default;

    manager(const manager&) = delete;
    manager& operator=(const manager&) = delete;

    ~manager()
    {
        stop();
        teardown();
    }

    bool configure(manager_config config, runtime_context* runtime = nullptr) noexcept
    {
        if (!workers_.empty())
        {
            return false;
        }
        if (!platform_check_required())
        {
            FRAMEWORK_REPORT(error_severity::fatal, errc::invariant_broken);
            return false;
        }
        rt_ = runtime != nullptr ? runtime : default_runtime();
        bind_runtime(rt_);
        if (!rt_->create(config.job_slot_capacity, config.frame_pool_blocks))
        {
            FRAMEWORK_REPORT(error_severity::fatal, errc::invariant_broken);
            teardown();
            return false;
        }
        if (!inject_.create(config.inject_capacity))
        {
            FRAMEWORK_REPORT(error_severity::fatal, errc::invariant_broken);
            teardown();
            return false;
        }
        config_ = config;
        lane_registry_.init(config.lane_capacity);
        suspended_.init(0);
        const std::size_t count = config.worker_count;
        dispatch_.init(count);
        workers_.reserve_exact(count);
        for (std::size_t i = 0; i < count; ++i)
        {
            void* memory =
                ::operator new(sizeof(worker), std::align_val_t(alignof(worker)), std::nothrow);
            if (memory == nullptr)
            {
                FRAMEWORK_REPORT(error_severity::fatal, errc::thread_spawn_failed);
                teardown();
                return false;
            }
            auto* w = new (memory) worker{};
            worker_config wc{};
            wc.local_capacity = config.local_capacity;
            wc.steal_batch = config.steal_batch;
            wc.pin_to_core = config.pin_workers;
            wc.core_index = static_cast<std::int32_t>(i);
            wc.priority_levels = config.priority_levels;
            wc.aging_threshold = config.aging_threshold;
            if (!w->create(static_cast<std::int32_t>(i), wc) ||
                !w->create_mailbox(config.mailbox_capacity == 0 ? mpmc_default_capacity
                                                               : config.mailbox_capacity))
            {
                w->~worker();
                ::operator delete(memory, std::align_val_t(alignof(worker)));
                teardown();
                return false;
            }
            workers_.push_back(w);
        }
        return true;
    }

    std::size_t worker_count() const noexcept
    {
        return workers_.size();
    }

    worker* worker_at(std::size_t index) noexcept
    {
        return index < workers_.size() ? workers_[index] : nullptr;
    }

    mpmc_queue& inject_queue() noexcept
    {
        return inject_;
    }

    manager_stats& stats() noexcept
    {
        return stats_;
    }

    std::uint64_t completed_count() const noexcept
    {
        return completed_.load(std::memory_order_acquire);
    }

    shutdown_phase phase() const noexcept
    {
        return static_cast<shutdown_phase>(phase_.load(std::memory_order_acquire));
    }

    bool stopping() const noexcept
    {
        return phase() != shutdown_phase::running;
    }

    suspended_registry& suspended() noexcept
    {
        return suspended_;
    }

    bool register_suspended(suspended_node* node) noexcept
    {
        if (!suspended_.add(node))
        {
            FRAMEWORK_REPORT(error_severity::error, errc::invariant_broken);
            return false;
        }
        return true;
    }

    void unregister_suspended(suspended_node* node) noexcept
    {
        suspended_.remove(node);
    }

    runtime_context* runtime() noexcept
    {
        return rt_ != nullptr ? rt_ : default_runtime();
    }

    const runtime_context* runtime() const noexcept
    {
        return rt_ != nullptr ? rt_ : default_runtime();
    }

    lane* create_lane(lane_config config) noexcept
    {
        if (lane_registry_.size() >= lane_registry_.capacity())
        {
            FRAMEWORK_REPORT(error_severity::error, errc::invariant_broken);
            return nullptr;
        }
        const std::uint32_t id = static_cast<std::uint32_t>(lane_registry_.size());
        lane* created = new (std::nothrow) lane{};
        if (created == nullptr)
        {
            FRAMEWORK_REPORT(error_severity::fatal, errc::invariant_broken);
            return nullptr;
        }
        if (!created->create(id, config, rt_))
        {
            delete created;
            return nullptr;
        }

        if (config.mode == lane_mode::exclusive)
        {
            created->bind_worker(workers_.empty()
                                     ? manager_pump_owner
                                     : static_cast<std::uint32_t>(id % workers_.size()));
        }
        if (!lane_registry_.push(created))
        {
            delete created;
            return nullptr;
        }
        return created;
    }

    lane* lane_at(std::size_t index) noexcept
    {
        return lane_registry_.at(index);
    }

    std::size_t lane_count() const noexcept
    {
        return lane_registry_.size();
    }

    job* take_job(std::uint32_t& slot_index) noexcept
    {
        job* const target = rt_->jobs_.allocate(slot_index);
        if (target != nullptr)
        {
            target->set_slot_backed(true);
        }
        return target;
    }

    job* slot_of(std::uint32_t slot_index) noexcept
    {
        return rt_->jobs_.slot_of(slot_index);
    }

    // 与自由函数 release_job 等价，但显式走本 manager 的池，不依赖 TLS 绑定
    void release_job_local(job* target) noexcept
    {
        if (target == nullptr)
        {
            return;
        }
        destroy_job(target);
        if (target->is_slot_backed())
        {
            const std::uint32_t index = target->slot_index_;
            target->set_slot_backed(false);
            rt_->jobs_.release(index);
        }
    }

    std::uint32_t submit_to_lane(lane* target_lane, job* target) noexcept
    {
        if (target_lane == nullptr || target == nullptr || stopping())
        {
            return slot_index_none;
        }
        const std::uint32_t index = rt_->jobs_.index_of(target);
        if (index == slot_index_none)
        {
            return slot_index_none;
        }
        if (target_lane->submit(index) == slot_index_none)
        {
            stats_.rejected_.fetch_add(1, std::memory_order_relaxed);
            return slot_index_none;
        }
        stats_.submitted_.fetch_add(1, std::memory_order_relaxed);

        wake_lane_worker(target_lane);
        return index;
    }

    std::size_t pump_lanes(std::uint32_t worker_id, std::size_t budget) noexcept
    {
        std::size_t done = 0;
        const std::size_t count = lane_registry_.size();
        if (count == 0 || budget == 0)
        {
            return 0;
        }

        std::uint64_t bits = rt_->lane_ready_.load(std::memory_order_acquire);
        while (bits != 0 && done < budget)
        {
            const std::uint32_t index = count_trailing_zeros(bits);
            bits &= (bits - 1);
            if (index >= count)
            {
                break;
            }
            done += pump_one_lane(worker_id, lane_registry_.at(index), budget - done);
        }
        // 位图仅覆盖前 64 个 lane；默认配置 lane_capacity=64，此分支通常不触发
        for (std::size_t i = lane_bitmap_bits; i < count && done < budget; ++i)
        {
            done += pump_one_lane(worker_id, lane_registry_.at(i), budget - done);
        }
        return done;
    }

    std::size_t pump_one_lane(std::uint32_t worker_id, lane* l, std::size_t budget) noexcept
    {
        if (l == nullptr || budget == 0)
        {
            return 0;
        }
        std::size_t done = 0;
        std::uint32_t index = mpmc_index_none;
        std::uint32_t used = 0;

        if (l->mode() == lane_mode::shared)
        {
            if (!l->try_acquire(worker_id))
            {
                return 0;
            }
        }
        else
        {
            if (l->bound_worker() != worker_id || !l->try_acquire(worker_id))
            {
                return 0;
            }
        }

        while (used < l->drain_budget() && done < budget && l->try_pop(index))
        {
            job* const target = rt_->jobs_.slot_of(index);
            if (target != nullptr)
            {
                run_job(target);
                release_job_local(target);
                completed_.fetch_add(1, std::memory_order_release);
                ++done;
            }
            ++used;
        }
        l->release_ownership(worker_id);
        return done;
    }

    std::uint32_t try_submit(job* target, std::int32_t preferred_worker = -1) noexcept
    {
        if (target == nullptr || stopping())
        {
            return slot_index_none;
        }
        const std::uint32_t index = rt_->jobs_.index_of(target);
        if (index == slot_index_none)
        {
            return slot_index_none;
        }
        std::size_t routed = 0;
        if (!try_route(index, preferred_worker, routed))
        {
            stats_.rejected_.fetch_add(1, std::memory_order_relaxed);
            return slot_index_none;
        }
        stats_.submitted_.fetch_add(1, std::memory_order_relaxed);
        wake_worker(routed);
        return index;
    }

    std::uint32_t submit(job* target, std::int32_t preferred_worker = -1) noexcept
    {
        if (target == nullptr || stopping())
        {
            return slot_index_none;
        }
        const std::uint32_t index = rt_->jobs_.index_of(target);
        if (index == slot_index_none)
        {
            return slot_index_none;
        }
        std::size_t routed = 0;
        if (workers_.empty())
        {
            if (!try_route(index, preferred_worker, routed))
            {
                stats_.rejected_.fetch_add(1, std::memory_order_relaxed);
                return slot_index_none;
            }
            stats_.submitted_.fetch_add(1, std::memory_order_relaxed);
            return index;
        }
        while (!try_route(index, preferred_worker, routed))
        {
            if (stopping())
            {
                return slot_index_none;
            }
            stats_.rejected_.fetch_add(1, std::memory_order_relaxed);
            const std::uint32_t expected = submit_park_.word_.load(std::memory_order_relaxed);
            park_once(&submit_park_, expected, default_park_ms);
        }
        stats_.submitted_.fetch_add(1, std::memory_order_relaxed);
        wake_worker(routed);
        return index;
    }

    void start() noexcept
    {
        phase_.store(static_cast<std::uint8_t>(shutdown_phase::running), std::memory_order_release);
        bind_runtime(rt_);
        if (workers_.empty())
        {
            return;
        }
        shutdown_.store(false, std::memory_order_relaxed);
        park_state_init(&submit_park_);

        for (std::size_t i = 0; i < workers_.size(); ++i)
        {
            workers_[i]->attach_lanes(&lane_registry_);
            workers_[i]->start(workers_.data(), workers_.size(), &inject_, &shutdown_, &completed_,
                               rt_);
        }
        running_.store(true, std::memory_order_release);
    }

    void stop() noexcept
    {
        stop(stop_mode::drain, destroy_policy::soft);
    }

    void stop(stop_mode mode, destroy_policy policy = destroy_policy::soft) noexcept
    {
        runtime_scope const scope{rt_};
        if (workers_.empty())
        {

            if (mode == stop_mode::drain)
            {
                phase_.store(static_cast<std::uint8_t>(shutdown_phase::draining),
                             std::memory_order_release);

                for (std::size_t i = 0; i < 1000000 && pump(256) > 0; ++i)
                {
                }
                (void)suspended_.resume_all();
            }
            else
            {
                phase_.store(static_cast<std::uint8_t>(shutdown_phase::aborting),
                             std::memory_order_release);
                discard_pending_jobs(policy);
                if (policy == destroy_policy::hard)
                {
                    destroy_suspended_frames();
                }
            }
            phase_.store(static_cast<std::uint8_t>(shutdown_phase::stopped),
                         std::memory_order_release);
            running_.store(false, std::memory_order_release);
            return;
        }

        if (mode == stop_mode::drain)
        {
            phase_.store(static_cast<std::uint8_t>(shutdown_phase::draining),
                         std::memory_order_release);

            drain(2000000);

            for (std::size_t i = 0; i < 64; ++i)
            {
                if (suspended_.size() == 0)
                {
                    break;
                }
                const std::size_t resumed = suspended_.resume_all();
                if (resumed == 0)
                {
                    break;
                }
                drain(2000000);
            }
        }
        else
        {
            phase_.store(static_cast<std::uint8_t>(shutdown_phase::aborting),
                         std::memory_order_release);
        }

        shutdown_.store(true, std::memory_order_release);
        for (worker* w : workers_)
        {
            if (w != nullptr)
            {
                w->unpark();
            }
        }
        unpark_all(&submit_park_, submit_park_.word_.load(std::memory_order_relaxed) + 1u);
        for (worker* w : workers_)
        {
            if (w != nullptr)
            {
                w->join();
            }
        }

        if (mode == stop_mode::abort)
        {
            discard_pending_jobs(policy);
            if (policy == destroy_policy::hard)
            {
                destroy_suspended_frames();
            }
        }

        running_.store(false, std::memory_order_release);
        phase_.store(static_cast<std::uint8_t>(shutdown_phase::stopped), std::memory_order_release);
    }

    bool running() const noexcept
    {
        return running_.load(std::memory_order_acquire);
    }

    std::size_t pump(std::size_t budget = 64) noexcept
    {
        runtime_scope const scope{rt_};
        std::size_t done = 0;
        std::uint32_t index = mpmc_index_none;
        while (done < budget && inject_.try_pop(index))
        {
            job* target = rt_->jobs_.slot_of(index);
            if (target != nullptr)
            {
                run_job(target);
                release_job_local(target);
                completed_.fetch_add(1, std::memory_order_release);
                ++done;
            }
        }
        if (done < budget)
        {
            done += pump_lanes(manager_pump_owner, budget - done);
        }
        return done;
    }

    void drain(std::size_t spin_limit = 0) noexcept
    {
        const std::uint64_t target = stats_.submitted_.load(std::memory_order_acquire);
        std::size_t spins = 0;
        while (completed_.load(std::memory_order_acquire) < target)
        {
            platform_yield();
            if (spin_limit != 0 && ++spins > spin_limit)
            {
                break;
            }
        }
    }

  private:
    void teardown() noexcept
    {
        for (worker* w : workers_)
        {
            if (w != nullptr)
            {
                w->~worker();

                ::operator delete(static_cast<void*>(w), std::align_val_t(alignof(worker)));
            }
        }
        workers_.clear();
        inject_.destroy();
        suspended_.release();
        const std::size_t lane_count = lane_registry_.size();
        for (std::size_t i = 0; i < lane_count; ++i)
        {
            delete lane_registry_.at(i);
        }
        lane_registry_.release();
        dispatch_.init(0);
    }

    void discard_pending_jobs(destroy_policy policy) noexcept
    {

        std::uint32_t index = mpmc_index_none;
        while (inject_.try_pop(index))
        {
            job* target = rt_->jobs_.slot_of(index);
            discard_one(target, policy);
        }

        for (worker* w : workers_)
        {
            if (w == nullptr)
            {
                continue;
            }
            std::uint32_t mailbox_index = mpmc_index_none;
            while (w->mailbox().try_pop(mailbox_index))
            {
                discard_one(rt_->jobs_.slot_of(mailbox_index), policy);
            }

            job* local = w->stack().pop();
            while (local != nullptr)
            {
                discard_one(local, policy);
                local = w->stack().pop();
            }
        }

        const std::size_t lane_count = lane_registry_.size();
        for (std::size_t i = 0; i < lane_count; ++i)
        {
            lane* l = lane_registry_.at(i);
            if (l == nullptr)
            {
                continue;
            }
            std::uint32_t lane_index = mpmc_index_none;
            while (l->try_pop(lane_index))
            {
                discard_one(rt_->jobs_.slot_of(lane_index), policy);
            }
        }
    }

    void discard_one(job* target, destroy_policy policy) noexcept
    {
        if (target == nullptr)
        {
            return;
        }

        if (policy == destroy_policy::hard || target->is_overflow_capture())
        {
            release_job_local(target);
            return;
        }

        target->invoke_ = nullptr;
        target->destroy_ = nullptr;
        if (target->is_slot_backed())
        {
            const std::uint32_t slot = target->slot_index_;
            target->set_slot_backed(false);
            rt_->jobs_.release(slot);
        }
    }

    void destroy_suspended_frames() noexcept
    {
        suspended_.destroy_all();
    }

    bool try_route(std::uint32_t index, std::int32_t preferred_worker,
                   std::size_t& out_target) noexcept
    {
        if (workers_.empty())
        {
            out_target = 0;
            return inject_.try_push(index);
        }
        const std::size_t count = workers_.size();
        if (preferred_worker >= 0 && static_cast<std::size_t>(preferred_worker) < count)
        {
            out_target = static_cast<std::size_t>(preferred_worker);
            return workers_[out_target]->mailbox().try_push(index);
        }

        fast_rand rng{mix_bits(route_ticket_.fetch_add(1, std::memory_order_relaxed) + 1u)};
        const std::size_t a = static_cast<std::size_t>(rng.next() % count);
        const std::size_t b = static_cast<std::size_t>(rng.next() % count);
        const std::size_t first =
            workers_[a]->mailbox().size() <= workers_[b]->mailbox().size() ? a : b;
        const std::size_t second = (first == a) ? b : a;

        out_target = first;
        if (workers_[first]->mailbox().try_push(index))
        {
            return true;
        }
        if (second != first)
        {
            out_target = second;
            if (workers_[second]->mailbox().try_push(index))
            {
                return true;
            }
        }
        // 受激退化：罕见路径下的正确性兜底（顺序探测其余 worker）
        for (std::size_t k = 0; k < count; ++k)
        {
            const std::size_t candidate = (first + k + 1u) % count;
            if (candidate == first || candidate == second)
            {
                continue;
            }
            out_target = candidate;
            if (workers_[candidate]->mailbox().try_push(index))
            {
                return true;
            }
        }
        return false;
    }

    void wake_worker(std::size_t index) noexcept
    {
        if (index >= workers_.size())
        {
            return;
        }
        worker* w = workers_[index];
        if (w != nullptr && w->is_parked())
        {
            w->unpark();
            stats_.wakes_.fetch_add(1, std::memory_order_relaxed);
        }
    }

    void wake_one_worker() noexcept
    {
        const std::size_t count = workers_.size();
        if (count == 0)
        {
            return;
        }
        // 抖动起点（替代 O(N) 全扫），避免所有提交者同时唤醒同一个 worker
        const std::size_t start = static_cast<std::size_t>(
            mix_bits(route_ticket_.fetch_add(1, std::memory_order_relaxed) + 1u) % count);
        for (std::size_t k = 0; k < count; ++k)
        {
            worker* w = workers_[(start + k) % count];
            if (w != nullptr && w->is_parked())
            {
                w->unpark();
                stats_.wakes_.fetch_add(1, std::memory_order_relaxed);
                return;
            }
        }
    }

    void wake_lane_worker(lane* target_lane) noexcept
    {
        if (target_lane == nullptr || workers_.empty())
        {
            return;
        }
        if (target_lane->mode() != lane_mode::shared)
        {
            const std::size_t bound = target_lane->bound_worker();
            if (bound < workers_.size())
            {
                worker* w = workers_[bound];
                if (w != nullptr && w->is_parked())
                {
                    w->unpark();
                    stats_.wakes_.fetch_add(1, std::memory_order_relaxed);
                }
            }
            return;
        }
        wake_one_worker();
    }

    manager_config config_{};
    mpmc_queue inject_{};
    dense<worker*> workers_{};
    lane_table lane_registry_{};
    dispatch_index dispatch_{};
    std::atomic<bool> shutdown_{false};
    std::atomic<bool> running_{false};
    std::atomic<std::uint64_t> completed_{0};
    std::atomic<std::uint8_t> phase_{static_cast<std::uint8_t>(shutdown_phase::running)};
    suspended_registry suspended_{};
    manager_stats stats_{};
    park_state submit_park_{};
    runtime_context* rt_{nullptr};
    std::atomic<std::uint64_t> route_ticket_{0};
};
} // namespace thread
