#pragma once

#include <atomic>
#include <chrono>
#include <coroutine>
#include <cstddef>
#include <cstdint>
#include <new>
#include <thread>

#include "part/dense.hpp"
#include "thread/pool/detail/frame_clock.hpp"
#include "thread/pool/detail/job_router.hpp"
#include "part/operating_message.hpp"
#include "thread/base/job.hpp"
#include "thread/execution/lane.hpp"
#include "thread/queue/mpmc_queue.hpp"
#include "thread/base/platform.hpp"
#include "thread/execution/runtime.hpp"
#include "thread/execution/scheduler.hpp"
#include "thread/task/task_builder.hpp"
#include "thread/task/task_admission.hpp"
#include "thread/task/task_blocking.hpp"
#include "thread/task/task_trace.hpp"
#include "thread/task/task_watchdog.hpp"
#include "thread/task/task_graph.hpp"
#include "thread/execution/worker.hpp"

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

enum class drain_status : std::uint8_t
{
    complete = 0,
    timeout = 1,
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
    // 持久任务图：task_capacity == 0 显式关闭
    std::size_t task_capacity{4096};
    const role_config* roles{nullptr};
    std::uint32_t role_count{0};
    std::size_t task_local_capacity{1024};
    std::size_t task_command_capacity{task_default_command_capacity};
    std::size_t task_inject_capacity{task_default_inject_capacity};
    std::size_t task_dependency_capacity{task_default_dependency_capacity};
    std::size_t context_capacity{context_default_capacity};
    std::size_t resource_capacity{256};
    std::size_t access_capacity{4096};
    bool watchdog_enabled{true};
    std::uint32_t watchdog_period_ms{100};
    std::uint64_t default_deadline_ns{0};
    std::uint64_t hang_grace_ns{1'000'000'000ull};
    std::uint64_t heartbeat_timeout_ns{0};
    std::uint64_t queue_timeout_ns{0};
    bool trace_enabled{false};
    std::uint32_t trace_capacity{256};
    std::uint32_t admission_max_inflight{0};
    std::uint32_t admission_tenant_slots{1};
    overload_policy admission_policy{overload_policy::reject};
    std::uint32_t blocking_threads{0};
    std::uint32_t blocking_capacity{1024};
};

struct manager_stats
{
    std::atomic<std::uint64_t> submitted_{0};
    std::atomic<std::uint64_t> rejected_{0};
    std::atomic<std::uint64_t> wakes_{0};
    std::atomic<std::uint64_t> drain_timeouts_{0};
    std::atomic<std::uint64_t> submit_waits_{0};
};

// 提交闸门：最高位关闭标志，低 63 位在途提交计数
// enter 与 close 通过整字 CAS 线性化，关闭后不再放行新提交
struct submission_gate
{
    static constexpr std::uint64_t closing_bit = 1ull << 63;

    std::atomic<std::uint64_t> word_{0};

    bool enter() noexcept
    {
        std::uint64_t value = word_.load(std::memory_order_acquire);
        for (;;)
        {
            if ((value & closing_bit) != 0)
            {
                return false;
            }
            if (word_.compare_exchange_weak(value, value + 1, std::memory_order_acq_rel,
                                            std::memory_order_acquire))
            {
                return true;
            }
        }
    }

    void leave() noexcept
    {
        word_.fetch_sub(1, std::memory_order_acq_rel);
    }

    void close() noexcept
    {
        word_.fetch_or(closing_bit, std::memory_order_acq_rel);
    }

    void open() noexcept
    {
        word_.store(0, std::memory_order_release);
    }

    std::uint64_t pending() const noexcept
    {
        return word_.load(std::memory_order_acquire) & ~closing_bit;
    }

    bool wait_empty(std::size_t max_spin = 1000000) noexcept
    {
        std::size_t spins = 0;
        while (pending() != 0)
        {
            if (++spins > max_spin)
            {
                return false;
            }
            if ((spins & 63u) == 0)
            {
                platform_yield();
            }
        }
        return true;
    }
};

struct manager
{
    // 在途提交计数守卫：覆盖所有 return 路径
    struct submit_scope
    {
        explicit submit_scope(submission_gate& gate) noexcept : gate_(gate)
        {
        }

        ~submit_scope()
        {
            gate_.leave();
        }

        submit_scope(const submit_scope&) = delete;
        submit_scope& operator=(const submit_scope&) = delete;

