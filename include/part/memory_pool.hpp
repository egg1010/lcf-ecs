#pragma once
#include <cstddef>
#include <cstdint>
#include <new>
#include <type_traits>
#include <bit>
#include <array>
#include "dense.hpp"
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

static_assert(!std::is_trivially_copyable_v<memory_block>,
              "memory_block must not be trivially copyable for correct dense<T> move semantics");

struct pool_stats
{
    size_t total_allocated;
    size_t total_used;            // 含 header
    size_t total_free;            // 含 header
    size_t free_block_count;
    size_t max_contiguous_free;
    double fragmentation;         // 碎片率 [0,1]
};

class memory_pool
{
private:
    // chunk 预分配池: 4 档 LIFO, 缓存释放的 chunk 避免 ::operator delete/new
    struct chunk_pool
    {
        static constexpr size_t TIERS = 4;
        static constexpr size_t TIER_SIZES[TIERS] = {8192, 32768, 131072, 524288};
        static constexpr size_t MAX_FREE_PER_TIER = 8;

        struct node
        {
            node* next;
            size_t size;
        };

        std::array<node*, TIERS> free_lists_{};
        std::array<size_t, TIERS> counts_{};

        [[nodiscard]] static constexpr size_t tier_index(size_t min_size) noexcept
        {
            for (size_t i = 0; i < TIERS; ++i)
            {
                if (min_size <= TIER_SIZES[i])
                {
                    return i;
                }
            }
            return TIERS;
        }

        [[nodiscard]] uint8_t* take(size_t min_size, size_t& actual_size) noexcept
        {
            size_t tier = tier_index(min_size);
            if (tier >= TIERS) [[unlikely]]
            {
                actual_size = min_size;
                return static_cast<uint8_t*>(::operator new(min_size));
            }
            node* n = free_lists_[tier];
            if (n) [[likely]]
            {
                free_lists_[tier] = n->next;
                --counts_[tier];
                actual_size = n->size;
                return reinterpret_cast<uint8_t*>(n);
            }
            actual_size = TIER_SIZES[tier];
            return static_cast<uint8_t*>(::operator new(TIER_SIZES[tier]));
        }

        void return_chunk(uint8_t* data, size_t size) noexcept
        {
            size_t tier = tier_index(size);
            if (tier >= TIERS || counts_[tier] >= MAX_FREE_PER_TIER) [[unlikely]]
            {
                ::operator delete(data);
                return;
            }
            node* n = reinterpret_cast<node*>(data);
            n->next = free_lists_[tier];
            n->size = size;
            free_lists_[tier] = n;
            ++counts_[tier];
        }

        void clear() noexcept
        {
            for (size_t tier = 0; tier < TIERS; ++tier)
            {
                while (free_lists_[tier])
                {
                    node* n = free_lists_[tier];
                    free_lists_[tier] = n->next;
                    ::operator delete(reinterpret_cast<uint8_t*>(n));
                }
                counts_[tier] = 0;
            }
        }

        chunk_pool() noexcept = default;
        ~chunk_pool() noexcept { clear(); }
        chunk_pool(const chunk_pool&) = delete;
        chunk_pool& operator=(const chunk_pool&) = delete;
        chunk_pool(chunk_pool&& o) noexcept
            : free_lists_(o.free_lists_)
            , counts_(o.counts_)
        {
            o.free_lists_.fill(nullptr);
            o.counts_.fill(0);
        }
        chunk_pool& operator=(chunk_pool&& o) noexcept
        {
            if (this != &o) [[likely]]
            {
                clear();
                free_lists_ = o.free_lists_;
                counts_ = o.counts_;
                o.free_lists_.fill(nullptr);
                o.counts_.fill(0);
            }
            return *this;
        }
    };

    // 块头: 16 字节
    struct alignas(16) block_header
    {
        size_t size_;                 // 含 in_use 标志
        block_header* prev_physical_;
    };

    // 空闲块链表节点
    struct free_node
    {
        block_header* next_;
        block_header* prev_;
    };

    static constexpr size_t SL_BITS = 4;
    static constexpr size_t SL_COUNT = 1 << SL_BITS;
    static constexpr size_t FL_MAX = 32;

    static constexpr size_t HEADER_SIZE = sizeof(block_header);
    static constexpr size_t FREE_NODE_SIZE = sizeof(free_node);
    static constexpr size_t DEFAULT_CHUNK_SIZE = 4096;
    static constexpr size_t ALIGNMENT = 16;
    static constexpr size_t MIN_SPLIT = HEADER_SIZE + FREE_NODE_SIZE;
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
        if (v)
        {
            h->size_ |= IN_USE_FLAG;
        }
        else
        {
            h->size_ &= ~IN_USE_FLAG;
        }
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

