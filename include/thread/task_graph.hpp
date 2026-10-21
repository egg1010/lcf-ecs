#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <new>
#include <thread>

#include "part/dense.hpp"
#include "thread/coroutine.hpp"
#include "thread/detail/context_store.hpp"
#include "thread/error.hpp"
#include "thread/error_report.hpp"
#include "thread/task_admission.hpp"
#include "thread/task_dependency.hpp"
#include "thread/task_trace.hpp"
#include "thread/task_identity.hpp"
#include "thread/task_node.hpp"
#include "thread/task_queue.hpp"
#include "thread/task_table.hpp"

namespace thread
{

inline constexpr std::uint32_t task_default_inject_capacity = 4096;
inline constexpr std::uint32_t task_default_command_capacity = 1024;
inline constexpr std::uint32_t task_default_dependency_capacity = 8192;
inline constexpr std::size_t task_steal_scratch = 64;
inline constexpr std::size_t task_mailbox_batch = 16;

struct task_graph_config
{
    std::size_t worker_count{0};
    std::size_t task_capacity{4096};
    const role_config* roles{nullptr};
    std::uint32_t role_count{0};
    std::size_t local_capacity{1024};
    std::size_t inject_capacity{task_default_inject_capacity};
    std::size_t command_capacity{task_default_command_capacity};
    std::size_t dependency_capacity{task_default_dependency_capacity};
    std::size_t steal_batch{task_steal_scratch};
    std::size_t context_capacity{context_default_capacity};
    std::size_t resource_capacity{256};
    std::size_t access_capacity{4096};
    std::size_t body_version_capacity{8};
};

struct task_metrics
{
    std::atomic<std::uint64_t> declared_{0};
    std::atomic<std::uint64_t> activations_{0};
    std::atomic<std::uint64_t> completions_{0};
    std::atomic<std::uint64_t> coalesced_{0};
    std::atomic<std::uint64_t> queued_{0};
    std::atomic<std::uint64_t> rejected_{0};
    std::atomic<std::uint64_t> stale_{0};
    std::atomic<std::uint64_t> migrations_{0};
    std::atomic<std::uint64_t> forwarded_{0};
    std::atomic<std::uint64_t> forward_failures_{0};
    std::atomic<std::uint64_t> reorders_{0};
    std::atomic<std::uint64_t> reparents_{0};
    std::atomic<std::uint64_t> faults_{0};
    std::atomic<std::uint64_t> stolen_{0};
    std::atomic<std::uint64_t> steal_denied_{0};
    std::atomic<std::uint64_t> overruns_{0};
    std::atomic<std::uint64_t> cancellations_{0};
    std::atomic<std::uint64_t> deactivations_{0};
    std::atomic<std::uint64_t> forwarded_to_role_{0};
    std::atomic<std::uint64_t> dependency_waits_{0};
    std::atomic<std::uint64_t> wait_ns_total_{0};
};

struct task_view
{
    task_id id_{};
    task_state state_{task_state::dormant};
    thread::role role_{thread::role::general};
    order_mode mode_{order_mode::parallel};
    affinity_kind affinity_{affinity_kind::none};
    std::uint32_t worker_{task_worker_none};
    bool stealable_{true};
    task_id parent_{};
    order_key key_{};
    std::uint64_t activations_{0};
    const char* name_{nullptr};

