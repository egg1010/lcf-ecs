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

    // slab 地址区间缓存, 供 find_slab 二分查找
    struct addr_range
    {
        const uint8_t* min;
        const uint8_t* max;
        uint8_t index;
    };
    alignas(64) std::array<addr_range, SLAB_COUNT> ranges_{};
    size_t range_count_{0};
    bool ranges_dirty_{true};

    // 无分支定位: 16→0, 17-32→1, 33-64→2, 65-128→3
    [[nodiscard]] static constexpr size_t slab_index(size_t n) noexcept
    {
        if (n <= SLAB_MAX)
        {
            return std::bit_width(n - 1) - 4;
        }
        return SLAB_COUNT;
    }

    void refresh_ranges() const noexcept
    {
        auto* self = const_cast<layered_allocator*>(this);
        for (size_t i = 0; i < SLAB_COUNT; ++i)
        {
            self->ranges_[i].min = slabs_[i].min_addr();
            self->ranges_[i].max = slabs_[i].max_addr();
            self->ranges_[i].index = static_cast<uint8_t>(i);
        }
        std::sort(self->ranges_.begin(), self->ranges_.end(),
            [](const addr_range& a, const addr_range& b) { return a.min < b.min; });
        self->range_count_ = SLAB_COUNT;
        self->ranges_dirty_ = false;
    }

    [[nodiscard]] FORCE_INLINE size_t find_slab(const void* p) const noexcept
    {
        const uint8_t* up = static_cast<const uint8_t*>(p);
        if (ranges_dirty_) [[unlikely]]
        {
            refresh_ranges();
        }
        size_t lo = 0, hi = range_count_;
        while (lo < hi)
        {
            size_t mid = lo + (hi - lo) / 2;
            if (ranges_[mid].max <= up)
            {
                lo = mid + 1;
            }
            else
            {
                hi = mid;
            }
        }
        if (lo < range_count_)
        {
            const auto& r = ranges_[lo];
            if (up >= r.min && up < r.max)
            {
                return r.index;
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
                ranges_dirty_ = true;
                return p;
            }
            return big_pool_.allocate(n);
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

    // 带大小提示, O(1) 定位
    FORCE_INLINE void deallocate(void* p, size_t n) noexcept
    {
        if (!p) [[unlikely]]
        {
            return;
        }
        size_t idx = slab_index(n);
        if (idx < SLAB_COUNT) [[likely]]
        {
            slabs_[idx].deallocate(p);
            return;
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

    template <typename T, typename... Args>
    [[nodiscard]] FORCE_INLINE T* construct(Args&&... args) noexcept
    {
        void* ptr = allocate(sizeof(T));
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
        deallocate(ptr, sizeof(T));
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
        ranges_dirty_ = true;
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
                    ranges_dirty_ = true;
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

    // slab 块固定 16/32/64/128 对齐, align <= SLAB_MAX 且为 2 的幂时 slab 可满足
    [[nodiscard]] void* allocate_aligned(size_t n, size_t align) noexcept
    {
        if (n == 0 || align == 0) [[unlikely]]
        {
            return nullptr;
        }
        if (n <= SLAB_MAX && align <= SLAB_MAX) [[likely]]
        {
            return allocate(n);
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