        submission_gate& gate_;
    };

    manager() noexcept = default;

    manager(const manager&) = delete;
    manager& operator=(const manager&) = delete;

    ~manager()
    {
        stop();
        teardown();
    }

    bool configure_impl(manager_config config, operating_message& error,
                   runtime_context* runtime = nullptr) noexcept
    {
        if (!workers_.empty())
        {
            error.write_failure(om_err_already_exists.code, msg_level::warn, msg::here());
            return false;
        }
        if (!platform_check_required())
        {
            error.write_failure(om_err_unsupported_operation.code, msg_level::error, msg::here());
            return false;
        }
        rt_ = runtime != nullptr ? runtime : default_runtime();
        bind_runtime(rt_);
        if (!rt_->create(config.job_slot_capacity, config.frame_pool_blocks))
        {
            teardown();
            error.write_failure(om_err_capacity_exceeded.code, msg_level::error, msg::here());
            return false;
        }
        if (!inject_.create(config.inject_capacity))
        {
            teardown();
            error.write_failure(om_err_capacity_exceeded.code, msg_level::error, msg::here());
            return false;
        }
        config_ = config;
        const std::size_t count = config.worker_count;
        const unsigned hardware = std::thread::hardware_concurrency();
        (void)hardware;
        if (config.task_capacity != 0)
        {
            task_graph_config task_config{};
            task_config.worker_count = config.worker_count;
            task_config.task_capacity = config.task_capacity;
            task_config.roles = config.roles;
            task_config.role_count = config.role_count;
            task_config.local_capacity = config.task_local_capacity;
            task_config.command_capacity = config.task_command_capacity;
            task_config.inject_capacity = config.task_inject_capacity;
            task_config.dependency_capacity = config.task_dependency_capacity;
            task_config.context_capacity = config.context_capacity;
            task_config.resource_capacity = config.resource_capacity;
            task_config.access_capacity = config.access_capacity;
            if (!tasks_.create_impl(task_config, error))
            {
                teardown();
                return false;
            }
            if (config.admission_max_inflight != 0)
            {
                admission_config policy{};
                policy.max_inflight_ = config.admission_max_inflight;
                policy.tenant_slots_ = config.admission_tenant_slots;
                policy.policy_ = config.admission_policy;
                if (!admission_.create(policy))
                {
                    teardown();
                    error.write_failure(om_err_capacity_exceeded.code, msg_level::error, msg::here());
                    return false;
                }
                tasks_.set_admission(&admission_);
            }
            if (config.trace_enabled && !trace_.create(count == 0 ? 1u : static_cast<std::uint32_t>(count),
                                                       config.trace_capacity))
            {
                teardown();
                error.write_failure(om_err_capacity_exceeded.code, msg_level::error, msg::here());
                return false;
            }
            if (config.trace_enabled)
            {
                trace_.set_enabled(true);
                tasks_.set_trace(&trace_);
            }
            task_hooks_.self_ = this;
            task_hooks_.step_ = &manager::task_step_thunk;
            task_hooks_.has_work_ = &manager::task_has_work_thunk;
            tasks_.set_wake_hook(this, &manager::task_wake_thunk);
            tasks_.set_work_submit(this, &manager::task_submit_work_thunk);
            clock_.start(count);
        }
        lane_registry_.init(config.lane_capacity);
        suspended_.init(0);
        dispatch_.init(count);
        workers_.increase_capacity(count);
        spawned_worker_count_ = 0;
        for (std::size_t i = 0; i < count; ++i)
        {
            if (tasks_.created() &&
                !tasks_.role_external(tasks_.worker_role(static_cast<std::uint32_t>(i))))
            {
                ++spawned_worker_count_;
            }
            void* memory =
                ::operator new(sizeof(worker), std::align_val_t(alignof(worker)), std::nothrow);
            if (memory == nullptr)
            {
                teardown();
                error.write_failure(om_err_out_of_memory.code, msg_level::error, msg::here());
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
                error.write_failure(om_err_spawn_failed.code, msg_level::error, msg::here());
                return false;
            }
            workers_.emplace_back(w);
        }
        // 通用工作项只路由到可执行 job 循环的槽位：外部接入槽位无线程，
        // 顺序且自动帧的槽位不会回落到 job 循环
        job_workers_.increase_capacity(count);
        job_capable_.resize_fill(count, 0);
        for (std::size_t i = 0; i < count; ++i)
        {
            bool capable = true;
            if (tasks_.created())
            {
                const thread::role id = tasks_.worker_role(static_cast<std::uint32_t>(i));
                if (tasks_.role_external(id))
                {
                    capable = false;
                }
                else if (tasks_.role_mode(id) == order_mode::sequential && tasks_.role_frame_auto(id))
                {
                    capable = false;
                }
            }
            if (capable)
            {
                job_workers_.emplace_back(static_cast<std::uint32_t>(i));
                job_capable_[i] = 1;
            }
        }
        router_.bind(inject_, workers_, job_workers_, job_capable_);
        return true;
    }

