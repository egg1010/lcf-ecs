#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <new>

#include "thread/pool/manager.hpp"
#include "thread/task/task_queue.hpp"

namespace thread
{

enum class group_message_kind : std::uint8_t
{
    activate = 0,
    deactivate = 1,
    cancel = 2,
};

struct group_message
{
    group_message_kind kind_{group_message_kind::activate};
    task_id task_{};
    std::uint32_t source_manager_{0};
    std::uint32_t reserved_{0};
};

struct remote_task_id
{
    std::uint32_t manager_id_{0};
    task_id task_{};
};

// 多 manager 协调：每 manager 一个有界入站队列，只传消息不迁移任务
struct manager_group
{
    struct entry
    {
        manager* target_{nullptr};
        mpmc_ring<group_message> inbox_{};
    };

    entry* entries_{nullptr};
    std::uint32_t count_{0};

    manager_group() noexcept = default;

    manager_group(const manager_group&) = delete;
    manager_group& operator=(const manager_group&) = delete;

    ~manager_group()
    {
        destroy();
    }

    bool create(manager** managers, std::uint32_t count, std::uint32_t capacity = 1024) noexcept
    {
        if (entries_ != nullptr || managers == nullptr || count == 0)
        {
            return false;
        }
        void* memory = ::operator new(sizeof(entry) * count, std::align_val_t(alignof(entry)),
                                      std::nothrow);
        if (memory == nullptr)
        {
            return false;
        }
        entries_ = static_cast<entry*>(memory);
        for (std::uint32_t i = 0; i < count; ++i)
        {
            new (&entries_[i]) entry{};
            entries_[i].target_ = managers[i];
            if (!entries_[i].inbox_.create(capacity))
            {
                destroy();
                return false;
            }
        }
        count_ = count;
        return true;
    }

    void destroy() noexcept
    {
        if (entries_ == nullptr)
        {
            return;
        }
        for (std::uint32_t i = 0; i < count_; ++i)
        {
            entries_[i].inbox_.destroy();
            entries_[i].~entry();
        }
        ::operator delete(static_cast<void*>(entries_), std::align_val_t(alignof(entry)));
        entries_ = nullptr;
        count_ = 0;
    }

    bool post(std::uint32_t manager_id, const group_message& message) noexcept
    {
        if (manager_id >= count_)
        {
            return false;
        }
        return entries_[manager_id].inbox_.try_push(message);
    }

    // 消费本 manager 的消息并转为激活/取消；返回处理条数
    std::size_t poll(std::uint32_t manager_id, std::size_t budget = 64) noexcept
    {
        if (manager_id >= count_)
        {
            return 0;
        }
        entry& slot = entries_[manager_id];
        if (slot.target_ == nullptr)
        {
            return 0;
        }
        std::size_t handled = 0;
        group_message message{};
        while (handled < budget && slot.inbox_.try_pop(message))
        {
            switch (message.kind_)
            {
            case group_message_kind::activate:
                (void)slot.target_->activate(message.task_);
                break;
            case group_message_kind::deactivate:
                (void)slot.target_->deactivate(message.task_);
                break;
            case group_message_kind::cancel:
                (void)slot.target_->cancel(message.task_);
                break;
            }
            ++handled;
        }
        return handled;
    }

    std::size_t poll_all(std::size_t budget_per_manager = 64) noexcept
    {
        std::size_t total = 0;
        for (std::uint32_t i = 0; i < count_; ++i)
        {
            total += poll(i, budget_per_manager);
        }
        return total;
    }
};
} // namespace thread
