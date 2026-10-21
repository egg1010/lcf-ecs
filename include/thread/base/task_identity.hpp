#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

#include "thread/job.hpp"

namespace thread
{

// ---------------------------------------------------------------------------
// 角色：id 即角色表下标，编码上限 65535；非枚举值通过 make_role 构造
// ---------------------------------------------------------------------------

enum class role : std::uint16_t
{
    general = 0,
    renderer = 1,
    logic = 2,
};

inline constexpr std::uint16_t role_id_max = 0xFFFFu;

constexpr std::size_t role_index(role value) noexcept
{
    return static_cast<std::size_t>(value);
}

constexpr role make_role(std::uint16_t id) noexcept
{
    return static_cast<role>(id);
}

enum class order_mode : std::uint8_t
{
    parallel = 0,
    sequential = 1,
};

enum class order_policy : std::uint8_t
{
    fifo = 0,
    keyed = 1,
};

enum class affinity_kind : std::uint8_t
{
    none = 0,
    role = 1,
    worker = 2,
};

enum class activation_policy : std::uint8_t
{
    manual = 0,
    once = 1,
    every_frames = 2,
    on_signal = 3,
};

// 重叠激活策略：就绪/运行期间再次 activate 的处理方式
enum class activation_overlap : std::uint8_t
{
    drop = 0,
    queue = 1,
    error = 2,
};

enum class task_state : std::uint8_t
{
    dormant = 0,
    ready = 1,
    running = 2,
    waiting = 3,
    completed = 4,
    disabled = 5,
    faulted = 6,
    cancelled = 7,
};

inline constexpr std::uint8_t task_state_constructing = 0xFFu;

inline const char* task_state_name(task_state state) noexcept
{
    switch (state)
    {
    case task_state::dormant:
        return "dormant";
    case task_state::ready:
        return "ready";
    case task_state::running:
        return "running";
    case task_state::waiting:
        return "waiting";
    case task_state::completed:
        return "completed";
    case task_state::disabled:
        return "disabled";
    case task_state::faulted:
        return "faulted";
    case task_state::cancelled:
        return "cancelled";
    }
    return "constructing";
}

enum class route_kind : std::uint8_t
{
    none = 0,
    sequential = 1,
    bound = 2,
    stealable = 3,
};

struct route_target
{
    route_kind kind_{route_kind::none};
    thread::role role_{thread::role::general};
    std::uint32_t worker_{0};
};

// ---------------------------------------------------------------------------
// 任务编号：任务表下标，永不复用
// ---------------------------------------------------------------------------

inline constexpr std::uint32_t invalid_task_index = 0xFFFFFFFFu;
inline constexpr std::uint32_t task_worker_none = 0x00FFFFFFu;
inline constexpr std::uint64_t invalid_order_key = 0xFFFFFFFFFFFFFFFFull;
inline constexpr std::uint8_t task_max_depth = 64;

struct task_id
{
    std::uint32_t index_{invalid_task_index};

    constexpr bool valid() const noexcept
    {
        return index_ != invalid_task_index;
    }

    constexpr bool operator==(const task_id& other) const noexcept = default;
};

inline constexpr task_id invalid_task{};

struct order_key
{
    std::uint64_t value_{0};

    constexpr bool operator==(const order_key& other) const noexcept = default;
    constexpr bool operator<(const order_key& other) const noexcept
    {
        return value_ < other.value_;
    }
};

// ---------------------------------------------------------------------------
// 执行票据：一次激活的 64 位凭证
// ---------------------------------------------------------------------------

struct ticket
{
    static constexpr std::uint64_t kind_bit = 1ull << 63;
    static constexpr std::uint64_t epoch_shift = 32;
    static constexpr std::uint64_t index_mask = 0xFFFFFFFFull;
    static constexpr std::uint64_t epoch_mask = 0x7FFFFFFFull;
    static constexpr std::uint64_t none_value = ~0ull;

    std::uint64_t value_{none_value};

    static constexpr ticket make_task(std::uint32_t index, std::uint32_t epoch) noexcept
    {
        return ticket{(static_cast<std::uint64_t>(epoch & epoch_mask) << epoch_shift) |
                      static_cast<std::uint64_t>(index)};
    }

    static constexpr ticket make_work(std::uint32_t index) noexcept
    {
        return ticket{kind_bit | static_cast<std::uint64_t>(index)};
    }

    static constexpr ticket none() noexcept
    {
        return ticket{none_value};
    }

    constexpr bool empty() const noexcept
    {
        return value_ == none_value;
    }

