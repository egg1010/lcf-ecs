#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

#include "thread/error.hpp"
#include "thread/platform.hpp"

namespace thread
{

struct waiter_node
{
    using wake_fn = void (*)(waiter_node* self) noexcept;

    std::atomic<waiter_node*> next_{nullptr};
    wake_fn wake_{nullptr};
    std::atomic<bool> signalled_{false};
};

inline void waiter_node_init(waiter_node* node, waiter_node::wake_fn wake) noexcept
{
    node->next_.store(nullptr, std::memory_order_relaxed);
    node->wake_ = wake;
    node->signalled_.store(false, std::memory_order_relaxed);
}

struct waiter_list
{
    std::atomic<waiter_node*> head_{nullptr};

    bool empty() const noexcept
    {
        return head_.load(std::memory_order_acquire) == nullptr;
    }

    void push(waiter_node* node) noexcept
    {
        waiter_node* head = head_.load(std::memory_order_relaxed);
        for (;;)
        {
            node->next_.store(head, std::memory_order_relaxed);
            if (head_.compare_exchange_weak(head, node, std::memory_order_release,
                                            std::memory_order_relaxed))
            {
                return;
            }
        }
    }

    waiter_node* drain() noexcept
    {
        waiter_node* head = head_.exchange(nullptr, std::memory_order_acq_rel);
        waiter_node* reversed = nullptr;
        while (head != nullptr)
        {
            waiter_node* next = head->next_.load(std::memory_order_relaxed);
            head->next_.store(reversed, std::memory_order_relaxed);
            reversed = head;
            head = next;
        }
        return reversed;
    }
};

inline std::size_t waiter_list_wake(waiter_node* list) noexcept
{
    std::size_t count = 0;
    while (list != nullptr)
    {
        waiter_node* next = list->next_.load(std::memory_order_relaxed);
        bool expected = false;
        if (list->signalled_.compare_exchange_strong(expected, true, std::memory_order_acq_rel,
                                                     std::memory_order_relaxed))
        {
            if (list->wake_ != nullptr)
            {
                list->wake_(list);
            }
            ++count;
        }
        list = next;
    }
    return count;
}

inline std::size_t waiter_list_wake_except(waiter_node* list, waiter_node* skip) noexcept
{
    std::size_t count = 0;
    while (list != nullptr)
    {
        waiter_node* next = list->next_.load(std::memory_order_relaxed);
        if (list != skip)
        {
            bool expected = false;
            if (list->signalled_.compare_exchange_strong(expected, true, std::memory_order_acq_rel,
                                                         std::memory_order_relaxed))
            {
                if (list->wake_ != nullptr)
                {
                    list->wake_(list);
                }
                ++count;
            }
        }
        list = next;
    }
    return count;
}

struct counter
{
    std::atomic<std::uint32_t> pending_{0};
    waiter_list waiters_{};

    counter() noexcept = default;

    counter(const counter&) = delete;
    counter& operator=(const counter&) = delete;

    void init(std::uint32_t initial = 0) noexcept
    {
        pending_.store(initial, std::memory_order_relaxed);

        (void)waiters_.drain();
    }

    void add(std::uint32_t delta = 1) noexcept
    {
        pending_.fetch_add(delta, std::memory_order_acq_rel);
    }

    std::uint32_t value() const noexcept
    {
        return pending_.load(std::memory_order_acquire);
    }

    bool ready() const noexcept
    {
        return pending_.load(std::memory_order_acquire) == 0;
    }

    bool complete_one() noexcept
    {
        const std::uint32_t previous = pending_.fetch_sub(1, std::memory_order_acq_rel);
        if (previous == 1)
        {
            std::atomic_thread_fence(std::memory_order_acquire);
            waiter_node* list = waiters_.drain();
            if (list != nullptr)
            {
                waiter_list_wake(list);
            }
            return true;
        }
        return false;
    }

    void complete(std::uint32_t delta = 1) noexcept
    {
        const std::uint32_t previous = pending_.fetch_sub(delta, std::memory_order_acq_rel);
        if (previous <= delta)
        {
            pending_.store(0, std::memory_order_release);
            std::atomic_thread_fence(std::memory_order_acquire);
            waiter_node* list = waiters_.drain();
            if (list != nullptr)
            {
                waiter_list_wake(list);
            }
        }
    }

