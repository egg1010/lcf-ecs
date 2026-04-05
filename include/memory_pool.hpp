#pragma once
#include <cstddef>
#include <cstdint>
#include <new>
#include <type_traits>
#include <array>
#include <bit>
#include "class_pool.hpp"

#define FORCE_INLINE inline

struct memory_block
{
    uint8_t* data_;
    size_t size_;

    constexpr memory_block() noexcept : data_(nullptr), size_(0) {}
    
    memory_block(uint8_t* data, size_t size) noexcept : data_(data), size_(size) {}
    
    ~memory_block() noexcept
    {
        if (data_) [[likely]]
        {
            ::operator delete(data_);
        }
    }

    memory_block(const memory_block&) = delete;
    memory_block& operator=(const memory_block&) = delete;
    
    memory_block(memory_block&& other) noexcept
        : data_(other.data_)
        , size_(other.size_)
    {
        other.data_ = nullptr;
        other.size_ = 0;
    }
    
    memory_block& operator=(memory_block&& other) noexcept
    {
        if (this != &other) [[likely]]
        {
            if (data_) [[likely]]
            {
                ::operator delete(data_);
            }
            data_ = other.data_;
            size_ = other.size_;
            other.data_ = nullptr;
            other.size_ = 0;
        }
        return *this;
    }
};

class memory_pool
{
private:
    struct block_header
    {
        size_t size_;
        bool in_use_;
        block_header* next_;
        block_header* prev_;
    };

    class_pool<memory_block> memory_chunks_;
    std::array<block_header*, 32> free_lists_;
    size_t total_allocated_;
    size_t total_used_;
    size_t chunk_size_;

    static constexpr size_t HEADER_SIZE = sizeof(block_header);
    static constexpr size_t DEFAULT_CHUNK_SIZE = 4096;

    [[nodiscard]] static constexpr size_t get_bucket_index(size_t size) noexcept
    {
        if (size <= 8) return 0;
        if (size <= 12) return 1;
        if (size <= 16) return 2;
        if (size <= 24) return 3;
        if (size <= 32) return 4;
        if (size <= 48) return 5;
        if (size <= 64) return 6;
        if (size <= 96) return 7;
        if (size <= 128) return 8;
        if (size <= 192) return 9;
        if (size <= 256) return 10;
        if (size <= 384) return 11;
        if (size <= 512) return 12;
        if (size <= 768) return 13;
        if (size <= 1024) return 14;
        if (size <= 1536) return 15;
        if (size <= 2048) return 16;
        if (size <= 3072) return 17;
        if (size <= 4096) return 18;
        if (size <= 6144) return 19;
        if (size <= 8192) return 20;
        if (size <= 12288) return 21;
        if (size <= 16384) return 22;
        if (size <= 24576) return 23;
        if (size <= 32768) return 24;
        if (size <= 49152) return 25;
        if (size <= 65536) return 26;
        if (size <= 98304) return 27;
        if (size <= 131072) return 28;
        if (size <= 196608) return 29;
        if (size <= 262144) return 30;
        return 31;
    }

    FORCE_INLINE
    void remove_from_free_list(block_header* block, size_t bucket) noexcept
    {
        if (free_lists_[bucket] == block) [[likely]]
        {
            free_lists_[bucket] = block->next_;
        }
        if (block->prev_) [[likely]]
        {
            block->prev_->next_ = block->next_;
        }
        if (block->next_) [[likely]]
        {
            block->next_->prev_ = block->prev_;
        }
        block->next_ = nullptr;
        block->prev_ = nullptr;
    }

    FORCE_INLINE
    void add_to_free_list(block_header* block, size_t bucket) noexcept
    {
        block->next_ = free_lists_[bucket];
        block->prev_ = nullptr;
        if (free_lists_[bucket]) [[likely]]
        {
            free_lists_[bucket]->prev_ = block;
        }
        free_lists_[bucket] = block;
    }

    void merge_adjacent_blocks(block_header* block) noexcept
    {
        uint8_t* block_ptr = reinterpret_cast<uint8_t*>(block);
        
        block_header* next_block = reinterpret_cast<block_header*>(block_ptr + HEADER_SIZE + block->size_);
        
        uint8_t* chunk_start = nullptr;
        for (size_t i = 0; i < memory_chunks_.size(); ++i)
        {
            uint8_t* chunk_data = memory_chunks_[i].data_;
            size_t chunk_size = memory_chunks_[i].size_;
            if (block_ptr >= chunk_data && block_ptr < chunk_data + chunk_size)
            {
                chunk_start = chunk_data;
                break;
            }
        }
        
        if (!chunk_start) [[unlikely]] return;
        
        if (next_block && !next_block->in_use_) [[likely]]
        {
            size_t next_bucket = get_bucket_index(next_block->size_);
            remove_from_free_list(next_block, next_bucket);
            
            block->size_ += HEADER_SIZE + next_block->size_;
        }
        
        if (block_ptr > chunk_start) [[likely]]
        {
            block_header* prev_block_candidate = reinterpret_cast<block_header*>(block_ptr - HEADER_SIZE);
            if (!prev_block_candidate->in_use_) [[likely]]
            {
                size_t prev_bucket = get_bucket_index(prev_block_candidate->size_);
                remove_from_free_list(prev_block_candidate, prev_bucket);
                
                prev_block_candidate->size_ += HEADER_SIZE + block->size_;
                block = prev_block_candidate;
            }
        }
        
        size_t new_bucket = get_bucket_index(block->size_);
        add_to_free_list(block, new_bucket);
    }