    constexpr bool is_task() const noexcept
    {
        return (value_ & kind_bit) == 0;
    }

    constexpr std::uint32_t index() const noexcept
    {
        return static_cast<std::uint32_t>(value_ & index_mask);
    }

    constexpr std::uint32_t epoch() const noexcept
    {
        return static_cast<std::uint32_t>((value_ >> epoch_shift) & epoch_mask);
    }

    constexpr bool operator==(const ticket& other) const noexcept = default;
};

// ---------------------------------------------------------------------------
// 身份字打包：路由与父子关系各占一个 64 位字，一次 CAS 生效
// ---------------------------------------------------------------------------

struct route_word
{
    static constexpr std::uint64_t role_shift = 0;
    static constexpr std::uint64_t mode_shift = 16;
    static constexpr std::uint64_t affinity_shift = 24;
    static constexpr std::uint64_t priority_shift = 32;
    static constexpr std::uint64_t worker_shift = 40;

    static constexpr std::uint64_t role_mask = 0xFFFFull << role_shift;
    static constexpr std::uint64_t mode_mask = 0xFFull << mode_shift;
    static constexpr std::uint64_t affinity_mask = 0xFFull << affinity_shift;
    static constexpr std::uint64_t priority_mask = 0xFFull << priority_shift;
    static constexpr std::uint64_t worker_mask = 0xFFFFFFull << worker_shift;

    std::uint64_t value_{0};

    static constexpr route_word make(thread::role r, order_mode m, affinity_kind a,
                                     thread::priority p, std::uint32_t worker) noexcept
    {
        route_word word{};
        word.value_ = ((static_cast<std::uint64_t>(r) << role_shift) & role_mask) |
                      ((static_cast<std::uint64_t>(m) << mode_shift) & mode_mask) |
                      ((static_cast<std::uint64_t>(a) << affinity_shift) & affinity_mask) |
                      ((static_cast<std::uint64_t>(p) << priority_shift) & priority_mask) |
                      ((static_cast<std::uint64_t>(worker) << worker_shift) & worker_mask);
        return word;
    }

    constexpr thread::role role_value() const noexcept
    {
        return static_cast<thread::role>((value_ & role_mask) >> role_shift);
    }

    constexpr order_mode mode() const noexcept
    {
        return static_cast<order_mode>((value_ & mode_mask) >> mode_shift);
    }

    constexpr affinity_kind affinity() const noexcept
    {
        return static_cast<affinity_kind>((value_ & affinity_mask) >> affinity_shift);
    }

    constexpr thread::priority priority_value() const noexcept
    {
        return static_cast<thread::priority>((value_ & priority_mask) >> priority_shift);
    }

    constexpr std::uint32_t worker() const noexcept
    {
        return static_cast<std::uint32_t>((value_ & worker_mask) >> worker_shift);
    }

    void set_role(thread::role value) noexcept
    {
        value_ = (value_ & ~role_mask) | ((static_cast<std::uint64_t>(value) << role_shift) & role_mask);
    }

    void set_mode(order_mode value) noexcept
    {
        value_ = (value_ & ~mode_mask) | ((static_cast<std::uint64_t>(value) << mode_shift) & mode_mask);
    }

    void set_affinity(affinity_kind value) noexcept
    {
        value_ = (value_ & ~affinity_mask) |
                 ((static_cast<std::uint64_t>(value) << affinity_shift) & affinity_mask);
    }

    void set_priority(thread::priority value) noexcept
    {
        value_ = (value_ & ~priority_mask) |
                 ((static_cast<std::uint64_t>(value) << priority_shift) & priority_mask);
    }

    void set_worker(std::uint32_t value) noexcept
    {
        value_ = (value_ & ~worker_mask) | ((static_cast<std::uint64_t>(value) << worker_shift) & worker_mask);
    }
};

struct parent_word
{
    static constexpr std::uint64_t parent_shift = 0;
    static constexpr std::uint64_t depth_shift = 32;
    static constexpr std::uint64_t flags_shift = 48;

    static constexpr std::uint64_t parent_mask = 0xFFFFFFFFull << parent_shift;
    static constexpr std::uint64_t depth_mask = 0xFFFFull << depth_shift;
    static constexpr std::uint64_t flags_mask = 0xFFFFull << flags_shift;

    static constexpr std::uint16_t flag_join_counted = 1u << 0;
    static constexpr std::uint16_t flag_detached = 1u << 1;
    static constexpr std::uint16_t flag_inherit_role = 1u << 2;

    // 默认值即“根任务”：父编号为 invalid_task_index
    std::uint64_t value_{parent_mask};

