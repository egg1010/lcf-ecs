#pragma once

// 由 task_graph.hpp 末尾包含，依赖类声明与成员，禁止单独包含
// 合并自：task_graph_wait_impl + task_graph_exec_impl

namespace thread
{

inline bool task_graph::prepare_switch(task_id id, thread::role target) noexcept
    {
    task_node* node = node_of(id);
    if (node == nullptr)
    {
        report(error_severity::error, errc::task_identity_invalid, "switch_to invalid task", id);
        return false;
    }
    const role_slot* slot = role_at(target);
    if (slot == nullptr)
    {
        report(error_severity::error, errc::role_unbound, "switch_to unknown role", id, target);
        return false;
    }
    const route_word route{node->route_.load(std::memory_order_acquire)};
    if (route.mode() == order_mode::sequential && slot->mode_ != order_mode::sequential)
    {
        report(error_severity::warning, errc::task_decl_invalid,
               "switch_to target lacks sequential domain", id, target);
        return false;
    }
    if (route.affinity() == affinity_kind::worker &&
        (node->flags_.load(std::memory_order_acquire) & flag_allow_steal) == 0)
    {
        report(error_severity::warning, errc::affinity_violation,
               "switch_to rejected on hard-bound task", id, target);
        return false;
    }
    for (;;)
    {
        route_word old_route{node->route_.load(std::memory_order_acquire)};
        route_word new_route = old_route;
        new_route.set_role(target);
        if (node->route_.compare_exchange_weak(old_route.value_, new_route.value_,
                                               std::memory_order_acq_rel,
                                               std::memory_order_acquire))
        {
            break;
        }
    }
    set_wait(id.index_, wait_kind::switch_role, 0u);
    metrics_.migrations_.fetch_add(1, std::memory_order_relaxed);
    if (trace_ != nullptr)
    {
        trace_->record(trace_kind::migrate, id, task_worker_none, target, 0);
    }
    return true;
    }

inline bool task_graph::prepare_frame_wait(task_id id, thread::role target) noexcept
    {
    if (node_of(id) == nullptr || role_at(target) == nullptr)
    {
        report(error_severity::warning, errc::role_unbound, "next_frame invalid role", id, target);
        return false;
    }
    set_wait(id.index_, wait_kind::frame, static_cast<std::uint32_t>(role_index(target)));
    return true;
    }

inline bool task_graph::prepare_frame_pair(task_id id, thread::role first, thread::role second) noexcept
    {
    if (node_of(id) == nullptr || role_at(first) == nullptr || role_at(second) == nullptr)
    {
        return false;
    }
    const std::size_t a = role_index(first);
    const std::size_t b = role_index(second);
    if (a >= 32 || b >= 32)
    {
        report(error_severity::warning, errc::invalid_argument,
               "frame_sync supports roles below 32", id);
        return false;
    }
    set_wait(id.index_, wait_kind::frame_sync,
             (1u << static_cast<std::uint32_t>(a)) | (1u << static_cast<std::uint32_t>(b)));
    return true;
    }

inline bool task_graph::prepare_join(task_id parent, task_id child) noexcept
    {
    task_node* node = node_of(parent);
    task_node* target = node_of(child);
    if (node == nullptr || target == nullptr)
    {
        report(error_severity::error, errc::task_identity_invalid, "join invalid task", parent);
        return false;
    }
    if (parent.index_ == child.index_)
    {
        report(error_severity::error, errc::task_state_conflict, "join on self rejected",
               parent);
        return false;
    }
    if (!activate(child))
    {
        const task_state state = target->state();
        if (state == task_state::disabled || state == task_state::faulted)
        {
            return false;
        }
    }
    const parent_word declared{target->parent_.load(std::memory_order_acquire)};
    if (declared.parent() != parent.index_ && child.index_ < waits_.join_parent_.size())
    {
        waits_.join_parent_[child.index_] = parent.index_;
    }
    node->pending_children_.fetch_add(1, std::memory_order_acq_rel);
    set_wait(parent.index_, wait_kind::join, child.index_);
    return true;
    }

inline bool task_graph::prepare_external_wait(task_id id) noexcept
    {
    if (node_of(id) == nullptr)
    {
        return false;
    }
    set_wait(id.index_, wait_kind::external, 0u);
    return true;
    }

inline bool task_graph::prepare_join_any(task_id parent, std::span<const task_id> children) noexcept
    {
    task_node* node = node_of(parent);
    if (node == nullptr || children.empty())
    {
        return false;
    }
    node->flags_.fetch_or(flag_join_any, std::memory_order_acq_rel);
    for (std::size_t i = 0; i < children.size(); ++i)
    {
        if (!prepare_join(parent, children[i]))
        {
            node->flags_.fetch_and(~flag_join_any, std::memory_order_release);
            return false;
        }
    }
    return true;
    }

inline void task_graph::clear_join_any(task_id parent) noexcept
    {
    task_node* node = node_of(parent);
    if (node != nullptr)
    {
        node->flags_.fetch_and(~flag_join_any, std::memory_order_release);
    }
    }

inline bool task_graph::prepare_timed(task_id id, std::uint64_t delay_ns) noexcept
    {
    if (node_of(id) == nullptr || id.index_ >= waits_.deadline_.size())
    {
        return false;
    }
    waits_.deadline_[id.index_] = monotonic_now_ns() + delay_ns;
    set_wait(id.index_, wait_kind::timed, 0u);
    return true;
    }

inline bool task_graph::wake_waiting(task_id id) noexcept
    {
    return wake_waiting_index(id.index_);
    }

inline bool task_graph::waiting(task_id id) const noexcept
    {
    return state_of(id) == task_state::waiting;
    }

inline std::uint64_t task_graph::heartbeat_of(task_id id) const noexcept
    {
    const task_node* node = node_of(id);
    return node != nullptr ? node->heartbeat_ns_.load(std::memory_order_relaxed) : 0;
    }

inline std::uint64_t task_graph::start_ns_of(task_id id) const noexcept
    {
    return id.valid() && id.index_ < waits_.start_ns_.size() ? waits_.start_ns_[id.index_] : 0;
    }

inline std::uint64_t task_graph::wait_since_of(task_id id) const noexcept
    {
    return id.valid() && id.index_ < waits_.since_.size() ? waits_.since_[id.index_] : 0;
    }

inline std::size_t task_graph::wake_timed(std::uint64_t now_ns) noexcept
    {
    std::size_t woken = 0;
    const std::uint32_t count = table_.size();
    for (std::uint32_t index = 0; index < count && index < waits_.state_.size(); ++index)
    {
        if (std::atomic_ref<std::uint8_t>{waits_.state_[index]}.load(
                std::memory_order_acquire) != wait_state_parked)
        {
            continue;
        }
        if (static_cast<wait_kind>(
                std::atomic_ref<std::uint8_t>{waits_.kind_[index]}.load(
                    std::memory_order_acquire)) != wait_kind::timed)
        {
            continue;
        }
        if (waits_.deadline_[index] > now_ns)
        {
            continue;
        }
        if (wake_waiting_index(index))
        {
            ++woken;
        }
    }
    return woken;
    }

inline bool task_graph::mark_hung(task_id id) noexcept
    {
    task_node* node = node_of(id);
    if (node == nullptr)
    {
        return false;
    }
    const std::uint32_t previous =
        node->flags_.fetch_or(flag_hung_reported, std::memory_order_acq_rel);
    return (previous & flag_hung_reported) == 0;
    }

inline void task_graph::release_task_wait_slot(std::uint32_t index) noexcept
    {
    if (index >= access_.wait_task_.size())
    {
        return;
    }
    const std::uint32_t resource = access_.wait_task_[index];
    if (resource == invalid_resource_index)
    {
        return;
    }
    const resource_id id{resource};
    resources_.lock_resource(resource);
    (void)resources_.dequeue_wait_locked(id, index);
    if (access_.wait_write_[index] != 0)
    {
        resources_.note_waiting_writer_locked(id, false);
    }
    resources_.unlock_resource(resource);
    access_.wait_task_[index] = invalid_resource_index;
    access_.wait_write_[index] = 0;
    }

inline bool task_graph::acquire_all(std::uint32_t index) noexcept
    {
    task_node* node = table_.at_index(index);
    if (node == nullptr || index >= access_.head_.size())
    {
        return true;
    }
    const std::uint32_t head = access_.head_[index];
    if (head == 0)
    {
        return true;
    }
    // 旧的等待位置先摘除，再按新的锁集重试
    if (access_.wait_task_[index] != invalid_resource_index)
    {
        release_task_wait_slot(index);
    }

    std::uint64_t stripes = 0;
    for (std::uint32_t edge = head; edge != 0 && edge <= access_.edges_.size();
         edge = access_.edges_[edge - 1].next_)
    {
        stripes |= 1ull << resource_table::stripe_of(access_.edges_[edge - 1].resource_);
    }
    resources_.lock_mask(stripes);

    std::uint32_t conflict = invalid_resource_index;
    bool conflict_write = false;
    for (std::uint32_t edge = head; edge != 0 && edge <= access_.edges_.size();
         edge = access_.edges_[edge - 1].next_)
    {
        const resource_edge& item = access_.edges_[edge - 1];
        const resource_id resource{item.resource_};
        const access_mode mode = static_cast<access_mode>(item.mode_);
        if (!resources_.can_acquire_locked(resource, mode))
        {
            conflict = item.resource_;
            conflict_write = mode == access_mode::write;
            break;
        }
    }
    if (conflict == invalid_resource_index)
    {
        for (std::uint32_t edge = head; edge != 0 && edge <= access_.edges_.size();
             edge = access_.edges_[edge - 1].next_)
        {
            const resource_edge& item = access_.edges_[edge - 1];
            resources_.acquire_locked(resource_id{item.resource_},
                                      static_cast<access_mode>(item.mode_), index);
        }
        node->flags_.fetch_or(flag_resources_held, std::memory_order_acq_rel);
        resources_.unlock_mask(stripes);
        return true;
    }

    const std::uint32_t state = node->state_.load(std::memory_order_acquire);
    if (state == static_cast<std::uint32_t>(task_state::ready))
    {
        std::uint32_t expected = state;
        if (!node->state_.compare_exchange_strong(expected,
                                                  static_cast<std::uint32_t>(task_state::waiting),
                                                  std::memory_order_acq_rel,
                                                  std::memory_order_acquire))
        {
            resources_.unlock_mask(stripes);
            return true;
        }
    }
    // 检查、置等待态与入链在同一临界区完成，避免检查后资源被释放导致丢唤醒
    access_.wait_task_[index] = conflict;
    access_.wait_write_[index] = conflict_write ? 1u : 0u;
    if (conflict_write)
    {
        resources_.note_waiting_writer_locked(resource_id{conflict}, true);
    }
    resources_.enqueue_wait_locked(resource_id{conflict}, index);
    waits_.since_[index] = monotonic_now_ns();
    resources_.unlock_mask(stripes);
    return false;
    }

inline void task_graph::release_all(std::uint32_t index) noexcept
    {
    task_node* node = table_.at_index(index);
    if (node == nullptr)
    {
        return;
    }
    if ((node->flags_.load(std::memory_order_acquire) & flag_resources_held) == 0)
    {
        return;
    }
    node->flags_.fetch_and(~flag_resources_held, std::memory_order_acq_rel);
    std::uint32_t head = access_.head_[index];
    while (head != 0 && head <= access_.edges_.size())
    {
        const resource_edge& item = access_.edges_[head - 1];
        const resource_id resource{item.resource_};
        resources_.lock_resource(item.resource_);
        resources_.release_locked(resource, static_cast<access_mode>(item.mode_), index);
        resources_.unlock_resource(item.resource_);
        wake_resource_waiters(resource);
        head = item.next_;
    }
    }

inline void task_graph::wake_resource_waiters(resource_id resource) noexcept
    {
    for (std::uint32_t guard = 0; guard < table_.capacity(); ++guard)
    {
        resources_.lock_resource(resource.value_);
        const std::uint32_t head = resources_.wait_head_locked(resource);
        if (head == invalid_resource_index)
        {
            resources_.unlock_resource(resource.value_);
            return;
        }
        if (!resources_.dequeue_wait_locked(resource, head))
        {
            resources_.unlock_resource(resource.value_);
            return;
        }
        const bool owned = head < access_.wait_task_.size() &&
                           access_.wait_task_[head] == resource.value_;
        if (owned)
        {
            if (access_.wait_write_[head] != 0)
            {
                resources_.note_waiting_writer_locked(resource, false);
            }
            access_.wait_task_[head] = invalid_resource_index;
            access_.wait_write_[head] = 0;
        }
        resources_.unlock_resource(resource.value_);

        task_node* node = table_.at_index(head);
        if (node == nullptr)
        {
            continue;
        }
        if (cancelled(task_id{head}))
        {
            node->state_.store(static_cast<std::uint32_t>(task_state::dormant),
                               std::memory_order_release);
            continue;
        }
        if (!acquire_all(head))
        {
            if (access_.wait_task_[head] == resource.value_)
            {
                return;
            }
            continue;
        }
        std::uint32_t expected = static_cast<std::uint32_t>(task_state::waiting);
        if (!node->state_.compare_exchange_strong(expected,
                                                  static_cast<std::uint32_t>(task_state::ready),
                                                  std::memory_order_acq_rel,
                                                  std::memory_order_acquire))
        {
            // 状态已被取消/停用改写，归还本次获取的资源
            release_all(head);
            continue;
        }
        const std::uint32_t epoch =
            node->dispatch_epoch_.fetch_add(1, std::memory_order_acq_rel) + 1;
        if (!route_to(node, epoch))
        {
            node->state_.store(static_cast<std::uint32_t>(task_state::dormant),
                               std::memory_order_release);
            release_all(head);
        }

    }
    }

inline void task_graph::set_wait(std::uint32_t index, wait_kind kind, std::uint32_t aux) noexcept
    {
    if (index >= waits_.state_.size())
    {
        return;
    }
    std::atomic_ref<std::uint32_t>{waits_.aux_[index]}.store(aux, std::memory_order_relaxed);
    std::atomic_ref<std::uint8_t>{waits_.kind_[index]}.store(
        static_cast<std::uint8_t>(kind), std::memory_order_relaxed);
    std::atomic_ref<std::uint8_t>{waits_.state_[index]}.store(wait_state_preparing,
                                                            std::memory_order_release);
    }

inline void task_graph::clear_wait(std::uint32_t index) noexcept
    {
    if (index >= waits_.state_.size())
    {
        return;
    }
    std::atomic_ref<std::uint8_t>{waits_.state_[index]}.store(wait_state_none,
                                                            std::memory_order_release);
    std::atomic_ref<std::uint8_t>{waits_.kind_[index]}.store(
        static_cast<std::uint8_t>(wait_kind::none), std::memory_order_release);
    std::atomic_ref<std::uint32_t>{waits_.aux_[index]}.store(0, std::memory_order_relaxed);
    }

inline void task_graph::register_waiter(std::uint32_t index) noexcept
    {
    task_node* node = table_.at_index(index);
    if (node == nullptr)
    {
        return;
    }
    const std::uint32_t previous = node->flags_.fetch_or(flag_frame_waiter,
                                                         std::memory_order_acq_rel);
    if ((previous & flag_frame_waiter) != 0)
    {
        return;
    }
    if (waits_.frame_waiters_.size() >= waits_.frame_waiters_.capacity())
    {
        report(error_severity::warning, errc::queue_full, "frame waiter table full",
               task_id{index});
        return;
    }
    waits_.frame_waiters_.push_back(index);
    }

inline void task_graph::suspend_bookkeeping(task_node* node, std::uint32_t index) noexcept
    {
    if (index < waits_.pending_.size() &&
        std::atomic_ref<std::uint8_t>{waits_.pending_[index]}.exchange(0,
                                                                     std::memory_order_acq_rel) != 0)
    {
        clear_wait(index);
        rearm(node, false);
        return;
    }
    const std::uint8_t state =
        std::atomic_ref<std::uint8_t>{waits_.state_[index]}.load(std::memory_order_acquire);
    const wait_kind kind =
        state == wait_state_preparing
            ? static_cast<wait_kind>(
                  std::atomic_ref<std::uint8_t>{waits_.kind_[index]}.load(std::memory_order_acquire))
            : wait_kind::none;
    if (kind == wait_kind::switch_role)
    {
        clear_wait(index);
        rearm(node, false);
        return;
    }
    if (kind == wait_kind::join && node->pending_children_.load(std::memory_order_acquire) == 0)
    {
        // 子任务在挂起前已完成：直接重排恢复，避免无人唤醒
        clear_wait(index);
        rearm(node, false);
        return;
    }
    if (kind == wait_kind::frame || kind == wait_kind::frame_sync)
    {
        register_waiter(index);
    }
    waits_.since_[index] = monotonic_now_ns();
    if (trace_ != nullptr)
    {
        trace_->record(trace_kind::wait_begin, task_id{index}, worker_of(node),
                       node->route().role_value(), 0);
    }
    node->state_.store(static_cast<std::uint32_t>(task_state::waiting),
                       std::memory_order_release);
    std::atomic_ref<std::uint8_t>{waits_.state_[index]}.store(wait_state_parked,
                                                            std::memory_order_release);
    // 完成事件若落在“置 waiting 之前”，这里补一次唤醒，避免丢失
    if (index < waits_.pending_.size() &&
        std::atomic_ref<std::uint8_t>{waits_.pending_[index]}.exchange(
            0, std::memory_order_acq_rel) != 0)
    {
        (void)wake_waiting_index(index);
    }
    }

inline std::uint32_t task_graph::worker_of(const task_node* node) const noexcept
    {
    const route_word route{node->route_.load(std::memory_order_acquire)};
    return route.affinity() == affinity_kind::worker ? route.worker() : task_worker_none;
    }

inline bool task_graph::wake_waiting_index(std::uint32_t index) noexcept
    {
    task_node* node = table_.at_index(index);
    if (node == nullptr)
    {
        return false;
    }
    std::uint32_t expected = static_cast<std::uint32_t>(task_state::waiting);
    if (!node->state_.compare_exchange_strong(expected,
                                              static_cast<std::uint32_t>(task_state::ready),
                                              std::memory_order_acq_rel,
                                              std::memory_order_acquire))
    {
        if (node->state_.load(std::memory_order_acquire) ==
                static_cast<std::uint32_t>(task_state::running) &&
            index < waits_.pending_.size())
        {
            // 完成事件先于挂起到达：记账，由挂起路径立即重排
            std::atomic_ref<std::uint8_t>{waits_.pending_[index]}.store(1,
                                                                      std::memory_order_release);
            return true;
        }
        return false;
    }
    clear_wait(index);
    waits_.since_[index] = monotonic_now_ns();
    if (trace_ != nullptr)
    {
        trace_->record(trace_kind::wait_end, task_id{index}, worker_of(node),
                       node->route().role_value(), 0);
    }
    const std::uint32_t epoch =
        node->dispatch_epoch_.fetch_add(1, std::memory_order_acq_rel) + 1;
    if (!route_to(node, epoch))
    {
        node->state_.store(static_cast<std::uint32_t>(task_state::dormant),
                           std::memory_order_release);
        report(error_severity::warning, errc::queue_full, "wake routing failed",
               task_id{index});
        return false;
    }
    return true;
    }

inline void task_graph::wake_frame_waiters(thread::role id) noexcept
    {
    if (waits_.frame_waiters_.size() == 0)
    {
        return;
    }
    const std::size_t target = role_index(id);
    const std::uint32_t bit = target < 32 ? (1u << static_cast<std::uint32_t>(target)) : 0u;
    for (std::size_t i = 0; i < waits_.frame_waiters_.size(); ++i)
    {
        const std::uint32_t index = waits_.frame_waiters_[i];
        task_node* node = table_.at_index(index);
        if (node == nullptr)
        {
            continue;
        }
        if ((node->flags_.load(std::memory_order_acquire) & flag_frame_waiter) == 0)
        {
            continue;
        }
        if (std::atomic_ref<std::uint8_t>{waits_.state_[index]}.load(std::memory_order_acquire) !=
            wait_state_parked)
        {
            continue;
        }
        const wait_kind kind = static_cast<wait_kind>(
            std::atomic_ref<std::uint8_t>{waits_.kind_[index]}.load(std::memory_order_acquire));
        if (kind == wait_kind::none)
        {
            node->flags_.fetch_and(~flag_frame_waiter, std::memory_order_release);
            continue;
        }
        if (kind == wait_kind::frame)
        {
            if (std::atomic_ref<std::uint32_t>{waits_.aux_[index]}.load(
                    std::memory_order_acquire) != target)
            {
                continue;
            }
            if (wake_waiting_index(index))
            {
                node->flags_.fetch_and(~flag_frame_waiter, std::memory_order_release);
            }
        }
        else if (kind == wait_kind::frame_sync && bit != 0u)
        {
            const std::uint32_t mask = std::atomic_ref<std::uint32_t>{waits_.aux_[index]}
                                           .fetch_and(~bit, std::memory_order_acq_rel);
            if ((mask & ~bit) == 0u && wake_waiting_index(index))
            {
                node->flags_.fetch_and(~flag_frame_waiter, std::memory_order_release);
            }
        }
    }
    }

inline bool task_graph::submit_work(job* target) noexcept
    {
    if (submit_job_ == nullptr || target == nullptr)
    {
        return false;
    }
    return submit_job_(submit_self_, target);
    }

inline bool task_graph::activate(task_id id) noexcept
    {
    return activate(id, activate_options{});
    }

inline bool task_graph::activate(task_id id, activate_options options) noexcept
    {
    task_node* node = node_of(id);
    if (node == nullptr)
    {
        report(error_severity::error, errc::task_identity_invalid, "activate invalid task", id);
        return false;
    }
    if (options.delay_frames_ != 0)
    {
        return arm_delay(id, options.delay_frames_);
    }
    if (cancelled(id))
    {
        report(error_severity::warning, errc::cancelled, "activate rejected by cancel scope", id);
        return false;
    }
    if (node->pending_deps_.load(std::memory_order_acquire) > 0)
    {
        report(error_severity::warning, errc::task_state_conflict,
               "activate rejected: dependencies pending", id);
        return false;
    }
    for (std::uint32_t attempt = 0; attempt < 8; ++attempt)
    {
        const std::uint32_t state = node->state_.load(std::memory_order_acquire);
        if (state == task_state_constructing)
        {
            return false;
        }
        if (state == static_cast<std::uint32_t>(task_state::disabled))
        {
            report(error_severity::warning, errc::task_state_conflict, "activate disabled task",
                   id);
            return false;
        }
        if (state == static_cast<std::uint32_t>(task_state::faulted))
        {
            report(error_severity::warning, errc::task_state_conflict, "activate faulted task",
                   id);
            return false;
        }
        if (state == static_cast<std::uint32_t>(task_state::ready) ||
            state == static_cast<std::uint32_t>(task_state::running) ||
            state == static_cast<std::uint32_t>(task_state::waiting))
        {
            if (options.queue_)
            {
                queue_pending(node);
                metrics_.queued_.fetch_add(1, std::memory_order_relaxed);
                return true;
            }
            if (node->overlap() == activation_overlap::error)
            {
                report(error_severity::warning, errc::task_state_conflict,
                       "overlapping activation rejected", id);
                return false;
            }
            metrics_.coalesced_.fetch_add(1, std::memory_order_relaxed);
            return true;
        }

        std::uint32_t expected = state;
        if (!node->state_.compare_exchange_strong(expected,
                                                  static_cast<std::uint32_t>(task_state::ready),
                                                  std::memory_order_acq_rel,
                                                  std::memory_order_acquire))
        {
            continue;
        }
        const std::uint32_t epoch =
            node->dispatch_epoch_.fetch_add(1, std::memory_order_acq_rel) + 1;
        if (admission_ != nullptr && !admission_->try_admit(0))
        {
            node->state_.store(static_cast<std::uint32_t>(task_state::dormant),
                               std::memory_order_release);
            report(error_severity::warning, errc::queue_full, "admission rejected", id);
            return false;
        }
        if (!acquire_all(id.index_))
        {
            // 资源冲突：已进入 waiting，由释放方唤醒；本次激活计入在途
            metrics_.dependency_waits_.fetch_add(1, std::memory_order_relaxed);
            metrics_.activations_.fetch_add(1, std::memory_order_relaxed);
            return true;
        }
        if (!route_to(node, epoch))
        {
            release_all(id.index_);
            node->state_.store(static_cast<std::uint32_t>(task_state::dormant),
                               std::memory_order_release);
            report(error_severity::warning, errc::queue_full, "activate queue full", id);
            return false;
        }
        metrics_.activations_.fetch_add(1, std::memory_order_relaxed);
        if (trace_ != nullptr)
        {
            trace_->record(trace_kind::enqueue, id, task_worker_none, node->route().role_value(),
                           0);
        }
        if (id.index_ < waits_.since_.size())
        {
            waits_.since_[id.index_] = monotonic_now_ns();
        }
        if ((node->flags_.load(std::memory_order_acquire) & flag_auto_start_children) != 0)
        {
            start_children(id);
        }
        return true;
    }
    return false;
    }

inline bool task_graph::deactivate(task_id id) noexcept
    {
    task_node* node = node_of(id);
    if (node == nullptr)
    {
        return false;
    }
    std::uint32_t expected = static_cast<std::uint32_t>(task_state::ready);
    if (!node->state_.compare_exchange_strong(expected,
                                              static_cast<std::uint32_t>(task_state::dormant),
                                              std::memory_order_acq_rel,
                                              std::memory_order_acquire))
    {
        return false;
    }
    node->dispatch_epoch_.fetch_add(1, std::memory_order_acq_rel);
    metrics_.deactivations_.fetch_add(1, std::memory_order_relaxed);
    return true;
    }

inline bool task_graph::take_ticket(std::uint32_t worker, ticket& out) noexcept
    {
    task_worker_slot* slot = worker_at(worker);
    if (slot == nullptr)
    {
        return false;
    }
    ticket taken = slot->bound_.pop();
    if (!taken.empty())
    {
        out = taken;
        return true;
    }
    taken = slot->stealable_.pop();
    if (!taken.empty())
    {
        out = taken;
        return true;
    }

    ticket batch[task_mailbox_batch];
    const std::size_t count = slot->mailbox_.try_pop_bulk(batch, task_mailbox_batch);
    if (count > 0)
    {
        for (std::size_t i = 1; i < count; ++i)
        {
            if (!stash(worker, batch[i]))
            {
                out = batch[i];
                for (std::size_t j = i + 1; j < count; ++j)
                {
                    (void)inject_.try_push(batch[j]);
                }
                return true;
            }
        }
        out = batch[0];
        return true;
    }

    if (inject_.try_pop(out))
    {
        return true;
    }

    const role_slot* self_role = role_at(worker_role(worker));
    if (self_role != nullptr && self_role->deterministic_)
    {
        return false;
    }
    const std::size_t victim_count = worker_count_;
    for (std::size_t k = 0; k < victim_count; ++k)
    {
        const std::size_t victim = (worker + k + 1) % victim_count;
        if (victim == worker || workers_ == nullptr)
        {
            continue;
        }
        const role_slot* victim_role = role_at(worker_role(static_cast<std::uint32_t>(victim)));
        if (victim_role != nullptr && victim_role->deterministic_)
        {
            continue;
        }
        const std::size_t got =
            workers_[victim].stealable_.steal(slot->scratch_, config_.steal_batch);
        if (got == 0)
        {
            continue;
        }
        for (std::size_t i = 1; i < got; ++i)
        {
            if (!stash(worker, slot->scratch_[i]))
            {
                (void)inject_.try_push(slot->scratch_[i]);
            }
        }
        slot->stolen_.fetch_add(got, std::memory_order_relaxed);
        metrics_.stolen_.fetch_add(got, std::memory_order_relaxed);
        out = slot->scratch_[0];
        return true;
    }
    return false;
    }

inline void task_graph::run_ticket(ticket value, std::uint32_t worker) noexcept
    {
    execute(value, worker, worker_role(worker), false);
    }

inline std::size_t task_graph::pump_sequential(thread::role id, std::uint32_t worker, std::size_t budget) noexcept
    {
    role_slot* slot = role_at(id);
    if (slot == nullptr || slot->sequencer_ == nullptr)
    {
        return 0;
    }
    drain_commands(*slot->sequencer_);
    std::size_t done = 0;
    while (done < budget)
    {
        // 依赖完成会在本轮内追加 arm 命令，先吸收再取下一个就绪任务
        if (slot->sequencer_->command_pending() != 0)
        {
            drain_commands(*slot->sequencer_);
        }
        heap_entry entry{};
        if (!slot->sequencer_->pop(entry))
        {
            break;
        }
        execute(ticket::make_task(entry.index_, entry.epoch_), worker, id, true);
        ++done;
    }
    return done;
    }

inline std::size_t task_graph::pump_role(thread::role id, std::size_t budget) noexcept
    {
    role_slot* slot = role_at(id);
    if (slot == nullptr)
    {
        return 0;
    }
    if (slot->external_ && !slot->attached_.load(std::memory_order_acquire))
    {
        report(error_severity::warning, errc::role_unbound, "pump_role on unattached thread", {},
               id);
        return 0;
    }
    if (slot->mode_ == order_mode::sequential)
    {
        return pump_sequential(id, slot->first_worker_, budget);
    }
    return pump_embedded(budget);
    }

inline std::size_t task_graph::pump_embedded(std::size_t budget) noexcept
    {
    std::size_t done = 0;
    ticket value{};
    while (done < budget && inject_.try_pop(value))
    {
        task_node* node = table_.at_index(value.index());
        if (node == nullptr)
        {
            continue;
        }
        const route_target target = target_of(node);
        execute(value, target.worker_ == task_worker_none ? task_worker_none : target.worker_,
                target.role_, target.kind_ == route_kind::sequential);
        ++done;
    }
    return done;
    }

inline void task_graph::wake_worker(std::uint32_t worker) noexcept
    {
    if (wake_fn_ != nullptr)
    {
        wake_fn_(wake_self_, worker);
    }
    }

inline route_target task_graph::target_of(task_node* node) noexcept
    {
    const route_word route{node->route_.load(std::memory_order_acquire)};
    route_target target{};
    target.role_ = route.role_value();
    const role_slot* slot = role_at(route.role_value());
    if (slot == nullptr)
    {
        target.kind_ = route_kind::none;
        return target;
    }
    if (slot->mode_ == order_mode::sequential || route.mode() == order_mode::sequential)
    {
        target.kind_ = route_kind::sequential;
        target.worker_ = slot->first_worker_;
        return target;
    }
    if (route.affinity() == affinity_kind::worker)
    {
        const std::uint32_t flags = node->flags_.load(std::memory_order_acquire);
        target.worker_ = route.worker();
        target.kind_ = (flags & flag_allow_steal) != 0 ? route_kind::stealable
                                                       : route_kind::bound;
        return target;
    }
    target.kind_ = route_kind::stealable;
    target.worker_ = pick_worker(slot, static_cast<std::uint32_t>(node - table_.nodes_));
    return target;
    }

inline std::uint32_t task_graph::pick_worker(const role_slot* slot, std::uint32_t task_index) noexcept
    {
    if (slot->worker_count_ <= 1)
    {
        return slot->first_worker_;
    }
    if (slot->deterministic_)
    {
        return slot->first_worker_ + (task_index % slot->worker_count_);
    }
    // 单次游标轮转：不做队列深度采样，避免读取其他 worker 的热缓存行
    const std::uint32_t offset = static_cast<std::uint32_t>(
        route_cursor_.fetch_add(1, std::memory_order_relaxed) % slot->worker_count_);
    return slot->first_worker_ + offset;
    }

inline bool task_graph::route_to(task_node* node, std::uint32_t epoch) noexcept
    {
    const std::uint32_t index = static_cast<std::uint32_t>(node - table_.nodes_);
    const route_target target = target_of(node);
    const ticket value = ticket::make_task(index, epoch);
    switch (target.kind_)
    {
    case route_kind::sequential:
    {
        role_slot* slot = role_at(target.role_);
        if (slot == nullptr || slot->sequencer_ == nullptr)
        {
            return false;
        }
        const task_command command{task_op::arm, index, epoch,
                                   node->order_key_.load(std::memory_order_acquire)};
        return slot->sequencer_->push_command(command);
    }
    case route_kind::bound:
    {
        task_worker_slot* worker = worker_at(target.worker_);
        if (worker == nullptr || !worker->mailbox_.try_push(value))
        {
            return false;
        }
        wake_worker(target.worker_);
        return true;
    }
    case route_kind::stealable:
    {
        const role_slot* slot = role_at(target.role_);
        if (slot == nullptr)
        {
            return false;
        }
        if (target.worker_ != task_worker_none)
        {
            task_worker_slot* preferred = worker_at(target.worker_);
            if (preferred != nullptr && preferred->mailbox_.try_push(value))
            {
                wake_worker(target.worker_);
                return true;
            }
        }
        for (std::uint32_t k = 0; k < slot->worker_count_; ++k)
        {
            const std::uint32_t candidate = slot->first_worker_ + k;
            if (workers_[candidate].mailbox_.try_push(value))
            {
                wake_worker(candidate);
                return true;
            }
        }
        if (!inject_.try_push(value))
        {
            return false;
        }
        if (worker_count_ != 0)
        {
            wake_worker(static_cast<std::uint32_t>(route_cursor_.fetch_add(1, std::memory_order_relaxed) %
                                                   worker_count_));
        }
        return true;
    }
    case route_kind::none:
    default:
        return false;
    }
    }

inline bool task_graph::stash(std::uint32_t worker, ticket value) noexcept
    {
    task_worker_slot* slot = worker_at(worker);
    if (slot == nullptr)
    {
        return false;
    }
    task_node* node = table_.at_index(value.index());
    if (node == nullptr)
    {
        return true;
    }
    const route_target target = target_of(node);
    if (target.kind_ == route_kind::bound && target.worker_ == worker)
    {
        return slot->bound_.push(value);
    }
    if (target.kind_ == route_kind::sequential)
    {
        return route_to(node, value.epoch());
    }
    return slot->stealable_.push(value);
    }

inline void task_graph::forward(task_node* node, ticket value, std::uint32_t worker, thread::role current) noexcept
    {
    metrics_.forwarded_.fetch_add(1, std::memory_order_relaxed);
    const route_target target = target_of(node);
    if (target.kind_ == route_kind::bound && target.worker_ != worker)
    {
        metrics_.steal_denied_.fetch_add(1, std::memory_order_relaxed);
    }
    if (target.kind_ == route_kind::sequential && target.role_ != current)
    {
        metrics_.forwarded_to_role_.fetch_add(1, std::memory_order_relaxed);
    }
    if (route_to(node, value.epoch()))
    {
        return;
    }
    metrics_.forward_failures_.fetch_add(1, std::memory_order_relaxed);
    report(error_severity::warning, errc::forward_failed,
           "ticket forward failed, executing locally", task_id{value.index()}, target.role_,
           worker);
    std::uint32_t expected = static_cast<std::uint32_t>(task_state::ready);
    if (node->state_.compare_exchange_strong(expected,
                                             static_cast<std::uint32_t>(task_state::running),
                                             std::memory_order_acq_rel,
                                             std::memory_order_acquire))
    {
        const std::uint32_t index = static_cast<std::uint32_t>(node - table_.nodes_);
        run_body(node, worker, current, index);
    }
    else
    {
        metrics_.stale_.fetch_add(1, std::memory_order_relaxed);
    }
    }

inline void task_graph::execute(ticket value, std::uint32_t worker, thread::role current, bool from_sequential) noexcept
    {
    task_node* node = table_.at_index(value.index());
    if (node == nullptr || node->constructing())
    {
        metrics_.stale_.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    if (node->dispatch_epoch_.load(std::memory_order_acquire) != value.epoch())
    {
        metrics_.stale_.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    if (node->state_.load(std::memory_order_acquire) !=
        static_cast<std::uint32_t>(task_state::ready))
    {
        metrics_.stale_.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    const route_target target = target_of(node);
    const bool matches =
        target.kind_ == route_kind::sequential
            ? (from_sequential && target.role_ == current)
            : (!from_sequential && target.role_ == current &&
               (target.kind_ == route_kind::stealable || target.worker_ == worker));
    if (!matches)
    {
        forward(node, value, worker, current);
        return;
    }
    std::uint32_t expected = static_cast<std::uint32_t>(task_state::ready);
    if (!node->state_.compare_exchange_strong(expected,
                                              static_cast<std::uint32_t>(task_state::running),
                                              std::memory_order_acq_rel,
                                              std::memory_order_acquire))
    {
        metrics_.stale_.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    run_body(node, worker, current, value.index());
    }

inline bool task_graph::run_coroutine(task_node* node, task_context& ctx, const task_body& effective) noexcept
    {
    std::coroutine_handle<> handle =
        std::coroutine_handle<>::from_address(node->coro_handle_.load(std::memory_order_acquire));
    if (handle == nullptr)
    {
        task<void> created{};
        if (effective.kind_ == task_body_coroutine_ctx)
        {
            if (effective.invoke_.coro_ctx_ == nullptr)
            {
                return false;
            }
            created = effective.invoke_.coro_ctx_(ctx);
        }
        else
        {
            if (effective.invoke_.coro_ == nullptr)
            {
                return false;
            }
            created = effective.invoke_.coro_(ctx, effective.object_);
        }
        handle = created.release();
        if (handle == nullptr)
        {
            metrics_.faults_.fetch_add(1, std::memory_order_relaxed);
            report(error_severity::error, errc::coroutine_frame_exhausted,
                   "coroutine frame allocation failed", ctx.id_, ctx.role_, ctx.worker_);
            node->flags_.fetch_or(flag_faulted, std::memory_order_release);
            return false;
        }
        node->coro_handle_.store(handle.address(), std::memory_order_release);
    }
    if (!handle.done())
    {
        handle.resume();
    }
    if (handle.done())
    {
        handle.destroy();
        node->coro_handle_.store(nullptr, std::memory_order_release);
        return false;
    }
    return true;
    }

inline bool task_graph::arm_delay(task_id id, std::uint32_t frames) noexcept
    {
    task_node* node = node_of(id);
    if (node == nullptr || id.index_ >= delay_frame_.size())
    {
        return false;
    }
    const role_slot* slot = role_at(node->route().role_value());
    if (slot == nullptr)
    {
        report(error_severity::error, errc::role_unbound, "delayed activate without role", id);
        return false;
    }
    const std::uint64_t target =
        slot->frame_.load(std::memory_order_acquire) + static_cast<std::uint64_t>(frames);
    delay_frame_[id.index_] = target;
    const std::uint32_t previous =
        node->flags_.fetch_or(flag_delay_armed, std::memory_order_acq_rel);
    if ((previous & flag_delay_armed) != 0)
    {
        return true;
    }
    if (delayed_.size() >= delayed_.capacity())
    {
        node->flags_.fetch_and(~flag_delay_armed, std::memory_order_acq_rel);
        report(error_severity::warning, errc::queue_full, "delayed activate table full", id);
        return false;
    }
    delayed_.push_back(id.index_);
    return true;
    }

inline void task_graph::finish(task_node* node, errc error) noexcept
    {
    if (error != errc::ok)
    {
        node->flags_.fetch_or(flag_faulted, std::memory_order_acq_rel);
        metrics_.faults_.fetch_add(1, std::memory_order_relaxed);
        release_all(static_cast<std::uint32_t>(node - table_.nodes_));
        if (admission_ != nullptr)
        {
            admission_->on_complete(0);
        }
        node->state_.store(static_cast<std::uint32_t>(task_state::faulted),
                           std::memory_order_release);
        metrics_.completions_.fetch_add(1, std::memory_order_release);
        return;
    }

    const std::uint32_t flags = node->flags_.load(std::memory_order_acquire);
    if ((flags & flag_join_children) != 0 &&
        (flags & flag_body_done) == 0 &&
        node->pending_children_.load(std::memory_order_acquire) > 0)
    {
        node->flags_.fetch_or(flag_body_done, std::memory_order_acq_rel);
        node->state_.store(static_cast<std::uint32_t>(task_state::waiting),
                           std::memory_order_release);
        return;
    }

    std::uint16_t runs = node->pending_runs_.load(std::memory_order_acquire);
    while (runs > 0)
    {
        if (node->pending_runs_.compare_exchange_weak(runs, static_cast<std::uint16_t>(runs - 1),
                                                      std::memory_order_acq_rel,
                                                      std::memory_order_acquire))
        {
            rearm(node, true);
            return;
        }
    }

    const std::uint32_t finished_index = static_cast<std::uint32_t>(node - table_.nodes_);
    release_all(finished_index);
    if (admission_ != nullptr)
    {
        admission_->on_complete(0);
    }
    node->state_.store(static_cast<std::uint32_t>(task_state::completed),
                       std::memory_order_release);
    node->flags_.fetch_and(~flag_identity_pending, std::memory_order_release);
    waits_.start_ns_[finished_index] = 0;
    waits_.since_[finished_index] = 0;
    metrics_.completions_.fetch_add(1, std::memory_order_release);

    const std::uint32_t index = static_cast<std::uint32_t>(node - table_.nodes_);
    notify_dependents(index);
    notify_parent(node);
    notify_join_waiter(index);
    }

inline void task_graph::notify_dependents(std::uint32_t index) noexcept
    {
    if (index >= dep_head_.size())
    {
        return;
    }
    std::uint32_t edge = dep_head_[index];
    while (edge != 0 && edge <= edges_.size())
    {
        const dep_edge& link = edges_[edge - 1];
        task_node* target = table_.at_index(link.to_);
        if (target != nullptr)
        {
            if ((target->flags_.load(std::memory_order_acquire) & flag_after_any) != 0)
            {
                std::uint32_t expected = 0;
                if (target->flags_.compare_exchange_strong(expected, flag_deps_fired,
                                                           std::memory_order_acq_rel,
                                                           std::memory_order_acquire))
                {
                    target->pending_deps_.store(0, std::memory_order_release);
                    (void)activate(task_id{link.to_});
                }
            }
            else
            {
                const std::uint32_t previous =
                    target->pending_deps_.fetch_sub(1, std::memory_order_acq_rel);
                if (previous == 1)
                {
                    target->pending_deps_.store(0, std::memory_order_release);
                    (void)activate(task_id{link.to_});
                }
                else if (previous == 0)
                {
                    target->pending_deps_.store(0, std::memory_order_release);
                }
            }
        }
        edge = link.next_;
    }
    }

inline void task_graph::notify_join_waiter(std::uint32_t index) noexcept
    {
    if (index >= waits_.join_parent_.size())
    {
        return;
    }
    const std::uint32_t parent_index = waits_.join_parent_[index];
    if (parent_index == invalid_task_index)
    {
        return;
    }
    waits_.join_parent_[index] = invalid_task_index;
    task_node* parent = table_.at_index(parent_index);
    if (parent == nullptr)
    {
        return;
    }
    const std::uint32_t previous =
        parent->pending_children_.fetch_sub(1, std::memory_order_acq_rel);
    if ((parent->flags_.load(std::memory_order_acquire) & flag_join_any) != 0 &&
        parent->state_.load(std::memory_order_acquire) ==
            static_cast<std::uint32_t>(task_state::waiting))
    {
        parent->pending_children_.store(0, std::memory_order_release);
        (void)wake_waiting_index(parent_index);
        return;
    }
    if (previous == 0)
    {
        parent->pending_children_.store(0, std::memory_order_release);
        return;
    }
    if (previous == 1 &&
        parent->state_.load(std::memory_order_acquire) ==
            static_cast<std::uint32_t>(task_state::waiting))
    {
        (void)wake_waiting_index(parent_index);
    }
    }

inline void task_graph::notify_parent(task_node* node) noexcept
    {
    const parent_word word{node->parent_.load(std::memory_order_acquire)};
    if (word.parent() == invalid_task_index)
    {
        return;
    }
    task_node* parent = table_.at_index(word.parent());
    if (parent == nullptr)
    {
        return;
    }
    const std::uint32_t previous = parent->pending_children_.fetch_sub(1, std::memory_order_acq_rel);
    if ((parent->flags_.load(std::memory_order_acquire) & flag_join_any) != 0 &&
        parent->state_.load(std::memory_order_acquire) ==
            static_cast<std::uint32_t>(task_state::waiting))
    {
        parent->pending_children_.store(0, std::memory_order_release);
        (void)wake_waiting_index(static_cast<std::uint32_t>(parent - table_.nodes_));
        return;
    }
    if (previous == 0)
    {
        parent->pending_children_.store(0, std::memory_order_release);
        return;
    }
    if (previous == 1 &&
        parent->state_.load(std::memory_order_acquire) ==
            static_cast<std::uint32_t>(task_state::waiting) &&
        (parent->flags_.load(std::memory_order_acquire) & flag_body_done) != 0)
    {
        parent->state_.store(static_cast<std::uint32_t>(task_state::completed),
                             std::memory_order_release);
        parent->flags_.fetch_and(~flag_body_done, std::memory_order_release);
        metrics_.completions_.fetch_add(1, std::memory_order_release);
        const std::uint32_t parent_index = static_cast<std::uint32_t>(parent - table_.nodes_);
        notify_dependents(parent_index);
        notify_parent(parent);
        return;
    }
    if (previous == 1 &&
        parent->state_.load(std::memory_order_acquire) ==
            static_cast<std::uint32_t>(task_state::waiting) &&
        (parent->flags_.load(std::memory_order_acquire) & flag_body_done) == 0)
    {
        const std::uint32_t parent_index = static_cast<std::uint32_t>(parent - table_.nodes_);
        (void)wake_waiting_index(parent_index);
    }
    }

inline void task_graph::start_children(task_id parent_id) noexcept
    {
    task_node* parent = node_of(parent_id);
    if (parent == nullptr)
    {
        return;
    }
    const std::uint32_t previous = parent->flags_.fetch_or(flag_auto_started,
                                                           std::memory_order_acq_rel);
    if ((previous & flag_auto_started) != 0)
    {
        return;
    }
    const std::uint32_t count = table_.size();
    for (std::uint32_t index = 0; index < count; ++index)
    {
        task_node* child = table_.at_index(index);
        if (child == nullptr || child->constructing())
        {
            continue;
        }
        const parent_word word{child->parent_.load(std::memory_order_acquire)};
        if (word.parent() != parent_id.index_)
        {
            continue;
        }
        if ((child->flags_.load(std::memory_order_acquire) & flag_detached) != 0)
        {
            continue;
        }
        parent->pending_children_.fetch_add(1, std::memory_order_acq_rel);
        (void)activate(task_id{index});
    }
    }

inline void task_graph::drain_commands(task_sequencer& sequencer) noexcept
    {
    task_command batch[64];
    for (;;)
    {
        const std::size_t count = sequencer.drain_commands(batch, 64);
        if (count == 0)
        {
            return;
        }
        for (std::size_t i = 0; i < count; ++i)
        {
            handle_command(sequencer, batch[i]);
        }
    }
    }

inline void task_graph::handle_command(task_sequencer& sequencer, const task_command& command) noexcept
    {
    task_node* node = table_.at_index(command.task_);
    if (node == nullptr || node->constructing())
    {
        return;
    }
    switch (command.op_)
    {
    case task_op::arm:
    {
        if (node->dispatch_epoch_.load(std::memory_order_acquire) != command.epoch_)
        {
            metrics_.stale_.fetch_add(1, std::memory_order_relaxed);
            return;
        }
        if (node->state_.load(std::memory_order_acquire) !=
            static_cast<std::uint32_t>(task_state::ready))
        {
            return;
        }
        if (node->heap_index_.load(std::memory_order_relaxed) != 0)
        {
            return;
        }
        (void)sequencer.insert(heap_entry{node->order_key_.load(std::memory_order_acquire),
                                          command.task_, command.epoch_});
        return;
    }
    case task_op::reorder:
        node->order_key_.store(command.key_, std::memory_order_release);
        (void)sequencer.reorder(command.task_, command.key_);
        return;
    default:
        return;
    }
    }

} // namespace thread
