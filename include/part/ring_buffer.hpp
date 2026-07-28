#pragma once

// 无界 ring_buffer: chunk 链表 + 静态 LIFO 池
// - 块大小 ≈ 4KB, SLOTS 编译期常量 (非 2 的幂, 精确填满)
// - 静态池按 T 共享, 同 T 不同 N 的实例共用缓存
// - push 永不失败 (OOM abort)
// - N 作为编译期最小保证容量, 实际无界

#include <cstddef>
#include <cstdint>
#include <new>
#include <type_traits>
#include <utility>
#include "force_inline.hpp"

// 按 T 提取 chunk 类型和静态池, 解耦 N
// 不同 N 的 ring_buffer<T, N> 共享同一个 ring_buffer_traits<T>::instance
template <typename T>
struct ring_buffer_traits
{
    static constexpr size_t CHUNK_OVERHEAD = sizeof(void*) + 2 * sizeof(uint32_t);
    static constexpr size_t TARGET_BYTES   = 4096;
    static constexpr size_t SLOTS          = (TARGET_BYTES - CHUNK_OVERHEAD) / sizeof(T);
    static_assert(SLOTS > 0, "T too large for 4KB chunk");

    struct chunk
    {
        T slots[SLOTS];
        chunk* next;
        uint32_t write;
        uint32_t read;
    };

    // 静态 LIFO 池: 缓存释放的 chunk, 避免反复 ::operator new/delete
    struct pool
    {
        static constexpr size_t MAX_FREE = 64;

        chunk* head{nullptr};
        size_t count{0};

        // acquire 快路径不维护 count (count 仅 release 用于 MAX_FREE 保护)
        [[nodiscard]] FORCE_INLINE chunk* acquire() noexcept
        {
            if (head) [[likely]]
            {
                chunk* c = head;
                head = c->next;
                return c;
            }
            chunk* c = static_cast<chunk*>(::operator new(sizeof(chunk), std::nothrow));
            if (!c) [[unlikely]] std::abort();
            return c;
        }

        FORCE_INLINE void release(chunk* c) noexcept
        {
            if (count < MAX_FREE) [[likely]]
            {
                c->next = head;
                head = c;
                ++count;
            }
            else
            {
                ::operator delete(c);
            }
        }

        ~pool() noexcept
        {
            while (head)
            {
                chunk* next = head->next;
                ::operator delete(head);
                head = next;
            }
            count = 0;
        }
    };

    static inline pool instance{};
};

template <typename T, size_t N = 1024>
class ring_buffer
{
    using traits     = ring_buffer_traits<T>;
    using chunk      = typename traits::chunk;
    using chunk_pool = typename traits::pool;

    static constexpr size_t SLOTS          = traits::SLOTS;
    static constexpr size_t INITIAL_CHUNKS = (N + SLOTS - 1) / SLOTS;
    static_assert(INITIAL_CHUNKS >= 1, "INITIAL_CHUNKS must be >= 1");

    chunk* active_head_{nullptr};
    chunk* active_tail_{nullptr};
    size_t total_count_{0};

public:
    ring_buffer() noexcept
    {
        // 预热池: 首个该 T 类型实例负责预分配, 后续实例零开销
        if (traits::instance.count < INITIAL_CHUNKS) [[unlikely]]
        {
            size_t need = INITIAL_CHUNKS - traits::instance.count;
            for (size_t i = 0; i < need; ++i)
            {
                chunk* c = static_cast<chunk*>(
                    ::operator new(sizeof(chunk), std::nothrow));
                if (!c) [[unlikely]] std::abort();
                c->next = traits::instance.head;
                traits::instance.head = c;
                ++traits::instance.count;
            }
        }
    }

    ~ring_buffer() noexcept { clear(); }

    ring_buffer(ring_buffer&& o) noexcept
        : active_head_(o.active_head_)
        , active_tail_(o.active_tail_)
        , total_count_(o.total_count_)
    {
        o.active_head_ = nullptr;
        o.active_tail_ = nullptr;
        o.total_count_ = 0;
    }