    bool attach_waiter(waiter_node* node) noexcept
    {
        if (ready())
        {
            return false;
        }
        waiters_.push(node);

        if (ready())
        {
            std::atomic_thread_fence(std::memory_order_acquire);
            waiter_node* list = waiters_.drain();
            if (list != nullptr)
            {
                waiter_list_wake_except(list, node);
            }
            return false;
        }
        return true;
    }
};

struct event
{
    std::atomic<bool> signalled_{false};
    waiter_list waiters_{};

    event() noexcept = default;

    event(const event&) = delete;
    event& operator=(const event&) = delete;

    void init() noexcept
    {
        signalled_.store(false, std::memory_order_relaxed);
        (void)waiters_.drain();
    }

    bool triggered() const noexcept
    {
        return signalled_.load(std::memory_order_acquire);
    }

    void trigger() noexcept
    {
        bool expected = false;
        if (!signalled_.compare_exchange_strong(expected, true, std::memory_order_acq_rel,
                                                std::memory_order_relaxed))
        {
            return;
        }
        std::atomic_thread_fence(std::memory_order_acquire);
        waiter_node* list = waiters_.drain();
        if (list != nullptr)
        {
            waiter_list_wake(list);
        }
    }

    bool attach_waiter(waiter_node* node) noexcept
    {
        if (triggered())
        {
            return false;
        }
        waiters_.push(node);
        if (triggered())
        {
            std::atomic_thread_fence(std::memory_order_acquire);
            waiter_node* list = waiters_.drain();
            if (list != nullptr)
            {
                waiter_list_wake_except(list, node);
            }
            return false;
        }
        return true;
    }
};

struct fence
{
    fence() noexcept = default;

    fence(const fence&) = delete;
    fence& operator=(const fence&) = delete;

    void init(std::uint32_t expected) noexcept
    {
        expected_.store(expected == 0 ? 1 : expected, std::memory_order_relaxed);
        arrived_.store(0, std::memory_order_relaxed);
        generation_.store(0, std::memory_order_relaxed);
    }

    std::uint32_t generation() const noexcept
    {
        return generation_.load(std::memory_order_acquire);
    }

    void arrive() noexcept
    {
        const std::uint64_t seq = arrived_.fetch_add(1, std::memory_order_acq_rel) + 1;
        const std::uint64_t expected = static_cast<std::uint64_t>(
            expected_.load(std::memory_order_acquire));
        if (expected != 0 && seq % expected == 0)
        {
            generation_.fetch_add(1, std::memory_order_acq_rel);
            std::atomic_thread_fence(std::memory_order_acquire);
            waiter_node* list = waiters_.drain();
            if (list != nullptr)
            {
                waiter_list_wake(list);
            }
        }
    }

    bool attach_waiter(waiter_node* node, std::uint32_t observed_generation) noexcept
    {
        if (generation_.load(std::memory_order_acquire) != observed_generation)
        {
            bool expected = false;
            node->signalled_.compare_exchange_strong(expected, true, std::memory_order_acq_rel,
                                                     std::memory_order_relaxed);
            if (node->wake_ != nullptr)
            {
                node->wake_(node);
            }
            return false;
        }
        waiters_.push(node);
        if (generation_.load(std::memory_order_acquire) != observed_generation)
        {
            std::atomic_thread_fence(std::memory_order_acquire);
            waiter_node* list = waiters_.drain();
            if (list != nullptr)
            {
                waiter_list_wake(list);
            }
        }
        return true;
    }

  private:
    std::atomic<std::uint32_t> expected_{1};
    std::atomic<std::uint64_t> arrived_{0};
    std::atomic<std::uint32_t> generation_{0};
    waiter_list waiters_{};
};

// 非 worker 线程可用；worker 内禁止（红线）

struct spin_latch
{
    park_state state_{};

    void reset() noexcept
    {
        park_state_init(&state_);
    }

    void release() noexcept
    {
        unpark_all(&state_, 1);
    }

    bool ready() const noexcept
    {
        return state_.word_.load(std::memory_order_acquire) != 0;
    }

    bool wait(std::uint32_t timeout_ms = infinite_park_ms) noexcept
    {
        for (int i = 0; i < 64; ++i)
        {
            if (ready())
            {
                return true;
            }
            platform_relax_pause();
        }
        for (int i = 0; i < 64; ++i)
        {
            if (ready())
            {
                return true;
            }
            platform_yield();
        }
        for (;;)
        {
            if (ready())
            {
                return true;
            }
            const park_result result = park_once(&state_, 0, timeout_ms);
            if (result == park_result::timed_out)
            {
                return ready();
            }
        }
    }
};
} // namespace thread
