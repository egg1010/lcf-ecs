#pragma once

#include <cstddef>
#include <cstdint>
#include <new>
#include <type_traits>
#include <utility>

#include "thread/task/task_graph.hpp"

namespace thread
{

// 声明期字段补丁：tag 只改字段，不建任务、不做类型擦除
struct task_spec
{
    thread::role role_{thread::role::general};
    order_mode mode_{order_mode::parallel};
    affinity_kind affinity_{affinity_kind::none};
    std::uint32_t worker_{task_worker_none};
    priority priority_{priority::normal};
    activation_policy activation_{activation_policy::manual};
    activation_overlap overlap_{activation_overlap::drop};
    std::uint32_t period_{1};
    std::uint32_t flags_{0};
    std::uint64_t key_{invalid_order_key};
    std::uint64_t deadline_{0};
    task_id after_{};
    bool any_predecessor_{false};
    context_id context_{};
    resource_id reads_[4]{};
    std::uint32_t read_count_{0};
    resource_id writes_[4]{};
    std::uint32_t write_count_{0};
};

struct task_chain
{
    task_id head_{};
    task_id tail_{};
};

namespace detail
{

inline task_body make_body(void (*fn)(task_context&) noexcept) noexcept
{
    task_body body{};
    body.kind_ = task_body_plain_ctx;
    body.invoke_.plain_ctx_ = fn;
    return body;
}

inline task_body make_body(void (*fn)(task_context&, void*) noexcept) noexcept
{
    task_body body{};
    body.kind_ = task_body_plain;
    body.invoke_.plain_ = fn;
    return body;
}

inline task_body make_body(task<void> (*fn)(task_context&)) noexcept
{
    task_body body{};
    body.kind_ = task_body_coroutine_ctx;
    body.invoke_.coro_ctx_ = fn;
    return body;
}

inline task_body make_body(task<void> (*fn)(task_context&, void*)) noexcept
{
    task_body body{};
    body.kind_ = task_body_coroutine;
    body.invoke_.coro_ = fn;
    return body;
}

template <typename F>
task_body make_body(F&& fn) noexcept
{
    using stored = std::decay_t<F>;
    task_body body{};
    stored* object = new (std::nothrow) stored(std::forward<F>(fn));
    if (object == nullptr)
    {
        return body;
    }
    body.kind_ = task_body_plain;
    body.destroy_ = +[](void* self) noexcept
    {
        delete static_cast<stored*>(self);
    };
    if constexpr (requires(stored& value, task_context& ctx) { value.run(ctx); })
    {
        body.invoke_.plain_ = +[](task_context& ctx, void* self) noexcept
        {
            static_cast<stored*>(self)->run(ctx);
        };
    }
    else
    {
        body.invoke_.plain_ = +[](task_context& ctx, void* self) noexcept
        {
            (*static_cast<stored*>(self))(ctx);
        };
    }
    body.object_ = object;
    return body;
}

// 注册期一次性应用字段补丁；失败返回 false 且不改动节点
inline bool apply_spec(task_graph& graph, task_id id, const task_spec& spec) noexcept
{
    if (!id.valid())
    {
        return false;
    }
    operating_message error;
    // 注册默认值：general / parallel / normal，与默认值相同则跳过写入
    if (spec.role_ != thread::role::general && !graph.set_role_impl(id, spec.role_, error))
    {
        return false;
    }
    if (spec.mode_ == order_mode::sequential && !graph.set_order_mode_impl(id, spec.mode_, error))
    {
        return false;
    }
    if (spec.affinity_ == affinity_kind::worker)
    {
        if (!graph.pin_impl(id, spec.worker_, error))
        {
            return false;
        }
        if (spec.flags_ & flag_allow_steal)
        {
            if (!graph.set_stealable_impl(id, true, error))
            {
                return false;
            }
        }
    }
    if (spec.mode_ == order_mode::parallel && spec.priority_ != priority::normal)
    {
        (void)graph.set_priority_impl(id, spec.priority_, error);
    }
    if (spec.key_ != invalid_order_key)
    {
        (void)graph.reorder_impl(id, order_key{spec.key_}, error);
    }
    (void)graph.set_activation(id, spec.activation_, spec.period_, spec.overlap_, error);
    if (spec.deadline_ != 0)
    {
        (void)graph.set_deadline_impl(id, spec.deadline_, error);
    }
    if ((spec.flags_ & flag_detached) != 0)
    {
        task_node* node = graph.node_at(id);
        if (node != nullptr)
        {
            node->flags_.fetch_or(flag_detached, std::memory_order_acq_rel);
        }
    }
    for (std::uint32_t i = 0; i < spec.read_count_; ++i)
    {
        if (!graph.add_access_impl(id, spec.reads_[i], access_mode::read, error))
        {
            return false;
        }
    }
    for (std::uint32_t i = 0; i < spec.write_count_; ++i)
    {
        if (!graph.add_access_impl(id, spec.writes_[i], access_mode::write, error))
        {
            return false;
        }
    }
    if (spec.context_.valid())
    {
        (void)graph.set_context_impl(id, spec.context_, error);
    }
    if (spec.after_.valid())
    {
        if (!graph.add_dependency_impl(spec.after_, id, error))
        {
            return false;
        }
        if (spec.any_predecessor_)
        {
            task_node* node = graph.node_at(id);
            if (node != nullptr)
            {
                node->flags_.fetch_or(flag_after_any, std::memory_order_acq_rel);
            }
        }
    }
    return true;
}

// 无参可调用对象适配为任务体
template <typename F> struct zero_arg_body
{
    F fn_{};

    void run(task_context&) noexcept
    {
        fn_();
    }
};

} // namespace detail

// ---------------------------------------------------------------------------
// 公开任务体构造：函数指针 / 带上下文可调用 / 无参可调用 / stage 管道
// ---------------------------------------------------------------------------

template <typename F> task_body make_task_body(F&& fn) noexcept
{
    using stored = std::decay_t<F>;
    if constexpr (std::is_same_v<stored, task<void> (*)(task_context&)>)
    {
        return detail::make_body(static_cast<task<void> (*)(task_context&)>(fn));
    }
    else if constexpr (std::is_same_v<stored, void (*)(task_context&)>)
    {
        return detail::make_body(static_cast<void (*)(task_context&)>(fn));
    }
    else if constexpr (requires(stored& value, task_context& ctx) { value.run(ctx); })
    {
        return detail::make_body(std::forward<F>(fn));
    }
    else if constexpr (std::is_invocable_v<stored&, task_context&>)
    {
        static_assert(std::is_void_v<std::invoke_result_t<stored&, task_context&>>,
                      "coroutine lambda is not supported; use a function pointer or app::spawn");
        return detail::make_body(std::forward<F>(fn));
    }
    else if constexpr (std::is_invocable_v<stored&>)
    {
        static_assert(std::is_void_v<std::invoke_result_t<stored&>>,
                      "task body must return void");
        return detail::make_body(detail::zero_arg_body<stored>{std::forward<F>(fn)});
    }
    else
    {
        static_assert(sizeof(stored) == 0, "unsupported task body signature");
        return task_body{};
    }
}

// ---------------------------------------------------------------------------
// 管道标签
// ---------------------------------------------------------------------------

struct on_tag
{
    thread::role role_{thread::role::general};
};

struct ordered_tag
{
};
struct unordered_tag
{
};
struct stealable_tag
{
};
struct detached_tag
{
};
struct once_tag
{
};
struct on_signal_tag
{
};

inline constexpr ordered_tag ordered{};
inline constexpr unordered_tag unordered{};
inline constexpr stealable_tag stealable{};
inline constexpr detached_tag detached{};
inline constexpr once_tag once{};
inline constexpr on_signal_tag on_signal{};

inline constexpr on_tag on(thread::role target) noexcept
{
    return on_tag{target};
}

struct pinned_tag
{
    std::uint32_t worker_{task_worker_none};
};

inline constexpr pinned_tag pinned(std::uint32_t worker) noexcept
{
    return pinned_tag{worker};
}

struct priority_tag
{
    priority value_{priority::normal};
};

inline constexpr priority_tag at_priority(priority value) noexcept
{
    return priority_tag{value};
}

struct frames_tag
{
    std::uint32_t period_{1};
};

inline constexpr frames_tag every_frames(std::uint32_t period) noexcept
{
    return frames_tag{period};
}

struct after_tag
{
    task_id task_{};
};

inline constexpr after_tag after(task_id id) noexcept
{
    return after_tag{id};
}

inline constexpr after_tag after_any(task_id id) noexcept
{
    return after_tag{id};
}

struct reads_tag
{
    resource_id resource_{};
};

struct writes_tag
{
    resource_id resource_{};
};

inline constexpr reads_tag reads(resource_id id) noexcept
{
    return reads_tag{id};
}

inline constexpr writes_tag writes(resource_id id) noexcept
{
    return writes_tag{id};
}

struct context_tag
{
    context_id context_{};
};

inline constexpr context_tag with_context(context_id id) noexcept
{
    return context_tag{id};
}

struct deadline_tag
{
    std::uint64_t timeout_ns_{0};
};

inline constexpr deadline_tag deadline(std::uint64_t timeout_ns) noexcept
{
    return deadline_tag{timeout_ns};
}

template <typename F> struct run_tag
{
    F fn_{};
};

template <typename F>
    requires(!requires(std::decay_t<F>& value, task_context& ctx) { value.run(ctx); })
run_tag<std::decay_t<F>> run(F&& fn) noexcept
{
    return run_tag<std::decay_t<F>>{std::forward<F>(fn)};
}

// 已构造好的任务体（声明表、手工组装体）直接提交
struct body_run_tag
{
    task_body body_{};
};

inline body_run_tag run(task_body body) noexcept
{
    return body_run_tag{body};
}


// ---------------------------------------------------------------------------
// stage：普通可调用对象串行组合，执行时顺序调用，不创建中间任务
// ---------------------------------------------------------------------------

template <typename T>
concept stage_like = requires(T& value, task_context& ctx) { value.run(ctx); };

template <typename F> struct stage_fn
{
    F fn_{};

    void run(task_context& ctx) noexcept
    {
        fn_(ctx);
    }
};

template <typename A, typename B> struct stage_pair
{
    A first_{};
    B second_{};

    void run(task_context& ctx) noexcept
    {
        first_.run(ctx);
        second_.run(ctx);
    }
};

template <typename F> stage_fn<std::decay_t<F>> stage(F&& fn) noexcept
{
    return stage_fn<std::decay_t<F>>{std::forward<F>(fn)};
}

template <stage_like A, stage_like B>
stage_pair<A, B> operator|(A first, B second) noexcept
{
    return stage_pair<A, B>{std::move(first), std::move(second)};
}

template <stage_like S> run_tag<S> run(S pipeline) noexcept
{
    return run_tag<S>{std::move(pipeline)};
}

// ---------------------------------------------------------------------------
// builder：声明期只改字段，run/then 提交时才建依赖
// ---------------------------------------------------------------------------

struct task_builder
{
    task_graph* graph_{nullptr};
    task_spec spec_{};
    task_id head_{};
    task_id tail_{};

