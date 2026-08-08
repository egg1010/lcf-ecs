#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <new>
#include <utility>
#include "../force_inline.hpp"

namespace memory {

// 线性分配器: 仅 reset/restore 整体回收, 无单个 deallocate
class arena_allocator
{
private:
    uint8_t* cursor_{nullptr};
    uint8_t* end_{nullptr};
    uint8_t* base_{nullptr};
    bool owns_memory_{false};
    size_t peak_used_{0};
    size_t allocation_count_{0};

    static constexpr size_t DEFAULT_ALIGN = 16;
    static constexpr size_t BASE_ALIGN = 64;

public:
    constexpr arena_allocator() noexcept = default;

    explicit arena_allocator(size_t capacity) noexcept
        : cursor_(static_cast<uint8_t*>(
              ::operator new(capacity, std::align_val_t{BASE_ALIGN}, std::nothrow)))
        , end_(cursor_ + capacity)
        , base_(cursor_)
        , owns_memory_(true)
    {}

    arena_allocator(void* buffer, size_t size) noexcept
        : cursor_(static_cast<uint8_t*>(buffer))
        , end_(cursor_ + size)
        , base_(cursor_)
        , owns_memory_(false)
    {}

    ~arena_allocator() noexcept
    {
        if (owns_memory_ && base_) [[likely]]
        {
            ::operator delete(base_, std::align_val_t{BASE_ALIGN});
        }
    }

    arena_allocator(const arena_allocator&) = delete;
    arena_allocator& operator=(const arena_allocator&) = delete;

    arena_allocator(arena_allocator&& other) noexcept
        : cursor_(other.cursor_)
        , end_(other.end_)
        , base_(other.base_)
        , owns_memory_(other.owns_memory_)
    {
        other.cursor_ = nullptr;
        other.end_ = nullptr;
        other.base_ = nullptr;
        other.owns_memory_ = false;
    }

    arena_allocator& operator=(arena_allocator&& other) noexcept
    {
        if (this != &other) [[likely]]
        {
            if (owns_memory_ && base_) [[likely]]
            {
                ::operator delete(base_, std::align_val_t{BASE_ALIGN});
            }
            cursor_ = other.cursor_;
            end_ = other.end_;
            base_ = other.base_;
            owns_memory_ = other.owns_memory_;
            other.cursor_ = nullptr;
            other.end_ = nullptr;
            other.base_ = nullptr;
            other.owns_memory_ = false;
        }
        return *this;
    }

    // 空指针/零字节由边界检查自然兜底, 无需额外分支
    [[nodiscard]] FORCE_INLINE void* allocate(size_t n, size_t align = DEFAULT_ALIGN) noexcept
    {
        uintptr_t addr = reinterpret_cast<uintptr_t>(cursor_);
        uintptr_t aligned = (addr + align - 1) & ~(static_cast<uintptr_t>(align) - 1);
        uint8_t* p = reinterpret_cast<uint8_t*>(aligned);
        uint8_t* next = p + n;
        if (next > end_) [[unlikely]]
        {
            return nullptr;
        }
        cursor_ = next;
        ++allocation_count_;
        size_t cur_used = static_cast<size_t>(cursor_ - base_);
        if (cur_used > peak_used_) [[unlikely]]
        {
            peak_used_ = cur_used;
        }
        return p;
    }

    [[nodiscard]] FORCE_INLINE void* allocate_zeroed(size_t n, size_t align = DEFAULT_ALIGN) noexcept
    {
        void* p = allocate(n, align);
        if (p) [[likely]]
        {
            std::memset(p, 0, n);
        }
        return p;
    }

    template <typename T>
    [[nodiscard]] FORCE_INLINE T* allocate_array(size_t count, size_t align = alignof(T)) noexcept
    {
        return static_cast<T*>(allocate(count * sizeof(T), align));
    }