    ring_buffer& operator=(ring_buffer&& o) noexcept
    {
        if (this != &o) [[likely]]
        {
            clear();
            active_head_ = o.active_head_;
            active_tail_ = o.active_tail_;
            total_count_ = o.total_count_;
            o.active_head_ = nullptr;
            o.active_tail_ = nullptr;
            o.total_count_ = 0;
        }
        return *this;
    }

    ring_buffer(const ring_buffer&)            = delete;
    ring_buffer& operator=(const ring_buffer&) = delete;

    [[nodiscard]] bool push(const T& event) noexcept
    {
        chunk* t = active_tail_;
        if (t) [[likely]]
        {
            uint32_t w = t->write;
            if (w < SLOTS) [[likely]]
            {
                if constexpr (std::is_trivially_copyable_v<T>)
                {
                    t->slots[w] = event;
                }
                else
                {
                    new (&t->slots[w]) T(event);
                }
                t->write = w + 1;
                ++total_count_;
                return true;
            }
        }
        return push_slow(event);
    }

    [[nodiscard]] bool push(T&& event) noexcept
    {
        if constexpr (std::is_trivially_copyable_v<T>)
        {
            return push(static_cast<const T&>(event));
        }
        else
        {
            chunk* t = active_tail_;
            if (t) [[likely]]
            {
                uint32_t w = t->write;
                if (w < SLOTS) [[likely]]
                {
                    new (&t->slots[w]) T(std::move(event));
                    t->write = w + 1;
                    ++total_count_;
                    return true;
                }
            }
            return push_slow(std::move(event));
        }
    }

    template <typename... Args>
    [[nodiscard]] bool emplace(Args&&... args) noexcept
    {
        chunk* t = active_tail_;
        if (t) [[likely]]
        {
            uint32_t w = t->write;
            if (w < SLOTS) [[likely]]
            {
                new (&t->slots[w]) T(std::forward<Args>(args)...);
                t->write = w + 1;
                ++total_count_;
                return true;
            }
        }
        return emplace_slow(std::forward<Args>(args)...);
    }

private:
    template <typename U>
    NOINLINE bool push_slow(U&& event) noexcept
    {
        chunk* c = traits::instance.acquire();
        if constexpr (std::is_trivially_copyable_v<T>)
        {
            c->slots[0] = std::forward<U>(event);
        }
        else
        {
            new (&c->slots[0]) T(std::forward<U>(event));
        }
        c->write = 1;
        c->read  = 0;
        c->next  = nullptr;
        if (active_tail_) [[likely]]
        {
            active_tail_->next = c;
        }
        else
        {
            active_head_ = c;
        }
        active_tail_ = c;
        ++total_count_;
        return true;
    }

    template <typename... Args>
    NOINLINE bool emplace_slow(Args&&... args) noexcept
    {
        chunk* c = traits::instance.acquire();
        new (&c->slots[0]) T(std::forward<Args>(args)...);
        c->write = 1;
        c->read  = 0;
        c->next  = nullptr;
        if (active_tail_) [[likely]]
        {
            active_tail_->next = c;
        }
        else
        {
            active_head_ = c;
        }
        active_tail_ = c;
        ++total_count_;
        return true;
    }

public:
    template <typename Func>
    size_t drain(Func&& handler) noexcept
    {
        size_t count = 0;
        while (active_head_)
        {
            chunk* c = active_head_;
            uint32_t r = c->read;
            uint32_t w = c->write;
            while (r < w)
            {
                handler(c->slots[r]);
                if constexpr (!std::is_trivially_copyable_v<T>)
                {
                    c->slots[r].~T();
                }
                ++r;
                ++count;
            }
            active_head_ = c->next;
            traits::instance.release(c);
        }
        active_tail_ = nullptr;
        total_count_ = 0;
        return count;
    }

