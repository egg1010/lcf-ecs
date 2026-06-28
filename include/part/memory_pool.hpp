#pragma once
#include <cstddef>
#include <cstdint>
#include <new>
#include <type_traits>
#include <bit>
#include <array>
#include "class_pool.hpp"
#include "force_inline.hpp"

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

// 内存池统计信息
struct pool_stats
{
    size_t total_allocated;       // 已分配 chunk 总量
    size_t total_used;            // 用户使用量(含 header)
    size_t total_free;            // 空闲量(含 header)
    size_t free_block_count;      // 空闲块数量
    size_t max_contiguous_free;   // 最大连续空闲块
    double fragmentation;         // 碎片率 [0,1]
};

class memory_pool
{
private:
    // 块头: 16 字节, next_/prev_ 仅 free 时有效, 存于数据区(free_node)
    struct alignas(16) block_header
    {
        size_t size_;                 // 含 in_use 标志
        block_header* prev_physical_;
    };

    // 空闲块数据区起始的链表节点
    struct free_node
    {
        block_header* next_;
        block_header* prev_;
    };

    static constexpr size_t SL_BITS = 4;
    static constexpr size_t SL_COUNT = 1 << SL_BITS;
    static constexpr size_t FL_MAX = 32;

    static constexpr size_t HEADER_SIZE = sizeof(block_header);         // 16
    static constexpr size_t FREE_NODE_SIZE = sizeof(free_node);         // 16
    static constexpr size_t DEFAULT_CHUNK_SIZE = 4096;
    static constexpr size_t ALIGNMENT = 16;
    static constexpr size_t MIN_SPLIT = HEADER_SIZE + FREE_NODE_SIZE;   // 32
    static constexpr size_t IN_USE_FLAG = 1;

    [[nodiscard]] FORCE_INLINE static bool is_in_use(const block_header* h) noexcept
    {
        return (h->size_ & IN_USE_FLAG) != 0;
    }

    [[nodiscard]] FORCE_INLINE static size_t block_size(const block_header* h) noexcept
    {
        return h->size_ & ~IN_USE_FLAG;
    }

    FORCE_INLINE static void set_in_use(block_header* h, bool v) noexcept
    {
        if (v) h->size_ |= IN_USE_FLAG;
        else h->size_ &= ~IN_USE_FLAG;
    }

    FORCE_INLINE static void set_block_size(block_header* h, size_t s) noexcept
    {
        h->size_ = (h->size_ & IN_USE_FLAG) | (s & ~IN_USE_FLAG);
    }

    FORCE_INLINE static void size_to_index(size_t size, size_t& fl, size_t& sl) noexcept
    {
        fl = static_cast<size_t>(std::bit_width(size)) - 1;
        if (fl >= FL_MAX) [[unlikely]]
        {
            fl = FL_MAX - 1;
            sl = SL_COUNT - 1;
            return;
        }
        sl = (size >> (fl - SL_BITS)) - SL_COUNT;
    }

    [[nodiscard]] FORCE_INLINE static free_node* get_free_node(block_header* h) noexcept
    {
        return reinterpret_cast<free_node*>(h + 1);
    }

    [[nodiscard]] FORCE_INLINE static const free_node* get_free_node(const block_header* h) noexcept
    {
        return reinterpret_cast<const free_node*>(h + 1);
    }

    class_pool<memory_block> memory_chunks_;
    std::array<block_header*, FL_MAX * SL_COUNT> free_lists_;
    uint32_t fl_bitmap_;
    std::array<uint32_t, FL_MAX> sl_bitmaps_;
    size_t total_allocated_;
    size_t total_used_;
    size_t chunk_size_;

    FORCE_INLINE
    void remove_from_free_list(block_header* block, size_t fl, size_t sl) noexcept
    {
        size_t index = fl * SL_COUNT + sl;
        free_node* node = get_free_node(block);

        if (node->prev_) [[likely]]
            get_free_node(node->prev_)->next_ = node->next_;
        else
            free_lists_[index] = node->next_;

        if (node->next_) [[likely]]
            get_free_node(node->next_)->prev_ = node->prev_;

        if (!free_lists_[index])
        {
            sl_bitmaps_[fl] &= ~(1U << sl);
            if (!sl_bitmaps_[fl])
                fl_bitmap_ &= ~(1U << fl);
        }
    }

    FORCE_INLINE
    void add_to_free_list(block_header* block, size_t fl, size_t sl) noexcept
    {
        size_t index = fl * SL_COUNT + sl;
        block_header* old_head = free_lists_[index];
        free_node* node = get_free_node(block);

        node->next_ = old_head;
        node->prev_ = nullptr;
        if (old_head) [[likely]]
            get_free_node(old_head)->prev_ = block;
        free_lists_[index] = block;

        sl_bitmaps_[fl] |= (1U << sl);
        fl_bitmap_ |= (1U << fl);
    }