    task_builder() noexcept = default;

    task_builder(task_graph* graph, task_id id) noexcept : graph_(graph), head_(id), tail_(id)
    {
    }

    task_builder& role(thread::role value) noexcept
    {
        spec_.role_ = value;
        return *this;
    }

    task_builder& ordered() noexcept
    {
        spec_.mode_ = order_mode::sequential;
        return *this;
    }

    task_builder& unordered() noexcept
    {
        spec_.mode_ = order_mode::parallel;
        return *this;
    }

    task_builder& pinned(std::uint32_t worker) noexcept
    {
        spec_.affinity_ = affinity_kind::worker;
        spec_.worker_ = worker;
        return *this;
    }

    task_builder& stealable(bool value = true) noexcept
    {
        if (value)
        {
            spec_.flags_ |= flag_allow_steal;
        }
        else
        {
            spec_.flags_ &= ~flag_allow_steal;
        }
        return *this;
    }

    task_builder& priority_of(priority value) noexcept
    {
        spec_.priority_ = value;
        return *this;
    }

    task_builder& order(std::uint64_t key) noexcept
    {
        spec_.key_ = key;
        return *this;
    }

    task_builder& frames(std::uint32_t period) noexcept
    {
        spec_.activation_ = activation_policy::every_frames;
        spec_.period_ = period;
        return *this;
    }