    bool valid() const noexcept
    {
        return id_.valid();
    }
};

struct role_slot
{
    thread::role id_{thread::role::general};
    char name_[16]{};
    std::uint32_t first_worker_{0};
    std::uint32_t worker_count_{0};
    order_mode mode_{order_mode::parallel};
    order_policy policy_{order_policy::keyed};
    bool external_{false};
    bool frame_auto_{false};
    bool deterministic_{false};
    std::uint32_t frame_rate_{60};
    std::atomic<std::uint64_t> frame_{0};
    std::atomic<std::uint64_t> epoch_{0};
    std::atomic<std::uint64_t> thread_hash_{0};
    std::atomic<bool> attached_{false};
    task_sequencer* sequencer_{nullptr};
};

struct task_worker_slot
{
    ticket_deque stealable_{};
    ticket_deque bound_{};
    mpmc_ring<ticket> mailbox_{};
    std::atomic<std::uint64_t> executed_{0};
    std::atomic<std::uint64_t> stolen_{0};
    ticket scratch_[task_steal_scratch]{};
};

struct dep_edge
{
    std::uint32_t to_{invalid_task_index};
    std::uint32_t next_{0};
};

// 持久任务图：注册后节点不删除、编号不复用、地址不搬移
// 等待状态表：等待类型、辅助字、挂起状态、截止时间与统计
struct task_wait_tables
{
    dense<std::uint8_t> kind_{};
    dense<std::uint32_t> aux_{};
    dense<std::uint8_t> state_{};
    dense<std::uint64_t> deadline_{};
    dense<std::uint64_t> start_ns_{};
    dense<std::uint64_t> since_{};
    dense<std::uint8_t> pending_{};
    dense<std::uint32_t> frame_waiters_{};
    dense<std::uint32_t> join_parent_{};
};

// 资源访问表：读写声明与等待队列
struct task_access_tables
{
    dense<std::uint32_t> head_{};
    dense<resource_edge> edges_{};
    dense<std::uint32_t> wait_task_{};
    dense<std::uint8_t> wait_write_{};
};

struct task_graph
{
    task_graph() noexcept = default;

    task_graph(const task_graph&) = delete;
    task_graph& operator=(const task_graph&) = delete;

    ~task_graph()
    {
        destroy();
    }

    // ------------------------------------------------------------------
    // 生命周期
    // ------------------------------------------------------------------

    bool create(const task_graph_config& config) noexcept;

    void destroy() noexcept;

    bool created() const noexcept;

    // ------------------------------------------------------------------
    // 拓扑查询
    // ------------------------------------------------------------------

    std::size_t worker_count() const noexcept;

    std::size_t role_count() const noexcept;

    std::size_t task_capacity() const noexcept;

    std::size_t task_count() const noexcept;

    role_slot* role_at(thread::role id) noexcept;

    const role_slot* role_at(thread::role id) const noexcept;

    order_mode role_mode(thread::role id) const noexcept;

    bool role_external(thread::role id) const noexcept;

    bool role_frame_auto(thread::role id) const noexcept;

    std::uint32_t role_frame_rate(thread::role id) const noexcept;

    thread::role worker_role(std::uint32_t worker) const noexcept;

    std::uint32_t task_statistics_live() const noexcept;

    // ------------------------------------------------------------------
    // 注册
    // ------------------------------------------------------------------

    task_id declare(const char* name, task_body body = {}) noexcept
    {
        route_word route = route_word::make(thread::role::general, order_mode::parallel,
                                            affinity_kind::none, priority::normal,
                                            task_worker_none);
        parent_word parent{};
        return declare_full(name, body, route, parent, invalid_order_key, 0);
    }

    task_id declare(const char* name, task_body body, thread::role target) noexcept;

    task_id declare_full(const char* name, task_body body, route_word route, parent_word parent,
                         std::uint64_t key, std::uint32_t flags) noexcept
    {
        if (!created_)
        {
            report(error_severity::error, errc::not_running, "task graph not created");
            return invalid_task;
        }
        if (!valid_route(route, true))
        {
            report(error_severity::error, errc::task_decl_invalid, "invalid route combination",
                   invalid_task, route.role_value(), task_worker_none);
            return invalid_task;
        }
        task_id id{};
        const std::uint32_t index = table_.size();
        const std::uint64_t resolved_key =
            key == invalid_order_key
                ? ((static_cast<std::uint64_t>(index) + 1ull) << 32)
                : key;
        task_node* node =
            table_.append(id, name, body, route, parent, resolved_key, flags);
        if (node == nullptr)
        {
            report(error_severity::error, errc::task_table_full, "task table full");
            return invalid_task;
        }
        (void)contexts_.inherit(parent.parent(), index);
        node->set_activation(activation_policy::manual, activation_overlap::drop);
        node->set_frame_period(1);
        deadline_[index] = 0;
        body_destroy_[index] = body.destroy_;
        metrics_.declared_.fetch_add(1, std::memory_order_relaxed);
        return id;
    }

