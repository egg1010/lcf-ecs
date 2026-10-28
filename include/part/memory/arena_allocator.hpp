#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <new>
#include <utility>
#include "../dense.hpp"
#include "../force_inline.hpp"
#include "../operating_message.hpp"
#include "memory_common.hpp"

namespace memory {

// 线性分配器: 仅 save_point/restore/reset 整体回收, 无单个释放
class arena_allocator
{
private:
    struct alloc_record
    {
        size_t offset;
        size_t size;
    };

    uint8_t* cursor_{ nullptr };
    uint8_t* end_{ nullptr };
    uint8_t* base_{ nullptr };
    bool owns_memory_{ false };
    dense<alloc_record> allocations_;
    size_t live_bytes_{ 0 };
    size_t peak_live_bytes_{ 0 };
    size_t acquire_count_{ 0 };

    static constexpr size_t BASE_ALIGN = 64;

    [[nodiscard]] const alloc_record* find_record(size_t offset) const noexcept
    {
        size_t lo = 0;
        size_t hi = allocations_.size();
        while (lo < hi)
        {
            size_t mid = lo + (hi - lo) / 2;
            if (allocations_[mid].offset < offset)
            {
                lo = mid + 1;
            }
            else
            {
                hi = mid;
            }
        }
        if (lo >= allocations_.size())
        {
            return nullptr;
        }
        return allocations_[lo].offset == offset ? &allocations_[lo] : nullptr;
    }

public:
    constexpr arena_allocator() noexcept = default;

    explicit arena_allocator(size_t capacity) noexcept
        : cursor_(static_cast<uint8_t*>(
              ::operator new(capacity, std::align_val_t{ BASE_ALIGN }, std::nothrow)))
        , end_(cursor_ ? cursor_ + capacity : nullptr)
        , base_(cursor_)
        , owns_memory_(true)
    {}

    arena_allocator(void* buffer, size_t size) noexcept
        : cursor_(static_cast<uint8_t*>(buffer))
        , end_(buffer ? static_cast<uint8_t*>(buffer) + size : nullptr)
        , base_(cursor_)
        , owns_memory_(false)
    {}

    ~arena_allocator() noexcept
    {
        if (owns_memory_ && base_) [[likely]]
        {
            ::operator delete(base_, std::align_val_t{ BASE_ALIGN });
        }
    }

    arena_allocator(const arena_allocator&) = delete;
    arena_allocator& operator=(const arena_allocator&) = delete;

    arena_allocator(arena_allocator&& other) noexcept
    {
        swap(other);
    }

    arena_allocator& operator=(arena_allocator&& other) noexcept
    {
        if (this != &other) [[likely]]
        {
            if (owns_memory_ && base_) [[likely]]
            {
                ::operator delete(base_, std::align_val_t{ BASE_ALIGN });
            }
            cursor_ = nullptr;
            end_ = nullptr;
            base_ = nullptr;
            owns_memory_ = false;
            allocations_.clear();
            live_bytes_ = 0;
            peak_live_bytes_ = 0;
            acquire_count_ = 0;
            swap(other);
        }
        return *this;
    }

    void swap(arena_allocator& other) noexcept
    {
        std::swap(cursor_, other.cursor_);
        std::swap(end_, other.end_);
        std::swap(base_, other.base_);
        std::swap(owns_memory_, other.owns_memory_);
        allocations_.swap(other.allocations_);
        std::swap(live_bytes_, other.live_bytes_);
        std::swap(peak_live_bytes_, other.peak_live_bytes_);
        std::swap(acquire_count_, other.acquire_count_);
    }

    // === 分配 ===
    [[nodiscard]] LCF_FORCE_INLINE void* acquire(size_t bytes) noexcept
    {
        return acquire_aligned(bytes, base_alignment);
    }

    [[nodiscard]] LCF_FORCE_INLINE void* acquire(
        size_t bytes, operating_message& error,
        std::source_location site = std::source_location::current()) noexcept
    {
        void* p = acquire(bytes);
        if (!p) [[unlikely]]
        {
            error.write_failure(om_err_capacity_exceeded.code, msg_level::error, site);
        }
        return p;
    }

    [[nodiscard]] LCF_FORCE_INLINE void* acquire_zeroed(size_t bytes) noexcept
    {
        void* p = acquire(bytes);
        if (p) [[likely]]
        {
            std::memset(p, 0, bytes);
        }
        return p;
    }

    [[nodiscard]] void* acquire_aligned(size_t bytes, size_t alignment) noexcept
    {
        if (bytes == 0 || alignment == 0) [[unlikely]]
        {
            return nullptr;
        }
        if ((alignment & (alignment - 1)) != 0) [[unlikely]]
        {
            return nullptr;
        }
        if (alignment < base_alignment)
        {
            alignment = base_alignment;
        }
        if (!cursor_) [[unlikely]]
        {
            return nullptr;
        }
        const uintptr_t addr = reinterpret_cast<uintptr_t>(cursor_);
        const uintptr_t aligned = (addr + alignment - 1) & ~(static_cast<uintptr_t>(alignment) - 1);
        const size_t offset = static_cast<size_t>(aligned - reinterpret_cast<uintptr_t>(base_));
        const size_t capacity = static_cast<size_t>(end_ - base_);
        if (offset > capacity || bytes > capacity - offset) [[unlikely]]
        {
            return nullptr;
        }
        if (bytes > SIZE_MAX - offset) [[unlikely]]
        {
            return nullptr;
        }
        allocations_.emplace_back(alloc_record{ offset, bytes });
        cursor_ = base_ + offset + bytes;
        live_bytes_ += bytes;
        if (live_bytes_ > peak_live_bytes_) [[unlikely]]
        {
            peak_live_bytes_ = live_bytes_;
        }
        ++acquire_count_;
        return base_ + offset;
    }

