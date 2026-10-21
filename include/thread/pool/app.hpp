#pragma once

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <thread>
#include <type_traits>
#include <utility>

#include "thread/pool/manager.hpp"
#include "thread/pool/profile.hpp"
#include "thread/pool/spawn.hpp"
#include "thread/task/task_builder.hpp"

namespace thread
{

// 应用骨架配置：默认线程预算为硬件线程数减一，主线程驱动帧
struct app_config
{
    manager_config manager{profile::server(profile::thread_budget())};
    std::uint32_t frame_rate{60};   // 0 表示不限速
};

class app;

// 任务句柄：糖层返回的轻量身份，可直接下沉到内核接口
struct task_handle
{
    app* owner_{nullptr};
    task_id id_{};

    bool valid() const noexcept;
    task_id id() const noexcept;
    task_state state() const noexcept;

    task_handle& after(task_handle predecessor) noexcept;
    task_handle& priority(thread::priority value) noexcept;
    task_handle& timeout(std::uint64_t timeout_ns) noexcept;
    task_handle& on(thread::role target) noexcept;

    bool activate() noexcept;
    bool disable() noexcept;
    bool enable() noexcept;
};

namespace app_detail
{

// 成员函数适配：接受 void() 与 void(task_context&) 两种签名
template <typename T, typename M> struct member_body
{
    T* object_{nullptr};
    M method_{};

    void run(task_context& ctx) noexcept
    {
        if constexpr (std::is_invocable_v<M, T*, task_context&>)
        {
            (object_->*method_)(ctx);
        }
        else
        {
            (void)ctx;
            (object_->*method_)();
        }
    }
};

} // namespace app_detail

// 开箱即用入口：默认布局、自动启停、内置帧循环
class app
{
  public:
    explicit app(app_config config = app_config{}) noexcept;

    ~app() noexcept;

    app(const app&) = delete;
    app& operator=(const app&) = delete;

    bool valid() const noexcept
    {
        return valid_;
    }

    bool running() const noexcept
    {
        return started_ && !quit_.load(std::memory_order_acquire);
    }

    std::size_t worker_count() const noexcept
    {
        return kernel_.worker_count();
    }

    thread::role main_role() const noexcept
    {
        return main_role_;
    }

    std::uint64_t frame_index() const noexcept
    {
        return valid_ ? kernel_.frame_of(main_role_) : 0;
    }

    // 注册：每帧激活 / 每帧并行 / 一次激活
    template <typename F> task_handle every_frame(const char* name, F&& fn) noexcept
    {
        return declare_task(name, std::forward<F>(fn), activation_policy::every_frames, 1u);
    }

    template <typename F> task_handle parallel(const char* name, F&& fn) noexcept
    {
        return declare_task(name, std::forward<F>(fn), activation_policy::every_frames, 1u);
    }

    template <typename F> task_handle once(const char* name, F&& fn) noexcept
    {
        return declare_task(name, std::forward<F>(fn), activation_policy::once, 1u);
    }

    template <typename T, typename M>
    task_handle every_frame(const char* name, M method, T* object) noexcept
    {
        return declare_task(name, app_detail::member_body<T, M>{object, method},
                            activation_policy::every_frames, 1u);
    }

    template <typename T, typename M> task_handle once(const char* name, M method, T* object) noexcept
    {
        return declare_task(name, app_detail::member_body<T, M>{object, method},
                            activation_policy::once, 1u);
    }

    // 一次性工作项：不建立任务身份，执行后归还槽位
    template <typename F> bool post(F&& fn) noexcept
    {
        if (!valid_)
        {
            return false;
        }
        std::uint32_t slot = slot_index_none;
        job* target = kernel_.take_job(slot);
        if (target == nullptr)
        {
            return false;
        }
        if (!fill_job(target, std::forward<F>(fn)))
        {
            kernel_.release_job_local(target);
            return false;
        }
        if (kernel_.submit(target) == slot_index_none)
        {
            kernel_.release_job_local(target);
            return false;
        }
        return true;
    }

    bool spawn(task<void>&& work) noexcept
    {
        return valid_ && thread::spawn(kernel_, std::move(work));
    }

    // 驱动
    int run() noexcept;
    int run_for(std::chrono::nanoseconds span) noexcept;
    bool tick() noexcept;

    void quit() noexcept
    {
        quit_.store(true, std::memory_order_release);
    }

    void stop() noexcept;

    void set_frame_rate(std::uint32_t rate) noexcept
    {
        frame_rate_ = rate;
    }

    // 查询与下沉
    task_state state_of(task_handle handle) const noexcept
    {
        return state_of(handle.id_);
    }

    task_state state_of(task_id id) const noexcept
    {
        return kernel_.state_of(id);
    }

    manager& kernel() noexcept
    {
        return kernel_;
    }

    const manager& kernel() const noexcept
    {
        return kernel_;
    }

    const task_metrics& stats() const noexcept
    {
        return kernel_.task_stats();
    }

    error_state last_error() const noexcept
    {
        return kernel_.last_error();
    }

    shutdown_phase phase() const noexcept
    {
        return kernel_.phase();
    }

  private:
    template <typename F>
    task_handle declare_task(const char* name, F&& fn, activation_policy policy,
                             std::uint32_t period) noexcept
    {
        if (!valid_)
        {
            return task_handle{};
        }
        const order_mode mode = kernel_.tasks().role_mode(main_role_);
        const route_word route = route_word::make(main_role_, mode, affinity_kind::none,
                                                  priority::normal, task_worker_none);
        task_graph& graph = kernel_.tasks();
        const task_id id = graph.declare_full(name, thread::make_task_body(std::forward<F>(fn)),
                                              route, parent_word{}, invalid_order_key, 0u);
        if (!id.valid())
        {
            return task_handle{};
        }
        (void)graph.set_activation(id, policy, period, activation_overlap::drop);
        if (policy == activation_policy::once)
        {
            (void)graph.activate(id);
        }
        return task_handle{this, id};
    }

