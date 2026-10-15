#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <new>
#include "../dense.hpp"
#include "../force_inline.hpp"

namespace memory {

// 固定块大小分配器
class slab_allocator
{
private:
    struct chunk_node
    {
        uint8_t* data;
        size_t size;
    };

    void* free_list_head_{nullptr};
    dense<chunk_node> chunks_;
    uint8_t* min_addr_{nullptr};
    uint8_t* max_addr_{nullptr};
    size_t chunk_count_{0};
    size_t block_size_;
    size_t alignment_;
    size_t blocks_per_chunk_;
    size_t total_blocks_{0};
    size_t free_blocks_{0};
    size_t peak_used_blocks_{0};

    static constexpr size_t DEFAULT_BLOCKS_PER_CHUNK = 256;

    // 块地址 = chunk 基址 + j*block_size_; 基址对齐到此值时所有块均满足该对齐
    //   取 block_size_ 整除的最大 2 的幂 (block_size_ 为 2 的幂时即自身)
    [[nodiscard]] static constexpr size_t block_alignment(size_t block_size) noexcept
    {
        return block_size & (~block_size + 1);
    }

    void grow() noexcept
    {
        size_t chunk_size = block_size_ * blocks_per_chunk_;
        size_t chunk_align = block_alignment(block_size_);
        uint8_t* data = static_cast<uint8_t*>(
            ::operator new(chunk_size, std::align_val_t{ chunk_align }, std::nothrow));
        if (!data) [[unlikely]] return;

        chunks_.emplace_back(data, chunk_size);
        size_t pos = chunks_.size() - 1;
        while (pos > 0 && chunks_[pos - 1].data > chunks_[pos].data)
        {
            chunk_node tmp = chunks_[pos - 1];
            chunks_[pos - 1] = chunks_[pos];
            chunks_[pos] = tmp;
            --pos;
        }
        if (min_addr_ == nullptr || data < min_addr_) min_addr_ = data;
        uint8_t* end_addr = data + chunk_size;
        if (end_addr > max_addr_) max_addr_ = end_addr;
        ++chunk_count_;

        for (size_t i = 0; i < blocks_per_chunk_; ++i)
        {
            void* block = data + i * block_size_;
            *(void**)block = free_list_head_;
            free_list_head_ = block;
            ++total_blocks_;
            ++free_blocks_;
        }
    }

public:
    explicit slab_allocator(size_t block_size, size_t alignment = 16,
                            size_t blocks_per_chunk = DEFAULT_BLOCKS_PER_CHUNK) noexcept
        : free_list_head_(nullptr)
        , chunks_()
        , block_size_((block_size + alignment - 1) & ~(alignment - 1))
        , alignment_(alignment)
        , blocks_per_chunk_(blocks_per_chunk)
    {}

    ~slab_allocator() noexcept
    {
        size_t chunk_align = block_alignment(block_size_);
        for (size_t i = 0; i < chunks_.size(); ++i)
        {
            ::operator delete(chunks_[i].data, std::align_val_t{ chunk_align });
        }
    }

    slab_allocator(const slab_allocator&) = delete;
    slab_allocator& operator=(const slab_allocator&) = delete;

    slab_allocator(slab_allocator&& other) noexcept
        : free_list_head_(other.free_list_head_)
        , chunks_(std::move(other.chunks_))
        , min_addr_(other.min_addr_)
        , max_addr_(other.max_addr_)
        , chunk_count_(other.chunk_count_)
        , block_size_(other.block_size_)
        , alignment_(other.alignment_)
        , blocks_per_chunk_(other.blocks_per_chunk_)
        , total_blocks_(other.total_blocks_)
        , free_blocks_(other.free_blocks_)
    {
        other.free_list_head_ = nullptr;
        other.min_addr_ = nullptr;
        other.max_addr_ = nullptr;
        other.chunk_count_ = 0;
        other.total_blocks_ = 0;
        other.free_blocks_ = 0;
    }

    slab_allocator& operator=(slab_allocator&& other) noexcept
    {
        if (this != &other) [[likely]]
        {
            size_t chunk_align = block_alignment(block_size_);
            for (size_t i = 0; i < chunks_.size(); ++i)
            {
                ::operator delete(chunks_[i].data, std::align_val_t{ chunk_align });
            }
            free_list_head_ = other.free_list_head_;
            chunks_ = std::move(other.chunks_);
            min_addr_ = other.min_addr_;
            max_addr_ = other.max_addr_;
            chunk_count_ = other.chunk_count_;
            block_size_ = other.block_size_;
            alignment_ = other.alignment_;
            blocks_per_chunk_ = other.blocks_per_chunk_;
            total_blocks_ = other.total_blocks_;
            free_blocks_ = other.free_blocks_;
            other.free_list_head_ = nullptr;
            other.min_addr_ = nullptr;
            other.max_addr_ = nullptr;
            other.chunk_count_ = 0;
            other.total_blocks_ = 0;
            other.free_blocks_ = 0;
        }
        return *this;
    }