    task_builder& activation_of(activation_policy policy,
                                activation_overlap overlap = activation_overlap::drop) noexcept
    {
        spec_.activation_ = policy;
        spec_.overlap_ = overlap;
        return *this;
    }

    task_builder& detach() noexcept
    {
        spec_.flags_ |= flag_detached;
        return *this;
    }

    task_builder& timeout(std::uint64_t timeout_ns) noexcept
    {
        spec_.deadline_ = timeout_ns;
        return *this;
    }

    task_builder& after_task(task_id id) noexcept
    {
        spec_.after_ = id;
        return *this;
    }

    task_builder& with_context(context_id id) noexcept
    {
        spec_.context_ = id;
        return *this;
    }

    task_builder& reads(resource_id id) noexcept
    {
        if (spec_.read_count_ < 4)
        {
            spec_.reads_[spec_.read_count_++] = id;
        }
        return *this;
    }

    task_builder& writes(resource_id id) noexcept
    {
        if (spec_.write_count_ < 4)
        {
            spec_.writes_[spec_.write_count_++] = id;
        }
        return *this;
    }

    task_id id() const noexcept
    {
        return tail_;
    }

    // 提交当前阶段：应用字段补丁并绑定任务体
    task_id run(task_body body) noexcept
    {
        if (graph_ == nullptr || !tail_.valid())
        {
            return invalid_task;
        }
        if (!detail::apply_spec(*graph_, tail_, spec_))
        {
            return invalid_task;
        }
        operating_message error;
        if (!graph_->set_body_impl(tail_, body, error))
        {
            return invalid_task;
        }
        if (spec_.activation_ == activation_policy::once)
        {
            (void)graph_->activate_impl(tail_, error);
        }
        return tail_;
    }

