#pragma once

#include <cstddef>
#include <cstdint>

// 侵入式定时节点：内嵌在连接/请求/任务对象里，零分配
struct timer_node
{
    timer_node* next_{nullptr};
    timer_node* prev_{nullptr};
    std::uint64_t expire_ns_{0};
    std::uint32_t bucket_{0};
    std::uint8_t level_{0};
    bool active_{false};
};

using timer_callback = void (*)(void* owner, timer_node* node) noexcept;

inline constexpr std::uint32_t timing_wheel_levels = 4;
inline constexpr std::uint32_t timing_wheel_slots = 64;
inline constexpr std::uint32_t timing_wheel_slot_bits = 6;
inline constexpr std::uint64_t timing_wheel_tick_ns = 1'000'000ull;   // 1 ms

// 四级分层时间轮（每级 64 槽）：只在本线程操作，不含线程与系统调用
struct timing_wheel
{
    timer_node* buckets_[timing_wheel_levels][timing_wheel_slots]{};
    std::uint64_t now_ns_{0};
    std::uint64_t current_tick_{1};
    timer_callback callback_{nullptr};
    void* owner_{nullptr};
    std::uint32_t capacity_{0};
    std::uint32_t count_{0};

    timing_wheel() noexcept = default;

    timing_wheel(const timing_wheel&) = delete;
    timing_wheel& operator=(const timing_wheel&) = delete;

    bool create(std::uint32_t capacity, timer_callback callback, void* owner,
                std::uint64_t now_ns) noexcept
    {
        if (callback == nullptr || capacity == 0)
        {
            return false;
        }
        callback_ = callback;
        owner_ = owner;
        capacity_ = capacity;
        count_ = 0;
        now_ns_ = now_ns;
        current_tick_ = (now_ns / timing_wheel_tick_ns) + 1ull;
        for (std::uint32_t level = 0; level < timing_wheel_levels; ++level)
        {
            for (std::uint32_t slot = 0; slot < timing_wheel_slots; ++slot)
            {
                buckets_[level][slot] = nullptr;
            }
        }
        return true;
    }

    void destroy() noexcept
    {
        for (std::uint32_t level = 0; level < timing_wheel_levels; ++level)
        {
            for (std::uint32_t slot = 0; slot < timing_wheel_slots; ++slot)
            {
                timer_node* node = buckets_[level][slot];
                while (node != nullptr)
                {
                    timer_node* next = node->next_;
                    node->next_ = nullptr;
                    node->prev_ = nullptr;
                    node->active_ = false;
                    node = next;
                }
                buckets_[level][slot] = nullptr;
            }
        }
        count_ = 0;
        capacity_ = 0;
        callback_ = nullptr;
        owner_ = nullptr;
    }

    std::uint32_t size() const noexcept
    {
        return count_;
    }

    std::uint32_t capacity() const noexcept
    {
        return capacity_;
    }

    bool add(timer_node& node, std::uint64_t delay_ns) noexcept
    {
        if (node.active_ || count_ >= capacity_)
        {
            return false;
        }
        node.expire_ns_ = (now_ns_ > 0 ? now_ns_ : 0) + delay_ns;
        node.active_ = true;
        link(node);
        ++count_;
        return true;
    }

    bool cancel(timer_node& node) noexcept
    {
        if (!node.active_)
        {
            return false;
        }
        unlink(node);
        node.active_ = false;
        if (count_ > 0)
        {
            --count_;
        }
        return true;
    }

    // 推进到 now：只处理走到的槽位，过期节点回调，未过期节点重新落桶
    std::size_t advance(std::uint64_t now_ns) noexcept
    {
        if (callback_ == nullptr || now_ns <= now_ns_)
        {
            if (now_ns > now_ns_)
            {
                now_ns_ = now_ns;
            }
            return 0;
        }
        now_ns_ = now_ns;
        const std::uint64_t target_tick = now_ns / timing_wheel_tick_ns;
        std::size_t fired = 0;
        while (current_tick_ <= target_tick)
        {
            const std::uint64_t tick = current_tick_;
            fired += process_slot(0, static_cast<std::uint32_t>(tick & (timing_wheel_slots - 1)),
                                  now_ns);
            if ((tick & (timing_wheel_slots - 1)) == 0)
            {
                fired += process_slot(1,
                                      static_cast<std::uint32_t>((tick >> timing_wheel_slot_bits) &
                                                                 (timing_wheel_slots - 1)),
                                      now_ns);
            }
            if ((tick & ((1ull << (timing_wheel_slot_bits * 2)) - 1)) == 0)
            {
                fired += process_slot(2,
                                      static_cast<std::uint32_t>((tick >> (timing_wheel_slot_bits * 2)) &
                                                                 (timing_wheel_slots - 1)),
                                      now_ns);
            }
            if ((tick & ((1ull << (timing_wheel_slot_bits * 3)) - 1)) == 0)
            {
                fired += process_slot(3,
                                      static_cast<std::uint32_t>((tick >> (timing_wheel_slot_bits * 3)) &
                                                                 (timing_wheel_slots - 1)),
                                      now_ns);
            }
            ++current_tick_;
        }
        return fired;
    }

  private:
    static std::uint32_t level_of(std::uint64_t delay_ticks) noexcept
    {
        if (delay_ticks < timing_wheel_slots)
        {
            return 0;
        }
        if (delay_ticks < (1ull << (timing_wheel_slot_bits * 2)))
        {
            return 1;
        }
        if (delay_ticks < (1ull << (timing_wheel_slot_bits * 3)))
        {
            return 2;
        }
        return 3;
    }

    void link(timer_node& node) noexcept
    {
        const std::uint64_t expire_tick = (node.expire_ns_ / timing_wheel_tick_ns) + 1ull;
        const std::uint64_t delay_ticks =
            expire_tick > current_tick_ ? expire_tick - current_tick_ : 1ull;
        const std::uint32_t level = level_of(delay_ticks);
        const std::uint32_t slot =
            static_cast<std::uint32_t>((expire_tick >> (timing_wheel_slot_bits * level)) &
                                       (timing_wheel_slots - 1));
        node.level_ = static_cast<std::uint8_t>(level);
        node.bucket_ = slot;
        timer_node* head = buckets_[level][slot];
        node.prev_ = nullptr;
        node.next_ = head;
        if (head != nullptr)
        {
            head->prev_ = &node;
        }
        buckets_[level][slot] = &node;
    }

    void unlink(timer_node& node) noexcept
    {
        if (node.prev_ != nullptr)
        {
            node.prev_->next_ = node.next_;
        }
        else if (node.level_ < timing_wheel_levels)
        {
            timer_node*& head = buckets_[node.level_][node.bucket_];
            if (head == &node)
            {
                head = node.next_;
            }
        }
        if (node.next_ != nullptr)
        {
            node.next_->prev_ = node.prev_;
        }
        node.next_ = nullptr;
        node.prev_ = nullptr;
    }

    std::size_t process_slot(std::uint32_t level, std::uint32_t slot,
                             std::uint64_t now_ns) noexcept
    {
        std::size_t fired = 0;
        timer_node* node = buckets_[level][slot];
        buckets_[level][slot] = nullptr;
        while (node != nullptr)
        {
            timer_node* next = node->next_;
            node->next_ = nullptr;
            node->prev_ = nullptr;
            node->active_ = false;
            if (count_ > 0)
            {
                --count_;
            }
            if (node->expire_ns_ <= now_ns)
            {
                callback_(owner_, node);
                ++fired;
            }
            else
            {
                node->active_ = true;
                link(*node);
                ++count_;
            }
            node = next;
        }
        return fired;
    }
};
