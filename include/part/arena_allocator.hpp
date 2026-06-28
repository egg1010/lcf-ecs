#pragma once
#include <cstddef>
#include <cstdint>
#include <new>
#include "force_inline.hpp"

// 线性 bump 分配器: 无 header, 无单个 deallocate, 仅 reset
// 两种模式: 自有内存(析构释放) / 借用外部 buffer(零所有权)
class arena_allocator
{
private:
    uint8_t* base_{nullptr};
    size_t offset_{0};
    size_t capacity_{0};
    bool owns_memory_{false};

    static constexpr size_t DEFAULT_ALIGN = 16;
    // base_ 对齐: cache line(64), 覆盖 AVX2(32)/AVX512(64), 与 class_pool 一致
    static constexpr size_t BASE_ALIGN = 64;

public:
    constexpr arena_allocator() noexcept = default;

    // 自有模式: 分配 capacity 字节, 析构时释放
    // base_ 64 字节对齐, 使 allocate(n, align) 支持 align <= 64
    explicit arena_allocator(size_t capacity) noexcept
        : base_(static_cast<uint8_t*>(
              ::operator new(capacity, std::align_val_t{BASE_ALIGN}, std::nothrow)))
        , offset_(0)
        , capacity_(capacity)
        , owns_memory_(true)
    {}

    // 借用模式: 使用外部 buffer, 不分配不释放
    arena_allocator(void* buffer, size_t size) noexcept
        : base_(static_cast<uint8_t*>(buffer))
        , offset_(0)
        , capacity_(size)
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
        : base_(other.base_)
        , offset_(other.offset_)
        , capacity_(other.capacity_)
        , owns_memory_(other.owns_memory_)
    {
        other.base_ = nullptr;
        other.offset_ = 0;
        other.capacity_ = 0;
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
            base_ = other.base_;
            offset_ = other.offset_;
            capacity_ = other.capacity_;
            owns_memory_ = other.owns_memory_;
            other.base_ = nullptr;
            other.offset_ = 0;
            other.capacity_ = 0;
            other.owns_memory_ = false;
        }
        return *this;
    }

    // bump 分配, 位运算对齐, 无分支
    [[nodiscard]] FORCE_INLINE void* allocate(size_t n, size_t align = DEFAULT_ALIGN) noexcept
    {
        if (n == 0 || !base_) [[unlikely]]
        {
            return nullptr;
        }
        size_t aligned = (offset_ + align - 1) & ~(align - 1);
        if (aligned + n > capacity_) [[unlikely]]
        {
            return nullptr;
        }
        void* p = base_ + aligned;
        offset_ = aligned + n;
        return p;
    }

    // 整体回收, 不调用析构
    FORCE_INLINE void reset() noexcept
    {
        offset_ = 0;
    }

    [[nodiscard]] constexpr size_t used() const noexcept { return offset_; }
    [[nodiscard]] constexpr size_t capacity() const noexcept { return capacity_; }
    [[nodiscard]] constexpr size_t remaining() const noexcept { return capacity_ - offset_; }
    [[nodiscard]] constexpr bool empty() const noexcept { return offset_ == 0; }

    [[nodiscard]] bool owns(const void* p) const noexcept
    {
        const uint8_t* up = static_cast<const uint8_t*>(p);
        return up >= base_ && up < base_ + capacity_;
    }
};