    std::size_t worker_count() const noexcept
    {
        return workers_.size();
    }

    // 实际派生的工作线程数：线程预算扣除原生主线程接入的外部角色槽位
    std::size_t spawned_worker_count() const noexcept
    {
        return spawned_worker_count_;
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
            return nullptr;
        }
        const std::uint32_t id = static_cast<std::uint32_t>(lane_registry_.size());
        lane* created = new (std::nothrow) lane{};
        if (created == nullptr)
        {
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

    // ------------------------------------------------------------------
    // 持久任务图接口（语义见 design §9）
    // ------------------------------------------------------------------

    task_graph& tasks() noexcept
    {
        return tasks_;
    }

    const task_graph& tasks() const noexcept
    {
        return tasks_;
    }

    bool tasks_enabled() const noexcept
    {
        return tasks_.created();
    }

    task_builder declare(const char* name) noexcept
    {
        operating_message error;
        return task_builder{&tasks_, tasks_.declare_impl(name, thread::task_body{}, error)};
    }

    task_id declare_task_impl(const char* name, task_body body, operating_message& error) noexcept
    {
        return tasks_.declare_impl(name, body, error);
    }

    task_id declare_task(const char* name, task_body body) noexcept
    {
        operating_message error;
        return declare_task_impl(name, body, error);
    }

    operating_message declare_all(const task_decl* decls, std::size_t count) noexcept
    {
        return thread::declare_all(tasks_, decls, count);
    }

    bool activate_impl(task_id id, operating_message& error) noexcept
    {
        return tasks_.activate_impl(id, error);
    }

    bool activate_impl(task_id id, activate_options options, operating_message& error) noexcept
    {
        return tasks_.activate_impl(id, options, error);
    }

    bool deactivate_impl(task_id id, operating_message& error) noexcept
    {
        return tasks_.deactivate_impl(id, error);
    }

    bool migrate_impl(task_id id, thread::role target, operating_message& error) noexcept
    {
        return tasks_.migrate_impl(id, target, error);
    }

    bool rebind_impl(task_id id, thread::role target, operating_message& error) noexcept
    {
        return tasks_.rebind_impl(id, target, error);
    }

    bool pin_impl(task_id id, std::uint32_t worker, operating_message& error) noexcept
    {
        return tasks_.pin_impl(id, worker, error);
    }

    bool set_stealable_impl(task_id id, bool value, operating_message& error) noexcept
    {
        return tasks_.set_stealable_impl(id, value, error);
    }

    bool set_order_mode_impl(task_id id, order_mode mode, operating_message& error) noexcept
    {
        return tasks_.set_order_mode_impl(id, mode, error);
    }

    bool set_priority_impl(task_id id, priority value, operating_message& error) noexcept
    {
        return tasks_.set_priority_impl(id, value, error);
    }

    bool reorder_impl(task_id id, order_key key, operating_message& error) noexcept
    {
        return tasks_.reorder_impl(id, key, error);
    }

    bool reparent_impl(task_id id, task_id parent, operating_message& error) noexcept
    {
        return tasks_.reparent_impl(id, parent, error);
    }

    bool disable_impl(task_id id, operating_message& error) noexcept
    {
        return tasks_.disable_impl(id, error);
    }

    bool enable_impl(task_id id, operating_message& error) noexcept
    {
        return tasks_.enable_impl(id, error);
    }

    bool cancel_impl(task_id scope, operating_message& error) noexcept
    {
        return tasks_.cancel_impl(scope, error);
    }

    bool clear_cancel_impl(task_id scope, operating_message& error) noexcept
    {
        return tasks_.clear_cancel_impl(scope, error);
    }

    bool cancelled(task_id id) const noexcept
    {
        return tasks_.cancelled(id);
    }

    bool set_deadline_impl(task_id id, std::uint64_t timeout_ns, operating_message& error) noexcept
    {
        return tasks_.set_deadline_impl(id, timeout_ns, error);
    }

    context_id create_context(std::uint64_t request_id, std::uint64_t trace_id,
                              std::uint64_t tenant_id) noexcept
    {
        return tasks_.create_context(request_id, trace_id, tenant_id);
    }

    bool set_context_impl(task_id id, context_id ctx, operating_message& error) noexcept
    {
        return tasks_.set_context_impl(id, ctx, error);
    }

    context_id context_of(task_id id) const noexcept
    {
        return tasks_.context_of(id);
    }

    bool add_access_impl(task_id id, resource_id resource, access_mode mode, operating_message& error) noexcept
    {
        return tasks_.add_access_impl(id, resource, mode, error);
    }

    resource_table& resources() noexcept
    {
        return tasks_.resources();
    }

    void release_context(context_id ctx) noexcept
    {
        tasks_.release_context(ctx);
    }

    task_view view(task_id id) noexcept
    {
        return tasks_.view(id);
    }

    task_state state_of(task_id id) const noexcept
    {
        return tasks_.state_of(id);
    }

    void set_error_sink(error_sink sink, void* user) noexcept
    {
        tasks_.set_error_sink(sink, user);
    }

    void set_completion_error(om_completion_error_fn fn, void* user) noexcept
    {
        tasks_.set_completion_error(fn, user);
    }


    const task_metrics& task_stats() const noexcept
    {
        return tasks_.metrics();
    }

    const watchdog_metrics& watchdog_stats() const noexcept
    {
        return watchdog_.metrics_;
    }

    trace_recorder& trace() noexcept
    {
        return trace_;
    }

    admission_controller& admission() noexcept
    {
        return admission_;
    }

    blocking_domain& blocking() noexcept
    {
        return blocking_;
    }

    std::size_t watchdog_scan(std::uint64_t now_ns) noexcept
    {
        return watchdog_.scan(now_ns);
    }

    bool attach_thread_impl(thread::role target, const char* label,
                       operating_message& error) noexcept
    {
        return tasks_.attach_thread_impl(target, label, error);
    }

    void detach_thread(thread::role target) noexcept
    {
        tasks_.detach_thread(target);
    }

    std::size_t pump_role(thread::role target, std::size_t budget) noexcept
    {
        return tasks_.pump_role(target, budget);
    }

    std::uint64_t begin_frame(thread::role target) noexcept
    {
        return tasks_.begin_frame(target);
    }

    void end_frame(thread::role target) noexcept
    {
        tasks_.end_frame(target);
    }

    std::uint64_t frame_of(thread::role target) const noexcept
    {
        return tasks_.frame_of(target);
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
        if (target->is_slot_backed())
        {
            const std::uint8_t previous =
                target->lifecycle_.exchange(job::lifecycle_retiring,
                                            std::memory_order_acq_rel);
            if (previous == job::lifecycle_free || previous == job::lifecycle_retiring)
            {
                return;
            }
            const std::uint32_t index = target->slot_index_;
            target->set_slot_backed(false);
            destroy_job(target);
            target->lifecycle_.store(job::lifecycle_free, std::memory_order_release);
            rt_->jobs_.release(index);
            return;
        }
        destroy_job(target);
    }

    std::uint32_t submit_to_lane(lane* target_lane, job* target) noexcept
    {
        if (target_lane == nullptr || target == nullptr || stopping())
        {
            return slot_index_none;
        }
        if (!gate_.enter())
        {
            return slot_index_none;
        }
        submit_scope guard{gate_};
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
        if (!gate_.enter())
        {
            return slot_index_none;
        }
        submit_scope guard{gate_};
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

    std::uint32_t try_submit(job* target, operating_message& error,
                             std::int32_t preferred_worker = -1) noexcept
    {
        const std::uint32_t index = try_submit(target, preferred_worker);
        if (index != slot_index_none)
        {
            return index;
        }
        if (target == nullptr)
        {
            error.write_failure(om_err_null_pointer.code, msg_level::warn, msg::here());
        }
        else if (stopping())
        {
            error.write_failure(om_err_shutting_down.code, msg_level::warn, msg::here());
        }
        else
        {
            error.write_failure(om_err_queue_full.code, msg_level::warn, msg::here());
        }
        return slot_index_none;
    }

    std::uint32_t submit(job* target, std::int32_t preferred_worker = -1) noexcept
    {
        if (target == nullptr || stopping())
        {
            return slot_index_none;
        }
        if (!gate_.enter())
        {
            return slot_index_none;
        }
        submit_scope guard{gate_};
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

    std::uint32_t submit(job* target, operating_message& error,
                         std::int32_t preferred_worker = -1) noexcept
    {
        const std::uint32_t index = submit(target, preferred_worker);
        if (index != slot_index_none)
        {
            return index;
        }
        if (target == nullptr)
        {
            error.write_failure(om_err_null_pointer.code, msg_level::warn, msg::here());
        }
        else if (stopping())
        {
            error.write_failure(om_err_shutting_down.code, msg_level::warn, msg::here());
        }
        else
        {
            error.write_failure(om_err_queue_full.code, msg_level::warn, msg::here());
        }
        return slot_index_none;
    }

    void start() noexcept
    {
        phase_.store(static_cast<std::uint8_t>(shutdown_phase::running), std::memory_order_release);
        gate_.open();
        bind_runtime(rt_);
        if (tasks_.created())
        {
            tasks_.set_draining(false);
        }
        if (workers_.empty())
        {
            return;
        }
        shutdown_.store(false, std::memory_order_relaxed);
        park_state_init(&submit_park_);

        for (std::size_t i = 0; i < workers_.size(); ++i)
        {
            workers_[i]->attach_lanes(&lane_registry_);
            bool spawn = true;
            if (tasks_.created())
            {
                const thread::role id = tasks_.worker_role(static_cast<std::uint32_t>(i));
                spawn = !tasks_.role_external(id);
            }
            workers_[i]->start(workers_.data(), workers_.size(), &inject_, &shutdown_, &completed_,
                               rt_, tasks_.created() ? &task_hooks_ : nullptr, spawn);
        }
        running_.store(true, std::memory_order_release);
        if (config_.blocking_threads != 0)
        {
            (void)blocking_.create(tasks_, config_.blocking_threads, config_.blocking_capacity);
        }
        if (tasks_.created() && config_.watchdog_enabled)
        {
            watchdog_config wd{};
            wd.period_ms_ = config_.watchdog_period_ms;
            wd.default_deadline_ns_ = config_.default_deadline_ns;
            wd.hang_grace_ns_ = config_.hang_grace_ns;
            wd.heartbeat_timeout_ns_ = config_.heartbeat_timeout_ns;
            wd.queue_timeout_ns_ = config_.queue_timeout_ns;
            (void)watchdog_.start(tasks_, wd);
        }
    }

    void stop() noexcept
    {
        stop(stop_mode::drain, destroy_policy::soft);
    }

    void stop(stop_mode mode, destroy_policy policy = destroy_policy::soft) noexcept
    {
        // 未成功 configure 时队列未创建：直接落 stopped，避免析构路径访问空队列
        if (!inject_.created())
        {
            phase_.store(static_cast<std::uint8_t>(shutdown_phase::stopped),
                         std::memory_order_release);
            running_.store(false, std::memory_order_release);
            return;
        }
        runtime_scope const scope{rt_};
        if (workers_.empty())
        {

            if (mode == stop_mode::drain)
            {
                phase_.store(static_cast<std::uint8_t>(shutdown_phase::draining),
                             std::memory_order_release);
                gate_.close();
                if (tasks_.created())
                {
                    tasks_.set_draining(true);
                }
                if (!gate_.wait_empty())
                {
                    stats_.submit_waits_.fetch_add(1, std::memory_order_relaxed);
                }

                std::size_t rounds = 0;
                while (rounds < 1000000 && pump(256) > 0)
                {
                    ++rounds;
                }
                if (rounds >= 1000000)
                {
                    stats_.drain_timeouts_.fetch_add(1, std::memory_order_relaxed);
                }
                (void)suspended_.resume_all();
            }
            else
            {
                phase_.store(static_cast<std::uint8_t>(shutdown_phase::aborting),
                             std::memory_order_release);
                gate_.close();
                discard_pending_jobs(policy);
                if (policy == destroy_policy::hard)
                {
                    destroy_suspended_frames();
                }
            }
            watchdog_.stop();
            blocking_.destroy();
            phase_.store(static_cast<std::uint8_t>(shutdown_phase::stopped),
                         std::memory_order_release);
            running_.store(false, std::memory_order_release);
            return;
        }

        if (mode == stop_mode::drain)
        {
            phase_.store(static_cast<std::uint8_t>(shutdown_phase::draining),
                         std::memory_order_release);
            gate_.close();
            if (tasks_.created())
            {
                // 关闭期间停止周期激活，避免 drain 与帧驱动互相追赶
                tasks_.set_draining(true);
            }
            // 先等在途提交落地，再取完成目标快照
            if (!gate_.wait_empty())
            {
                stats_.submit_waits_.fetch_add(1, std::memory_order_relaxed);
            }

            (void)drain(2000000);

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
            gate_.close();
            if (tasks_.created())
            {
                tasks_.set_draining(true);
            }
        }

        watchdog_.stop();
        blocking_.destroy();
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
        if (!inject_.created())
        {
            return 0;
        }
        runtime_scope const scope{rt_};
        std::size_t done = 0;
        if (workers_.empty() && tasks_.created())
        {
            done += tasks_.pump_role(thread::role::general, budget);
        }
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

    drain_status drain(std::size_t spin_limit = 0, bool include_new_submissions = false) noexcept
    {
        std::uint64_t target = include_new_submissions
                                   ? 0
                                   : stats_.submitted_.load(std::memory_order_acquire);
        std::size_t spins = 0;
        for (;;)
        {
            if (include_new_submissions)
            {
                target = stats_.submitted_.load(std::memory_order_acquire);
            }
            const bool jobs_done = completed_.load(std::memory_order_acquire) >= target;
            const bool tasks_done =
                !tasks_.created() ||
                (tasks_.task_statistics_live() == 0 && !tasks_.busy());
            if (jobs_done && tasks_done)
            {
                return drain_status::complete;
            }
            // 无工作线程时主动消费注入队列与通道，避免空转
            const std::size_t progressed = workers_.empty() ? pump(64) : 0;
            if (progressed == 0)
            {
                platform_yield();
            }
            if (spin_limit != 0 && ++spins > spin_limit)
            {
                stats_.drain_timeouts_.fetch_add(1, std::memory_order_relaxed);
                return drain_status::timeout;
            }
        }
    }

  private:
    static bool task_step_thunk(void* self, std::uint32_t worker) noexcept
    {
        return static_cast<manager*>(self)->task_step(worker);
    }

    static void task_wake_thunk(void* self, std::uint32_t worker) noexcept
    {
        auto* owner = static_cast<manager*>(self);
        if (worker < owner->workers_.size())
        {
            thread::worker* target = owner->workers_[worker];
            if (target != nullptr && target->is_parked())
            {
                target->unpark();
            }
        }
    }

    static bool task_has_work_thunk(void* self, std::uint32_t worker) noexcept
    {
        return static_cast<manager*>(self)->tasks_.has_worker_work(worker);
    }

    static bool task_submit_work_thunk(void* self, job* target) noexcept
    {
        auto* owner = static_cast<manager*>(self);
        return owner->try_submit(target) != slot_index_none;
    }

    bool task_step(std::uint32_t worker) noexcept
    {
        if (!tasks_.created() || worker >= tasks_.worker_count())
        {
            return false;
        }
        const thread::role id = tasks_.worker_role(worker);
        if (tasks_.role_mode(id) == order_mode::sequential)
        {
            if (tasks_.role_frame_auto(id))
            {
                (void)tasks_.begin_frame(id);
                (void)tasks_.pump_sequential(id, worker, 256);
                tasks_.end_frame(id);
                frame_pace(worker, id);
                return true;
            }
            return tasks_.pump_sequential(id, worker, 256) > 0;
        }
        ticket value{};
        if (tasks_.take_ticket(worker, value))
        {
            tasks_.run_ticket(value, worker);
            return true;
        }
        return false;
    }

    void frame_pace(std::uint32_t worker, thread::role id) noexcept
    {
        clock_.pace(worker, tasks_.role_frame_rate(id));
    }

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
        tasks_.destroy();
        trace_.destroy();
        admission_.destroy();
        clock_.clear();
        router_.reset();
        job_workers_.clear();
        job_capable_.clear();
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

        if (target->is_slot_backed())
        {
            const std::uint8_t previous =
                target->lifecycle_.exchange(job::lifecycle_retiring,
                                            std::memory_order_acq_rel);
            if (previous == job::lifecycle_free || previous == job::lifecycle_retiring)
            {
                return;
            }
        }
        target->invoke_ = nullptr;
        target->destroy_ = nullptr;
        if (target->is_slot_backed())
        {
            const std::uint32_t slot = target->slot_index_;
            target->set_slot_backed(false);
            target->lifecycle_.store(job::lifecycle_free, std::memory_order_release);
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
        return router_.route(index, preferred_worker, out_target);
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
    submission_gate gate_{};
    std::atomic<std::uint8_t> phase_{static_cast<std::uint8_t>(shutdown_phase::running)};
    suspended_registry suspended_{};
    manager_stats stats_{};
    park_state submit_park_{};
    runtime_context* rt_{nullptr};
    std::atomic<std::uint64_t> route_ticket_{0};
    task_graph tasks_{};
    task_watchdog watchdog_{};
    trace_recorder trace_{};
    admission_controller admission_{};
    blocking_domain blocking_{};
    worker_task_hooks task_hooks_{};
    dense<std::uint32_t> job_workers_{};
    dense<std::uint8_t> job_capable_{};
    job_router router_{};
    frame_clock clock_{};
    std::size_t spawned_worker_count_{0};
public:
    operating_message activate(task_id id) noexcept
    {
        operating_message error;
        (void)activate_impl(id, error);
        return error;
    }

    operating_message add_access(task_id id, resource_id resource, access_mode mode) noexcept
    {
        operating_message error;
        (void)add_access_impl(id, resource, mode, error);
        return error;
    }

    operating_message attach_thread(thread::role target, const char* label) noexcept
    {
        operating_message error;
        (void)attach_thread_impl(target, label, error);
        return error;
    }

    operating_message cancel(task_id scope) noexcept
    {
        operating_message error;
        (void)cancel_impl(scope, error);
        return error;
    }

    operating_message clear_cancel(task_id scope) noexcept
    {
        operating_message error;
        (void)clear_cancel_impl(scope, error);
        return error;
    }

    operating_message configure(manager_config config, runtime_context* runtime = nullptr) noexcept
    {
        operating_message error;
        (void)configure_impl(config, error, runtime);
        return error;
    }

    operating_message deactivate(task_id id) noexcept
    {
        operating_message error;
        (void)deactivate_impl(id, error);
        return error;
    }

    operating_message disable(task_id id) noexcept
    {
        operating_message error;
        (void)disable_impl(id, error);
        return error;
    }

    operating_message enable(task_id id) noexcept
    {
        operating_message error;
        (void)enable_impl(id, error);
        return error;
    }

    operating_message migrate(task_id id, thread::role target) noexcept
    {
        operating_message error;
        (void)migrate_impl(id, target, error);
        return error;
    }

    operating_message pin(task_id id, std::uint32_t worker) noexcept
    {
        operating_message error;
        (void)pin_impl(id, worker, error);
        return error;
    }

    operating_message rebind(task_id id, thread::role target) noexcept
    {
        operating_message error;
        (void)rebind_impl(id, target, error);
        return error;
    }

    operating_message reorder(task_id id, order_key key) noexcept
    {
        operating_message error;
        (void)reorder_impl(id, key, error);
        return error;
    }

    operating_message reparent(task_id id, task_id parent) noexcept
    {
        operating_message error;
        (void)reparent_impl(id, parent, error);
        return error;
    }

    operating_message set_context(task_id id, context_id ctx) noexcept
    {
        operating_message error;
        (void)set_context_impl(id, ctx, error);
        return error;
    }

    operating_message set_deadline(task_id id, std::uint64_t timeout_ns) noexcept
    {
        operating_message error;
        (void)set_deadline_impl(id, timeout_ns, error);
        return error;
    }

    operating_message set_order_mode(task_id id, order_mode mode) noexcept
    {
        operating_message error;
        (void)set_order_mode_impl(id, mode, error);
        return error;
    }

    operating_message set_priority(task_id id, priority value) noexcept
    {
        operating_message error;
        (void)set_priority_impl(id, value, error);
        return error;
    }

    operating_message set_stealable(task_id id, bool value) noexcept
    {
        operating_message error;
        (void)set_stealable_impl(id, value, error);
        return error;
    }

};
} // namespace thread