    // ------------------------------------------------------------------
    // 身份变更
    // ------------------------------------------------------------------

    bool set_body(task_id id, task_body body) noexcept;

    bool migrate(task_id id, thread::role target) noexcept;

    bool rebind(task_id id, thread::role target) noexcept;

    bool set_role(task_id id, thread::role target) noexcept;

    bool pin(task_id id, std::uint32_t worker) noexcept;

    bool set_stealable(task_id id, bool value) noexcept;

    bool set_order_mode(task_id id, order_mode mode) noexcept;

    bool set_priority(task_id id, priority value) noexcept;

    bool reorder(task_id id, order_key key) noexcept;

    bool reparent(task_id id, task_id parent) noexcept;

    bool add_dependency(task_id before, task_id after) noexcept;

    bool set_activation(task_id id, activation_policy policy, std::uint32_t period = 1,
                        activation_overlap overlap = activation_overlap::drop) noexcept
    {
        task_node* node = node_of(id);
        if (node == nullptr)
        {
            return false;
        }
        node->set_activation(policy, overlap);
        node->set_frame_period(period);
        if (policy == activation_policy::every_frames && id.index_ < periodic_flag_.size() &&
            periodic_flag_[id.index_] == 0)
        {
            if (periodic_.size() < periodic_.capacity())
            {
                periodic_.push_back(id.index_);
                periodic_flag_[id.index_] = 1;
            }
        }
        return true;
    }

    bool add_access(task_id id, resource_id resource, access_mode mode) noexcept;

    resource_table& resources() noexcept;

    // 时间预算：任务体通过 ctx.budget_exceeded() 查询，用于跳帧/降级
    bool set_budget(task_id id, std::uint64_t budget_ns) noexcept;

    std::uint64_t budget_of(task_id id) const noexcept;

    bool set_deadline(task_id id, std::uint64_t timeout_ns) noexcept;

    context_id create_context(std::uint64_t request_id, std::uint64_t trace_id,
                              std::uint64_t tenant_id) noexcept
    {
        return contexts_.create_context(request_id, trace_id, tenant_id);
    }

    bool set_context(task_id id, context_id ctx) noexcept;

    context_id context_of(task_id id) const noexcept;

    void release_context(context_id ctx) noexcept;

    // ------------------------------------------------------------------
    // 协程等待：由 task_flow 的等待原语在 await_suspend 内调用
    // ------------------------------------------------------------------

    bool prepare_switch(task_id id, thread::role target) noexcept;

    bool prepare_frame_wait(task_id id, thread::role target) noexcept;

    bool prepare_frame_pair(task_id id, thread::role first, thread::role second) noexcept;

    bool prepare_join(task_id parent, task_id child) noexcept;

    // IO / 阻塞卸载的等待登记：完成事件通过 wake_waiting 回投
    bool prepare_external_wait(task_id id) noexcept;

    bool prepare_join_any(task_id parent, std::span<const task_id> children) noexcept;

    void clear_join_any(task_id parent) noexcept;

    bool prepare_timed(task_id id, std::uint64_t delay_ns) noexcept;

    bool wake_waiting(task_id id) noexcept;

    bool waiting(task_id id) const noexcept;

    void set_work_submit(void* self, bool (*submit)(void*, job*) noexcept) noexcept;

    // ------------------------------------------------------------------
    // 看门狗观测面：只读字段与定时唤醒入口
    // ------------------------------------------------------------------

    std::uint64_t deadline_of(task_id id) const noexcept;