    template <typename Func>
    size_t drain_with_budget(size_t budget, Func&& handler) noexcept
    {
        size_t count = 0;
        while (count < budget && active_head_)
        {
            chunk* c = active_head_;
            uint32_t r = c->read;
            uint32_t w = c->write;
            while (count < budget && r < w)
            {
                handler(c->slots[r]);
                if constexpr (!std::is_trivially_copyable_v<T>)
                {
                    c->slots[r].~T();
                }
                ++r;
                ++count;
            }
            c->read = r;
            if (r == w) [[unlikely]]
            {
                active_head_ = c->next;
                traits::instance.release(c);
                if (!active_head_) [[unlikely]]
                {
                    active_tail_ = nullptr;
                }
            }
        }
        total_count_ -= count;
        return count;
    }

    [[nodiscard]] FORCE_INLINE const T* peek() const noexcept
    {
        chunk* h = active_head_;
        if (h && h->read < h->write) [[likely]]
        {
            return &h->slots[h->read];
        }
        return nullptr;
    }

    [[nodiscard]] bool pop() noexcept
    {
        chunk* h = active_head_;
        if (h) [[likely]]
        {
            uint32_t r = h->read;
            if (r < h->write) [[likely]]
            {
                if constexpr (!std::is_trivially_copyable_v<T>)
                {
                    h->slots[r].~T();
                }
                ++r;
                --total_count_;
                if (r == h->write) [[unlikely]]
                {
                    active_head_ = h->next;
                    traits::instance.release(h);
                    if (!active_head_) [[unlikely]]
                    {
                        active_tail_ = nullptr;
                    }
                }
                else
                {
                    h->read = r;
                }
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] bool empty() const noexcept { return total_count_ == 0; }
    [[nodiscard]] bool has_pending() const noexcept { return total_count_ != 0; }
    [[nodiscard]] size_t pending_count() const noexcept { return total_count_; }

    // 编译期最小保证容量 (实际无界)
    [[nodiscard]] static constexpr size_t capacity() noexcept { return N; }

    void clear() noexcept
    {
        if constexpr (std::is_trivially_copyable_v<T>)
        {
            // trivially copyable: 无需析构, 统计链长后整链拼接到池头
            if (active_head_)
            {
                size_t added = 1;
                chunk* tail_chunk = active_head_;
                while (tail_chunk->next)
                {
                    tail_chunk = tail_chunk->next;
                    ++added;
                }
                tail_chunk->next = traits::instance.head;
                traits::instance.head = active_head_;
                traits::instance.count += added;
                // 超出 MAX_FREE 则截断尾部并 delete
                while (traits::instance.count > traits::pool::MAX_FREE)
                {
                    chunk* old = traits::instance.head;
                    traits::instance.head = old->next;
                    ::operator delete(old);
                    --traits::instance.count;
                }
                active_head_ = nullptr;
                active_tail_ = nullptr;
                total_count_ = 0;
            }
        }
        else
        {
            while (active_head_)
            {
                chunk* next = active_head_->next;
                for (size_t i = active_head_->read; i < active_head_->write; ++i)
                {
                    active_head_->slots[i].~T();
                }
                traits::instance.release(active_head_);
                active_head_ = next;
            }
            active_tail_ = nullptr;
            total_count_ = 0;
        }
    }

    // 释放静态池所有缓存块 (内存紧张时调用)
    static void shrink_static_pool() noexcept
    {
        while (traits::instance.head)
        {
            chunk* next = traits::instance.head->next;
            ::operator delete(traits::instance.head);
            traits::instance.head = next;
        }
        traits::instance.count = 0;
    }

    [[nodiscard]] static size_t static_pool_size() noexcept
    {
        return traits::instance.count;
    }

    // 单块槽位数 (编译期常量)
    [[nodiscard]] static constexpr size_t slots_per_chunk() noexcept
    {
        return SLOTS;
    }
};