    dense<memory_block> memory_chunks_;
    std::array<block_header*, FL_MAX * SL_COUNT> free_lists_;
    uint32_t fl_bitmap_;
    std::array<uint32_t, FL_MAX> sl_bitmaps_;
    size_t total_allocated_;
    size_t total_used_;
    size_t free_block_count_;   // 增量维护: free_lists + small_cache + wilderness 的空闲块总数
    size_t chunk_size_;
    chunk_pool chunk_pool_;

    // 小块缓存: 16 个 size class (16-256B), LIFO 单链表
    static constexpr size_t SMALL_CLASS_COUNT = 16;
    static constexpr size_t SMALL_MAX_SIZE = 256;
    static constexpr size_t SMALL_CACHE_MAX = 1 << 20;
    std::array<void*, SMALL_CLASS_COUNT> small_heads_{};
    std::array<uint32_t, SMALL_CLASS_COUNT> small_counts_{};

    // wilderness: 上次 split 的剩余块, 不在 free list 中
    block_header* wilderness_ = nullptr;

    [[nodiscard]] static constexpr size_t small_class_index(size_t aligned_size) noexcept
    {
        return (aligned_size >> 4) - 1;
    }

    void flush_cache() noexcept
    {
        if (wilderness_)
        {
            size_t w_fl, w_sl;
            size_to_index(block_size(wilderness_), w_fl, w_sl);
            add_to_free_list(wilderness_, w_fl, w_sl); // ++free_block_count_
            wilderness_ = nullptr;                      // wilderness_ 块转入 free_list, 净不变
            --free_block_count_;
        }

        for (size_t idx = 0; idx < SMALL_CLASS_COUNT; ++idx)
        {
            while (small_heads_[idx])
            {
                void* ptr = small_heads_[idx];
                small_heads_[idx] = *static_cast<void**>(ptr);
                --small_counts_[idx];
                --free_block_count_; // small_cache 块被取出, 将通过 merge 进入 free_list

                uint8_t* bp = static_cast<uint8_t*>(ptr) - HEADER_SIZE;
                block_header* block = reinterpret_cast<block_header*>(bp);
                set_in_use(block, false);
                merge_adjacent_blocks(block); // 内部 add_to_free_list 会 ++
            }
        }
    }

    FORCE_INLINE
    void remove_from_free_list(block_header* block, size_t fl, size_t sl) noexcept
    {
        size_t index = fl * SL_COUNT + sl;
        free_node* node = get_free_node(block);

        if (node->prev_) [[likely]]
        {
            get_free_node(node->prev_)->next_ = node->next_;
        }
        else
        {
            free_lists_[index] = node->next_;
        }

        if (node->next_) [[likely]]
        {
            get_free_node(node->next_)->prev_ = node->prev_;
        }

        if (!free_lists_[index])
        {
            sl_bitmaps_[fl] &= ~(1U << sl);
            if (!sl_bitmaps_[fl])
            {
                fl_bitmap_ &= ~(1U << fl);
            }
        }
        --free_block_count_;
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
        {
            get_free_node(old_head)->prev_ = block;
        }
        free_lists_[index] = block;

        sl_bitmaps_[fl] |= (1U << sl);
        fl_bitmap_ |= (1U << fl);
        ++free_block_count_;
    }

