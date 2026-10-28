#pragma once
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <bit>
#include "slab_allocator.hpp"
#include "list_memory_pool.hpp"
#include "../dense.hpp"
#include "../force_inline.hpp"
#include "memory_common.hpp"

namespace memory {

// 分层分配器: 小块走 slab, 大块走 list_memory_pool; 删除入口按归属层透传
class layered_allocator
{
private:
    static constexpr size_t SLAB_COUNT = 4;
    static constexpr size_t SLAB_SIZES[SLAB_COUNT] = { 16, 32, 64, 128 };
    static constexpr size_t SLAB_MAX = 128;

    struct slab_range
    {
        uintptr_t begin;
        uintptr_t end;
        slab_allocator::chunk_view view;
        size_t layer;
    };

    std::array<slab_allocator, SLAB_COUNT> slabs_;
    list_memory_pool big_pool_;
    dense<slab_range> slab_ranges_;
    std::array<size_t, SLAB_COUNT> indexed_chunk_counts_{};
    size_t live_bytes_{ 0 };
    size_t peak_live_bytes_{ 0 };

    // 16→0, 17-32→1, 33-64→2, 65-128→3
    [[nodiscard]] static constexpr size_t slab_index(size_t n) noexcept
    {
        if (n <= SLAB_SIZES[0])
        {
            return 0;
        }
        if (n <= SLAB_MAX)
        {
            return static_cast<size_t>(std::bit_width(n - 1)) - 4;
        }
        return SLAB_COUNT;
    }

    void account_live_delta(size_t before, size_t after) noexcept
    {
        if (after > before) [[likely]]
        {
            const size_t added = after - before;
            if (live_bytes_ > SIZE_MAX - added) [[unlikely]]
            {
                LCF_TRAP();
            }
            live_bytes_ += added;
            if (live_bytes_ > peak_live_bytes_) [[unlikely]]
            {
                peak_live_bytes_ = live_bytes_;
            }
        }
        else if (after < before)
        {
            const size_t removed = before - after;
            if (removed > live_bytes_) [[unlikely]]
            {
                LCF_TRAP();
            }
            live_bytes_ -= removed;
        }
    }

    void refresh_slab_ranges() noexcept
    {
        slab_ranges_.clear();
        for (size_t layer = 0; layer < SLAB_COUNT; ++layer)
        {
            const slab_allocator& slab = slabs_[layer];
            indexed_chunk_counts_[layer] = slab.chunks_.size();
            for (size_t i = 0; i < slab.chunks_.size(); ++i)
            {
                const slab_allocator::chunk_view view = slab_allocator::view_of(slab.chunks_[i]);
                slab_ranges_.emplace_back(slab_range{
                    reinterpret_cast<uintptr_t>(view.data),
                    reinterpret_cast<uintptr_t>(view.end),
                    view,
                    layer,
                });
            }
        }
        std::sort(slab_ranges_.begin(), slab_ranges_.end(),
                  [](const slab_range& a, const slab_range& b)
                  {
                      return a.begin < b.begin;
                  });
    }

    void sync_slab_ranges(size_t layer) noexcept
    {
        if (indexed_chunk_counts_[layer] != slabs_[layer].chunks_.size()) [[unlikely]]
        {
            refresh_slab_ranges();
        }
    }

    [[nodiscard]] const slab_range* find_slab_range(const void* p) const noexcept
    {
        const uintptr_t addr = reinterpret_cast<uintptr_t>(p);
        size_t lo = 0;
        size_t hi = slab_ranges_.size();
        while (lo < hi)
        {
            const size_t mid = lo + (hi - lo) / 2;
            if (slab_ranges_[mid].begin <= addr)
            {
                lo = mid + 1;
            }
            else
            {
                hi = mid;
            }
        }
        if (lo == 0) [[unlikely]]
        {
            return nullptr;
        }
        const slab_range& range = slab_ranges_[lo - 1];
        return addr < range.end ? &range : nullptr;
    }