    void merge_adjacent_blocks(block_header* block) noexcept
    {
        uint8_t* next_block_ptr = reinterpret_cast<uint8_t*>(block) + HEADER_SIZE + block_size(block);
        block_header* next_block = reinterpret_cast<block_header*>(next_block_ptr);

        if (!is_in_use(next_block)) [[likely]]
        {
            size_t next_fl, next_sl;
            size_to_index(block_size(next_block), next_fl, next_sl);
            remove_from_free_list(next_block, next_fl, next_sl);

            set_block_size(block, block_size(block) + HEADER_SIZE + block_size(next_block));

            uint8_t* after_next_ptr = reinterpret_cast<uint8_t*>(block) + HEADER_SIZE + block_size(block);
            block_header* after_next = reinterpret_cast<block_header*>(after_next_ptr);
            after_next->prev_physical_ = block;
        }

        if (block->prev_physical_) [[likely]]
        {
            block_header* prev_block = block->prev_physical_;
            if (!is_in_use(prev_block)) [[likely]]
            {
                size_t prev_fl, prev_sl;
                size_to_index(block_size(prev_block), prev_fl, prev_sl);
                remove_from_free_list(prev_block, prev_fl, prev_sl);

                set_block_size(prev_block, block_size(prev_block) + HEADER_SIZE + block_size(block));
                block = prev_block;

                uint8_t* after_ptr = reinterpret_cast<uint8_t*>(block) + HEADER_SIZE + block_size(block);
                block_header* after_block = reinterpret_cast<block_header*>(after_ptr);
                after_block->prev_physical_ = block;
            }
        }

        size_t new_fl, new_sl;
        size_to_index(block_size(block), new_fl, new_sl);
        add_to_free_list(block, new_fl, new_sl);
    }

    void add_new_chunk(size_t min_size) noexcept
    {
        size_t new_chunk_size = (min_size + 2 * HEADER_SIZE + chunk_size_ - 1) / chunk_size_ * chunk_size_;

        uint8_t* chunk_ptr = static_cast<uint8_t*>(::operator new(new_chunk_size));

        block_header* sentinel = reinterpret_cast<block_header*>(chunk_ptr + new_chunk_size - HEADER_SIZE);
        set_block_size(sentinel, 0);
        set_in_use(sentinel, true);
        sentinel->prev_physical_ = reinterpret_cast<block_header*>(chunk_ptr);

        block_header* header = reinterpret_cast<block_header*>(chunk_ptr);
        set_block_size(header, new_chunk_size - 2 * HEADER_SIZE);
        set_in_use(header, false);
        header->prev_physical_ = nullptr;

        size_t fl, sl;
        size_to_index(block_size(header), fl, sl);
        add_to_free_list(header, fl, sl);

        memory_chunks_.emplace_back(chunk_ptr, new_chunk_size);
        total_allocated_ += new_chunk_size;
    }

    FORCE_INLINE
    void split_block(block_header* block, size_t needed_size) noexcept
    {
        size_t bsize = block_size(block);
        if (bsize < needed_size + MIN_SPLIT) [[unlikely]]
        {
            return;
        }

        size_t remaining_size = bsize - needed_size - HEADER_SIZE;
        uint8_t* block_ptr = reinterpret_cast<uint8_t*>(block);

        block_header* new_block = reinterpret_cast<block_header*>(block_ptr + HEADER_SIZE + needed_size);
        set_block_size(new_block, remaining_size);
        set_in_use(new_block, false);
        new_block->prev_physical_ = block;

        uint8_t* after_ptr = reinterpret_cast<uint8_t*>(new_block) + HEADER_SIZE + remaining_size;
        block_header* after_block = reinterpret_cast<block_header*>(after_ptr);
        after_block->prev_physical_ = new_block;

        size_t new_fl, new_sl;
        size_to_index(remaining_size, new_fl, new_sl);
        add_to_free_list(new_block, new_fl, new_sl);

        set_block_size(block, needed_size);
    }

public:
    explicit memory_pool(size_t chunk_size = DEFAULT_CHUNK_SIZE) noexcept
        : free_lists_{}
        , fl_bitmap_(0)
        , sl_bitmaps_{}
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

        size = (size + ALIGNMENT - 1) & ~(ALIGNMENT - 1);

        size_t fl, sl;
    retry:
        size_to_index(size, fl, sl);