    std::uint64_t heartbeat_of(task_id id) const noexcept;

    std::uint64_t start_ns_of(task_id id) const noexcept;

    std::uint64_t wait_since_of(task_id id) const noexcept;

    void set_default_deadline(std::uint64_t timeout_ns) noexcept;

    // 定时等待到期唤醒：由看门狗周期调用
    std::size_t wake_timed(std::uint64_t now_ns) noexcept;

    void raise(error_severity severity, errc code, task_id id, const char* text) noexcept;

    // 卡住上报去重：首次标记返回 true
    bool mark_hung(task_id id) noexcept;

    bool submit_work(job* target) noexcept;

    context_table& contexts() noexcept;

    bool disable(task_id id) noexcept;

    bool enable(task_id id) noexcept;

    bool cancel(task_id scope) noexcept;

    bool clear_cancel(task_id scope) noexcept;

    bool cancelled(task_id id) const noexcept;

    // ------------------------------------------------------------------
    // 激活
    // ------------------------------------------------------------------

    bool activate(task_id id) noexcept;

    bool activate(task_id id, activate_options options) noexcept;

    bool deactivate(task_id id) noexcept;

    // ------------------------------------------------------------------
    // 执行入口（worker 调用）
    // ------------------------------------------------------------------

    bool take_ticket(std::uint32_t worker, ticket& out) noexcept;

    void run_ticket(ticket value, std::uint32_t worker) noexcept;

    std::size_t pump_sequential(thread::role id, std::uint32_t worker, std::size_t budget) noexcept;

    std::size_t pump_role(thread::role id, std::size_t budget) noexcept;

    std::size_t pump_embedded(std::size_t budget) noexcept;

    // ------------------------------------------------------------------
    // 帧模型
    // ------------------------------------------------------------------

    std::uint64_t begin_frame(thread::role id) noexcept;

    void end_frame(thread::role id) noexcept;

    std::uint64_t frame_of(thread::role id) const noexcept;

    std::uint64_t epoch_of(thread::role id) const noexcept;

    // ------------------------------------------------------------------
    // 外部线程接入
    // ------------------------------------------------------------------

    bool attach_thread(thread::role id, const char* label = nullptr) noexcept
    {
        role_slot* slot = role_at(id);
        if (slot == nullptr)
        {
            report(error_severity::error, errc::role_unbound, "attach_thread unknown role", {}, id);
            return false;
        }
        if (!slot->external_)
        {
            report(error_severity::error, errc::role_unbound,
                   "attach_thread requires external role", {}, id);
            return false;
        }
        bool expected = false;
        if (!slot->attached_.compare_exchange_strong(expected, true, std::memory_order_acq_rel,
                                                     std::memory_order_acquire))
        {
            report(error_severity::warning, errc::already_registered,
                   "attach_thread called twice", {}, id);
            return false;
        }
        slot->thread_hash_.store(static_cast<std::uint64_t>(std::hash<std::thread::id>{}(
                                     std::this_thread::get_id())),
                                 std::memory_order_release);
        if (label != nullptr)
        {
            std::size_t i = 0;
            for (; i + 1 < sizeof(slot->name_) && label[i] != '\0'; ++i)
            {
                slot->name_[i] = label[i];
            }
            slot->name_[i] = '\0';
        }
        return true;
    }

    void detach_thread(thread::role id) noexcept;

    bool thread_attached(thread::role id) const noexcept;

    // ------------------------------------------------------------------
    // 排空与统计
    // ------------------------------------------------------------------

    bool busy() const noexcept;

    // 空闲停放前的谓词：该 worker 是否有可执行工作
    bool has_worker_work(std::uint32_t worker) const noexcept;