    // 合并相邻空闲块
    [[gnu::noinline]] void merge_adjacent_blocks(block_header* block) noexcept
    {
        uint8_t* next_block_ptr = reinterpret_cast<uint8_t*>(block) + HEADER_SIZE + block_size(block);
        block_header* next_block = reinterpret_cast<block_header*>(next_block_ptr);

        if (!is_in_use(next_block)) [[likely]]
        {
            if (next_block == wilderness_)
            {
                wilderness_ = nullptr;
                --free_block_count_; // wilderness_ 被合并
            }
            else
            {
                size_t next_fl, next_sl;
                size_to_index(block_size(next_block), next_fl, next_sl);
                remove_from_free_list(next_block, next_fl, next_sl);
            }

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
                if (prev_block == wilderness_)
                {
                    wilderness_ = nullptr;
                    --free_block_count_; // wilderness_ 被合并
                }
                else
                {
                    size_t prev_fl, prev_sl;
                    size_to_index(block_size(prev_block), prev_fl, prev_sl);
                    remove_from_free_list(prev_block, prev_fl, prev_sl);
                }

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
        size_t need = min_size + 2 * HEADER_SIZE;
        size_t new_chunk_size;
        uint8_t* chunk_ptr = chunk_pool_.take(need, new_chunk_size);

        block_header* sentinel = reinterpret_cast<block_header*>(chunk_ptr + new_chunk_size - HEADER_SIZE);
        set_block_size(sentinel, 0);
        set_in_use(sentinel, true);
        sentinel->prev_physical_ = reinterpret_cast<block_header*>(chunk_ptr);

        block_header* header = reinterpret_cast<block_header*>(chunk_ptr);
        set_block_size(header, new_chunk_size - 2 * HEADER_SIZE);
        set_in_use(header, false);
        header->prev_physical_ = nullptr;

        if (wilderness_)
        {
            size_t fl, sl;
            size_to_index(block_size(header), fl, sl);
            add_to_free_list(header, fl, sl);
        }
        else
        {
            wilderness_ = header;
            ++free_block_count_; // 新 chunk 的 wilderness_
        }

        // 二分查找插入位置, 保持按地址排序
        size_t lo = 0, hi = memory_chunks_.size();
        while (lo < hi)
        {
            size_t mid = lo + (hi - lo) / 2;
            if (memory_chunks_[mid].data_ < chunk_ptr)
            {
                lo = mid + 1;
            }
            else
            {
                hi = mid;
            }
        }
        memory_chunks_.emplace(memory_chunks_.begin() + lo, chunk_ptr, new_chunk_size);

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
        new_block->size_ = remaining_size;
        new_block->prev_physical_ = block;

        uint8_t* after_ptr = reinterpret_cast<uint8_t*>(new_block) + HEADER_SIZE + remaining_size;
        block_header* after_block = reinterpret_cast<block_header*>(after_ptr);
        after_block->prev_physical_ = new_block;

        size_t new_fl, new_sl;
        size_to_index(remaining_size, new_fl, new_sl);
        add_to_free_list(new_block, new_fl, new_sl);

        block->size_ = (block->size_ & IN_USE_FLAG) | needed_size;
    }

public:
    explicit memory_pool(size_t chunk_size = DEFAULT_CHUNK_SIZE) noexcept
        : free_lists_{}
        , fl_bitmap_(0)
        , sl_bitmaps_{}
        , total_allocated_(0)
        , total_used_(0)
        , free_block_count_(0)
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

        // 小块缓存
        if (size <= SMALL_MAX_SIZE) [[likely]]
        {
            size_t idx = small_class_index(size);
            void* p = small_heads_[idx];
            if (p) [[likely]]
            {
                small_heads_[idx] = *static_cast<void**>(p);
                --small_counts_[idx];
                total_used_ += size + HEADER_SIZE;
                --free_block_count_; // small_cache 块被取出
                return p;
            }
        }

    retry:
        if (wilderness_) [[likely]]
        {
            size_t wsize = block_size(wilderness_);
            if (wsize >= size) [[likely]]
            {
                block_header* block = wilderness_;
                if (wsize >= size + MIN_SPLIT)
                {
                    size_t remaining_size = wsize - size - HEADER_SIZE;
                    uint8_t* block_ptr = reinterpret_cast<uint8_t*>(block);
                    block_header* new_block = reinterpret_cast<block_header*>(block_ptr + HEADER_SIZE + size);
                    new_block->size_ = remaining_size;
                    new_block->prev_physical_ = block;

                    wilderness_ = new_block;
                    block->size_ = size | IN_USE_FLAG;
                    total_used_ += size + HEADER_SIZE;
                    // wilderness_ 块数不变: 原 wilderness_→in_use, 新 wilderness_ 替代
                }
                else
                {
                    wilderness_ = nullptr;
                    set_in_use(block, true);
                    total_used_ += wsize + HEADER_SIZE;
                    --free_block_count_; // wilderness_ 被使用
                }
                return reinterpret_cast<uint8_t*>(block) + HEADER_SIZE;
            }
            size_t w_fl, w_sl;
            size_to_index(wsize, w_fl, w_sl);
            add_to_free_list(wilderness_, w_fl, w_sl); // ++free_block_count_
            wilderness_ = nullptr;                      // --free_block_count_ (净不变)
            --free_block_count_;
        }

        // TLSF 路径
        size_t fl, sl;
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
                bool has_cached = false;
                for (size_t i = 0; i < SMALL_CLASS_COUNT; ++i)
                {
                    if (small_counts_[i])
                    {
                        has_cached = true;
                        break;
                    }
                }
                if (has_cached)
                {
                    flush_cache();
                }
                add_new_chunk(size);
                goto retry;
            }
        }

        size_t index = fl * SL_COUNT + sl;
        block_header* block = free_lists_[index];
        remove_from_free_list(block, fl, sl);

        size_t bsize = block_size(block);
        if (bsize >= size + MIN_SPLIT) [[likely]]
        {
            split_block(block, size);
        }

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

        size_t bsize = block_size(block);

        // 小块缓存
        if (bsize <= SMALL_MAX_SIZE) [[likely]]
        {
            size_t idx = small_class_index(bsize);
            if (small_counts_[idx] < SMALL_CACHE_MAX) [[likely]]
            {
                *static_cast<void**>(ptr) = small_heads_[idx];
                small_heads_[idx] = ptr;
                ++small_counts_[idx];
                total_used_ -= bsize + HEADER_SIZE;
                ++free_block_count_; // small_cache 块放入
                return;
            }
        }

        // TLSF 路径
        set_in_use(block, false);
        total_used_ -= bsize + HEADER_SIZE;

        block_header* prev = block->prev_physical_;
        block_header* next = reinterpret_cast<block_header*>(block_ptr + HEADER_SIZE + bsize);

        if (is_in_use(next) && (!prev || is_in_use(prev))) [[likely]]
        {
            size_t fl, sl;
            size_to_index(bsize, fl, sl);
            add_to_free_list(block, fl, sl);
            return;
        }

        merge_adjacent_blocks(block);
    }

    // sized deallocate: size 必须与 allocate(size) 一致
    FORCE_INLINE
    void deallocate(void* ptr, size_t size) noexcept
    {
        if (!ptr) [[unlikely]]
        {
            return;
        }

        size_t aligned = (size + ALIGNMENT - 1) & ~(ALIGNMENT - 1);

        if (aligned <= SMALL_MAX_SIZE) [[likely]]
        {
            size_t idx = small_class_index(aligned);
            *static_cast<void**>(ptr) = small_heads_[idx];
            small_heads_[idx] = ptr;
            ++small_counts_[idx];
            total_used_ -= aligned + HEADER_SIZE;
            ++free_block_count_; // small_cache 块放入
            return;
        }

        deallocate(ptr);
    }

    // 模板化 sized 路径: 编译期常量传播, 消除对齐计算与小块分支
    template <size_t Size>
    [[nodiscard]] FORCE_INLINE
    void* allocate_sized() noexcept
    {
        if constexpr (Size == 0)
        {
            return nullptr;
        }

        constexpr size_t Aligned = (Size + ALIGNMENT - 1) & ~(ALIGNMENT - 1);

        if constexpr (Aligned <= SMALL_MAX_SIZE)
        {
            constexpr size_t Idx = small_class_index(Aligned);
            void* p = small_heads_[Idx];
            if (p) [[likely]]
            {
                small_heads_[Idx] = *static_cast<void**>(p);
                --small_counts_[Idx];
                total_used_ += Aligned + HEADER_SIZE;
                return p;
            }
        }
        return allocate(Size);
    }

    template <size_t Size>
    FORCE_INLINE
    void deallocate_sized(void* ptr) noexcept
    {
        if (!ptr) [[unlikely]]
        {
            return;
        }

        constexpr size_t Aligned = (Size + ALIGNMENT - 1) & ~(ALIGNMENT - 1);

        if constexpr (Aligned <= SMALL_MAX_SIZE)
        {
            constexpr size_t Idx = small_class_index(Aligned);
            *static_cast<void**>(ptr) = small_heads_[Idx];
            small_heads_[Idx] = ptr;
            ++small_counts_[Idx];
            total_used_ -= Aligned + HEADER_SIZE;
            return;
        }

        deallocate(ptr);
    }

    template <typename T, typename... Args>
    [[nodiscard]] FORCE_INLINE
    T* construct(Args&&... args) noexcept
    {
        void* ptr = allocate_sized<sizeof(T)>();
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
        deallocate_sized<sizeof(T)>(ptr);
    }

    [[nodiscard]] constexpr size_t total_allocated() const noexcept { return total_allocated_; }
    [[nodiscard]] constexpr size_t total_used() const noexcept { return total_used_; }
    [[nodiscard]] constexpr size_t chunk_size() const noexcept { return chunk_size_; }
    [[nodiscard]] constexpr bool empty() const noexcept { return total_used_ == 0; }

    [[nodiscard]] bool owns(const void* ptr) const noexcept
    {
        const uint8_t* p = static_cast<const uint8_t*>(ptr);
        const size_t sz = memory_chunks_.size();
        if (sz == 0) [[unlikely]]
        {
            return false;
        }

        size_t lo = 0;
        size_t hi = sz;
        while (lo < hi)
        {
            size_t mid = lo + (hi - lo) / 2;
            if (memory_chunks_[mid].data_ <= p)
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
            return false;
        }
        const auto& c = memory_chunks_[lo - 1];
        return p >= c.data_ && p < c.data_ + c.size_;
    }

    [[nodiscard]] pool_stats stats() const noexcept
    {
        pool_stats s{};
        s.total_allocated = total_allocated_;
        s.total_used = total_used_;
        s.total_free = (total_allocated_ >= total_used_) ? (total_allocated_ - total_used_) : 0;
        s.free_block_count = free_block_count_; // O(1) 增量维护
        s.max_contiguous_free = 0;

        // 仅遍历求 max_contiguous_free (用于 fragmentation)
        for (size_t fl = 0; fl < FL_MAX; ++fl)
        {
            if (!(fl_bitmap_ & (1U << fl)))
            {
                continue;
            }
            for (size_t sl = 0; sl < SL_COUNT; ++sl)
            {
                if (!(sl_bitmaps_[fl] & (1U << sl)))
                {
                    continue;
                }
                const block_header* b = free_lists_[fl * SL_COUNT + sl];
                while (b)
                {
                    size_t bs = block_size(b);
                    if (bs > s.max_contiguous_free)
                    {
                        s.max_contiguous_free = bs;
                    }
                    b = get_free_node(b)->next_;
                }
            }
        }

        // wilderness 可能是最大连续空闲块
        if (wilderness_)
        {
            size_t ws = block_size(wilderness_);
            if (ws > s.max_contiguous_free)
            {
                s.max_contiguous_free = ws;
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
            if (!(fl_bitmap_ & (1U << fl)))
            {
                continue;
            }
            for (size_t sl = 0; sl < SL_COUNT; ++sl)
            {
                if (!(sl_bitmaps_[fl] & (1U << sl)))
                {
                    continue;
                }
                const block_header* b = free_lists_[fl * SL_COUNT + sl];
                while (b)
                {
                    fn(reinterpret_cast<void*>(const_cast<uint8_t*>(
                        reinterpret_cast<const uint8_t*>(b) + HEADER_SIZE)), block_size(b));
                    b = get_free_node(b)->next_;
                }
            }
        }

        // 遍历小块缓存
        for (size_t idx = 0; idx < SMALL_CLASS_COUNT; ++idx)
        {
            void* p = small_heads_[idx];
            while (p)
            {
                void* next = *static_cast<void**>(p);
                const block_header* b = reinterpret_cast<const block_header*>(
                    static_cast<const uint8_t*>(p) - HEADER_SIZE);
                fn(p, block_size(b));
                p = next;
            }
        }

        // 遍历 wilderness
        if (wilderness_)
        {
            fn(reinterpret_cast<void*>(const_cast<uint8_t*>(
                reinterpret_cast<const uint8_t*>(wilderness_) + HEADER_SIZE)), block_size(wilderness_));
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

        // 刷新小块缓存, 使缓存块参与合并和回收
        flush_cache();

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
                uint8_t* raw = chunk.data_;
                size_t sz = chunk.size_;
                chunk.data_ = nullptr;
                chunk.size_ = 0;
                it = memory_chunks_.erase(it);
                chunk_pool_.return_chunk(raw, sz);

                if (total_allocated_ <= target)
                {
                    break;
                }
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
        for (auto& chunk : memory_chunks_)
        {
            chunk_pool_.return_chunk(chunk.data_, chunk.size_);
            chunk.data_ = nullptr;
            chunk.size_ = 0;
        }
        memory_chunks_.clear();
        free_lists_.fill(nullptr);
        fl_bitmap_ = 0;
        sl_bitmaps_.fill(0);
        small_heads_.fill(nullptr);
        small_counts_.fill(0);
        wilderness_ = nullptr;
        total_allocated_ = 0;
        total_used_ = 0;
        free_block_count_ = 0;
    }
};