    [[nodiscard]] FORCE_INLINE void* allocate() noexcept
    {
        if (!free_list_head_) [[unlikely]]
        {
            grow();
            if (!free_list_head_) [[unlikely]] return nullptr;
        }
        void* p = free_list_head_;
        free_list_head_ = *(void**)p;
        --free_blocks_;
        size_t used = total_blocks_ - free_blocks_;
        if (used > peak_used_blocks_) [[unlikely]]
        {
            peak_used_blocks_ = used;
        }
        return p;
    }

    FORCE_INLINE void deallocate(void* p) noexcept
    {
        if (!p) [[unlikely]] return;
        *(void**)p = free_list_head_;
        free_list_head_ = p;
        ++free_blocks_;
    }

    [[nodiscard]] FORCE_INLINE bool owns(const void* p) const noexcept
    {
        const uint8_t* up = static_cast<const uint8_t*>(p);
        if (!min_addr_ || up < min_addr_ || up >= max_addr_) [[unlikely]] return false;

        if (chunk_count_ == 1) [[likely]]
        {
            const chunk_node& c = chunks_[0];
            return up >= c.data && up < c.data + c.size;
        }
        size_t lo = 0;
        size_t hi = chunk_count_;
        while (lo < hi)
        {
            size_t mid = lo + (hi - lo) / 2;
            if (chunks_[mid].data <= up)
            {
                lo = mid + 1;
            }
            else
            {
                hi = mid;
            }
        }
        if (lo == 0) [[unlikely]] return false;
        const chunk_node& c = chunks_[lo - 1];
        return up >= c.data && up < c.data + c.size;
    }

    // 重置: 释放所有块到初始状态, 不归还底层内存
    void reset() noexcept
    {
        free_list_head_ = nullptr;
        for (size_t i = 0; i < chunks_.size(); ++i)
        {
            for (size_t j = 0; j < blocks_per_chunk_; ++j)
            {
                void* block = chunks_[i].data + j * block_size_;
                *(void**)block = free_list_head_;
                free_list_head_ = block;
            }
        }
        free_blocks_ = total_blocks_;
    }

    [[nodiscard]] constexpr size_t block_size() const noexcept { return block_size_; }
    [[nodiscard]] constexpr size_t total_blocks() const noexcept { return total_blocks_; }
    [[nodiscard]] constexpr size_t free_blocks() const noexcept { return free_blocks_; }
    [[nodiscard]] constexpr size_t total_used() const noexcept { return total_blocks_ - free_blocks_; }
    [[nodiscard]] constexpr size_t total_bytes() const noexcept { return total_blocks_ * block_size_; }
    [[nodiscard]] constexpr size_t used_bytes() const noexcept { return (total_blocks_ - free_blocks_) * block_size_; }
    [[nodiscard]] constexpr bool empty() const noexcept { return free_blocks_ == total_blocks_; }

    [[nodiscard]] FORCE_INLINE void* allocate_zeroed() noexcept
    {
        void* p = allocate();
        if (p) [[likely]]
        {
            std::memset(p, 0, block_size_);
        }
        return p;
    }

    // M05: 校验 sizeof/alignof 均不超过块容量与块对齐, 不满足返回 nullptr
    template <typename T, typename... Args>
    [[nodiscard]] FORCE_INLINE T* construct(Args&&... args) noexcept
    {
        if (sizeof(T) > block_size_ || alignof(T) > block_size_) [[unlikely]]
        {
            return nullptr;
        }
        void* ptr = allocate();
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
        deallocate(ptr);
    }

    // 预分配至少 blocks 个空闲块
    void reserve(size_t blocks) noexcept
    {
        if (blocks <= free_blocks_) [[likely]]
        {
            return;
        }
        size_t need = blocks - free_blocks_;
        size_t chunks_needed = (need + blocks_per_chunk_ - 1) / blocks_per_chunk_;
        for (size_t i = 0; i < chunks_needed; ++i)
        {
            grow();
            if (free_blocks_ >= blocks)
            {
                break;
            }
        }
    }

    [[nodiscard]] constexpr size_t free_bytes() const noexcept { return free_blocks_ * block_size_; }
    [[nodiscard]] constexpr size_t peak_used_blocks() const noexcept { return peak_used_blocks_; }

    [[nodiscard]] const uint8_t* min_addr() const noexcept { return min_addr_; }
    [[nodiscard]] const uint8_t* max_addr() const noexcept { return max_addr_; }
};

} // namespace memory
