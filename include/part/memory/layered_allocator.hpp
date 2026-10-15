#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <algorithm>
#include "slab_allocator.hpp"
#include "list_memory_pool.hpp"
#include "../force_inline.hpp"

namespace memory {

// 分层分配器: 小块走 slab, 大块走 list_memory_pool
class layered_allocator
{
private:
    // 4 档 slab (2 的幂)
    static constexpr size_t SLAB_COUNT = 4;
    static constexpr size_t SLAB_SIZES[SLAB_COUNT] = {16, 32, 64, 128};
    static constexpr size_t SLAB_MAX = 128;

    std::array<slab_allocator, SLAB_COUNT> slabs_;
    list_memory_pool big_pool_;

    // 无分支定位: 16→0, 17-32→1, 33-64→2, 65-128→3
    //   n<=16 显式返回 0, 避免 bit_width(n-1)-4 对 n=1..8 无符号下溢 (M10)
    [[nodiscard]] static constexpr size_t slab_index(size_t n) noexcept
    {
        if (n <= SLAB_SIZES[0])
        {
            return 0;
        }
        if (n <= SLAB_MAX)
        {
            return std::bit_width(n - 1) - 4;
        }
        return SLAB_COUNT;
    }

    // M03: 逐 slab 精确 owns 判定, 各 slab chunk 地址可交错, 不能用整体跨度二分
    //   owns 内部先 min/max 快速拒绝, 多数情况 O(1); 命中范围才二分 chunk
    [[nodiscard]] FORCE_INLINE size_t find_slab(const void* p) const noexcept
    {
        for (size_t i = 0; i < SLAB_COUNT; ++i)
        {
            if (slabs_[i].owns(p))
            {
                return i;
            }
        }
        return SLAB_COUNT;
    }

public:
    layered_allocator() noexcept
        : slabs_{
            slab_allocator(SLAB_SIZES[0]),
            slab_allocator(SLAB_SIZES[1]),
            slab_allocator(SLAB_SIZES[2]),
            slab_allocator(SLAB_SIZES[3]),
        }
        , big_pool_()
    {}

    ~layered_allocator() noexcept = default;

    layered_allocator(const layered_allocator&) = delete;
    layered_allocator& operator=(const layered_allocator&) = delete;
    layered_allocator(layered_allocator&&) noexcept = default;
    layered_allocator& operator=(layered_allocator&&) noexcept = default;

    // slab 失败自动降级到 big_pool
    [[nodiscard]] FORCE_INLINE void* allocate(size_t n) noexcept
    {
        if (n == 0) [[unlikely]]
        {
            return nullptr;
        }
        size_t idx = slab_index(n);
        if (idx < SLAB_COUNT) [[likely]]
        {
            void* p = slabs_[idx].allocate();
            if (p) [[likely]]
            {
                return p;
            }
            return big_pool_.allocate(n);   // M09: 回退块, 释放时经 owns 判定回大池
        }
        return big_pool_.allocate(n);
    }

    // 无大小提示时用 find_slab 定位
    FORCE_INLINE void deallocate(void* p) noexcept
    {
        if (!p) [[unlikely]]
        {
            return;
        }
        size_t idx = find_slab(p);
        if (idx < SLAB_COUNT) [[likely]]
        {
            slabs_[idx].deallocate(p);
            return;
        }
        big_pool_.soft_deallocate(p);
    }

    // 带大小提示: 按档位直查候选 slab, 经 owns 确认后释放
    //   slab 分配失败回退到大池的块不属 slab, owns 拦截后正确回大池 (M09)
    FORCE_INLINE void deallocate(void* p, size_t n) noexcept
    {
        if (!p) [[unlikely]]
        {
            return;
        }
        size_t idx = slab_index(n);
        if (idx < SLAB_COUNT) [[likely]]
        {
            if (slabs_[idx].owns(p)) [[likely]]
            {
                slabs_[idx].deallocate(p);
                return;
            }
        }
        big_pool_.soft_deallocate(p);
    }

