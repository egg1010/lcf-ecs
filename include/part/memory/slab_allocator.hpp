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

// 固定块大小分配器: 软删除与硬删除效果相同, 块回到固定大小空闲链
class slab_allocator
{
private:
    friend class layered_allocator;

    struct chunk_control
    {
        uint8_t* data;
        uint64_t* used;
    };

    struct chunk_node
    {
        uint8_t* data;
        uint8_t* end;
        chunk_control* control;
        size_t alloc_size;
    };

    struct chunk_view
    {
        uint8_t* data;
        uint8_t* end;
        chunk_control* control;
    };

    [[nodiscard]] static chunk_view view_of(const chunk_node& c) noexcept
    {
        return chunk_view{ c.data, c.end, c.control };
    }

    void* free_list_head_{ nullptr };
    dense<chunk_node> chunks_;
    size_t block_size_{ 0 };
    size_t alignment_{ base_alignment };
    size_t blocks_per_chunk_{ 0 };
    size_t bitmap_bytes_{ 0 };
    size_t total_blocks_{ 0 };
    size_t free_blocks_{ 0 };
    size_t reserved_bytes_{ 0 };
    size_t live_bytes_{ 0 };
    size_t peak_live_bytes_{ 0 };
    size_t acquire_count_{ 0 };
    size_t release_count_{ 0 };
    size_t soft_delete_count_{ 0 };
    size_t hard_delete_count_{ 0 };

    static constexpr size_t DEFAULT_BLOCKS_PER_CHUNK = 256;

    // 块地址 = chunk 基址 + j*block_size_; 基址对齐到此值时所有块均满足该对齐
    [[nodiscard]] static constexpr size_t block_alignment(size_t block_size) noexcept
    {
        return block_size & (~block_size + 1);
    }

    [[nodiscard]] static constexpr size_t bitmap_bytes_for(size_t blocks) noexcept
    {
        return ((blocks + 63) / 64) * sizeof(uint64_t);
    }

    [[nodiscard]] static bool bit_test(const chunk_control* control, size_t idx) noexcept
    {
        return (control->used[idx >> 6] >> (idx & 63)) & 1U;
    }

    static void bit_set(const chunk_control* control, size_t idx) noexcept
    {
        control->used[idx >> 6] |= (uint64_t{ 1 } << (idx & 63));
    }

    static void bit_clear(const chunk_control* control, size_t idx) noexcept
    {
        control->used[idx >> 6] &= ~(uint64_t{ 1 } << (idx & 63));
    }

