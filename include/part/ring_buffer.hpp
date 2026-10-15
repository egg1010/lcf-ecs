#pragma once

#include <cstddef>
#include <cstdint>
#include <new>
#include <type_traits>
#include <utility>
#include "force_inline.hpp"
#include "memory/oom_handler.hpp"

template <typename T, size_t N>
class ring_buffer
{
    static constexpr size_t ceil_pow2(size_t n) noexcept
    {
        size_t p = 1;
        while (p < n)
        {
            p <<= 1;
        }
        return p;
    }

public:
    static constexpr size_t CAP = ceil_pow2(N);
    static constexpr size_t ALIGN = alignof(T) < 16 ? 16 : alignof(T);

private:
    static_assert(N >= 1, "ring_buffer capacity must be >= 1");
    static_assert(CAP <= static_cast<size_t>(-1) / sizeof(T), "ring_buffer byte size overflow");

    T* slots_;
    size_t head_{0};
    size_t tail_{0};
    size_t count_{0};
    uint64_t dropped_{0};

    [[nodiscard]] FORCE_INLINE size_t next_index(size_t i) const noexcept
    {
        return (i + 1) & (CAP - 1);
    }

    FORCE_INLINE void destroy_at(size_t i) noexcept
    {
        if constexpr (!std::is_trivially_destructible_v<T>)
        {
            slots_[i].~T();
        }
    }

    // trivially_copyable 直接赋值避免 placement new 的副作用屏障 (否则热路径寄存器失效)
    FORCE_INLINE void store_at(size_t i, const T& v) noexcept
    {
        if constexpr (std::is_trivially_copyable_v<T>)
        {
            slots_[i] = v;
        }
        else
        {
            new (&slots_[i]) T(v);
        }
    }

    FORCE_INLINE void store_at(size_t i, T&& v) noexcept
    {
        if constexpr (std::is_trivially_copyable_v<T>)
        {
            slots_[i] = v;
        }
        else
        {
            new (&slots_[i]) T(std::move(v));
        }
    }

    [[nodiscard]] static T* allocate_slots() noexcept
    {
        constexpr size_t bytes = CAP * sizeof(T);
        void* p = ::operator new(bytes, std::align_val_t{ALIGN}, std::nothrow);
        if (!p) [[unlikely]]
        {
            memory::handle_oom(bytes, "ring_buffer::ring_buffer");
        }
        return static_cast<T*>(p);
    }

public:
    ring_buffer() noexcept
        : slots_(allocate_slots())
    {}

    // 手写移动: 默认复制裸指针会造成双释放
    ring_buffer(ring_buffer&& o) noexcept
        : slots_(o.slots_)
        , head_(o.head_)
        , tail_(o.tail_)
        , count_(o.count_)
        , dropped_(o.dropped_)
    {
        o.slots_ = nullptr;
        o.head_ = 0;
        o.tail_ = 0;
        o.count_ = 0;
        o.dropped_ = 0;
    }

    ring_buffer& operator=(ring_buffer&& o) noexcept
    {
        if (this != &o) [[likely]]
        {
            clear();
            ::operator delete(slots_, CAP * sizeof(T), std::align_val_t{ALIGN});
            slots_ = o.slots_;
            head_ = o.head_;
            tail_ = o.tail_;
            count_ = o.count_;
            dropped_ = o.dropped_;
            o.slots_ = nullptr;
            o.head_ = 0;
            o.tail_ = 0;
            o.count_ = 0;
            o.dropped_ = 0;
        }
        return *this;
    }

    ring_buffer(const ring_buffer&) = delete;
    ring_buffer& operator=(const ring_buffer&) = delete;

    ~ring_buffer() noexcept
    {
        clear();
        if (slots_) [[likely]]
        {
            ::operator delete(slots_, CAP * sizeof(T), std::align_val_t{ALIGN});
        }
    }

    [[nodiscard]] FORCE_INLINE bool try_push(const T& v) noexcept
    {
        if (count_ == CAP) [[unlikely]]
        {
            return false;
        }
        store_at(tail_, v);
        tail_ = next_index(tail_);
        ++count_;
        return true;
    }

    [[nodiscard]] FORCE_INLINE bool try_push(T&& v) noexcept
    {
        if (count_ == CAP) [[unlikely]]
        {
            return false;
        }
        store_at(tail_, std::move(v));
        tail_ = next_index(tail_);
        ++count_;
        return true;
    }