    template <typename T, typename... Args>
    [[nodiscard]] LCF_FORCE_INLINE T* create(Args&&... args) noexcept
    {
        void* ptr = acquire_aligned(sizeof(T), alignof(T));
        if (!ptr) [[unlikely]]
        {
            return nullptr;
        }
        return new (ptr) T(std::forward<Args>(args)...);
    }

    template <typename T, typename... Args>
    [[nodiscard]] T* create_array(size_t count, Args&&... args) noexcept
    {
        if (count == 0 || count > SIZE_MAX / sizeof(T)) [[unlikely]]
        {
            return nullptr;
        }
        void* memory = acquire_aligned(count * sizeof(T), alignof(T));
        if (!memory) [[unlikely]]
        {
            return nullptr;
        }
        T* arr = static_cast<T*>(memory);
        for (size_t i = 0; i < count; ++i)
        {
            new (arr + i) T(args...);
        }
        return arr;
    }

    template <typename T>
    LCF_FORCE_INLINE void dispose(T* ptr) noexcept
    {
        if (ptr) [[likely]]
        {
            ptr->~T();
        }
    }

    template <typename T>
    LCF_FORCE_INLINE void dispose_array(T* ptr, size_t count) noexcept
    {
        if (ptr)
        {
            for (size_t i = count; i > 0; --i)
            {
                ptr[i - 1].~T();
            }
        }
    }

    // === 查询 ===
    [[nodiscard]] bool owns(const void* ptr) const noexcept
    {
        if (!ptr || !base_) [[unlikely]]
        {
            return false;
        }
        const uintptr_t up = reinterpret_cast<uintptr_t>(ptr);
        const uintptr_t begin = reinterpret_cast<uintptr_t>(base_);
        if (up < begin) [[unlikely]]
        {
            return false;
        }
        return find_record(static_cast<size_t>(up - begin)) != nullptr;
    }

    [[nodiscard]] memory_stats stats() const noexcept
    {
        memory_stats s{};
        s.reserved_bytes = capacity();
        s.live_bytes = live_bytes_;
        s.reusable_bytes = remaining();
        s.peak_live_bytes = peak_live_bytes_;
        s.acquire_count = acquire_count_;
        return s;
    }

    [[nodiscard]] bool empty() const noexcept
    {
        return live_bytes_ == 0;
    }

    // === 池管理 ===
    void reset_stats() noexcept
    {
        acquire_count_ = 0;
        peak_live_bytes_ = live_bytes_;
    }

    [[nodiscard]] LCF_FORCE_INLINE size_t save_point() const noexcept
    {
        return cursor_ ? static_cast<size_t>(cursor_ - base_) : 0;
    }

    // 恢复到保存的分配点 (子作用域回收)
    LCF_FORCE_INLINE void restore(size_t point) noexcept
    {
        if (!cursor_) [[unlikely]]
        {
            return;
        }
        const size_t used = static_cast<size_t>(cursor_ - base_);
        if (point > used) [[unlikely]]
        {
            return;
        }
        size_t keep = 0;
        while (keep < allocations_.size() && allocations_[keep].offset < point)
        {
            ++keep;
        }
        if (keep > 0)
        {
            const alloc_record& last_kept = allocations_[keep - 1];
            if (last_kept.offset > SIZE_MAX - last_kept.size ||
                last_kept.offset + last_kept.size > point) [[unlikely]]
            {
                return;
            }
        }
        while (allocations_.size() > keep)
        {
            live_bytes_ -= allocations_[allocations_.size() - 1].size;
            allocations_.pop_back();
        }
        cursor_ = base_ + point;
    }

    // 整体回收, 不调用析构, 不归还底层内存
    LCF_FORCE_INLINE void reset() noexcept
    {
        cursor_ = base_;
        allocations_.clear();
        live_bytes_ = 0;
    }

    // === 池专属查询 ===
    [[nodiscard]] constexpr size_t used() const noexcept
    {
        return cursor_ ? static_cast<size_t>(cursor_ - base_) : 0;
    }

    [[nodiscard]] constexpr size_t capacity() const noexcept
    {
        return end_ ? static_cast<size_t>(end_ - base_) : 0;
    }

    [[nodiscard]] constexpr size_t remaining() const noexcept
    {
        return end_ ? static_cast<size_t>(end_ - cursor_) : 0;
    }
};

inline void swap(arena_allocator& a, arena_allocator& b) noexcept
{
    a.swap(b);
}

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
    int current_{ 0 };

public:
    explicit double_buffered_arena(size_t capacity_per_frame) noexcept
        : arenas_{ arena_allocator(capacity_per_frame), arena_allocator(capacity_per_frame) }
    {}

    ~double_buffered_arena() noexcept = default;

    double_buffered_arena(const double_buffered_arena&) = delete;
    double_buffered_arena& operator=(const double_buffered_arena&) = delete;

    void flip() noexcept
    {
        current_ ^= 1;
        arenas_[current_].reset();
    }

    [[nodiscard]] LCF_FORCE_INLINE arena_allocator& current() noexcept
    {
        return arenas_[current_];
    }

    // 上一帧 (读取侧, 渲染线程仍在用)
    [[nodiscard]] LCF_FORCE_INLINE arena_allocator& previous() noexcept
    {
        return arenas_[current_ ^ 1];
    }
};

} // namespace memory