    void add_new_chunk(size_t min_size) noexcept
    {
        size_t new_chunk_size = (min_size + HEADER_SIZE + chunk_size_ - 1) / chunk_size_ * chunk_size_;
        
        uint8_t* chunk_ptr = static_cast<uint8_t*>(::operator new(new_chunk_size));
        
        block_header* header = reinterpret_cast<block_header*>(chunk_ptr);
        header->size_ = new_chunk_size - HEADER_SIZE;
        header->in_use_ = false;
        header->next_ = nullptr;
        header->prev_ = nullptr;
        
        size_t bucket = get_bucket_index(header->size_);
        add_to_free_list(header, bucket);
        
        memory_chunks_.emplace_back(chunk_ptr, new_chunk_size);
        total_allocated_ += new_chunk_size;
    }

    FORCE_INLINE
    void split_block(block_header* block, size_t needed_size, size_t bucket) noexcept
    {
        if (block->size_ < needed_size + HEADER_SIZE + 8) [[unlikely]]
        {
            return;
        }

        size_t remaining_size = block->size_ - needed_size - HEADER_SIZE;
        uint8_t* block_ptr = reinterpret_cast<uint8_t*>(block);
        
        block_header* new_block = reinterpret_cast<block_header*>(block_ptr + HEADER_SIZE + needed_size);
        new_block->size_ = remaining_size;
        new_block->in_use_ = false;
        new_block->next_ = nullptr;
        new_block->prev_ = nullptr;
        
        size_t new_bucket = get_bucket_index(remaining_size);
        add_to_free_list(new_block, new_bucket);
        
        block->size_ = needed_size;
    }

public:
    explicit memory_pool(size_t chunk_size = DEFAULT_CHUNK_SIZE) noexcept
        : free_lists_{}
        , total_allocated_(0)
        , total_used_(0)
        , chunk_size_(chunk_size)
    {}

    ~memory_pool() noexcept = default;

    memory_pool(const memory_pool&) = delete;
    memory_pool& operator=(const memory_pool&) = delete;
    memory_pool(memory_pool&&) noexcept = default;
    memory_pool& operator=(memory_pool&&) noexcept = default;

    [[nodiscard]] FORCE_INLINE
    void* allocate(size_t size) noexcept
    {
        if (size == 0) [[unlikely]]
        {
            return nullptr;
        }

        size = (size + 7) & ~7;
        size_t bucket = get_bucket_index(size);

        if (free_lists_[bucket]) [[likely]]
        {
            block_header* block = free_lists_[bucket];
            remove_from_free_list(block, bucket);

            split_block(block, size, bucket);

            block->in_use_ = true;
            total_used_ += block->size_ + HEADER_SIZE;
            return reinterpret_cast<uint8_t*>(block) + HEADER_SIZE;
        }

        for (size_t b = bucket + 1; b < free_lists_.size(); ++b)
        {
            if (free_lists_[b]) [[likely]]
            {
                block_header* block = free_lists_[b];
                remove_from_free_list(block, b);

                split_block(block, size, b);

                block->in_use_ = true;
                total_used_ += block->size_ + HEADER_SIZE;
                return reinterpret_cast<uint8_t*>(block) + HEADER_SIZE;
            }
        }

        add_new_chunk(size);
        return allocate(size);
    }

    FORCE_INLINE
    void deallocate(void* ptr) noexcept
    {
        if (!ptr) [[unlikely]]
        {
            return;
        }

        uint8_t* block_ptr = reinterpret_cast<uint8_t*>(ptr) - HEADER_SIZE;
        block_header* block = reinterpret_cast<block_header*>(block_ptr);

        if (!block->in_use_) [[unlikely]]
        {
            return;
        }

        block->in_use_ = false;
        total_used_ -= block->size_ + HEADER_SIZE;

        merge_adjacent_blocks(block);
    }

    template <typename T, typename... Args>
    [[nodiscard]] FORCE_INLINE
    T* construct(Args&&... args) noexcept
    {
        void* ptr = allocate(sizeof(T));
        if (!ptr) [[unlikely]]
        {
            return nullptr;
        }
        return new (ptr) T(std::forward<Args>(args)...);
    }

    template <typename T>
    FORCE_INLINE
    void destroy(T* ptr) noexcept
    {
        if (!ptr) [[unlikely]]
        {
            return;
        }
        ptr->~T();
        deallocate(ptr);
    }

    [[nodiscard]] constexpr size_t total_allocated() const noexcept { return total_allocated_; }
    [[nodiscard]] constexpr size_t total_used() const noexcept { return total_used_; }
    [[nodiscard]] constexpr size_t chunk_size() const noexcept { return chunk_size_; }
    [[nodiscard]] constexpr bool empty() const noexcept { return total_used_ == 0; }

    FORCE_INLINE
    void resize(size_t size) noexcept
    {
        if (size == 0 || size <= total_allocated_) [[likely]]
        {
            return;
        }
        add_new_chunk(size);
    }

    FORCE_INLINE
    void reset() noexcept
    {
        memory_chunks_.clear();
        free_lists_.fill(nullptr);
        total_allocated_ = 0;
        total_used_ = 0;
    }
};
