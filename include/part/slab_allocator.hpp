#pragma once
#include <cstddef>
#include <cstdint>
#include <new>
#include "force_inline.hpp"

// 固定块大小对象池: 侵入式 free list, 零 header, O(1) push/pop
// chunk 用 operator new 独立分配, 不依赖 memory_pool
class slab_allocator
{
private:
    struct chunk_node
    {
        chunk_node* next;
        uint8_t* data;
        size_t size;
    };

    void* free_list_head_{nullptr};
    chunk_node* chunks_{nullptr};
    size_t block_size_;
    size_t alignment_;
    size_t blocks_per_chunk_;
    size_t total_blocks_{0};
    size_t free_blocks_{0};

    static constexpr size_t DEFAULT_BLOCKS_PER_CHUNK = 256;

    // 分配新 chunk, 切分入 free list
    void grow() noexcept
    {
        size_t chunk_size = block_size_ * blocks_per_chunk_;
        uint8_t* data = static_cast<uint8_t*>(
            ::operator new(chunk_size, std::nothrow));
        if (!data) [[unlikely]] return;

        chunk_node* node = static_cast<chunk_node*>(
            ::operator new(sizeof(chunk_node), std::nothrow));
        if (!node) [[unlikely]]
        {
            ::operator delete(data);
            return;
        }
        node->data = data;
        node->size = chunk_size;
        node->next = chunks_;
        chunks_ = node;

        // 切分 chunk: 块首 8 字节存 next 指针
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
        , chunks_(nullptr)
        , block_size_((block_size + alignment - 1) & ~(alignment - 1))
        , alignment_(alignment)
        , blocks_per_chunk_(blocks_per_chunk)
    {}

    ~slab_allocator() noexcept
    {
        chunk_node* c = chunks_;
        while (c) [[likely]]
        {
            chunk_node* next = c->next;
            ::operator delete(c->data);
            ::operator delete(c);
            c = next;
        }
    }

    slab_allocator(const slab_allocator&) = delete;
    slab_allocator& operator=(const slab_allocator&) = delete;

    slab_allocator(slab_allocator&& other) noexcept
        : free_list_head_(other.free_list_head_)
        , chunks_(other.chunks_)
        , block_size_(other.block_size_)
        , alignment_(other.alignment_)
        , blocks_per_chunk_(other.blocks_per_chunk_)
        , total_blocks_(other.total_blocks_)
        , free_blocks_(other.free_blocks_)
    {
        other.free_list_head_ = nullptr;
        other.chunks_ = nullptr;
        other.total_blocks_ = 0;
        other.free_blocks_ = 0;
    }

    slab_allocator& operator=(slab_allocator&& other) noexcept
    {
        if (this != &other) [[likely]]
        {
            chunk_node* c = chunks_;
            while (c) [[likely]]
            {
                chunk_node* next = c->next;
                ::operator delete(c->data);
                ::operator delete(c);
                c = next;
            }
            free_list_head_ = other.free_list_head_;
            chunks_ = other.chunks_;
            block_size_ = other.block_size_;
            alignment_ = other.alignment_;
            blocks_per_chunk_ = other.blocks_per_chunk_;
            total_blocks_ = other.total_blocks_;
            free_blocks_ = other.free_blocks_;
            other.free_list_head_ = nullptr;
            other.chunks_ = nullptr;
            other.total_blocks_ = 0;
            other.free_blocks_ = 0;
        }
        return *this;
    }

    // O(1) pop, 无 header
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
        return p;
    }

    // O(1) push, 无 header
    FORCE_INLINE void deallocate(void* p) noexcept
    {
        if (!p) [[unlikely]] return;
        *(void**)p = free_list_head_;
        free_list_head_ = p;
        ++free_blocks_;
    }

    [[nodiscard]] bool owns(const void* p) const noexcept
    {
        const uint8_t* up = static_cast<const uint8_t*>(p);
        chunk_node* c = chunks_;
        while (c) [[likely]]
        {
            if (up >= c->data && up < c->data + c->size) [[likely]]
            {
                return true;
            }
            c = c->next;
        }
        return false;
    }

    [[nodiscard]] constexpr size_t block_size() const noexcept { return block_size_; }
    [[nodiscard]] constexpr size_t total_blocks() const noexcept { return total_blocks_; }
    [[nodiscard]] constexpr size_t free_blocks() const noexcept { return free_blocks_; }
    [[nodiscard]] constexpr bool empty() const noexcept { return free_blocks_ == total_blocks_; }
};
