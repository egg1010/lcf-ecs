#pragma once
#include <cstddef>
#include <cstdint>
#include <new>
#include <type_traits>
#include "class_pool.hpp"

struct memory_block
{
    uint8_t* data_;
    size_t size_;

    memory_block() noexcept : data_(nullptr), size_(0) {}
    
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

    class_pool<memory_block> memory_chunks_;  // 所有内存块的容器
    block_header* free_list_;                   // 空闲块链表头指针
    size_t total_allocated_;                    // 总共分配的字节数
    size_t total_used_;                         // 当前使用的字节数
    size_t chunk_size_;                         // 单个内存块的默认大小

    static constexpr size_t HEADER_SIZE = sizeof(block_header);
    static constexpr size_t DEFAULT_CHUNK_SIZE = 4096;

    void add_new_chunk(size_t min_size)
    {
        size_t new_chunk_size = (min_size + HEADER_SIZE + chunk_size_ - 1) / chunk_size_ * chunk_size_;
        
        uint8_t* chunk_ptr = static_cast<uint8_t*>(::operator new(new_chunk_size));
        
        block_header* header = reinterpret_cast<block_header*>(chunk_ptr);
        header->size_ = new_chunk_size - HEADER_SIZE;
        header->in_use_ = false;
        header->next_ = free_list_;
        header->prev_ = nullptr;
        
        if (free_list_) [[likely]]
        {
            free_list_->prev_ = header;
        }
        free_list_ = header;
        
        memory_chunks_.emplace_back(chunk_ptr, new_chunk_size);
        total_allocated_ += new_chunk_size;
    }

    void split_block(block_header* block, size_t needed_size)
    {
        if (block->size_ < needed_size + HEADER_SIZE + 16) [[unlikely]]
        {
            return;
        }

        size_t remaining_size = block->size_ - needed_size - HEADER_SIZE;
        uint8_t* block_ptr = reinterpret_cast<uint8_t*>(block);
        
        block_header* new_block = reinterpret_cast<block_header*>(block_ptr + HEADER_SIZE + needed_size);
        new_block->size_ = remaining_size;
        new_block->in_use_ = false;
        new_block->next_ = block->next_;
        new_block->prev_ = block;
        
        if (block->next_) [[likely]]
        {
            block->next_->prev_ = new_block;
        }
        block->next_ = new_block;
        block->size_ = needed_size;
    }

    void merge_with_next(block_header* block)
    {
        if (!block->next_ || block->next_->in_use_) [[unlikely]]
        {
            return;
        }

        block_header* next_block = block->next_;
        block->size_ += HEADER_SIZE + next_block->size_;
        block->next_ = next_block->next_;
        
        if (block->next_) [[likely]]
        {
            block->next_->prev_ = block;
        }
    }

public:
    explicit memory_pool(size_t chunk_size = DEFAULT_CHUNK_SIZE) noexcept
        : free_list_(nullptr)
        , total_allocated_(0)
        , total_used_(0)
        , chunk_size_(chunk_size)
    {}

    ~memory_pool() noexcept = default;

    memory_pool(const memory_pool&) = delete;
    memory_pool& operator=(const memory_pool&) = delete;
    memory_pool(memory_pool&&) noexcept = default;
    memory_pool& operator=(memory_pool&&) noexcept = default;

    [[nodiscard]] void* allocate(size_t size)
    {
        if (size == 0) [[unlikely]]
        {
            return nullptr;
        }

        size = (size + 7) & ~7;

        block_header* current = free_list_;
        while (current)
        {
            if (!current->in_use_ && current->size_ >= size) [[likely]]
            {
                split_block(current, size);
                current->in_use_ = true;
                
                if (current == free_list_) [[unlikely]]
                {
                    free_list_ = current->next_;
                }
                else if (current->prev_) [[likely]]
                {
                    current->prev_->next_ = current->next_;
                }
                if (current->next_) [[likely]]
                {
                    current->next_->prev_ = current->prev_;
                }
                
                total_used_ += current->size_ + HEADER_SIZE;
                return reinterpret_cast<uint8_t*>(current) + HEADER_SIZE;
            }
            current = current->next_;
        }

        add_new_chunk(size);
        return allocate(size);
    }

    void deallocate(void* ptr)
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

        block->next_ = free_list_;
        block->prev_ = nullptr;
        if (free_list_) [[likely]]
        {
            free_list_->prev_ = block;
        }
        free_list_ = block;

        merge_with_next(block);
        if (block->prev_) [[likely]]
        {
            merge_with_next(block->prev_);
        }
    }

    template <typename T, typename... Args>
    [[nodiscard]] T* construct(Args&&... args)
    {
        void* ptr = allocate(sizeof(T));
        if (!ptr) [[unlikely]]
        {
            return nullptr;
        }
        return new (ptr) T(std::forward<Args>(args)...);
    }

    template <typename T>
    void destroy(T* ptr)
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

    void resize(size_t size)
    {
        if (size == 0 || size <= total_allocated_) [[likely]]
        {
            return;
        }
        add_new_chunk(size);
    }

    void reset() noexcept
    {
        memory_chunks_.clear();
        free_list_ = nullptr;
        total_allocated_ = 0;
        total_used_ = 0;
    }
};