    template <size_t MinSize>
    bool release_routed(void* ptr, bool hard) noexcept
    {
        if (!ptr) [[unlikely]]
        {
            return false;
        }
        if (const slab_range* range = find_slab_range(ptr))
        {
            slab_allocator& slab = slabs_[range->layer];
            const size_t before = slab.live_bytes_;
            const bool released = slab.release_in_chunk(range->view, ptr, MinSize, hard);
            account_live_delta(before, slab.live_bytes_);
            return released;
        }
        const size_t before = big_pool_.live_bytes_;
        bool released = false;
        if constexpr (MinSize > 0)
        {
            released = hard ? big_pool_.hard_delete_sized<MinSize>(ptr)
                            : big_pool_.soft_delete_sized<MinSize>(ptr);
        }
        else
        {
            released = hard ? big_pool_.hard_delete(ptr)
                            : big_pool_.soft_delete(ptr);
        }
        account_live_delta(before, big_pool_.live_bytes_);
        return released;
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

    layered_allocator(layered_allocator&& other) noexcept
        : layered_allocator()
    {
        slabs_.swap(other.slabs_);
        big_pool_.swap(other.big_pool_);
        slab_ranges_.swap(other.slab_ranges_);
        std::swap(indexed_chunk_counts_, other.indexed_chunk_counts_);
        std::swap(live_bytes_, other.live_bytes_);
        std::swap(peak_live_bytes_, other.peak_live_bytes_);
    }

    layered_allocator& operator=(layered_allocator&& other) noexcept
    {
        if (this != &other) [[likely]]
        {
            release_all();
            reset_stats();
            slabs_.swap(other.slabs_);
            big_pool_.swap(other.big_pool_);
            slab_ranges_.swap(other.slab_ranges_);
            std::swap(indexed_chunk_counts_, other.indexed_chunk_counts_);
            std::swap(live_bytes_, other.live_bytes_);
            std::swap(peak_live_bytes_, other.peak_live_bytes_);
        }
        return *this;
    }

    // === 分配 ===
    [[nodiscard]] FORCE_INLINE void* acquire(size_t bytes) noexcept
    {
        if (bytes == 0) [[unlikely]]
        {
            return nullptr;
        }
        const size_t idx = slab_index(bytes);
        if (idx < SLAB_COUNT) [[likely]]
        {
            slab_allocator& slab = slabs_[idx];
            const size_t before = slab.live_bytes_;
            void* p = slab.acquire();
            account_live_delta(before, slab.live_bytes_);
            if (p) [[likely]]
            {
                sync_slab_ranges(idx);
                return p;
            }
        }
        const size_t before = big_pool_.live_bytes_;
        void* p = big_pool_.acquire(bytes);
        account_live_delta(before, big_pool_.live_bytes_);
        return p;
    }

    [[nodiscard]] FORCE_INLINE void* acquire(
        size_t bytes, operating_message& error,
        std::source_location site = std::source_location::current()) noexcept
    {
        void* p = acquire(bytes);
        if (!p) [[unlikely]]
        {
            error.write_failure(om_err_out_of_memory.code, msg_level::error, site);
        }
        return p;
    }

    template <size_t Size>
    [[nodiscard]] FORCE_INLINE void* acquire_sized() noexcept
    {
        static_assert(Size > 0, "acquire_sized<Size> requires Size > 0");
        if constexpr (Size <= SLAB_MAX)
        {
            constexpr size_t idx = slab_index(Size);
            slab_allocator& slab = slabs_[idx];
            const size_t before = slab.live_bytes_;
            void* p = slab.acquire_sized<Size>();
            account_live_delta(before, slab.live_bytes_);
            if (p) [[likely]]
            {
                sync_slab_ranges(idx);
                return p;
            }
        }
        const size_t before = big_pool_.live_bytes_;
        void* p = big_pool_.acquire_sized<Size>();
        account_live_delta(before, big_pool_.live_bytes_);
        return p;
    }

    template <size_t Size>
    [[nodiscard]] FORCE_INLINE void* acquire_sized(
        operating_message& error,
        std::source_location site = std::source_location::current()) noexcept
    {
        void* p = acquire_sized<Size>();
        if (!p) [[unlikely]]
        {
            error.write_failure(om_err_out_of_memory.code, msg_level::error, site);
        }
        return p;
    }

    [[nodiscard]] FORCE_INLINE void* acquire_zeroed(size_t bytes) noexcept
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
        const size_t idx = slab_index(bytes);
        if (idx < SLAB_COUNT && alignment <= SLAB_SIZES[idx]) [[likely]]
        {
            slab_allocator& slab = slabs_[idx];
            const size_t before = slab.live_bytes_;
            void* p = slab.acquire();
            account_live_delta(before, slab.live_bytes_);
            if (p) [[likely]]
            {
                sync_slab_ranges(idx);
                return p;
            }
        }
        const size_t before = big_pool_.live_bytes_;
        void* p = big_pool_.acquire_aligned(bytes, alignment);
        account_live_delta(before, big_pool_.live_bytes_);
        return p;
    }

