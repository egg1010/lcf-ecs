#pragma once

#include <atomic>
#include <cstdint>
#include <new>

#include "thread/task/task_node.hpp"

namespace thread
{

// 追加式任务表：configure 期一次性分配，运行期不扩容、不搬移、不删除
struct task_table
{
    task_node* nodes_{nullptr};
    std::atomic<std::uint32_t> count_{0};
    std::atomic<std::uint32_t> high_water_{0};
    std::uint32_t capacity_{0};

    task_table() noexcept = default;

    task_table(const task_table&) = delete;
    task_table& operator=(const task_table&) = delete;

    ~task_table()
    {
        destroy();
    }

    bool create(std::uint32_t capacity) noexcept
    {
        if (nodes_ != nullptr || capacity == 0)
        {
            return false;
        }
        void* memory = ::operator new(sizeof(task_node) * capacity,
                                      std::align_val_t(task_node_align), std::nothrow);
        if (memory == nullptr)
        {
            return false;
        }
        nodes_ = static_cast<task_node*>(memory);
        for (std::uint32_t i = 0; i < capacity; ++i)
        {
            new (&nodes_[i]) task_node{};
        }
        capacity_ = capacity;
        count_.store(0, std::memory_order_relaxed);
        high_water_.store(0, std::memory_order_relaxed);
        return true;
    }

    void destroy() noexcept
    {
        if (nodes_ == nullptr)
        {
            return;
        }
        const std::uint32_t used = count_.load(std::memory_order_acquire);
        for (std::uint32_t i = 0; i < used && i < capacity_; ++i)
        {
            nodes_[i].~task_node();
        }
        ::operator delete(static_cast<void*>(nodes_), std::align_val_t(task_node_align));
        nodes_ = nullptr;
        capacity_ = 0;
        count_.store(0, std::memory_order_relaxed);
        high_water_.store(0, std::memory_order_relaxed);
    }

    task_node* at(task_id id) noexcept
    {
        if (!id.valid() || id.index_ >= capacity_)
        {
            return nullptr;
        }
        return &nodes_[id.index_];
    }

    const task_node* at(task_id id) const noexcept
    {
        if (!id.valid() || id.index_ >= capacity_)
        {
            return nullptr;
        }
        return &nodes_[id.index_];
    }

    task_node* at_index(std::uint32_t index) noexcept
    {
        return index < capacity_ ? &nodes_[index] : nullptr;
    }

    std::uint32_t capacity() const noexcept
    {
        return capacity_;
    }

    std::uint32_t size() const noexcept
    {
        return count_.load(std::memory_order_acquire);
    }

    // 预留编号并初始化节点；返回 nullptr 表示容量耗尽
    task_node* append(task_id& out, const char* name, task_body body, route_word route,
                      parent_word parent, std::uint64_t key, std::uint32_t flags) noexcept
    {
        const std::uint32_t index = count_.fetch_add(1, std::memory_order_acq_rel);
        if (index >= capacity_)
        {
            count_.store(capacity_, std::memory_order_release);
            return nullptr;
        }
        task_node* node = &nodes_[index];

        node->flags_.store(flags, std::memory_order_relaxed);
        node->dispatch_epoch_.store(0, std::memory_order_relaxed);
        node->heap_index_.store(0, std::memory_order_relaxed);
        node->route_.store(route.value_, std::memory_order_relaxed);
        node->parent_.store(parent.value_, std::memory_order_relaxed);
        node->order_key_.store(key, std::memory_order_relaxed);
        node->pending_deps_.store(0, std::memory_order_relaxed);
        node->pending_runs_.store(0, std::memory_order_relaxed);
        node->pending_children_.store(0, std::memory_order_relaxed);
        node->activation_count_.store(0, std::memory_order_relaxed);
        node->total_run_ns_.store(0, std::memory_order_relaxed);
        node->heartbeat_ns_.store(0, std::memory_order_relaxed);
        node->coro_handle_.store(nullptr, std::memory_order_relaxed);
        node->body_kind_ = body.kind_;
        node->invoke_ = body.invoke_;
        node->body_object_ = body.object_;
        node->reserved0_ = 0;
        node->activation_policy_.store(0, std::memory_order_relaxed);
        node->frame_period_.store(1, std::memory_order_relaxed);
        node->set_name(name);

        out = task_id{index};
        std::uint32_t high = high_water_.load(std::memory_order_relaxed);
        while (index + 1u > high &&
               !high_water_.compare_exchange_weak(high, index + 1u, std::memory_order_relaxed,
                                                  std::memory_order_relaxed))
        {
        }
        // 发布：状态从 constructing 迁移到 dormant
        node->state_.store(static_cast<std::uint32_t>(task_state::dormant),
                           std::memory_order_release);
        return node;
    }
};

} // namespace thread
