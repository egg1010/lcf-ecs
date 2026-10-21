#pragma once

#include <cstdint>

#include "part/dense.hpp"
#include "thread/task/task_context.hpp"
#include "thread/base/task_identity.hpp"

namespace thread
{

// 任务上下文存储：上下文表 + 每任务绑定，负责继承与引用计数
struct context_store
{
    dense<context_id> task_{};
    context_table table_{};

    bool create(std::uint32_t context_capacity, std::uint32_t task_capacity) noexcept
    {
        task_.resize_fill(task_capacity, invalid_context);
        if (context_capacity != 0 && !table_.create(context_capacity))
        {
            return false;
        }
        return true;
    }

    void destroy() noexcept
    {
        release_all();
        table_.destroy();
        task_.clear();
    }

    context_id create_context(std::uint64_t request_id, std::uint64_t trace_id,
                              std::uint64_t tenant_id) noexcept
    {
        return table_.create(request_id, trace_id, tenant_id);
    }

    bool set(task_id id, context_id ctx) noexcept
    {
        if (!id.valid() || id.index_ >= task_.size())
        {
            return false;
        }
        if (ctx.valid() && !table_.retain(ctx))
        {
            return false;
        }
        const context_id previous = task_[id.index_];
        task_[id.index_] = ctx;
        if (previous.valid() && !(previous == ctx))
        {
            table_.release(previous);
        }
        return true;
    }

    context_id of(task_id id) const noexcept
    {
        return id.valid() && id.index_ < task_.size() ? task_[id.index_] : invalid_context;
    }

    context_id at(std::uint32_t index) const noexcept
    {
        return index < task_.size() ? task_[index] : invalid_context;
    }

    // 子任务默认继承父任务上下文
    context_id inherit(std::uint32_t parent_index, std::uint32_t child_index) noexcept
    {
        if (parent_index == invalid_task_index || parent_index >= task_.size() ||
            child_index >= task_.size())
        {
            return invalid_context;
        }
        const context_id value = task_[parent_index];
        task_[child_index] = value;
        if (value.valid())
        {
            (void)table_.retain(value);
        }
        return value;
    }

    void release(context_id ctx) noexcept
    {
        table_.release(ctx);
    }

    void release_all() noexcept
    {
        for (std::uint32_t i = 0; i < task_.size(); ++i)
        {
            const context_id id = task_[i];
            if (id.valid())
            {
                table_.release(id);
            }
        }
    }

    context_table& table() noexcept
    {
        return table_;
    }

    const context_table& table() const noexcept
    {
        return table_;
    }
};

} // namespace thread
