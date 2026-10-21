#pragma once

#include <atomic>
#include <cstdint>
#include <new>
#include <thread>

namespace thread
{

inline constexpr std::uint32_t invalid_resource_index = 0xFFFFFFFFu;

// 分片锁数量：2 的幂，stripe = 资源编号 & (count - 1)
inline constexpr std::size_t resource_lock_count = 64;

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

// 资源状态：低 32 位读者计数，高 32 位写者任务索引 + 1（0 表示无写者）
struct alignas(64) resource_state
{
    std::atomic<std::uint64_t> state_{0};
    std::atomic<std::uint32_t> waiting_writers_{0};
    std::atomic<std::uint32_t> wait_head_{invalid_resource_index};

    static std::uint32_t writer_of(std::uint64_t value) noexcept
    {
        return static_cast<std::uint32_t>(value >> 32);
    }

    static std::uint64_t pack_writer(std::uint32_t task) noexcept
    {
        return static_cast<std::uint64_t>(task + 1) << 32;
    }
};

// 每把锁独占一条缓存行，避免相邻分片伪共享
struct alignas(64) resource_lock
{
    std::atomic_flag flag{};
};

// 资源状态表：固定容量；等待链用任务 side table 串接，不分配边节点
struct resource_table
{
    resource_state* states_{nullptr};
    std::atomic<std::uint32_t>* wait_next_{nullptr};
    std::uint32_t capacity_{0};
    std::uint32_t task_capacity_{0};
    resource_lock locks_[resource_lock_count];

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
        for (std::size_t i = 0; i < resource_lock_count; ++i)
        {
            locks_[i].flag.clear(std::memory_order_relaxed);
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

    static std::size_t stripe_of(std::uint32_t resource) noexcept
    {
        return resource & (resource_lock_count - 1);
    }

    void lock_stripe(std::size_t stripe) noexcept
    {
        resource_lock& slot = locks_[stripe];
        std::size_t spins = 0;
        while (slot.flag.test_and_set(std::memory_order_acquire))
        {
            ++spins;
            if ((spins & 63u) == 0)
            {
                std::this_thread::yield();
            }
        }
    }

    void unlock_stripe(std::size_t stripe) noexcept
    {
        locks_[stripe].flag.clear(std::memory_order_release);
    }

    // 按 stripe 升序加锁、降序解锁；mask 的每一位对应一个 stripe
    void lock_mask(std::uint64_t mask) noexcept
    {
        for (std::size_t stripe = 0; stripe < resource_lock_count; ++stripe)
        {
            if ((mask & (1ull << stripe)) != 0)
            {
                lock_stripe(stripe);
            }
        }
    }

    void unlock_mask(std::uint64_t mask) noexcept
    {
        for (std::size_t stripe = resource_lock_count; stripe > 0; --stripe)
        {
            if ((mask & (1ull << (stripe - 1))) != 0)
            {
                unlock_stripe(stripe - 1);
            }
        }
    }

    void lock_resource(std::uint32_t resource) noexcept
    {
        lock_stripe(stripe_of(resource));
    }

    void unlock_resource(std::uint32_t resource) noexcept
    {
        unlock_stripe(stripe_of(resource));
    }

    // 以下接口要求持有对应资源的分片锁
    bool can_acquire_locked(resource_id id, access_mode mode) noexcept
    {
        resource_state* state = at(id);
        if (state == nullptr)
        {
            return false;
        }
        const std::uint64_t value = state->state_.load(std::memory_order_relaxed);
        if (mode == access_mode::write)
        {
            return value == 0;
        }
        if (resource_state::writer_of(value) != 0)
        {
            return false;
        }
        // 写者优先：已有写者排队时新读者不插队
        return state->waiting_writers_.load(std::memory_order_relaxed) == 0;
    }

    void acquire_locked(resource_id id, access_mode mode, std::uint32_t task) noexcept
    {
        resource_state* state = at(id);
        if (state == nullptr)
        {
            return;
        }
        if (mode == access_mode::write)
        {
            state->state_.store(resource_state::pack_writer(task), std::memory_order_release);
        }
        else
        {
            state->state_.fetch_add(1, std::memory_order_release);
        }
    }

    void release_locked(resource_id id, access_mode mode, std::uint32_t task) noexcept
    {
        resource_state* state = at(id);
        if (state == nullptr)
        {
            return;
        }
        if (mode == access_mode::write)
        {
            std::uint64_t expected = resource_state::pack_writer(task);
            (void)state->state_.compare_exchange_strong(expected, 0, std::memory_order_release,
                                                        std::memory_order_relaxed);
        }
        else
        {
            (void)state->state_.fetch_sub(1, std::memory_order_release);
        }
    }

    std::uint32_t wait_head_locked(resource_id id) noexcept
    {
        resource_state* state = at(id);
        return state != nullptr ? state->wait_head_.load(std::memory_order_relaxed)
                                : invalid_resource_index;
    }

    void enqueue_wait_locked(resource_id id, std::uint32_t task) noexcept
    {
        resource_state* state = at(id);
        if (state == nullptr || task >= task_capacity_)
        {
            return;
        }
        const std::uint32_t head = state->wait_head_.load(std::memory_order_relaxed);
        wait_next_[task].store(head, std::memory_order_relaxed);
        state->wait_head_.store(task, std::memory_order_release);
    }

    bool dequeue_wait_locked(resource_id id, std::uint32_t task) noexcept
    {
        resource_state* state = at(id);
        if (state == nullptr || task >= task_capacity_)
        {
            return false;
        }
        std::uint32_t previous = invalid_resource_index;
        std::uint32_t cursor = state->wait_head_.load(std::memory_order_relaxed);
        while (cursor != invalid_resource_index)
        {
            if (cursor == task)
            {
                const std::uint32_t next = wait_next_[task].load(std::memory_order_relaxed);
                if (previous == invalid_resource_index)
                {
                    state->wait_head_.store(next, std::memory_order_release);
                }
                else
                {
                    wait_next_[previous].store(next, std::memory_order_release);
                }
                wait_next_[task].store(invalid_resource_index, std::memory_order_relaxed);
                return true;
            }
            previous = cursor;
            cursor = wait_next_[cursor].load(std::memory_order_relaxed);
        }
        return false;
    }

    void note_waiting_writer_locked(resource_id id, bool add) noexcept
    {
        resource_state* state = at(id);
        if (state == nullptr)
        {
            return;
        }
        if (add)
        {
            state->waiting_writers_.fetch_add(1, std::memory_order_relaxed);
        }
        else
        {
            state->waiting_writers_.fetch_sub(1, std::memory_order_relaxed);
        }
    }
};
} // namespace thread
