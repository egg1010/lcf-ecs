#pragma once

#include <atomic>
#include <cstdint>
#include <new>

namespace thread
{

inline constexpr std::uint32_t invalid_resource_index = 0xFFFFFFFFu;

struct resource_id
{
    std::uint32_t value_{invalid_resource_index};

    bool valid() const noexcept
    {
        return value_ != invalid_resource_index;
    }

    bool operator==(const resource_id& other) const noexcept = default;
};

enum class access_mode : std::uint8_t
{
    read = 0,
    write = 1,
};

struct resource_access
{
    resource_id id_{};
    access_mode mode_{access_mode::read};
};

// 任务访问声明：按任务串链，注册期一次性写入
struct resource_edge
{
    std::uint32_t resource_{invalid_resource_index};
    std::uint32_t next_{0};
    std::uint32_t mode_{0};
};

struct resource_state
{
    std::atomic<std::uint32_t> writer_{invalid_resource_index};
    std::atomic<std::uint32_t> readers_{0};
    std::atomic<std::uint32_t> waiting_writers_{0};
    std::atomic<std::uint32_t> wait_head_{invalid_resource_index};
    std::atomic<std::uint32_t> reserved_{0};
};

// 资源状态表：固定容量；等待链用任务 side table 串接，不分配边节点
struct resource_table
{
    resource_state* states_{nullptr};
    std::atomic<std::uint32_t>* wait_next_{nullptr};
    std::uint32_t capacity_{0};
    std::uint32_t task_capacity_{0};

    resource_table() noexcept = default;

    resource_table(const resource_table&) = delete;
    resource_table& operator=(const resource_table&) = delete;

    ~resource_table()
    {
        destroy();
    }

    bool create(std::uint32_t capacity, std::uint32_t task_capacity) noexcept
    {
        if (states_ != nullptr || capacity == 0 || task_capacity == 0)
        {
            return false;
        }
        void* state_memory =
            ::operator new(sizeof(resource_state) * capacity, std::align_val_t(64), std::nothrow);
        void* next_memory = ::operator new(sizeof(std::atomic<std::uint32_t>) * task_capacity,
                                           std::align_val_t(64), std::nothrow);
        if (state_memory == nullptr || next_memory == nullptr)
        {
            ::operator delete(state_memory, std::align_val_t(64));
            ::operator delete(next_memory, std::align_val_t(64));
            return false;
        }
        states_ = static_cast<resource_state*>(state_memory);
        for (std::uint32_t i = 0; i < capacity; ++i)
        {
            new (&states_[i]) resource_state{};
        }
        wait_next_ = static_cast<std::atomic<std::uint32_t>*>(next_memory);
        for (std::uint32_t i = 0; i < task_capacity; ++i)
        {
            new (&wait_next_[i]) std::atomic<std::uint32_t>(invalid_resource_index);
        }
        capacity_ = capacity;
        task_capacity_ = task_capacity;
        return true;
    }

    void destroy() noexcept
    {
        if (states_ != nullptr)
        {
            for (std::uint32_t i = 0; i < capacity_; ++i)
            {
                states_[i].~resource_state();
            }
            ::operator delete(static_cast<void*>(states_), std::align_val_t(64));
            ::operator delete(static_cast<void*>(wait_next_), std::align_val_t(64));
            states_ = nullptr;
            wait_next_ = nullptr;
            capacity_ = 0;
            task_capacity_ = 0;
        }
    }

    resource_state* at(resource_id id) noexcept
    {
        return id.value_ < capacity_ ? &states_[id.value_] : nullptr;
    }

    bool can_acquire(resource_id id, access_mode mode) noexcept
    {
        resource_state* state = at(id);
        if (state == nullptr)
        {
            return false;
        }
        if (mode == access_mode::write)
        {
            return state->writer_.load(std::memory_order_acquire) == invalid_resource_index &&
                   state->readers_.load(std::memory_order_acquire) == 0;
        }
        if (state->writer_.load(std::memory_order_acquire) != invalid_resource_index)
        {
            return false;
        }
        // 写者优先：已有写者排队时新读者不插队
        return state->waiting_writers_.load(std::memory_order_acquire) == 0;
    }

    void acquire(resource_id id, access_mode mode, std::uint32_t task) noexcept
    {
        resource_state* state = at(id);
        if (state == nullptr)
        {
            return;
        }
        if (mode == access_mode::write)
        {
            state->writer_.store(task, std::memory_order_release);
        }
        else
        {
            state->readers_.fetch_add(1, std::memory_order_acq_rel);
        }
    }

    void release(resource_id id, access_mode mode, std::uint32_t task) noexcept
    {
        resource_state* state = at(id);
        if (state == nullptr)
        {
            return;
        }
        if (mode == access_mode::write)
        {
            std::uint32_t expected = task;
            (void)state->writer_.compare_exchange_strong(expected, invalid_resource_index,
                                                         std::memory_order_acq_rel,
                                                         std::memory_order_acquire);
        }
        else
        {
            (void)state->readers_.fetch_sub(1, std::memory_order_acq_rel);
        }
    }

    void enqueue_wait(resource_id id, std::uint32_t task) noexcept
    {
        resource_state* state = at(id);
        if (state == nullptr || task >= task_capacity_)
        {
            return;
        }
        if (wait_next_[task].load(std::memory_order_relaxed) != invalid_resource_index)
        {
            return;
        }
        std::uint32_t head = state->wait_head_.load(std::memory_order_acquire);
        for (;;)
        {
            wait_next_[task].store(head, std::memory_order_relaxed);
            if (state->wait_head_.compare_exchange_weak(head, task, std::memory_order_acq_rel,
                                                        std::memory_order_acquire))
            {
                return;
            }
        }
    }

    bool dequeue_wait(resource_id id, std::uint32_t task) noexcept
    {
        resource_state* state = at(id);
        if (state == nullptr || task >= task_capacity_)
        {
            return false;
        }
        std::uint32_t head = state->wait_head_.load(std::memory_order_acquire);
        std::uint32_t previous = invalid_resource_index;
        std::uint32_t cursor = head;
        while (cursor != invalid_resource_index)
        {
            if (cursor == task)
            {
                const std::uint32_t next =
                    wait_next_[task].load(std::memory_order_relaxed);
                if (previous == invalid_resource_index)
                {
                    std::uint32_t expected = task;
                    if (!state->wait_head_.compare_exchange_strong(expected, next,
                                                                    std::memory_order_acq_rel,
                                                                    std::memory_order_acquire))
                    {
                        return false;
                    }
                }
                else
                {
                    wait_next_[previous].store(next, std::memory_order_release);
                }
                wait_next_[task].store(invalid_resource_index, std::memory_order_relaxed);
                return true;
            }
            previous = cursor;
            cursor = wait_next_[cursor].load(std::memory_order_acquire);
        }
        return false;
    }

    std::uint32_t wait_head(resource_id id) const noexcept
    {
        return id.value_ < capacity_ ? states_[id.value_].wait_head_.load(std::memory_order_acquire)
                                     : invalid_resource_index;
    }

    void note_waiting_writer(resource_id id, bool add) noexcept
    {
        resource_state* state = at(id);
        if (state == nullptr)
        {
            return;
        }
        if (add)
        {
            state->waiting_writers_.fetch_add(1, std::memory_order_acq_rel);
        }
        else
        {
            state->waiting_writers_.fetch_sub(1, std::memory_order_acq_rel);
        }
    }
};
} // namespace thread