    std::size_t drain(std::size_t limit = 1000000) noexcept
    {
        std::size_t executed = 0;
        for (std::size_t round = 0; round < limit; ++round)
        {
            std::size_t progress = 0;
            for (std::uint32_t i = 0; i < role_count_; ++i)
            {
                if (roles_[i].sequencer_ != nullptr)
                {
                    progress += pump_sequential(roles_[i].id_, roles_[i].first_worker_, 64);
                }
            }
            for (std::uint32_t w = 0; w < worker_count_ && progress < 64; ++w)
            {
                ticket value{};
                while (progress < 64 && take_ticket(w, value))
                {
                    run_ticket(value, w);
                    ++progress;
                }
            }
            progress += pump_embedded(64);
            executed += progress;
            if (progress == 0)
            {
                break;
            }
        }
        return executed;
    }

    task_view view(task_id id) noexcept;

    task_state state_of(task_id id) const noexcept;

    task_node* node_at(task_id id) noexcept;

    task_metrics& metrics() noexcept;

    const task_metrics& metrics() const noexcept;

    void set_draining(bool value) noexcept;

    bool draining() const noexcept;

    // 热重载：替换下一次激活使用的任务体；旧版本保留到 manager 销毁
    bool replace_body(task_id id, task_body body) noexcept;

    std::uint32_t body_version(task_id id) noexcept;

    void set_trace(trace_recorder* recorder) noexcept;

    trace_recorder* trace() const noexcept;

    void set_admission(admission_controller* controller) noexcept;

    void set_error_sink(error_sink sink, void* user) noexcept;

    // 入队后唤醒空闲 worker；由 manager 注入，图不感知线程实现
    void set_wake_hook(void* self, void (*wake)(void*, std::uint32_t) noexcept) noexcept;

    void wake_worker(std::uint32_t worker) noexcept;

    const error_reporter& reporter() const noexcept;

    error_state last_error() const noexcept;

  private:
    // ------------------------------------------------------------------
    // 建表
    // ------------------------------------------------------------------

    void release_task_wait_slot(std::uint32_t index) noexcept;

    bool acquire_all(std::uint32_t index) noexcept;

    void release_all(std::uint32_t index) noexcept;

    void wake_resource_waiters(resource_id resource) noexcept;

    void set_wait(std::uint32_t index, wait_kind kind, std::uint32_t aux) noexcept;

    void clear_wait(std::uint32_t index) noexcept;

    void register_waiter(std::uint32_t index) noexcept;

    void suspend_bookkeeping(task_node* node, std::uint32_t index) noexcept;

    std::uint32_t worker_of(const task_node* node) const noexcept;

    bool wake_waiting_index(std::uint32_t index) noexcept;

    void wake_frame_waiters(thread::role id) noexcept;

    void release_bodies() noexcept;

    bool create_workers(const task_graph_config& config) noexcept;

    bool create_roles(const task_graph_config& config) noexcept;

    // ------------------------------------------------------------------
    // 路由
    // ------------------------------------------------------------------

    task_node* node_of(task_id id) noexcept;

    const task_node* node_of(task_id id) const noexcept;

    task_worker_slot* worker_at(std::uint32_t worker) noexcept;

    bool worker_in_role(std::uint32_t worker, thread::role id) const noexcept;

    bool valid_route(const route_word& route, bool check_workers) const noexcept;

    route_target target_of(task_node* node) noexcept;

    std::uint32_t pick_worker(const role_slot* slot, std::uint32_t task_index) noexcept;

    bool route_to(task_node* node, std::uint32_t epoch) noexcept;

    // 本 worker 收到票据后按当前路由放回本地队列
    bool stash(std::uint32_t worker, ticket value) noexcept;

    void forward(task_node* node, ticket value, std::uint32_t worker, thread::role current) noexcept;

    // ------------------------------------------------------------------
    // 执行与完成
    // ------------------------------------------------------------------

    void execute(ticket value, std::uint32_t worker, thread::role current, bool from_sequential) noexcept;