    void pace() noexcept;

    app_config config_{};
    manager kernel_{};
    role_config layout_[1]{};
    thread::role main_role_{thread::role::general};
    std::chrono::steady_clock::time_point next_frame_{};
    std::atomic<bool> quit_{false};
    std::uint32_t frame_rate_{60};
    bool valid_{false};
    bool started_{false};
};

inline app::app(app_config config) noexcept : config_(config)
{
    if (config_.manager.roles == nullptr || config_.manager.role_count == 0)
    {
        // 默认布局：单 general 并行域覆盖全部工作线程，主线程只驱动帧
        layout_[0].name_ = "general";
        layout_[0].first_worker_ = 0;
        layout_[0].worker_count_ = static_cast<std::uint32_t>(config_.manager.worker_count);
        layout_[0].mode_ = order_mode::parallel;
        layout_[0].policy_ = order_policy::keyed;
        config_.manager.roles = layout_;
        config_.manager.role_count = 1;
    }
    frame_rate_ = config_.frame_rate;
    main_role_ = thread::role::general;
    valid_ = kernel_.configure(config_.manager);
    if (!valid_)
    {
        return;
    }
    if (kernel_.tasks().role_at(main_role_) == nullptr)
    {
        // 自定义角色表必须包含 general，否则糖层没有默认落点
        kernel_.stop(stop_mode::drain);
        valid_ = false;
        return;
    }
    kernel_.start();
    started_ = true;
}

inline app::~app() noexcept
{
    stop();
}

inline int app::run() noexcept
{
    if (!valid_)
    {
        return 1;
    }
    while (!quit_.load(std::memory_order_acquire))
    {
        if (!tick())
        {
            break;
        }
    }
    stop();
    return 0;
}

inline int app::run_for(std::chrono::nanoseconds span) noexcept
{
    if (!valid_)
    {
        return 1;
    }
    const auto deadline = std::chrono::steady_clock::now() + span;
    while (!quit_.load(std::memory_order_acquire) &&
           std::chrono::steady_clock::now() < deadline)
    {
        if (!tick())
        {
            break;
        }
    }
    stop();
    return 0;
}

inline bool app::tick() noexcept
{
    if (!valid_)
    {
        return false;
    }
    kernel_.begin_frame(main_role_);
    if (kernel_.worker_count() == 0)
    {
        // 嵌入式：由调用线程执行就绪任务
        kernel_.pump_role(main_role_, 1024);
    }
    kernel_.end_frame(main_role_);
    pace();
    return true;
}

inline void app::stop() noexcept
{
    if (!started_)
    {
        return;
    }
    started_ = false;
    kernel_.stop(stop_mode::drain);
}

inline void app::pace() noexcept
{
    if (frame_rate_ == 0)
    {
        return;
    }
    const auto period =
        std::chrono::nanoseconds(1'000'000'000ll / static_cast<long long>(frame_rate_));
    const auto now = std::chrono::steady_clock::now();
    if (next_frame_.time_since_epoch().count() == 0 || next_frame_ < now)
    {
        next_frame_ = now;
    }
    next_frame_ += period;
    // Windows 默认 15.6 ms 定时器粒度：末段让步自旋，避免节拍被吞
    for (;;)
    {
        const auto current = std::chrono::steady_clock::now();
        if (current >= next_frame_)
        {
            break;
        }
        const auto remain = next_frame_ - current;
        if (remain > std::chrono::milliseconds(16))
        {
            std::this_thread::sleep_until(next_frame_ - std::chrono::milliseconds(4));
        }
        else
        {
            std::this_thread::yield();
        }
    }
}

inline bool task_handle::valid() const noexcept
{
    return owner_ != nullptr && id_.valid();
}

inline task_id task_handle::id() const noexcept
{
    return id_;
}

inline task_state task_handle::state() const noexcept
{
    return owner_ != nullptr ? owner_->state_of(id_) : task_state::dormant;
}

inline task_handle& task_handle::after(task_handle predecessor) noexcept
{
    if (owner_ != nullptr && predecessor.valid())
    {
        (void)owner_->kernel().tasks().add_dependency(predecessor.id_, id_);
    }
    return *this;
}

inline task_handle& task_handle::priority(thread::priority value) noexcept
{
    if (owner_ != nullptr)
    {
        (void)owner_->kernel().tasks().set_priority(id_, value);
    }
    return *this;
}

inline task_handle& task_handle::timeout(std::uint64_t timeout_ns) noexcept
{
    if (owner_ != nullptr)
    {
        (void)owner_->kernel().tasks().set_deadline(id_, timeout_ns);
    }
    return *this;
}

inline task_handle& task_handle::on(thread::role target) noexcept
{
    if (owner_ != nullptr)
    {
        (void)owner_->kernel().tasks().migrate(id_, target);
    }
    return *this;
}

inline bool task_handle::activate() noexcept
{
    return owner_ != nullptr && owner_->kernel().tasks().activate(id_);
}

inline bool task_handle::disable() noexcept
{
    return owner_ != nullptr && owner_->kernel().tasks().disable(id_);
}

inline bool task_handle::enable() noexcept
{
    return owner_ != nullptr && owner_->kernel().tasks().enable(id_);
}

} // namespace thread