    // 追加一个阶段：新建任务并用 after 建立偏序
    task_builder& then(task_body body) noexcept
    {
        if (graph_ == nullptr)
        {
            return *this;
        }
        route_word route =
            route_word::make(spec_.role_, spec_.mode_, affinity_kind::none, spec_.priority_,
                             task_worker_none);
        parent_word parent{};
        const std::uint64_t key =
            spec_.key_ == invalid_order_key ? invalid_order_key : spec_.key_ + 1u;
        operating_message error;
        const task_id next =
            graph_->declare_full_impl(nullptr, body, route, parent, key, spec_.flags_, error);
        if (!next.valid())
        {
            return *this;
        }
        task_spec stage = spec_;
        stage.after_ = tail_;
        stage.key_ = invalid_order_key;
        (void)detail::apply_spec(*graph_, next, stage);
        tail_ = next;
        return *this;
    }

    template <typename F> task_builder& then(F&& fn) noexcept
    {
        return then(detail::make_body(std::forward<F>(fn)));
    }

    task_chain commit() noexcept
    {
        if (graph_ != nullptr && tail_.valid())
        {
            (void)detail::apply_spec(*graph_, tail_, spec_);
        }
        return task_chain{head_, tail_};
    }
};

// ---------------------------------------------------------------------------
// builder 与标签组合
// ---------------------------------------------------------------------------

inline task_builder operator|(task_builder builder, on_tag tag) noexcept
{
    builder.spec_.role_ = tag.role_;
    return builder;
}

inline task_builder operator|(task_builder builder, ordered_tag) noexcept
{
    builder.spec_.mode_ = order_mode::sequential;
    return builder;
}

inline task_builder operator|(task_builder builder, unordered_tag) noexcept
{
    builder.spec_.mode_ = order_mode::parallel;
    return builder;
}

inline task_builder operator|(task_builder builder, stealable_tag) noexcept
{
    builder.spec_.flags_ |= flag_allow_steal;
    return builder;
}

inline task_builder operator|(task_builder builder, detached_tag) noexcept
{
    builder.spec_.flags_ |= flag_detached;
    return builder;
}

inline task_builder operator|(task_builder builder, once_tag) noexcept
{
    builder.spec_.activation_ = activation_policy::once;
    return builder;
}

inline task_builder operator|(task_builder builder, on_signal_tag) noexcept
{
    builder.spec_.activation_ = activation_policy::on_signal;
    return builder;
}

inline task_builder operator|(task_builder builder, pinned_tag tag) noexcept
{
    builder.spec_.affinity_ = affinity_kind::worker;
    builder.spec_.worker_ = tag.worker_;
    return builder;
}

inline task_builder operator|(task_builder builder, priority_tag tag) noexcept
{
    builder.spec_.priority_ = tag.value_;
    return builder;
}

inline task_builder operator|(task_builder builder, frames_tag tag) noexcept
{
    builder.spec_.activation_ = activation_policy::every_frames;
    builder.spec_.period_ = tag.period_;
    return builder;
}

inline task_builder operator|(task_builder builder, after_tag tag) noexcept
{
    builder.spec_.after_ = tag.task_;
    return builder;
}

inline task_builder operator|(task_builder builder, deadline_tag tag) noexcept
{
    builder.spec_.deadline_ = tag.timeout_ns_;
    return builder;
}

inline task_builder operator|(task_builder builder, context_tag tag) noexcept
{
    builder.spec_.context_ = tag.context_;
    return builder;
}

inline task_builder operator|(task_builder builder, reads_tag tag) noexcept
{
    if (builder.spec_.read_count_ < 4)
    {
        builder.spec_.reads_[builder.spec_.read_count_++] = tag.resource_;
    }
    return builder;
}

inline task_builder operator|(task_builder builder, writes_tag tag) noexcept
{
    if (builder.spec_.write_count_ < 4)
    {
        builder.spec_.writes_[builder.spec_.write_count_++] = tag.resource_;
    }
    return builder;
}

template <typename F> task_id operator|(task_builder builder, run_tag<F> tag) noexcept
{
    return builder.run(detail::make_body(std::move(tag.fn_)));
}

inline task_id operator|(task_builder builder, body_run_tag tag) noexcept
{
    return builder.run(tag.body_);
}

// ---------------------------------------------------------------------------
// 声明表
// ---------------------------------------------------------------------------

struct task_decl
{
    const char* name_{nullptr};
    thread::role role_{thread::role::general};
    order_mode mode_{order_mode::parallel};
    affinity_kind affinity_{affinity_kind::none};
    std::uint32_t worker_{task_worker_none};
    order_key key_{};
    void (*body_)(task_context&) noexcept{nullptr};
    bool allow_steal_{false};
};

inline bool declare_all_impl(task_graph& graph, const task_decl* decls, std::size_t count,
                              operating_message& error) noexcept
{
    if (decls == nullptr || count == 0)
    {
        error.write_failure(om_err_invalid_argument.code, msg_level::warn, msg::here());
        return false;
    }
    for (std::size_t i = 0; i < count; ++i)
    {
        const task_decl& decl = decls[i];
        if (decl.name_ == nullptr || decl.body_ == nullptr)
        {
            error.write_failure(om_err_invalid_argument.code, msg_level::warn, msg::here());
            return false;
        }
        if (role_index(decl.role_) >= graph.role_count())
        {
            error.write_failure(om_err_invalid_argument.code, msg_level::warn, msg::here());
            return false;
        }
        if (decl.mode_ == order_mode::sequential &&
            graph.role_mode(decl.role_) != order_mode::sequential)
        {
            error.write_failure(om_err_invalid_argument.code, msg_level::warn, msg::here());
            return false;
        }
        if (decl.affinity_ == affinity_kind::worker)
        {
            if (decl.worker_ >= graph.worker_count())
            {
                error.write_failure(om_err_invalid_argument.code, msg_level::warn, msg::here());
                return false;
            }
            if (decl.allow_steal_ && decl.mode_ != order_mode::parallel)
            {
                error.write_failure(om_err_invalid_argument.code, msg_level::warn, msg::here());
                return false;
            }
        }
        else if (decl.allow_steal_)
        {
            error.write_failure(om_err_invalid_argument.code, msg_level::warn, msg::here());
            return false;
        }
    }

    dense<task_id> created{};
    created.increase_capacity(count);
    for (std::size_t i = 0; i < count; ++i)
    {
        const task_decl& decl = decls[i];
        task_builder builder{&graph, graph.declare_impl(decl.name_, {}, error)};
        if (!builder.tail_.valid())
        {
            for (std::size_t k = 0; k < created.size(); ++k)
            {
                (void)graph.disable_impl(created[k], error);
            }
            error.write_failure(om_err_capacity_exceeded.code, msg_level::warn, msg::here());
            return false;
        }
        builder.spec_.role_ = decl.role_;
        builder.spec_.mode_ = decl.mode_;
        builder.spec_.affinity_ = decl.affinity_;
        builder.spec_.worker_ = decl.worker_;
        builder.spec_.key_ = decl.key_.value_ == 0 ? invalid_order_key : decl.key_.value_;
        builder.spec_.flags_ = decl.allow_steal_ ? flag_allow_steal : 0u;
        if (!builder.run(detail::make_body(decl.body_)).valid())
        {
            for (std::size_t k = 0; k < created.size(); ++k)
            {
                (void)graph.disable_impl(created[k], error);
            }
            error.write_failure(om_err_invalid_argument.code, msg_level::warn, msg::here());
            return false;
        }
        created.emplace_back(builder.tail_);
    }
    return true;
}
inline operating_message declare_all(task_graph& graph, const task_decl* decls,
                                       std::size_t count) noexcept
{
    operating_message error;
    (void)declare_all_impl(graph, decls, count, error);
    return error;
}

} // namespace thread