    void run_body(task_node* node, std::uint32_t worker, thread::role current,
                  std::uint32_t index) noexcept
    {
        task_context& ctx = views_[index];
        ctx = task_context{};
        ctx.id_ = task_id{index};
        ctx.role_ = current;
        ctx.worker_ = worker;
        const role_slot* slot = role_at(current);
        ctx.frame_ = slot != nullptr ? slot->frame_.load(std::memory_order_acquire) : 0;
        ctx.node_ = node;
        ctx.scope_ = task_id{parent_word{node->parent_.load(std::memory_order_acquire)}.parent()};
        ctx.deadline_ns_ = index < deadline_.size() ? deadline_[index] : 0;
        ctx.budget_ns_ = index < budget_ns_.size() ? budget_ns_[index] : 0;
        ctx.activation_ = node->activation_count_.fetch_add(1, std::memory_order_relaxed) + 1;
        ctx.graph_ = this;
        ctx.context_ = contexts_.at(index);

        const std::uint64_t start = monotonic_now_ns();
        node->heartbeat_ns_.store(start, std::memory_order_relaxed);
        waits_.start_ns_[index] = start;
        ctx.start_ns_ = start;
        if (trace_ != nullptr)
        {
            trace_->record(trace_kind::start, task_id{index}, worker, current, 0);
        }
        waits_.since_[index] = 0;

        task_context* const previous_context = current_task_slot();
        current_task_slot() = &ctx;
        task_body effective{};
        effective.kind_ = node->body_kind_;
        effective.invoke_ = node->invoke_;
        effective.object_ = node->body_object_;
        if (hot_bodies_ != nullptr && index < view_capacity_)
        {
            void* raw = hot_bodies_[index].load(std::memory_order_acquire);
            if (raw != nullptr)
            {
                const body_entry* entry = static_cast<const body_entry*>(raw);
                effective = entry->body_;
            }
        }

        bool suspended = false;
        if (effective.kind_ == task_body_coroutine || effective.kind_ == task_body_coroutine_ctx)
        {
            suspended = run_coroutine(node, ctx, effective);
        }
        else if (effective.kind_ == task_body_plain_ctx)
        {
            if (effective.invoke_.plain_ctx_ != nullptr)
            {
                effective.invoke_.plain_ctx_(ctx);
            }
        }
        else if (effective.invoke_.plain_ != nullptr)
        {
            effective.invoke_.plain_(ctx, effective.object_);
        }

        current_task_slot() = previous_context;
        const std::uint64_t elapsed = monotonic_now_ns() - start;
        if (trace_ != nullptr)
        {
            trace_->record(trace_kind::end, task_id{index}, worker, current, 0, elapsed);
        }
        node->total_run_ns_.fetch_add(elapsed, std::memory_order_relaxed);
        if (suspended)
        {
            suspend_bookkeeping(node, index);
            return;
        }
        clear_wait(index);
        const bool failed =
            (node->flags_.load(std::memory_order_acquire) & flag_body_failed) != 0;
        finish(node, failed ? errc::task_body_faulted : errc::ok);
    }

    bool run_coroutine(task_node* node, task_context& ctx, const task_body& effective) noexcept;

    // 延迟激活：登记目标帧，由该角色 begin_frame 转入正常激活
    bool arm_delay(task_id id, std::uint32_t frames) noexcept;

    void queue_pending(task_node* node) noexcept    {
        std::uint16_t current = node->pending_runs_.load(std::memory_order_acquire);
        while (current < 0xFFFFu)
        {
            if (node->pending_runs_.compare_exchange_weak(current, static_cast<std::uint16_t>(current + 1),
                                                          std::memory_order_acq_rel,
                                                          std::memory_order_acquire))
            {
                return;
            }
        }
    }

    void finish(task_node* node, errc error) noexcept;