    template <typename... Args>
    [[nodiscard]] FORCE_INLINE T* try_emplace(Args&&... args) noexcept
    {
        if (count_ == CAP) [[unlikely]]
        {
            return nullptr;
        }
        T* slot = &slots_[tail_];
        new (slot) T(std::forward<Args>(args)...);
        tail_ = next_index(tail_);
        ++count_;
        return slot;
    }

    // 满则覆盖最旧, 返回本次覆盖条数 (0 或 1)
    [[nodiscard]] FORCE_INLINE size_t push_overwrite(const T& v) noexcept
    {
        size_t overwritten = overwrite_oldest_if_full();
        store_at(tail_, v);
        tail_ = next_index(tail_);
        ++count_;
        return overwritten;
    }

    [[nodiscard]] FORCE_INLINE size_t push_overwrite(T&& v) noexcept
    {
        size_t overwritten = overwrite_oldest_if_full();
        store_at(tail_, std::move(v));
        tail_ = next_index(tail_);
        ++count_;
        return overwritten;
    }

    template <typename... Args>
    [[nodiscard]] FORCE_INLINE T* emplace_overwrite(Args&&... args) noexcept
    {
        overwrite_oldest_if_full();
        T* slot = &slots_[tail_];
        new (slot) T(std::forward<Args>(args)...);
        tail_ = next_index(tail_);
        ++count_;
        return slot;
    }

    [[nodiscard]] FORCE_INLINE T* front() noexcept
    {
        return count_ ? &slots_[head_] : nullptr;
    }

    [[nodiscard]] FORCE_INLINE const T* front() const noexcept
    {
        return count_ ? &slots_[head_] : nullptr;
    }

    [[nodiscard]] FORCE_INLINE bool pop() noexcept
    {
        if (!count_) [[unlikely]]
        {
            return false;
        }
        destroy_at(head_);
        head_ = next_index(head_);
        --count_;
        return true;
    }

    [[nodiscard]] FORCE_INLINE bool try_pop(T& out) noexcept
    {
        if (!count_) [[unlikely]]
        {
            return false;
        }
        out = std::move(slots_[head_]);
        destroy_at(head_);
        head_ = next_index(head_);
        --count_;
        return true;
    }

    // drain 回调内 try_push 合法 (新元素进入本轮尾部); 禁止回调内调覆盖版入队
    template <typename Func>
    size_t drain(Func&& handler) noexcept
    {
        size_t consumed = 0;
        while (count_) [[likely]]
        {
            handler(slots_[head_]);
            destroy_at(head_);
            head_ = next_index(head_);
            --count_;
            ++consumed;
        }
        return consumed;
    }

    template <typename Func>
    size_t drain_with_budget(size_t budget, Func&& handler) noexcept
    {
        size_t consumed = 0;
        while (consumed < budget && count_) [[likely]]
        {
            handler(slots_[head_]);
            destroy_at(head_);
            head_ = next_index(head_);
            --count_;
            ++consumed;
        }
        return consumed;
    }

    template <typename Func>
    size_t visit(Func&& handler) const noexcept
    {
        size_t visited = 0;
        size_t idx = head_;
        for (size_t i = 0; i < count_; ++i)
        {
            handler(slots_[idx]);
            idx = next_index(idx);
            ++visited;
        }
        return visited;
    }

    [[nodiscard]] FORCE_INLINE size_t front_run(T*& out_ptr, size_t& out_len) noexcept
    {
        if (!count_) [[unlikely]]
        {
            out_len = 0;
            return 0;
        }
        out_ptr = &slots_[head_];
        out_len = (CAP - head_ < count_) ? (CAP - head_) : count_;
        return out_len;
    }

    FORCE_INLINE size_t pop_run(size_t n) noexcept
    {
        if (n > count_) [[unlikely]]
        {
            n = count_;
        }
        for (size_t i = 0; i < n; ++i)
        {
            destroy_at(head_);
            head_ = next_index(head_);
        }
        count_ -= n;
        return n;
    }

    [[nodiscard]] FORCE_INLINE size_t size() const noexcept { return count_; }
    [[nodiscard]] FORCE_INLINE bool empty() const noexcept { return count_ == 0; }
    [[nodiscard]] static constexpr size_t capacity() noexcept { return CAP; }
    [[nodiscard]] FORCE_INLINE uint64_t dropped_count() const noexcept { return dropped_; }

    void clear() noexcept
    {
        pop_run(count_);
    }

private:
    FORCE_INLINE size_t overwrite_oldest_if_full() noexcept
    {
        if (count_ == CAP) [[unlikely]]
        {
            destroy_at(head_);
            head_ = next_index(head_);
            --count_;
            ++dropped_;
            return 1;
        }
        return 0;
    }
};