    [[nodiscard]] const chunk_node* find_chunk(const void* p) const noexcept
    {
        if (!p || chunks_.empty()) [[unlikely]]
        {
            return nullptr;
        }
        const uint8_t* up = static_cast<const uint8_t*>(p);
        size_t lo = 0;
        size_t hi = chunks_.size();
        const uintptr_t addr = reinterpret_cast<uintptr_t>(up);
        while (lo < hi)
        {
            size_t mid = lo + (hi - lo) / 2;
            if (reinterpret_cast<uintptr_t>(chunks_[mid].data) <= addr)
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
        const chunk_node& c = chunks_[lo - 1];
        return (addr >= reinterpret_cast<uintptr_t>(c.data) &&
                addr < reinterpret_cast<uintptr_t>(c.end)) ? &c : nullptr;
    }

    // 仅存活块起始地址返回有效索引
    [[nodiscard]] static size_t live_index_in(const chunk_view& c, const void* p,
                                              size_t block_size) noexcept
    {
        const uintptr_t begin = reinterpret_cast<uintptr_t>(c.data);
        const uintptr_t end = reinterpret_cast<uintptr_t>(c.end);
        const uintptr_t addr = reinterpret_cast<uintptr_t>(p);
        if (addr < begin || addr >= end) [[unlikely]]
        {
            return SIZE_MAX;
        }
        const uintptr_t offset = addr - begin;
        if (offset % block_size != 0)
        {
            return SIZE_MAX;
        }
        const size_t idx = static_cast<size_t>(offset / block_size);
        if (idx >= static_cast<size_t>((end - begin) / block_size))
        {
            return SIZE_MAX;
        }
        return bit_test(c.control, idx) ? idx : SIZE_MAX;
    }

    [[nodiscard]] size_t size_in_chunk(const chunk_view& c, const void* p) const noexcept
    {
        return live_index_in(c, p, block_size_) != SIZE_MAX ? block_size_ : 0;
    }

    [[nodiscard]] bool release_in_chunk(const chunk_view& c, void* p, size_t min_size,
                                        bool hard) noexcept
    {
        const size_t idx = live_index_in(c, p, block_size_);
        if (idx == SIZE_MAX || block_size_ < min_size) [[unlikely]]
        {
            return false;
        }
        bit_clear(c.control, idx);
        live_bytes_ -= block_size_;
        ++release_count_;
        if (hard)
        {
            ++hard_delete_count_;
        }
        else
        {
            ++soft_delete_count_;
        }
        auto* node = static_cast<void**>(p);
        node[0] = free_list_head_;
        node[1] = c.control;
        free_list_head_ = p;
        ++free_blocks_;
        return true;
    }

    void grow() noexcept
    {
        if (block_size_ == 0 || blocks_per_chunk_ == 0) [[unlikely]]
        {
            return;
        }
        const size_t chunk_size = block_size_ * blocks_per_chunk_;
        const size_t chunk_align = block_alignment(block_size_);
        const size_t control_bytes = sizeof(chunk_control) + bitmap_bytes_;
        uint8_t* data = static_cast<uint8_t*>(
            ::operator new(chunk_size + control_bytes,
                          std::align_val_t{ chunk_align }, std::nothrow));
        if (!data) [[unlikely]]
        {
            return;
        }

        auto* control = reinterpret_cast<chunk_control*>(data + chunk_size);
        control->data = data;
        control->used = reinterpret_cast<uint64_t*>(
            data + chunk_size + sizeof(chunk_control));
        std::memset(control->used, 0, bitmap_bytes_);

        chunks_.emplace_back(data, data + chunk_size, control, chunk_size + control_bytes);
        size_t pos = chunks_.size() - 1;
        while (pos > 0 &&
               reinterpret_cast<uintptr_t>(chunks_[pos - 1].data) >
                   reinterpret_cast<uintptr_t>(chunks_[pos].data))
        {
            chunk_node tmp = chunks_[pos - 1];
            chunks_[pos - 1] = chunks_[pos];
            chunks_[pos] = tmp;
            --pos;
        }

        for (size_t i = 0; i < blocks_per_chunk_; ++i)
        {
            uint8_t* block = data + i * block_size_;
            auto* node = reinterpret_cast<void**>(block);
            node[0] = free_list_head_;
            node[1] = control;
            free_list_head_ = block;
            ++total_blocks_;
            ++free_blocks_;
        }
        reserved_bytes_ += chunk_size + control_bytes;
    }

    bool release_impl(void* p, bool hard) noexcept
    {
        if (!p) [[unlikely]]
        {
            return false;
        }
        const chunk_node* c = find_chunk(p);
        if (!c) [[unlikely]]
        {
            return false;
        }
        const size_t idx = live_index_in(view_of(*c), p, block_size_);
        if (idx == SIZE_MAX) [[unlikely]]
        {
            return false;
        }
        bit_clear(c->control, idx);
        live_bytes_ -= block_size_;
        ++release_count_;
        if (hard)
        {
            ++hard_delete_count_;
        }
        else
        {
            ++soft_delete_count_;
        }
        auto* node = static_cast<void**>(p);
        node[0] = free_list_head_;
        node[1] = c->control;
        free_list_head_ = p;
        ++free_blocks_;
        return true;
    }

public:
    explicit slab_allocator(size_t block_size, size_t alignment = base_alignment,
                            size_t blocks_per_chunk = DEFAULT_BLOCKS_PER_CHUNK) noexcept
    {
        if (alignment < base_alignment)
        {
            alignment = base_alignment;
        }
        if ((alignment & (alignment - 1)) != 0)
        {
            alignment = base_alignment;
        }
        alignment_ = alignment;
        if (block_size == 0)
        {
            return;
        }
        const size_t mask = alignment - 1;
        if (block_size > SIZE_MAX - mask)
        {
            return;
        }
        block_size_ = (block_size + mask) & ~mask;
        blocks_per_chunk_ = blocks_per_chunk == 0 ? 1 : blocks_per_chunk;
        if (block_size_ > SIZE_MAX / blocks_per_chunk_)
        {
            blocks_per_chunk_ = SIZE_MAX / block_size_;
        }
        if (blocks_per_chunk_ == 0)
        {
            block_size_ = 0;
            return;
        }
        bitmap_bytes_ = bitmap_bytes_for(blocks_per_chunk_);
        const size_t chunk_size = block_size_ * blocks_per_chunk_;
        const size_t control_bytes = sizeof(chunk_control) + bitmap_bytes_;
        if (chunk_size > SIZE_MAX - control_bytes)
        {
            block_size_ = 0;
            blocks_per_chunk_ = 0;
            bitmap_bytes_ = 0;
        }
    }

    ~slab_allocator() noexcept
    {
        release_all();
    }

    slab_allocator(const slab_allocator&) = delete;
    slab_allocator& operator=(const slab_allocator&) = delete;

    slab_allocator(slab_allocator&& other) noexcept
    {
        swap(other);
    }

    slab_allocator& operator=(slab_allocator&& other) noexcept
    {
        if (this != &other) [[likely]]
        {
            release_all();
            swap(other);
        }
        return *this;
    }

    void swap(slab_allocator& other) noexcept
    {
        std::swap(free_list_head_, other.free_list_head_);
        chunks_.swap(other.chunks_);
        std::swap(block_size_, other.block_size_);
        std::swap(alignment_, other.alignment_);
        std::swap(blocks_per_chunk_, other.blocks_per_chunk_);
        std::swap(bitmap_bytes_, other.bitmap_bytes_);
        std::swap(total_blocks_, other.total_blocks_);
        std::swap(free_blocks_, other.free_blocks_);
        std::swap(reserved_bytes_, other.reserved_bytes_);
        std::swap(live_bytes_, other.live_bytes_);
        std::swap(peak_live_bytes_, other.peak_live_bytes_);
        std::swap(acquire_count_, other.acquire_count_);
        std::swap(release_count_, other.release_count_);
        std::swap(soft_delete_count_, other.soft_delete_count_);
        std::swap(hard_delete_count_, other.hard_delete_count_);
    }

    // === 分配 ===
    [[nodiscard]] LCF_FORCE_INLINE void* acquire(
        operating_message& error,
        std::source_location site = std::source_location::current()) noexcept
    {
        void* p = acquire();
        if (!p) [[unlikely]]
        {
            error.write_failure(om_err_out_of_memory.code, msg_level::error, site);
        }
        return p;
    }

    [[nodiscard]] LCF_FORCE_INLINE void* acquire() noexcept
    {
        if (!free_list_head_) [[unlikely]]
        {
            grow();
            if (!free_list_head_) [[unlikely]]
            {
                return nullptr;
            }
        }
        void* p = free_list_head_;
        auto* node = static_cast<void**>(p);
        free_list_head_ = node[0];
        auto* control = static_cast<chunk_control*>(node[1]);
        const size_t idx = static_cast<size_t>(
            (static_cast<uint8_t*>(p) - control->data) / block_size_);
        bit_set(control, idx);
        --free_blocks_;
        live_bytes_ += block_size_;
        if (live_bytes_ > peak_live_bytes_) [[unlikely]]
        {
            peak_live_bytes_ = live_bytes_;
        }
        ++acquire_count_;
        return p;
    }

    template <size_t Size>
    [[nodiscard]] LCF_FORCE_INLINE void* acquire_sized() noexcept
    {
        static_assert(Size > 0, "acquire_sized<Size> requires Size > 0");
        if (Size > block_size_) [[unlikely]]
        {
            return nullptr;
        }
        return acquire();
    }

    [[nodiscard]] LCF_FORCE_INLINE void* acquire_zeroed() noexcept
    {
        void* p = acquire();
        if (p) [[likely]]
        {
            std::memset(p, 0, block_size_);
        }
        return p;
    }

    template <typename T, typename... Args>
    [[nodiscard]] LCF_FORCE_INLINE T* create(Args&&... args) noexcept
    {
        if (sizeof(T) > block_size_ || alignof(T) > block_alignment(block_size_)) [[unlikely]]
        {
            return nullptr;
        }
        void* ptr = acquire();
        if (!ptr) [[unlikely]]
        {
            return nullptr;
        }
        return new (ptr) T(std::forward<Args>(args)...);
    }

    // === 释放: 软删除与硬删除等价 ===
    LCF_FORCE_INLINE bool soft_delete(void* ptr) noexcept
    {
        return release_impl(ptr, false);
    }

    LCF_FORCE_INLINE bool hard_delete(void* ptr) noexcept
    {
        return release_impl(ptr, true);
    }

    template <size_t Size>
    LCF_FORCE_INLINE bool soft_delete_sized(void* ptr) noexcept
    {
        static_assert(Size > 0, "soft_delete_sized<Size> requires Size > 0");
        if (Size > block_size_) [[unlikely]]
        {
            return false;
        }
        return release_impl(ptr, false);
    }

    template <size_t Size>
    LCF_FORCE_INLINE bool hard_delete_sized(void* ptr) noexcept
    {
        static_assert(Size > 0, "hard_delete_sized<Size> requires Size > 0");
        if (Size > block_size_) [[unlikely]]
        {
            return false;
        }
        return release_impl(ptr, true);
    }

    bool soft_delete(void* ptr, operating_message& error,
                     std::source_location site = std::source_location::current()) noexcept
    {
        if (release_impl(ptr, false))
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
        if (release_impl(ptr, true))
        {
            return true;
        }
        error.write_failure((ptr == nullptr) ? om_err_null_pointer.code : om_err_not_found.code,
                            msg_level::warn, site);
        return false;
    }

    template <typename T>
    LCF_FORCE_INLINE void dispose(T* ptr) noexcept
    {
        if (ptr) [[likely]]
        {
            ptr->~T();
        }
    }

    // === 查询 ===
    [[nodiscard]] bool owns(const void* p) const noexcept
    {
        const chunk_node* c = find_chunk(p);
        return c && live_index_in(view_of(*c), p, block_size_) != SIZE_MAX;
    }

    [[nodiscard]] size_t size_of(const void* p) const noexcept
    {
        return owns(p) ? block_size_ : 0;
    }

    [[nodiscard]] memory_stats stats() const noexcept
    {
        memory_stats s{};
        s.reserved_bytes = reserved_bytes_;
        s.live_bytes = live_bytes_;
        s.reusable_bytes = free_blocks_ * block_size_;
        s.peak_live_bytes = peak_live_bytes_;
        s.acquire_count = acquire_count_;
        s.release_count = release_count_;
        s.soft_delete_count = soft_delete_count_;
        s.hard_delete_count = hard_delete_count_;
        return s;
    }

    [[nodiscard]] constexpr bool empty() const noexcept
    {
        return live_bytes_ == 0;
    }

    // === 池管理 ===
    void reset_stats() noexcept
    {
        acquire_count_ = 0;
        release_count_ = 0;
        soft_delete_count_ = 0;
        hard_delete_count_ = 0;
        peak_live_bytes_ = live_bytes_;
    }

    // 全部块回到空闲链, 不归还底层内存
    void reset() noexcept
    {
        free_list_head_ = nullptr;
        for (size_t i = 0; i < chunks_.size(); ++i)
        {
            const chunk_node& c = chunks_[i];
            std::memset(c.control->used, 0, bitmap_bytes_);
            for (size_t j = 0; j < blocks_per_chunk_; ++j)
            {
                uint8_t* block = c.data + j * block_size_;
                auto* node = reinterpret_cast<void**>(block);
                node[0] = free_list_head_;
                node[1] = c.control;
                free_list_head_ = block;
            }
        }
        free_blocks_ = total_blocks_;
        live_bytes_ = 0;
    }

    void release_all() noexcept
    {
        for (size_t i = 0; i < chunks_.size(); ++i)
        {
            ::operator delete(chunks_[i].data,
                              std::align_val_t{ block_alignment(block_size_) });
        }
        chunks_.clear();
        free_list_head_ = nullptr;
        total_blocks_ = 0;
        free_blocks_ = 0;
        reserved_bytes_ = 0;
        live_bytes_ = 0;
    }

    // === 池专属查询 ===
    [[nodiscard]] constexpr size_t block_size() const noexcept { return block_size_; }
    [[nodiscard]] constexpr size_t total_blocks() const noexcept { return total_blocks_; }
    [[nodiscard]] constexpr size_t free_blocks() const noexcept { return free_blocks_; }
};

inline void swap(slab_allocator& a, slab_allocator& b) noexcept
{
    a.swap(b);
}

} // namespace memory