    // 同档原位 / 跨层迁移
    [[nodiscard]] void* reallocate(void* p, size_t old_n, size_t new_n) noexcept
    {
        if (new_n == 0)
        {
            deallocate(p, old_n);
            return nullptr;
        }
        if (!p)
        {
            return allocate(new_n);
        }
        size_t old_idx = slab_index(old_n);
        size_t new_idx = slab_index(new_n);
        if (old_idx == new_idx && old_idx < SLAB_COUNT)
        {
            return p;
        }
        if (old_idx >= SLAB_COUNT && new_idx >= SLAB_COUNT)
        {
            if (big_pool_.reallocate_inplace(p, old_n, new_n))
            {
                return p;
            }
        }
        void* new_p = allocate(new_n);
        if (!new_p) [[unlikely]]
        {
            return nullptr;
        }
        size_t copy_size = old_n < new_n ? old_n : new_n;
        std::memcpy(new_p, p, copy_size);
        deallocate(p, old_n);
        return new_p;
    }

    // M05: 过度对齐类型 (alignof(T) > 16) 走 allocate_aligned, destroy 对称释放
    template <typename T, typename... Args>
    [[nodiscard]] FORCE_INLINE T* construct(Args&&... args) noexcept
    {
        void* ptr;
        if constexpr (alignof(T) > SLAB_SIZES[0])
        {
            ptr = allocate_aligned(sizeof(T), alignof(T));
        }
        else
        {
            ptr = allocate(sizeof(T));
        }
        if (!ptr) [[unlikely]]
        {
            return nullptr;
        }
        return new (ptr) T(std::forward<Args>(args)...);
    }

    template <typename T>
    FORCE_INLINE void destroy(T* ptr) noexcept
    {
        if (!ptr) [[unlikely]]
        {
            return;
        }
        ptr->~T();
        if constexpr (alignof(T) > SLAB_SIZES[0])
        {
            deallocate_aligned(ptr);
        }
        else
        {
            deallocate(ptr, sizeof(T));
        }
    }

    [[nodiscard]] bool owns(const void* p) const noexcept
    {
        size_t idx = find_slab(p);
        if (idx < SLAB_COUNT)
        {
            return true;
        }
        return big_pool_.owns(const_cast<void*>(p));
    }

    // 帧级回收: slab 重建自由链表, big_pool 释放全部
    void reset() noexcept
    {
        for (size_t i = 0; i < SLAB_COUNT; ++i)
        {
            slabs_[i].reset();
        }
        big_pool_.release_all_memory();
    }

    [[nodiscard]] size_t total_allocated_bytes() const noexcept
    {
        size_t total = big_pool_.total_allocated_bytes();
        for (size_t i = 0; i < SLAB_COUNT; ++i)
        {
            total += slabs_[i].used_bytes();
        }
        return total;
    }

    [[nodiscard]] size_t total_capacity_bytes() const noexcept
    {
        size_t total = big_pool_.total_capacity_bytes();
        for (size_t i = 0; i < SLAB_COUNT; ++i)
        {
            total += slabs_[i].total_bytes();
        }
        return total;
    }

    [[nodiscard]] FORCE_INLINE void* allocate_zeroed(size_t n) noexcept
    {
        if (n <= SLAB_MAX) [[likely]]
        {
            size_t idx = slab_index(n);
            if (idx < SLAB_COUNT)
            {
                void* p = slabs_[idx].allocate_zeroed();
                if (p) [[likely]]
                {
                    return p;
                }
            }
        }
        return big_pool_.allocate_zeroed(n);
    }

    [[nodiscard]] FORCE_INLINE bool reallocate_inplace(void* p, size_t old_n, size_t new_n) noexcept
    {
        if (!p) [[unlikely]]
        {
            return false;
        }
        size_t old_idx = slab_index(old_n);
        size_t new_idx = slab_index(new_n);
        if (old_idx == new_idx && old_idx < SLAB_COUNT)
        {
            return true;
        }
        if (old_idx >= SLAB_COUNT && new_idx >= SLAB_COUNT)
        {
            return big_pool_.reallocate_inplace(p, old_n, new_n);
        }
        return false;
    }