        uint32_t sl_mask = sl_bitmaps_[fl] >> sl;
        if (sl_mask) [[likely]]
        {
            sl += static_cast<size_t>(std::countr_zero(sl_mask));
        }
        else
        {
            uint32_t fl_mask = fl_bitmap_ >> (fl + 1);
            if (fl_mask) [[likely]]
            {
                fl += 1 + static_cast<size_t>(std::countr_zero(fl_mask));
                sl = static_cast<size_t>(std::countr_zero(sl_bitmaps_[fl]));
            }
            else
            {
                add_new_chunk(size);
                goto retry;
            }
        }

        size_t index = fl * SL_COUNT + sl;
        block_header* block = free_lists_[index];
        remove_from_free_list(block, fl, sl);

        split_block(block, size);

        set_in_use(block, true);
        total_used_ += block_size(block) + HEADER_SIZE;
        return reinterpret_cast<uint8_t*>(block) + HEADER_SIZE;
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

        if (!is_in_use(block)) [[unlikely]]
        {
            return;
        }

        set_in_use(block, false);
        total_used_ -= block_size(block) + HEADER_SIZE;

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

    // 判断指针是否属于本池
    [[nodiscard]] bool owns(const void* ptr) const noexcept
    {
        const uint8_t* p = static_cast<const uint8_t*>(ptr);
        for (const auto& chunk : memory_chunks_)
        {
            if (p >= chunk.data_ && p < chunk.data_ + chunk.size_)
            {
                return true;
            }
        }
        return false;
    }

    // 统计信息
    [[nodiscard]] pool_stats stats() const noexcept
    {
        pool_stats s{};
        s.total_allocated = total_allocated_;
        s.total_used = total_used_;
        s.total_free = (total_allocated_ >= total_used_) ? (total_allocated_ - total_used_) : 0;
        s.free_block_count = 0;
        s.max_contiguous_free = 0;

        for (size_t fl = 0; fl < FL_MAX; ++fl)
        {
            if (!(fl_bitmap_ & (1U << fl))) continue;
            for (size_t sl = 0; sl < SL_COUNT; ++sl)
            {
                if (!(sl_bitmaps_[fl] & (1U << sl))) continue;
                const block_header* b = free_lists_[fl * SL_COUNT + sl];
                while (b)
                {
                    size_t bs = block_size(b);
                    s.free_block_count++;
                    if (bs > s.max_contiguous_free) s.max_contiguous_free = bs;
                    b = get_free_node(b)->next_;
                }
            }
        }

        s.fragmentation = (s.total_free > 0 && s.max_contiguous_free < s.total_free)
            ? 1.0 - static_cast<double>(s.max_contiguous_free) / static_cast<double>(s.total_free)
            : 0.0;
        return s;
    }

    // 遍历空闲块, 回调签名: void(void* data_ptr, size_t block_size)
    template <typename Fn>
    void iterate_free(Fn&& fn) const noexcept
    {
        for (size_t fl = 0; fl < FL_MAX; ++fl)
        {
            if (!(fl_bitmap_ & (1U << fl))) continue;
            for (size_t sl = 0; sl < SL_COUNT; ++sl)
            {
                if (!(sl_bitmaps_[fl] & (1U << sl))) continue;
                const block_header* b = free_lists_[fl * SL_COUNT + sl];
                while (b)
                {
                    fn(reinterpret_cast<void*>(const_cast<uint8_t*>(
                        reinterpret_cast<const uint8_t*>(b) + HEADER_SIZE)), block_size(b));
                    b = get_free_node(b)->next_;
                }
            }
        }
    }

    FORCE_INLINE
    void increase_capacity(size_t size) noexcept
    {
        if (size == 0 || size <= total_allocated_) [[likely]]
        {
            return;
        }
        add_new_chunk(size);
    }

    void reduce_capacity(size_t target) noexcept
    {
        if (target >= total_allocated_) [[likely]]
        {
            return;
        }

        for (auto it = memory_chunks_.begin(); it != memory_chunks_.end(); )
        {
            memory_block& chunk = *it;
            block_header* first_block = reinterpret_cast<block_header*>(chunk.data_);

            if (!is_in_use(first_block) && block_size(first_block) == chunk.size_ - 2 * HEADER_SIZE)
            {
                size_t fl, sl;
                size_to_index(block_size(first_block), fl, sl);
                remove_from_free_list(first_block, fl, sl);

                total_allocated_ -= chunk.size_;
                it = memory_chunks_.erase(it);

                if (total_allocated_ <= target)
                    break;
            }
            else
            {
                ++it;
            }
        }
    }

    FORCE_INLINE
    void reset() noexcept
    {
        memory_chunks_.clear();
        free_lists_.fill(nullptr);
        fl_bitmap_ = 0;
        sl_bitmaps_.fill(0);
        total_allocated_ = 0;
        total_used_ = 0;
    }
};
