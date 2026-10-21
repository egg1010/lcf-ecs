#pragma once

#include <atomic>
#include <cstdint>
#include <new>

#include "thread/task_identity.hpp"
#include "thread/tls.hpp"

namespace thread
{

inline constexpr std::uint32_t invalid_context_index = 0xFFFFFFFFu;
inline constexpr std::uint32_t context_default_capacity = 1024;

// 稳定上下文编号：index + generation，防 ABA；子任务与 IO 只复制 8 字节
struct context_id
{
    std::uint32_t index_{invalid_context_index};
    std::uint32_t generation_{0};

    bool valid() const noexcept
    {
        return index_ != invalid_context_index;
    }

    bool operator==(const context_id& other) const noexcept = default;
};

inline constexpr context_id invalid_context{};

struct context_slot
{
    std::uint64_t request_id_{0};
    std::uint64_t trace_id_{0};
    std::uint64_t tenant_id_{0};
    std::uint32_t generation_{1};
    std::uint32_t refs_{0};
    std::uint32_t state_{0};
    std::uint32_t reserved_{0};
};

// 固定容量上下文表：创建期分配，运行期不搬移；字段用 atomic_ref 访问
struct context_table
{
    context_slot* slots_{nullptr};
    std::uint32_t capacity_{0};
    std::uint32_t cursor_{0};

    context_table() noexcept = default;

    context_table(const context_table&) = delete;
    context_table& operator=(const context_table&) = delete;

    ~context_table()
    {
        destroy();
    }

    bool create(std::uint32_t capacity) noexcept
    {
        if (slots_ != nullptr || capacity == 0)
        {
            return false;
        }
        void* memory = ::operator new(sizeof(context_slot) * capacity, std::align_val_t(64),
                                      std::nothrow);
        if (memory == nullptr)
        {
            return false;
        }
        slots_ = static_cast<context_slot*>(memory);
        for (std::uint32_t i = 0; i < capacity; ++i)
        {
            new (&slots_[i]) context_slot{};
        }
        capacity_ = capacity;
        cursor_ = 0;
        return true;
    }

    void destroy() noexcept
    {
        if (slots_ == nullptr)
        {
            return;
        }
        ::operator delete(static_cast<void*>(slots_), std::align_val_t(64));
        slots_ = nullptr;
        capacity_ = 0;
        cursor_ = 0;
    }

    bool created() const noexcept
    {
        return slots_ != nullptr;
    }

    context_id create(std::uint64_t request_id, std::uint64_t trace_id,
                      std::uint64_t tenant_id) noexcept
    {
        if (slots_ == nullptr)
        {
            return invalid_context;
        }
        for (std::uint32_t probe = 0; probe < capacity_; ++probe)
        {
            const std::uint32_t index = (cursor_ + probe) % capacity_;
            context_slot& slot = slots_[index];
            std::atomic_ref<std::uint32_t> state{slot.state_};
            std::uint32_t expected = 0;
            if (!state.compare_exchange_strong(expected, 1u, std::memory_order_acq_rel,
                                               std::memory_order_acquire))
            {
                continue;
            }
            std::atomic_ref<std::uint32_t> generation{slot.generation_};
            const std::uint32_t next = generation.fetch_add(1, std::memory_order_acq_rel) + 1u;
            slot.request_id_ = request_id;
            slot.trace_id_ = trace_id;
            slot.tenant_id_ = tenant_id;
            std::atomic_ref<std::uint32_t>{slot.refs_}.store(1u, std::memory_order_release);
            cursor_ = (index + 1u) % capacity_;
            return context_id{index, next};
        }
        return invalid_context;
    }

    context_slot* at(context_id id) noexcept
    {
        if (!id.valid() || id.index_ >= capacity_)
        {
            return nullptr;
        }
        context_slot& slot = slots_[id.index_];
        if (std::atomic_ref<std::uint32_t>{slot.state_}.load(std::memory_order_acquire) == 0)
        {
            return nullptr;
        }
        if (std::atomic_ref<std::uint32_t>{slot.generation_}.load(std::memory_order_acquire) !=
            id.generation_)
        {
            return nullptr;
        }
        return &slot;
    }

    bool retain(context_id id) noexcept
    {
        context_slot* slot = at(id);
        if (slot == nullptr)
        {
            return false;
        }
        std::atomic_ref<std::uint32_t> refs{slot->refs_};
        std::uint32_t current = refs.load(std::memory_order_acquire);
        for (;;)
        {
            if (current == 0)
            {
                return false;
            }
            if (refs.compare_exchange_weak(current, current + 1u, std::memory_order_acq_rel,
                                           std::memory_order_acquire))
            {
                return true;
            }
        }
    }

    void release(context_id id) noexcept
    {
        context_slot* slot = at(id);
        if (slot == nullptr)
        {
            return;
        }
        std::atomic_ref<std::uint32_t> refs{slot->refs_};
        if (refs.fetch_sub(1u, std::memory_order_acq_rel) == 1u)
        {
            std::atomic_ref<std::uint32_t>{slot->state_}.store(0u, std::memory_order_release);
        }
    }

    std::uint64_t request_id(context_id id) noexcept
    {
        const context_slot* slot = at(id);
        return slot != nullptr ? slot->request_id_ : 0;
    }

    std::uint64_t trace_id(context_id id) noexcept
    {
        const context_slot* slot = at(id);
        return slot != nullptr ? slot->trace_id_ : 0;
    }

    std::uint64_t tenant_id(context_id id) noexcept
    {
        const context_slot* slot = at(id);
        return slot != nullptr ? slot->tenant_id_ : 0;
    }
};

struct task_node;
struct task_graph;

// 单次激活的轻量视图：不拥有资源，不得保存到下一次激活
struct task_context
{
    task_id id_{};
    thread::role role_{thread::role::general};
    std::uint32_t worker_{task_worker_none};
    std::uint64_t frame_{0};
    std::uint32_t activation_{0};
    float delta_seconds_{0.0f};
    void* user_{nullptr};
    task_id scope_{};
    std::uint64_t deadline_ns_{0};
    std::uint64_t budget_ns_{0};
    std::uint64_t start_ns_{0};
    task_node* node_{nullptr};
    task_graph* graph_{nullptr};
    context_id context_{};

    bool cancelled() const noexcept;

    void heartbeat() noexcept;

    // 任务体报告不可恢复错误：本次激活以 faulted 结束，不影响其他任务
    void fail() noexcept;

    // 帧/时间预算：任务体可主动查询，用于跳帧或降级
    bool budget_exceeded() const noexcept;
};

// 当前线程正在执行的任务上下文；不在任务体内时为空
inline task_context*& current_task_slot() noexcept
{
    return tls().task_;
}

// 任务体内查询：返回本次激活的上下文指针
inline task_context* current_task_context() noexcept
{
    return current_task_slot();
}
} // namespace thread