    static constexpr parent_word make(std::uint32_t parent, std::uint16_t depth,
                                      std::uint16_t flags) noexcept
    {
        parent_word word{};
        word.value_ = ((static_cast<std::uint64_t>(parent) << parent_shift) & parent_mask) |
                      ((static_cast<std::uint64_t>(depth) << depth_shift) & depth_mask) |
                      ((static_cast<std::uint64_t>(flags) << flags_shift) & flags_mask);
        return word;
    }

    constexpr std::uint32_t parent() const noexcept
    {
        return static_cast<std::uint32_t>((value_ & parent_mask) >> parent_shift);
    }

    constexpr std::uint16_t depth() const noexcept
    {
        return static_cast<std::uint16_t>((value_ & depth_mask) >> depth_shift);
    }

    constexpr std::uint16_t flags() const noexcept
    {
        return static_cast<std::uint16_t>((value_ & flags_mask) >> flags_shift);
    }

    void set_parent(std::uint32_t value) noexcept
    {
        value_ = (value_ & ~parent_mask) | ((static_cast<std::uint64_t>(value) << parent_shift) & parent_mask);
    }

    void set_depth(std::uint16_t value) noexcept
    {
        value_ = (value_ & ~depth_mask) | ((static_cast<std::uint64_t>(value) << depth_shift) & depth_mask);
    }

    void set_flags(std::uint16_t value) noexcept
    {
        value_ = (value_ & ~flags_mask) | ((static_cast<std::uint64_t>(value) << flags_shift) & flags_mask);
    }
};

// ---------------------------------------------------------------------------
// 任务标志
// ---------------------------------------------------------------------------

inline constexpr std::uint32_t flag_coroutine = 1u << 0;
inline constexpr std::uint32_t flag_auto_start_children = 1u << 1;
inline constexpr std::uint32_t flag_join_children = 1u << 2;
inline constexpr std::uint32_t flag_detached = 1u << 3;
inline constexpr std::uint32_t flag_inherit_role = 1u << 4;
inline constexpr std::uint32_t flag_identity_pending = 1u << 5;
inline constexpr std::uint32_t flag_allow_steal = 1u << 6;
inline constexpr std::uint32_t flag_cancelled = 1u << 7;
inline constexpr std::uint32_t flag_after_any = 1u << 8;
inline constexpr std::uint32_t flag_faulted = 1u << 9;
inline constexpr std::uint32_t flag_external_trigger = 1u << 10;
inline constexpr std::uint32_t flag_deps_fired = 1u << 11;
inline constexpr std::uint32_t flag_auto_started = 1u << 12;
inline constexpr std::uint32_t flag_body_done = 1u << 13;
inline constexpr std::uint32_t flag_delay_armed = 1u << 14;
inline constexpr std::uint32_t flag_frame_waiter = 1u << 16;
inline constexpr std::uint32_t flag_hung_reported = 1u << 17;
inline constexpr std::uint32_t flag_resources_held = 1u << 18;
inline constexpr std::uint32_t flag_body_failed = 1u << 19;
inline constexpr std::uint32_t flag_join_any = 1u << 20;

// 协程挂起原因：由等待原语在 await_suspend 中写入，域线程在挂起返回后消费
enum class wait_kind : std::uint8_t
{
    none = 0,
    switch_role = 1,
    frame = 2,
    frame_sync = 3,
    join = 4,
    external = 5,
    timed = 6,
};

inline constexpr std::uint8_t wait_state_none = 0;
inline constexpr std::uint8_t wait_state_preparing = 1;
inline constexpr std::uint8_t wait_state_parked = 2;

struct activate_options
{
    bool queue_{false};
    std::uint32_t delay_frames_{0};
};

struct role_config
{
    const char* name_{nullptr};
    std::uint32_t first_worker_{0};
    std::uint32_t worker_count_{0};
    order_mode mode_{order_mode::parallel};
    order_policy policy_{order_policy::keyed};
    bool external_{false};
    bool frame_auto_{false};
    bool deterministic_{false};
    std::uint32_t frame_rate_{60};
};

static_assert(std::atomic<std::uint64_t>::is_always_lock_free,
              "task graph requires lock-free 64-bit atomics");
static_assert(sizeof(ticket) == 8, "ticket must stay 8 bytes");
static_assert(sizeof(task_id) == 4, "task_id must stay 4 bytes");
static_assert(sizeof(route_word) == 8, "route_word must stay 8 bytes");
static_assert(sizeof(parent_word) == 8, "parent_word must stay 8 bytes");
} // namespace thread