    template <typename T, typename... Args>
    [[nodiscard]] FORCE_INLINE T* create(Args&&... args) noexcept
    {
        void* ptr;
        if constexpr (alignof(T) > base_alignment)
        {
            ptr = acquire_aligned(sizeof(T), alignof(T));
        }
        else
        {
            ptr = acquire_sized<sizeof(T)>();
        }
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
        void* memory;
        if constexpr (alignof(T) > base_alignment)
        {
            memory = acquire_aligned(count * sizeof(T), alignof(T));
        }
        else
        {
            memory = acquire(count * sizeof(T));
        }
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

    // === 释放 ===
    bool soft_delete(void* ptr) noexcept
    {
        return release_routed<0>(ptr, false);
    }

    bool hard_delete(void* ptr) noexcept
    {
        return release_routed<0>(ptr, true);
    }

    template <size_t Size>
    bool soft_delete_sized(void* ptr) noexcept
    {
        static_assert(Size > 0, "soft_delete_sized<Size> requires Size > 0");
        return release_routed<Size>(ptr, false);
    }

    template <size_t Size>
    bool hard_delete_sized(void* ptr) noexcept
    {
        static_assert(Size > 0, "hard_delete_sized<Size> requires Size > 0");
        return release_routed<Size>(ptr, true);
    }

    bool soft_delete(void* ptr, operating_message& error,
                     std::source_location site = std::source_location::current()) noexcept
    {
        if (soft_delete(ptr))
        {
            return true;
        }
        error.write_failure((ptr == nullptr) ? om_err_null_pointer.code : om_err_not_found.code,
                            msg_level::warn, site);
        return false;
    }

    bool hard_delete(void* ptr, operating_message& error,
                     std::source_location site = std::source_location::current()) noexcept
    {
        if (hard_delete(ptr))
        {
            return true;
        }
        error.write_failure((ptr == nullptr) ? om_err_null_pointer.code : om_err_not_found.code,
                            msg_level::warn, site);
        return false;
    }

    template <typename T>
    FORCE_INLINE void dispose(T* ptr) noexcept
    {
        if (ptr) [[likely]]
        {
            ptr->~T();
        }
    }

    template <typename T>
    FORCE_INLINE void dispose_array(T* ptr, size_t count) noexcept
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
        if (!ptr) [[unlikely]]
        {
            return false;
        }
        if (const slab_range* range = find_slab_range(ptr))
        {
            return slabs_[range->layer].size_in_chunk(range->view, ptr) != 0;
        }
        return big_pool_.owns(ptr);
    }

    [[nodiscard]] size_t size_of(const void* ptr) const noexcept
    {
        if (!ptr) [[unlikely]]
        {
            return 0;
        }
        if (const slab_range* range = find_slab_range(ptr))
        {
            return slabs_[range->layer].size_in_chunk(range->view, ptr);
        }
        return big_pool_.size_of(ptr);
    }