    // M04: 按 align 路由, 仅当请求对齐不超过 slab 档位实际保证时才走 slab
    //   slab 档位 i 块大小 SLAB_SIZES[i], 块按其大小对齐, 故可保证 min(SLAB_SIZES[idx], n 的 2 幂整除值)
    [[nodiscard]] void* allocate_aligned(size_t n, size_t align) noexcept
    {
        if (n == 0 || align == 0) [[unlikely]]
        {
            return nullptr;
        }
        align = std::bit_ceil(align);   // 对齐掩码要求 2 的幂
        if (n <= SLAB_MAX) [[likely]]
        {
            size_t idx = slab_index(n);
            if (idx < SLAB_COUNT && align <= SLAB_SIZES[idx]) [[likely]]
            {
                // 档位块大小是 2 的幂, 块按块大小对齐, 故满足 align <= 块大小
                void* p = slabs_[idx].allocate();
                if (p) [[likely]]
                {
                    return p;
                }
            }
        }
        return big_pool_.allocate_aligned(n, align);
    }

    void deallocate_aligned(void* p) noexcept
    {
        if (!p) [[unlikely]]
        {
            return;
        }
        size_t idx = find_slab(p);
        if (idx < SLAB_COUNT) [[likely]]
        {
            slabs_[idx].deallocate(p);
            return;
        }
        big_pool_.deallocate_aligned(p);
    }

    [[nodiscard]] size_t allocation_size(const void* p) const noexcept
    {
        if (!p) [[unlikely]]
        {
            return 0;
        }
        size_t idx = find_slab(p);
        if (idx < SLAB_COUNT)
        {
            return slabs_[idx].block_size();
        }
        return big_pool_.allocation_size(const_cast<void*>(p));
    }

    template <typename T, typename... Args>
    [[nodiscard]] FORCE_INLINE T* construct_array(size_t count, Args&&... args) noexcept
    {
        T* arr = static_cast<T*>(allocate(count * sizeof(T)));
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

    [[nodiscard]] size_t peak_allocated_bytes() const noexcept
    {
        size_t total = big_pool_.peak_allocated_bytes();
        for (size_t i = 0; i < SLAB_COUNT; ++i)
        {
            total += slabs_[i].peak_used_blocks() * slabs_[i].block_size();
        }
        return total;
    }

    struct layered_stats
    {
        size_t slab_used_bytes;
        size_t slab_total_bytes;
        size_t big_pool_used_bytes;
        size_t big_pool_total_bytes;
        size_t big_pool_peak_bytes;
    };

    [[nodiscard]] layered_stats stats() const noexcept
    {
        layered_stats s{};
        for (size_t i = 0; i < SLAB_COUNT; ++i)
        {
            s.slab_used_bytes += slabs_[i].used_bytes();
            s.slab_total_bytes += slabs_[i].total_bytes();
        }
        s.big_pool_used_bytes = big_pool_.total_allocated_bytes();
        s.big_pool_total_bytes = big_pool_.total_capacity_bytes();
        s.big_pool_peak_bytes = big_pool_.peak_allocated_bytes();
        return s;
    }

    [[nodiscard]] constexpr size_t slab_max() const noexcept { return SLAB_MAX; }
    [[nodiscard]] list_memory_pool& big_pool() noexcept { return big_pool_; }
    [[nodiscard]] const list_memory_pool& big_pool() const noexcept { return big_pool_; }
    [[nodiscard]] slab_allocator& slab(size_t i) noexcept { return slabs_[i]; }
    [[nodiscard]] const slab_allocator& slab(size_t i) const noexcept { return slabs_[i]; }
};

} // namespace memory
