#pragma once

// 由 task_graph.hpp 末尾包含，依赖类声明与成员，禁止单独包含
// 合并自：task_graph_lifecycle_impl + task_graph_mutation_impl + task_graph_query_impl

namespace thread
{

inline bool task_graph::create(const task_graph_config& config) noexcept
    {
    if (table_.capacity() != 0 || config.task_capacity == 0)
    {
        return false;
    }
    if (config.worker_count > task_worker_none)
    {
        report(error_severity::fatal, errc::invalid_argument, "worker_count exceeds 24-bit field");
        return false;
    }
    config_ = config;
    worker_count_ = static_cast<std::uint32_t>(config.worker_count);

    if (!table_.create(static_cast<std::uint32_t>(config.task_capacity)))
    {
        report(error_severity::fatal, errc::invariant_broken, "task table allocation failed");
        return false;
    }
    if (!inject_.create(config.inject_capacity))
    {
        report(error_severity::fatal, errc::invariant_broken, "task inject queue failed");
        destroy();
        return false;
    }
    dep_head_.resize_fill(config.task_capacity, 0);
    budget_ns_.resize_fill(config.task_capacity, 0);
    body_destroy_.resize_fill(config.task_capacity, nullptr);
    delay_frame_.resize_fill(config.task_capacity, 0);
    delayed_.reserve_exact(config.task_capacity);
    waits_.kind_.resize_fill(config.task_capacity, 0);
    waits_.aux_.resize_fill(config.task_capacity, 0);
    waits_.state_.resize_fill(config.task_capacity, 0);
    waits_.deadline_.resize_fill(config.task_capacity, 0);
    waits_.start_ns_.resize_fill(config.task_capacity, 0);
    waits_.since_.resize_fill(config.task_capacity, 0);
    access_.head_.resize_fill(config.task_capacity, 0);
    access_.edges_.reserve_exact(config.access_capacity);
    access_.wait_task_.resize_fill(config.task_capacity, invalid_resource_index);
    access_.wait_write_.resize_fill(config.task_capacity, 0);
    waits_.pending_.resize_fill(config.task_capacity, 0);
    body_versions_.reserve_exact(config.task_capacity);
    body_version_count_.resize_fill(config.task_capacity, 0);
    body_version_capacity_ = config.body_version_capacity == 0 ? 1 : config.body_version_capacity;
    hot_bodies_ = static_cast<std::atomic<void*>*>(
        ::operator new(sizeof(std::atomic<void*>) * config.task_capacity,
                      std::align_val_t(alignof(std::atomic<void*>)), std::nothrow));
    if (hot_bodies_ == nullptr)
    {
        report(error_severity::fatal, errc::invariant_broken, "hot body table allocation failed");
        destroy();
        return false;
    }
    for (std::size_t i = 0; i < config.task_capacity; ++i)
    {
        new (&hot_bodies_[i]) std::atomic<void*>(nullptr);
    }
    if (config.resource_capacity != 0 &&
        !resources_.create(static_cast<std::uint32_t>(config.resource_capacity),
                           static_cast<std::uint32_t>(config.task_capacity)))
    {
        report(error_severity::fatal, errc::invariant_broken, "resource table allocation failed");
        destroy();
        return false;
    }
    waits_.frame_waiters_.reserve_exact(config.task_capacity);
    waits_.join_parent_.resize_fill(config.task_capacity, invalid_task_index);
    view_capacity_ = static_cast<std::uint32_t>(config.task_capacity);
    void* view_memory = ::operator new(sizeof(task_context) * view_capacity_,
                                       std::align_val_t(alignof(task_context)), std::nothrow);
    if (view_memory == nullptr)
    {
        report(error_severity::fatal, errc::invariant_broken, "task context view allocation failed");
        destroy();
        return false;
    }
    views_ = static_cast<task_context*>(view_memory);
    for (std::uint32_t i = 0; i < view_capacity_; ++i)
    {
        new (&views_[i]) task_context{};
    }
    if (!contexts_.create(static_cast<std::uint32_t>(config.context_capacity),
                          static_cast<std::uint32_t>(config.task_capacity)))
    {
        report(error_severity::fatal, errc::invariant_broken, "context table allocation failed");
        destroy();
        return false;
    }
    if (config.dependency_capacity != 0)
    {
        edges_.reserve_exact(config.dependency_capacity);
    }
    deadline_.resize_fill(config.task_capacity, 0);
    periodic_.reserve_exact(config.task_capacity);
    periodic_flag_.resize_fill(config.task_capacity, 0);

    if (!create_workers(config))
    {
        destroy();
        return false;
    }
    if (!create_roles(config))
    {
        destroy();
        return false;
    }
    created_ = true;
    return true;
    }

inline void task_graph::destroy() noexcept
    {
    if (roles_ != nullptr)
    {
        for (std::uint32_t i = 0; i < role_count_; ++i)
        {
            if (roles_[i].sequencer_ != nullptr)
            {
                roles_[i].sequencer_->destroy();
                roles_[i].sequencer_->~task_sequencer();
                ::operator delete(static_cast<void*>(roles_[i].sequencer_),
                                  std::align_val_t(alignof(task_sequencer)));
                roles_[i].sequencer_ = nullptr;
            }
            roles_[i].~role_slot();
        }
        ::operator delete(static_cast<void*>(roles_), std::align_val_t(alignof(role_slot)));
        roles_ = nullptr;
    }
    release_bodies();
    if (workers_ != nullptr)
    {
        for (std::uint32_t i = 0; i < worker_count_; ++i)
        {
            workers_[i].~task_worker_slot();
        }
        ::operator delete(static_cast<void*>(workers_),
                          std::align_val_t(alignof(task_worker_slot)));
        workers_ = nullptr;
    }
    contexts_.destroy();
    inject_.destroy();
    table_.destroy();
    dep_head_.clear();
    edges_.clear();
    deadline_.clear();
    budget_ns_.clear();
    body_destroy_.clear();
    delay_frame_.clear();
    delayed_.clear();
    waits_.kind_.clear();
    waits_.aux_.clear();
    waits_.state_.clear();
    waits_.deadline_.clear();
    waits_.start_ns_.clear();
    waits_.since_.clear();
    access_.head_.clear();
    access_.edges_.clear();
    access_.wait_task_.clear();
    access_.wait_write_.clear();
    waits_.pending_.clear();
    for (std::size_t i = 0; i < body_versions_.size(); ++i)
    {
        delete body_versions_[i];
    }
    body_versions_.clear();
    body_version_count_.clear();
    if (hot_bodies_ != nullptr)
    {
        ::operator delete(static_cast<void*>(hot_bodies_),
                          std::align_val_t(alignof(std::atomic<void*>)));
        hot_bodies_ = nullptr;
    }
    resources_.destroy();
    waits_.frame_waiters_.clear();
    waits_.join_parent_.clear();
    if (views_ != nullptr)
    {
        for (std::uint32_t i = 0; i < view_capacity_; ++i)
        {
            views_[i].~task_context();
        }
        ::operator delete(static_cast<void*>(views_), std::align_val_t(alignof(task_context)));
        views_ = nullptr;
        view_capacity_ = 0;
    }
    periodic_.clear();
    periodic_flag_.clear();
    role_count_ = 0;
    worker_count_ = 0;
    created_ = false;
    }

inline std::uint64_t task_graph::begin_frame(thread::role id) noexcept
    {
    role_slot* slot = role_at(id);
    if (slot == nullptr)
    {
        return 0;
    }
    const std::uint64_t frame = slot->frame_.fetch_add(1, std::memory_order_acq_rel) + 1;
    if (slot->sequencer_ != nullptr && slot->sequencer_->ready_size() != 0)
    {
        metrics_.overruns_.fetch_add(1, std::memory_order_relaxed);
    }
    if (draining_.load(std::memory_order_acquire))
    {
        return frame;
    }
    // 延迟激活：本角色到帧后转入正常激活
    for (std::size_t i = 0; i < delayed_.size(); ++i)
    {
        const std::uint32_t index = delayed_[i];
        task_node* node = table_.at_index(index);
        if (node == nullptr ||
            (node->flags_.load(std::memory_order_acquire) & flag_delay_armed) == 0)
        {
            continue;
        }
        if (role_index(node->route().role_value()) != role_index(id))
        {
            continue;
        }
        if (index >= delay_frame_.size() || frame < delay_frame_[index])
        {
            continue;
        }
        node->flags_.fetch_and(~flag_delay_armed, std::memory_order_acq_rel);
        (void)activate(task_id{index});
    }
    for (std::size_t i = 0; i < periodic_.size(); ++i)
    {
        const std::uint32_t index = periodic_[i];
        task_node* node = table_.at_index(index);
        if (node == nullptr)
        {
            continue;
        }
        if (role_index(node->route().role_value()) != role_index(id))
        {
            continue;
        }
        if (node->pending_deps_.load(std::memory_order_acquire) > 0)
        {
            continue;
        }
        const std::uint32_t period = node->frame_period();
        if (period > 1 && (frame % period) != 0)
        {
            continue;
        }
        (void)activate(task_id{index});
    }
    return frame;
    }

inline void task_graph::end_frame(thread::role id) noexcept
    {
    role_slot* slot = role_at(id);
    if (slot == nullptr)
    {
        return;
    }
    slot->epoch_.fetch_add(1, std::memory_order_acq_rel);
    wake_frame_waiters(id);
    }

inline void task_graph::detach_thread(thread::role id) noexcept
    {
    role_slot* slot = role_at(id);
    if (slot == nullptr)
    {
        return;
    }
    slot->attached_.store(false, std::memory_order_release);
    slot->thread_hash_.store(0, std::memory_order_release);
    }

inline bool task_graph::thread_attached(thread::role id) const noexcept
    {
    const role_slot* slot = role_at(id);
    return slot != nullptr && slot->attached_.load(std::memory_order_acquire);
    }

inline void task_graph::release_bodies() noexcept
    {
    if (body_destroy_.size() == 0)
    {
        return;
    }
    const std::uint32_t count = table_.size();
    for (std::uint32_t i = 0; i < count && i < body_destroy_.size(); ++i)
    {
        task_body_destroy destroy = body_destroy_[i];
        if (destroy == nullptr)
        {
            continue;
        }
        task_node* node = table_.at_index(i);
        if (node != nullptr && node->body_object_ != nullptr)
        {
            destroy(node->body_object_);
            node->body_object_ = nullptr;
        }
        body_destroy_[i] = nullptr;
    }
    }

inline bool task_graph::create_workers(const task_graph_config& config) noexcept
    {
    if (worker_count_ == 0)
    {
        worker_role_.clear();
        return true;
    }
    void* memory = ::operator new(sizeof(task_worker_slot) * worker_count_,
                                  std::align_val_t(alignof(task_worker_slot)), std::nothrow);
    if (memory == nullptr)
    {
        report(error_severity::fatal, errc::invariant_broken, "worker slot allocation failed");
        return false;
    }
    workers_ = static_cast<task_worker_slot*>(memory);
    for (std::uint32_t i = 0; i < worker_count_; ++i)
    {
        new (&workers_[i]) task_worker_slot{};
        if (!workers_[i].stealable_.create(config.local_capacity) ||
            !workers_[i].bound_.create(config.local_capacity) ||
            !workers_[i].mailbox_.create(config.local_capacity))
        {
            report(error_severity::fatal, errc::invariant_broken, "worker queue create failed");
            return false;
        }
    }
    worker_role_.resize_fill(worker_count_, 0);
    return true;
    }

inline bool task_graph::create_roles(const task_graph_config& config) noexcept
    {
    role_count_ = config.role_count == 0 ? 1u : config.role_count;
    if (role_count_ > role_id_max)
    {
        report(error_severity::fatal, errc::invalid_argument, "role count exceeds 65535");
        return false;
    }
    void* memory =
        ::operator new(sizeof(role_slot) * role_count_, std::align_val_t(alignof(role_slot)),
                       std::nothrow);
    if (memory == nullptr)
    {
        report(error_severity::fatal, errc::invariant_broken, "role table allocation failed");
        return false;
    }
    roles_ = static_cast<role_slot*>(memory);
    for (std::uint32_t i = 0; i < role_count_; ++i)
    {
        new (&roles_[i]) role_slot{};
        roles_[i].id_ = make_role(static_cast<std::uint16_t>(i));
    }

    dense<std::uint8_t> owner{};
    if (worker_count_ != 0)
    {
        owner.resize_fill(worker_count_, 0xFFu);
    }

    for (std::uint32_t i = 0; i < role_count_; ++i)
    {
        const role_config* source = nullptr;
        if (config.roles != nullptr)
        {
            source = &config.roles[i];
        }
        role_slot& slot = roles_[i];
        if (source != nullptr)
        {
            slot.first_worker_ = source->first_worker_;
            slot.worker_count_ = source->worker_count_;
            slot.mode_ = source->mode_;
            slot.policy_ = source->policy_;
            slot.external_ = source->external_;
            slot.frame_auto_ = source->frame_auto_;
            slot.deterministic_ = source->deterministic_;
            slot.frame_rate_ = source->frame_rate_ == 0 ? 60 : source->frame_rate_;
            if (source->name_ != nullptr)
            {
                std::size_t n = 0;
                for (; n + 1 < sizeof(slot.name_) && source->name_[n] != '\0'; ++n)
                {
                    slot.name_[n] = source->name_[n];
                }
                slot.name_[n] = '\0';
            }
        }
        else
        {
            slot.first_worker_ = 0;
            slot.worker_count_ = worker_count_;
            slot.mode_ = order_mode::parallel;
        }

        if (slot.worker_count_ == 0)
        {
            // 嵌入式模式：无工作线程，由调用线程 pump 顺序域
            if (worker_count_ != 0 || role_count_ != 1)
            {
                report(error_severity::fatal, errc::invalid_argument, "role has no worker");
                return false;
            }
            slot.mode_ = order_mode::sequential;
            slot.first_worker_ = task_worker_none;
        }
        if (static_cast<std::size_t>(slot.first_worker_) + slot.worker_count_ > worker_count_ &&
            slot.worker_count_ != 0)
        {
            report(error_severity::fatal, errc::invalid_argument, "role range out of bounds");
            return false;
        }
        if (slot.mode_ == order_mode::sequential && slot.worker_count_ > 1)
        {
            report(error_severity::fatal, errc::invalid_argument,
                   "sequential role requires exactly one worker");
            return false;
        }
        if (slot.external_ && slot.mode_ != order_mode::sequential)
        {
            report(error_severity::fatal, errc::invalid_argument,
                   "external role must be sequential");
            return false;
        }
        for (std::uint32_t w = slot.first_worker_; w < slot.first_worker_ + slot.worker_count_;
             ++w)
        {
            if (owner[w] != 0xFFu)
            {
                report(error_severity::fatal, errc::invalid_argument, "role ranges overlap");
                return false;
            }
            owner[w] = static_cast<std::uint8_t>(i);
            worker_role_[w] = static_cast<std::uint8_t>(i);
        }

        if (slot.mode_ == order_mode::sequential)
        {
            void* seq_memory = ::operator new(sizeof(task_sequencer),
                                              std::align_val_t(alignof(task_sequencer)),
                                              std::nothrow);
            if (seq_memory == nullptr)
            {
                report(error_severity::fatal, errc::invariant_broken,
                       "sequencer allocation failed");
                return false;
            }
            slot.sequencer_ = new (seq_memory) task_sequencer{};
            if (!slot.sequencer_->create(slot.id_, config.command_capacity, config.task_capacity,
                                         table_, slot.policy_))
            {
                report(error_severity::fatal, errc::invariant_broken, "sequencer create failed");
                return false;
            }
        }
    }

    for (std::uint32_t w = 0; w < worker_count_; ++w)
    {
        if (owner[w] == 0xFFu)
        {
            report(error_severity::fatal, errc::invalid_argument, "worker not covered by role");
            return false;
        }
    }
    return true;
    }

inline bool task_graph::set_body(task_id id, task_body body) noexcept
    {
    task_node* node = node_of(id);
    if (node == nullptr)
    {
        report(error_severity::error, errc::task_identity_invalid, "set_body invalid task", id);
        return false;
    }
    node->body_kind_ = body.kind_;
    node->invoke_ = body.invoke_;
    node->body_object_ = body.object_;
    if (id.index_ < body_destroy_.size())
    {
        body_destroy_[id.index_] = body.destroy_;
    }
    return true;
    }

inline bool task_graph::migrate(task_id id, thread::role target) noexcept
    {
    return set_role(id, target);
    }

inline bool task_graph::rebind(task_id id, thread::role target) noexcept
    {
    return set_role(id, target);
    }

inline bool task_graph::set_role(task_id id, thread::role target) noexcept
    {
    task_node* node = node_of(id);
    if (node == nullptr)
    {
        report(error_severity::error, errc::task_identity_invalid, "migrate on invalid task", id);
        return false;
    }
    const role_slot* slot = role_at(target);
    if (slot == nullptr)
    {
        report(error_severity::error, errc::role_unbound, "migrate to unbound role", id, target);
        return false;
    }
    for (;;)
    {
        route_word old_route{node->route_.load(std::memory_order_acquire)};
        route_word new_route = old_route;
        new_route.set_role(target);
        if (!valid_route(new_route, true))
        {
            report(error_severity::error, errc::affinity_violation,
                   "migrate rejected by combination matrix", id, target);
            return false;
        }
        if (node->route_.compare_exchange_weak(old_route.value_, new_route.value_,
                                               std::memory_order_acq_rel,
                                               std::memory_order_acquire))
        {
            break;
        }
    }
    if (node->state() == task_state::running)
    {
        node->flags_.fetch_or(flag_identity_pending, std::memory_order_release);
    }
    metrics_.migrations_.fetch_add(1, std::memory_order_relaxed);
    return true;
    }

inline bool task_graph::pin(task_id id, std::uint32_t worker) noexcept
    {
    task_node* node = node_of(id);
    if (node == nullptr)
    {
        report(error_severity::error, errc::task_identity_invalid, "pin on invalid task", id);
        return false;
    }
    if (worker >= worker_count_)
    {
        report(error_severity::error, errc::no_such_worker, "pin target out of range", id);
        return false;
    }
    for (;;)
    {
        route_word old_route{node->route_.load(std::memory_order_acquire)};
        route_word new_route = old_route;
        new_route.set_affinity(affinity_kind::worker);
        new_route.set_worker(worker);
        if (role_index(new_route.role_value()) >= role_count_ ||
            !worker_in_role(worker, new_route.role_value()))
        {
            report(error_severity::warning, errc::affinity_violation,
                   "pin target not in task role", id, new_route.role_value(), worker);
            return false;
        }
        if (node->route_.compare_exchange_weak(old_route.value_, new_route.value_,
                                               std::memory_order_acq_rel,
                                               std::memory_order_acquire))
        {
            break;
        }
    }
    node->flags_.fetch_and(~flag_allow_steal, std::memory_order_release);
    return true;
    }

inline bool task_graph::set_stealable(task_id id, bool value) noexcept
    {
    task_node* node = node_of(id);
    if (node == nullptr)
    {
        report(error_severity::error, errc::task_identity_invalid, "set_stealable invalid task",
               id);
        return false;
    }
    const route_word route{node->route_.load(std::memory_order_acquire)};
    if (route.affinity() != affinity_kind::worker || route.mode() == order_mode::sequential)
    {
        report(error_severity::warning, errc::invalid_steal_policy,
               "set_stealable requires bound parallel task", id, route.role_value());
        return false;
    }
    std::uint32_t flags = node->flags_.load(std::memory_order_acquire);
    for (;;)
    {
        const std::uint32_t wanted = value ? (flags | flag_allow_steal)
                                           : (flags & ~flag_allow_steal);
        if (node->flags_.compare_exchange_weak(flags, wanted, std::memory_order_acq_rel,
                                               std::memory_order_acquire))
        {
            return true;
        }
    }
    }

inline bool task_graph::set_order_mode(task_id id, order_mode mode) noexcept
    {
    task_node* node = node_of(id);
    if (node == nullptr)
    {
        report(error_severity::error, errc::task_identity_invalid, "set_order_mode invalid task",
               id);
        return false;
    }
    for (;;)
    {
        route_word old_route{node->route_.load(std::memory_order_acquire)};
        route_word new_route = old_route;
        new_route.set_mode(mode);
        if (!valid_route(new_route, true))
        {
            report(error_severity::warning, errc::task_decl_invalid,
                   "sequential requires a sequential role", id, new_route.role_value());
            return false;
        }
        if (node->route_.compare_exchange_weak(old_route.value_, new_route.value_,
                                               std::memory_order_acq_rel,
                                               std::memory_order_acquire))
        {
            return true;
        }
    }
    }

inline bool task_graph::set_priority(task_id id, priority value) noexcept
    {
    task_node* node = node_of(id);
    if (node == nullptr)
    {
        report(error_severity::error, errc::task_identity_invalid, "set_priority invalid task",
               id);
        return false;
    }
    const route_word current{node->route_.load(std::memory_order_acquire)};
    if (current.mode() == order_mode::sequential)
    {
        report(error_severity::warning, errc::invalid_argument,
               "priority is not supported on sequential tasks", id);
        return false;
    }
    for (;;)
    {
        route_word old_route{node->route_.load(std::memory_order_acquire)};
        route_word new_route = old_route;
        new_route.set_priority(value);
        if (node->route_.compare_exchange_weak(old_route.value_, new_route.value_,
                                               std::memory_order_acq_rel,
                                               std::memory_order_acquire))
        {
            return true;
        }
    }
    }

inline bool task_graph::reorder(task_id id, order_key key) noexcept
    {
    task_node* node = node_of(id);
    if (node == nullptr)
    {
        report(error_severity::error, errc::task_identity_invalid, "reorder invalid task", id);
        return false;
    }
    if (key.value_ == invalid_order_key)
    {
        report(error_severity::error, errc::order_key_exhausted, "order key exhausted", id);
        return false;
    }
    const route_word route{node->route_.load(std::memory_order_acquire)};
    if (route.mode() == order_mode::sequential)
    {
        role_slot* slot = role_at(route.role_value());
        if (slot == nullptr || slot->sequencer_ == nullptr)
        {
            return false;
        }
        if (slot->policy_ == order_policy::fifo)
        {
            report(error_severity::warning, errc::order_policy_unsupported,
                   "fifo role does not support reorder", id, route.role_value());
            return false;
        }
        node->order_key_.store(key.value_, std::memory_order_release);
        const task_command command{task_op::reorder, id.index_, 0, key.value_};
        if (!slot->sequencer_->push_command(command))
        {
            report(error_severity::warning, errc::queue_full, "reorder command queue full", id);
            return false;
        }
        metrics_.reorders_.fetch_add(1, std::memory_order_relaxed);
        return true;
    }
    node->order_key_.store(key.value_, std::memory_order_release);
    metrics_.reorders_.fetch_add(1, std::memory_order_relaxed);
    return true;
    }

inline bool task_graph::reparent(task_id id, task_id parent) noexcept
    {
    task_node* node = node_of(id);
    if (node == nullptr)
    {
        report(error_severity::error, errc::task_identity_invalid, "reparent invalid task", id);
        return false;
    }
    if (parent.valid())
    {
        task_node* target = node_of(parent);
        if (target == nullptr)
        {
            report(error_severity::error, errc::task_identity_invalid, "reparent invalid parent",
                   parent);
            return false;
        }
        if (parent.index_ == id.index_ || is_ancestor(parent.index_, id.index_))
        {
            report(error_severity::error, errc::task_state_conflict, "reparent would form a cycle",
                   id);
            return false;
        }
        if (target->state() == task_state::completed)
        {
            report(error_severity::warning, errc::task_state_conflict,
                   "reparent target already completed", id);
            return false;
        }
    }
    const std::uint16_t depth = parent.valid() ? depth_of(parent.index_) : 0;
    if (depth + 1 > task_max_depth)
    {
        report(error_severity::error, errc::task_depth_exceeded, "reparent depth exceeded", id);
        return false;
    }
    for (;;)
    {
        parent_word old_word{node->parent_.load(std::memory_order_acquire)};
        parent_word new_word = old_word;
        new_word.set_parent(parent.valid() ? parent.index_ : invalid_task_index);
        new_word.set_depth(static_cast<std::uint16_t>(depth + 1));
        if (node->parent_.compare_exchange_weak(old_word.value_, new_word.value_,
                                                std::memory_order_acq_rel,
                                                std::memory_order_acquire))
        {
            break;
        }
    }
    metrics_.reparents_.fetch_add(1, std::memory_order_relaxed);
    return true;
    }

inline bool task_graph::add_dependency(task_id before, task_id after) noexcept
    {
    task_node* target = node_of(after);
    if (target == nullptr || node_of(before) == nullptr)
    {
        report(error_severity::error, errc::task_identity_invalid, "dependency endpoints invalid",
               after);
        return false;
    }
    if (before.index_ == after.index_)
    {
        report(error_severity::error, errc::task_decl_invalid, "self dependency rejected", after);
        return false;
    }
    if (edges_.size() >= edges_.capacity())
    {
        report(error_severity::error, errc::invariant_broken, "dependency table full", after);
        return false;
    }
    const std::uint32_t head = dep_head_[before.index_];
    edges_.push_back(dep_edge{after.index_, head});
    dep_head_[before.index_] = static_cast<std::uint32_t>(edges_.size());
    target->pending_deps_.fetch_add(1, std::memory_order_acq_rel);
    return true;
    }

inline bool task_graph::add_access(task_id id, resource_id resource, access_mode mode) noexcept
    {
    if (!id.valid() || id.index_ >= access_.head_.size() || !resource.valid())
    {
        return false;
    }
    if (access_.edges_.size() >= access_.edges_.capacity())
    {
        report(error_severity::warning, errc::queue_full, "access table full", id);
        return false;
    }
    resource_edge edge{};
    edge.resource_ = resource.value_;
    edge.mode_ = static_cast<std::uint32_t>(mode);
    edge.next_ = access_.head_[id.index_];
    access_.edges_.push_back(edge);
    access_.head_[id.index_] = static_cast<std::uint32_t>(access_.edges_.size());
    return true;
    }

inline bool task_graph::set_budget(task_id id, std::uint64_t budget_ns) noexcept
    {
    task_node* node = node_of(id);
    if (node == nullptr || id.index_ >= budget_ns_.size())
    {
        report(error_severity::error, errc::task_identity_invalid, "set_budget invalid task", id);
        return false;
    }
    budget_ns_[id.index_] = budget_ns;
    return true;
    }

inline bool task_graph::set_deadline(task_id id, std::uint64_t timeout_ns) noexcept
    {
    task_node* node = node_of(id);
    if (node == nullptr || id.index_ >= deadline_.size())
    {
        return false;
    }
    deadline_[id.index_] = timeout_ns;
    return true;
    }

inline bool task_graph::set_context(task_id id, context_id ctx) noexcept
    {
    if (!contexts_.set(id, ctx))
    {
        if (id.valid() && ctx.valid())
        {
            report(error_severity::error, errc::task_identity_invalid, "set_context invalid id",
                   id);
        }
        return false;
    }
    return true;
    }

inline void task_graph::set_work_submit(void* self, bool (*submit)(void*, job*) noexcept) noexcept
    {
    submit_self_ = self;
    submit_job_ = submit;
    }

inline void task_graph::set_default_deadline(std::uint64_t timeout_ns) noexcept
    {
    default_deadline_ns_.store(timeout_ns, std::memory_order_release);
    }

inline bool task_graph::disable(task_id id) noexcept
    {
    task_node* node = node_of(id);
    if (node == nullptr)
    {
        return false;
    }
    std::uint32_t state = node->state_.load(std::memory_order_acquire);
    for (;;)
    {
        if (state == task_state_constructing)
        {
            return false;
        }
        if (node->state_.compare_exchange_weak(state, static_cast<std::uint32_t>(task_state::disabled),
                                               std::memory_order_acq_rel,
                                               std::memory_order_acquire))
        {
            return true;
        }
    }
    }

inline bool task_graph::enable(task_id id) noexcept
    {
    task_node* node = node_of(id);
    if (node == nullptr)
    {
        return false;
    }
    std::uint32_t state = node->state_.load(std::memory_order_acquire);
    for (;;)
    {
        if (state != static_cast<std::uint32_t>(task_state::disabled) &&
            state != static_cast<std::uint32_t>(task_state::faulted))
        {
            return false;
        }
        if (node->state_.compare_exchange_weak(state, static_cast<std::uint32_t>(task_state::dormant),
                                               std::memory_order_acq_rel,
                                               std::memory_order_acquire))
        {
            node->flags_.fetch_and(~flag_faulted, std::memory_order_release);
            return true;
        }
    }
    }

inline bool task_graph::cancel(task_id scope) noexcept
    {
    task_node* node = node_of(scope);
    if (node == nullptr)
    {
        return false;
    }
    node->flags_.fetch_or(flag_cancelled, std::memory_order_acq_rel);
    metrics_.cancellations_.fetch_add(1, std::memory_order_relaxed);
    if (trace_ != nullptr)
    {
        trace_->record(trace_kind::cancel, scope, task_worker_none,
                       node->route().role_value(), 0);
    }
    return true;
    }

inline bool task_graph::clear_cancel(task_id scope) noexcept
    {
    task_node* node = node_of(scope);
    if (node == nullptr)
    {
        return false;
    }
    node->flags_.fetch_and(~flag_cancelled, std::memory_order_acq_rel);
    return true;
    }

inline void task_graph::set_draining(bool value) noexcept
    {
    draining_.store(value, std::memory_order_release);
    }

inline bool task_graph::replace_body(task_id id, task_body body) noexcept
    {
    if (!id.valid() || hot_bodies_ == nullptr || id.index_ >= view_capacity_)
    {
        return false;
    }
    if (body_version_count_.size() != 0 &&
        body_version_count_[id.index_] >= body_version_capacity_)
    {
        report(error_severity::warning, errc::queue_full, "body version capacity reached", id);
        return false;
    }
    auto* entry = new (std::nothrow) body_entry{body, ++body_version_counter_};
    if (entry == nullptr)
    {
        report(error_severity::error, errc::invariant_broken, "body version allocation failed",
               id);
        return false;
    }
    hot_bodies_[id.index_].store(static_cast<void*>(entry), std::memory_order_release);
    if (body_versions_.size() < body_versions_.capacity())
    {
        body_versions_.push_back(entry);
    }
    if (body_version_count_.size() != 0)
    {
        ++body_version_count_[id.index_];
    }
    return true;
    }

inline void task_graph::set_trace(trace_recorder* recorder) noexcept
    {
    trace_ = recorder;
    }

inline void task_graph::set_admission(admission_controller* controller) noexcept
    {
    admission_ = controller;
    }

inline void task_graph::set_error_sink(error_sink sink, void* user) noexcept
    {
    reporter_.set_sink(sink, user);
    }

inline void task_graph::set_wake_hook(void* self, void (*wake)(void*, std::uint32_t) noexcept) noexcept
    {
    wake_self_ = self;
    wake_fn_ = wake;
    }

inline bool task_graph::created() const noexcept
    {
    return created_;
    }

inline std::size_t task_graph::worker_count() const noexcept
    {
    return worker_count_;
    }

inline std::size_t task_graph::role_count() const noexcept
    {
    return role_count_;
    }

inline std::size_t task_graph::task_capacity() const noexcept
    {
    return table_.capacity();
    }

inline std::size_t task_graph::task_count() const noexcept
    {
    return table_.size();
    }

inline role_slot* task_graph::role_at(thread::role id) noexcept
    {
    const std::size_t index = role_index(id);
    return index < role_count_ ? &roles_[index] : nullptr;
    }

inline const role_slot* task_graph::role_at(thread::role id) const noexcept
    {
    const std::size_t index = role_index(id);
    return index < role_count_ ? &roles_[index] : nullptr;
    }

inline order_mode task_graph::role_mode(thread::role id) const noexcept
    {
    const role_slot* slot = role_at(id);
    return slot != nullptr ? slot->mode_ : order_mode::parallel;
    }

inline bool task_graph::role_external(thread::role id) const noexcept
    {
    const role_slot* slot = role_at(id);
    return slot != nullptr && slot->external_;
    }

inline bool task_graph::role_frame_auto(thread::role id) const noexcept
    {
    const role_slot* slot = role_at(id);
    return slot != nullptr && slot->frame_auto_;
    }

inline std::uint32_t task_graph::role_frame_rate(thread::role id) const noexcept
    {
    const role_slot* slot = role_at(id);
    return slot != nullptr ? slot->frame_rate_ : 0;
    }

inline thread::role task_graph::worker_role(std::uint32_t worker) const noexcept
    {
    return worker < worker_role_.size() ? static_cast<thread::role>(worker_role_[worker])
                                       : thread::role::general;
    }

inline std::uint32_t task_graph::task_statistics_live() const noexcept
    {
    const std::uint64_t started = metrics_.activations_.load(std::memory_order_relaxed);
    const std::uint64_t finished = metrics_.completions_.load(std::memory_order_relaxed);
    return static_cast<std::uint32_t>(started > finished ? started - finished : 0);
    }

inline task_id task_graph::declare(const char* name, task_body body, thread::role target) noexcept
    {
    route_word route = route_word::make(target, order_mode::parallel, affinity_kind::none,
                                        priority::normal, task_worker_none);
    parent_word parent{};
    return declare_full(name, body, route, parent, invalid_order_key, 0);
    }

inline resource_table& task_graph::resources() noexcept
    {
    return resources_;
    }

inline std::uint64_t task_graph::budget_of(task_id id) const noexcept
    {
    return id.valid() && id.index_ < budget_ns_.size() ? budget_ns_[id.index_] : 0;
    }

inline context_id task_graph::context_of(task_id id) const noexcept
    {
    return contexts_.of(id);
    }

inline void task_graph::release_context(context_id ctx) noexcept
    {
    contexts_.release(ctx);
    }

inline std::uint64_t task_graph::deadline_of(task_id id) const noexcept
    {
    if (!id.valid() || id.index_ >= deadline_.size())
    {
        return 0;
    }
    const std::uint64_t explicit_value = deadline_[id.index_];
    return explicit_value != 0 ? explicit_value
                               : default_deadline_ns_.load(std::memory_order_relaxed);
    }

inline void task_graph::raise(error_severity severity, errc code, task_id id, const char* text) noexcept
    {
    report(severity, code, text, id);
    }

inline context_table& task_graph::contexts() noexcept
    {
    return contexts_.table();
    }

inline bool task_graph::cancelled(task_id id) const noexcept
    {
    std::uint32_t index = id.valid() ? id.index_ : invalid_task_index;
    for (std::uint32_t depth = 0; depth < task_max_depth; ++depth)
    {
        if (index == invalid_task_index || index >= table_.capacity())
        {
            return false;
        }
        const task_node* node = &table_.nodes_[index];
        if (node->constructing())
        {
            return false;
        }
        if ((node->flags_.load(std::memory_order_acquire) & flag_cancelled) != 0)
        {
            return true;
        }
        const std::uint32_t parent = parent_word{node->parent_.load(std::memory_order_acquire)}
                                         .parent();
        if (parent == index || parent == invalid_task_index)
        {
            return false;
        }
        index = parent;
    }
    return false;
    }

inline std::uint64_t task_graph::frame_of(thread::role id) const noexcept
    {
    const role_slot* slot = role_at(id);
    return slot != nullptr ? slot->frame_.load(std::memory_order_acquire) : 0;
    }

inline std::uint64_t task_graph::epoch_of(thread::role id) const noexcept
    {
    const role_slot* slot = role_at(id);
    return slot != nullptr ? slot->epoch_.load(std::memory_order_acquire) : 0;
    }

inline bool task_graph::busy() const noexcept
    {
    if (inject_.size() > 0)
    {
        return true;
    }
    for (std::uint32_t i = 0; i < worker_count_; ++i)
    {
        if (!workers_[i].bound_.empty() || !workers_[i].stealable_.empty() ||
            workers_[i].mailbox_.size() > 0)
        {
            return true;
        }
    }
    for (std::uint32_t i = 0; i < role_count_; ++i)
    {
        if (roles_[i].sequencer_ != nullptr &&
            (roles_[i].sequencer_->ready_size() != 0 ||
             roles_[i].sequencer_->command_pending() != 0))
        {
            return true;
        }
    }
    return false;
    }

inline bool task_graph::has_worker_work(std::uint32_t worker) const noexcept
    {
    if (!created_)
    {
        return false;
    }
    if (worker < worker_count_)
    {
        if (!workers_[worker].bound_.empty() || !workers_[worker].stealable_.empty() ||
            workers_[worker].mailbox_.size() > 0)
        {
            return true;
        }
    }
    if (inject_.size() > 0)
    {
        return true;
    }
    const thread::role id = worker_role(worker);
    const role_slot* slot = role_at(id);
    if (slot != nullptr && slot->sequencer_ != nullptr && slot->first_worker_ == worker)
    {
        return slot->sequencer_->ready_size() != 0 || slot->sequencer_->command_pending() != 0;
    }
    return false;
    }

inline task_view task_graph::view(task_id id) noexcept
    {
    task_view result{};
    const task_node* node = node_of(id);
    if (node == nullptr)
    {
        report(error_severity::warning, errc::task_identity_invalid, "view invalid task", id);
        return result;
    }
    result.id_ = id;
    result.state_ = node->state();
    const route_word route{node->route_.load(std::memory_order_acquire)};
    result.role_ = route.role_value();
    result.mode_ = route.mode();
    result.affinity_ = route.affinity();
    result.worker_ = route.affinity() == affinity_kind::worker ? route.worker()
                                                              : task_worker_none;
    result.stealable_ = route.affinity() != affinity_kind::worker ||
                        (node->flags_.load(std::memory_order_acquire) & flag_allow_steal) != 0;
    result.parent_ = task_id{parent_word{node->parent_.load(std::memory_order_acquire)}.parent()};
    result.key_ = order_key{node->order_key_.load(std::memory_order_acquire)};
    result.activations_ = node->activation_count_.load(std::memory_order_relaxed);
    result.name_ = node->name_;
    return result;
    }

inline task_state task_graph::state_of(task_id id) const noexcept
    {
    const task_node* node = node_of(id);
    return node != nullptr ? node->state() : task_state::faulted;
    }

inline task_node* task_graph::node_at(task_id id) noexcept
    {
    return node_of(id);
    }

inline task_metrics& task_graph::metrics() noexcept
    {
    return metrics_;
    }

inline const task_metrics& task_graph::metrics() const noexcept
    {
    return metrics_;
    }

inline bool task_graph::draining() const noexcept
    {
    return draining_.load(std::memory_order_acquire);
    }

inline std::uint32_t task_graph::body_version(task_id id) noexcept
    {
    if (!id.valid() || hot_bodies_ == nullptr || id.index_ >= view_capacity_)
    {
        return 0;
    }
    void* raw = hot_bodies_[id.index_].load(std::memory_order_acquire);
    return raw != nullptr ? static_cast<const body_entry*>(raw)->version_ : 0;
    }

inline trace_recorder* task_graph::trace() const noexcept
    {
    return trace_;
    }

inline const error_reporter& task_graph::reporter() const noexcept
    {
    return reporter_;
    }

inline error_state task_graph::last_error() const noexcept
    {
    return last_state_;
    }

inline task_node* task_graph::node_of(task_id id) noexcept
    {
    return table_.at(id);
    }

inline const task_node* task_graph::node_of(task_id id) const noexcept
    {
    return table_.at(id);
    }

inline task_worker_slot* task_graph::worker_at(std::uint32_t worker) noexcept
    {
    return worker < worker_count_ ? &workers_[worker] : nullptr;
    }

inline bool task_graph::worker_in_role(std::uint32_t worker, thread::role id) const noexcept
    {
    const role_slot* slot = role_at(id);
    return slot != nullptr && worker >= slot->first_worker_ &&
           worker < slot->first_worker_ + slot->worker_count_;
    }

inline bool task_graph::valid_route(const route_word& route, bool check_workers) const noexcept
    {
    const role_slot* slot = role_at(route.role_value());
    if (slot == nullptr)
    {
        return role_index(route.role_value()) < role_count_;
    }
    if (route.mode() == order_mode::sequential && slot->mode_ != order_mode::sequential)
    {
        return false;
    }
    if (route.affinity() == affinity_kind::worker)
    {
        if (!check_workers || worker_count_ == 0)
        {
            return true;
        }
        if (route.worker() == task_worker_none || route.worker() >= worker_count_)
        {
            return false;
        }
        if (!worker_in_role(route.worker(), route.role_value()))
        {
            return false;
        }
    }
    return true;
    }

inline bool task_graph::is_ancestor(std::uint32_t candidate, std::uint32_t node) const noexcept
    {
    std::uint32_t cursor = node;
    for (std::uint32_t depth = 0; depth < task_max_depth; ++depth)
    {
        if (cursor >= table_.capacity())
        {
            return false;
        }
        const std::uint32_t parent =
            parent_word{table_.nodes_[cursor].parent_.load(std::memory_order_acquire)}.parent();
        if (parent == invalid_task_index)
        {
            return false;
        }
        if (parent == candidate)
        {
            return true;
        }
        cursor = parent;
    }
    return true;
    }

inline std::uint16_t task_graph::depth_of(std::uint32_t node) const noexcept
    {
    return parent_word{table_.nodes_[node].parent_.load(std::memory_order_acquire)}.depth();
    }

inline bool task_graph::periodic_listed(std::uint32_t index) const noexcept
    {
    return index < periodic_flag_.size() && periodic_flag_[index] != 0;
    }

} // namespace thread