    template <typename T, typename... Args>
    [[nodiscard]] FORCE_INLINE T* construct(Args&&... args) noexcept
    {
        void* ptr = allocate(sizeof(T), alignof(T));
        if (!ptr) [[unlikely]]
        {
            return nullptr;
        }
        return new (ptr) T(std::forward<Args>(args)...);
    }

    template <typename T, typename... Args>
    [[nodiscard]] FORCE_INLINE T* construct_array(size_t count, Args&&... args) noexcept
    {
        T* arr = allocate_array<T>(count);
        if (!arr) [[unlikely]]
        {
            return nullptr;
        }
        for (size_t i = 0; i < count; ++i)
        {
            new (arr + i) T(std::forward<Args>(args)...);
        }
        return arr;
    }

    template <typename T>
    FORCE_INLINE void destroy(T* p) noexcept
    {
        if (p) [[likely]]
        {
            p->~T();
        }
    }

    // 无单个 deallocate, 仅 reset/restore 整体回收
    // deallocate 为 no-op, 仅用于泛型代码统一调用
    FORCE_INLINE void deallocate(void* /*p*/) noexcept {}
    FORCE_INLINE void deallocate(void* /*p*/, size_t /*n*/) noexcept {}

    [[nodiscard]] FORCE_INLINE size_t save_point() const noexcept
    {
        return static_cast<size_t>(cursor_ - base_);
    }

    // 恢复到保存的分配点 (子作用域回收)
    FORCE_INLINE void restore(size_t point) noexcept
    {
        uint8_t* target = base_ + point;
        if (target <= cursor_) [[likely]]
        {
            cursor_ = target;
        }
    }

    // 整体回收, 不调用析构
    FORCE_INLINE void reset() noexcept
    {
        cursor_ = base_;
    }

    [[nodiscard]] constexpr size_t used() const noexcept { return static_cast<size_t>(cursor_ - base_); }
    [[nodiscard]] constexpr size_t capacity() const noexcept { return static_cast<size_t>(end_ - base_); }
    [[nodiscard]] constexpr size_t remaining() const noexcept { return static_cast<size_t>(end_ - cursor_); }
    [[nodiscard]] constexpr bool empty() const noexcept { return cursor_ == base_; }

    [[nodiscard]] constexpr size_t peak_used() const noexcept { return peak_used_; }
    [[nodiscard]] constexpr size_t allocation_count() const noexcept { return allocation_count_; }

    [[nodiscard]] bool owns(const void* p) const noexcept
    {
        const uint8_t* up = static_cast<const uint8_t*>(p);
        return up >= base_ && up < end_;
    }
};

// RAII 作用域守卫: 构造 save_point, 析构 restore
class arena_scope
{
private:
    arena_allocator& arena_;
    size_t saved_;

public:
    explicit arena_scope(arena_allocator& a) noexcept
        : arena_(a)
        , saved_(a.save_point())
    {}

    ~arena_scope() noexcept
    {
        arena_.restore(saved_);
    }

    arena_scope(const arena_scope&) = delete;
    arena_scope& operator=(const arena_scope&) = delete;
};

// 双帧交替 arena: flip() 切换并 reset 新帧, 旧帧数据仍可安全读取
class double_buffered_arena
{
private:
    arena_allocator arenas_[2];
    int current_{0};

public:
    explicit double_buffered_arena(size_t capacity_per_frame) noexcept
        : arenas_{arena_allocator(capacity_per_frame), arena_allocator(capacity_per_frame)}
    {}

    ~double_buffered_arena() noexcept = default;

    double_buffered_arena(const double_buffered_arena&) = delete;
    double_buffered_arena& operator=(const double_buffered_arena&) = delete;

    void flip() noexcept
    {
        current_ ^= 1;
        arenas_[current_].reset();
    }

    [[nodiscard]] FORCE_INLINE arena_allocator& current() noexcept
    {
        return arenas_[current_];
    }

    // 上一帧 (读取侧, 渲染线程仍在用)
    [[nodiscard]] FORCE_INLINE arena_allocator& previous() noexcept
    {
        return arenas_[current_ ^ 1];
    }
};

} // namespace memory