    [[nodiscard]] memory_stats stats() const noexcept
    {
        memory_stats s{};
        for (size_t i = 0; i < SLAB_COUNT; ++i)
        {
            const memory_stats part = slabs_[i].stats();
            s.reserved_bytes += part.reserved_bytes;
            s.reusable_bytes += part.reusable_bytes;
            s.acquire_count += part.acquire_count;
            s.release_count += part.release_count;
            s.soft_delete_count += part.soft_delete_count;
            s.hard_delete_count += part.hard_delete_count;
        }
        const memory_stats big = big_pool_.stats();
        s.reserved_bytes += big.reserved_bytes;
        s.reusable_bytes += big.reusable_bytes;
        s.live_bytes = live_bytes_;
        s.peak_live_bytes = peak_live_bytes_;
        s.acquire_count += big.acquire_count;
        s.release_count += big.release_count;
        s.soft_delete_count += big.soft_delete_count;
        s.hard_delete_count += big.hard_delete_count;
        return s;
    }

    [[nodiscard]] bool empty() const noexcept
    {
        return live_bytes_ == 0;
    }

    // === 池管理 ===
    void reset_stats() noexcept
    {
        for (size_t i = 0; i < SLAB_COUNT; ++i)
        {
            slabs_[i].reset_stats();
        }
        big_pool_.reset_stats();
        peak_live_bytes_ = live_bytes_;
    }

    void reset() noexcept
    {
        for (size_t i = 0; i < SLAB_COUNT; ++i)
        {
            slabs_[i].reset();
        }
        big_pool_.reset();
        live_bytes_ = 0;
    }

    void release_all() noexcept
    {
        for (size_t i = 0; i < SLAB_COUNT; ++i)
        {
            slabs_[i].release_all();
        }
        big_pool_.release_all();
        slab_ranges_.reduce_capacity(0);
        indexed_chunk_counts_.fill(0);
        live_bytes_ = 0;
    }

    // === 重分配 ===
    [[nodiscard]] bool resize_inplace(void* ptr, size_t old_bytes, size_t new_bytes) noexcept
    {
        if (!ptr || new_bytes == 0) [[unlikely]]
        {
            return false;
        }
        if (const slab_range* range = find_slab_range(ptr))
        {
            slab_allocator& slab = slabs_[range->layer];
            if (slab.size_in_chunk(range->view, ptr) == 0) [[unlikely]]
            {
                return false;
            }
            const size_t capacity = slab.block_size();
            return old_bytes <= capacity && new_bytes <= capacity;
        }
        const size_t before = big_pool_.live_bytes_;
        const bool resized = big_pool_.resize_inplace(ptr, old_bytes, new_bytes);
        account_live_delta(before, big_pool_.live_bytes_);
        return resized;
    }

    [[nodiscard]] void* resize(void* ptr, size_t old_bytes, size_t new_bytes) noexcept
    {
        if (new_bytes == 0) [[unlikely]]
        {
            soft_delete(ptr);
            return nullptr;
        }
        if (!ptr) [[unlikely]]
        {
            return acquire(new_bytes);
        }
        if (const slab_range* range = find_slab_range(ptr))
        {
            slab_allocator& slab = slabs_[range->layer];
            const size_t capacity = slab.block_size();
            if (slab.size_in_chunk(range->view, ptr) == 0) [[unlikely]]
            {
                return nullptr;
            }
            if (old_bytes <= capacity && new_bytes <= capacity)
            {
                return ptr;
            }
            void* new_ptr = acquire_aligned(new_bytes, capacity);
            if (!new_ptr) [[unlikely]]
            {
                return nullptr;
            }
            size_t copy_size = old_bytes < new_bytes ? old_bytes : new_bytes;
            if (copy_size > capacity)
            {
                copy_size = capacity;
            }
            if (copy_size > 0)
            {
                std::memcpy(new_ptr, ptr, copy_size);
            }
            soft_delete(ptr);
            return new_ptr;
        }
        const size_t before = big_pool_.live_bytes_;
        void* resized = big_pool_.resize(ptr, old_bytes, new_bytes);
        account_live_delta(before, big_pool_.live_bytes_);
        return resized;
    }

    // === 池专属访问 ===
    [[nodiscard]] constexpr size_t slab_max() const noexcept { return SLAB_MAX; }
    [[nodiscard]] const list_memory_pool& big_pool() const noexcept { return big_pool_; }
    [[nodiscard]] const slab_allocator& slab(size_t i) const noexcept { return slabs_[i]; }
};

} // namespace memory