    void rearm(task_node* node, bool new_activation = false) noexcept
    {
        std::uint32_t expected = static_cast<std::uint32_t>(task_state::running);
        if (!node->state_.compare_exchange_strong(expected,
                                                  static_cast<std::uint32_t>(task_state::ready),
                                                  std::memory_order_acq_rel,
                                                  std::memory_order_acquire))
        {
            metrics_.completions_.fetch_add(1, std::memory_order_release);
            return;
        }
        const std::uint32_t epoch = node->dispatch_epoch_.fetch_add(1, std::memory_order_acq_rel) + 1;
        if (!route_to(node, epoch))
        {
            node->state_.store(static_cast<std::uint32_t>(task_state::dormant),
                               std::memory_order_release);
            return;
        }
        if (new_activation)
        {
            metrics_.activations_.fetch_add(1, std::memory_order_relaxed);
        }
    }

    void notify_dependents(std::uint32_t index) noexcept;

    void notify_join_waiter(std::uint32_t index) noexcept;

    void notify_parent(task_node* node) noexcept;

    void start_children(task_id parent_id) noexcept;

    // ------------------------------------------------------------------
    // 顺序域命令
    // ------------------------------------------------------------------

    void drain_commands(task_sequencer& sequencer) noexcept;

    void handle_command(task_sequencer& sequencer, const task_command& command) noexcept;

    // ------------------------------------------------------------------
    // 依赖
    // ------------------------------------------------------------------

    bool is_ancestor(std::uint32_t candidate, std::uint32_t node) const noexcept;

    std::uint16_t depth_of(std::uint32_t node) const noexcept;

    bool periodic_listed(std::uint32_t index) const noexcept;

    void report(error_severity severity, errc code, const char* text, task_id id = invalid_task,
                thread::role id_role = thread::role::general,
                std::uint32_t worker = task_worker_none) noexcept
    {
        last_state_.code_ = code;
        last_state_.severity_ = severity;
        set_last_error(code, severity, 0);
        metrics_.rejected_.fetch_add(1, std::memory_order_relaxed);
        reporter_.emit(severity, code, 0, id, id_role, worker, std::string_view{text});
    }

    task_graph_config config_{};
    task_table table_{};
    role_slot* roles_{nullptr};
    task_worker_slot* workers_{nullptr};
    mpmc_ring<ticket> inject_{};
    dense<std::uint32_t> dep_head_{};
    dense<dep_edge> edges_{};
    dense<std::uint64_t> deadline_{};
    dense<std::uint64_t> budget_ns_{};
    dense<task_body_destroy> body_destroy_{};
    dense<std::uint64_t> delay_frame_{};
    dense<std::uint32_t> delayed_{};
    context_store contexts_{};
    resource_table resources_{};
    dense<std::uint32_t> body_version_count_{};
    std::size_t body_version_capacity_{8};
    task_wait_tables waits_{};
    task_access_tables access_{};
    std::atomic<std::uint64_t> default_deadline_ns_{0};
    task_context* views_{nullptr};
    std::uint32_t view_capacity_{0};
    struct body_entry
    {
        task_body body_{};
        std::uint32_t version_{0};
    };

    trace_recorder* trace_{nullptr};
    admission_controller* admission_{nullptr};
    std::atomic<void*>* hot_bodies_{nullptr};
    dense<body_entry*> body_versions_{};
    std::uint32_t body_version_counter_{0};
    void* submit_self_{nullptr};
    bool (*submit_job_)(void*, job*) noexcept{nullptr};
    dense<std::uint32_t> periodic_{};
    dense<std::uint8_t> periodic_flag_{};
    dense<std::uint8_t> worker_role_{};
    task_metrics metrics_{};
    error_reporter reporter_{};
    error_state last_state_{};
    std::atomic<std::uint64_t> route_cursor_{0};
    std::atomic<bool> draining_{false};
    void* wake_self_{nullptr};
    void (*wake_fn_)(void*, std::uint32_t) noexcept{nullptr};
    std::uint32_t worker_count_{0};
    std::uint32_t role_count_{0};
    bool created_{false};
};
} // namespace thread

#include "thread/detail/task_graph_runtime_impl.hpp"
#include "thread/detail/task_graph_define_impl.hpp"

