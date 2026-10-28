#pragma once
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <new>
#include <type_traits>
#include <utility>
#include "../dense.hpp"
#include "../force_inline.hpp"
#include "../operating_message.hpp"
#include "memory_common.hpp"
#include "oom_handler.hpp"

namespace memory {

class memory_pool
{
private:
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

    // chunk 预分配池
    struct chunk_pool
    {
        static constexpr size_t TIERS = 4;
        static constexpr size_t TIER_SIZES[TIERS] = { 8192, 32768, 131072, 524288 };
        static constexpr size_t MAX_FREE_PER_TIER = 8;

        struct node
        {
            node* next;
            size_t size;
        };

        std::array<node*, TIERS> free_lists_{};
        std::array<size_t, TIERS> counts_{};
        size_t cached_reserved_{ 0 };

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
            const size_t tier = tier_index(min_size);
            if (tier >= TIERS) [[unlikely]]
            {
                actual_size = min_size;
                uint8_t* p = static_cast<uint8_t*>(::operator new(min_size, std::nothrow));
                if (!p) [[unlikely]]
                {
                    handle_oom(min_size, "memory_pool::chunk_pool::take(oversize)");
                }
                return p;
            }
            node* n = free_lists_[tier];
            if (n) [[likely]]
            {
                free_lists_[tier] = n->next;
                --counts_[tier];
                cached_reserved_ -= n->size;
                actual_size = n->size;
                return reinterpret_cast<uint8_t*>(n);
            }
            actual_size = TIER_SIZES[tier];
            uint8_t* p = static_cast<uint8_t*>(::operator new(TIER_SIZES[tier], std::nothrow));
            if (!p) [[unlikely]]
            {
                handle_oom(TIER_SIZES[tier], "memory_pool::chunk_pool::take");
            }
            return p;
        }

        void return_chunk(uint8_t* data, size_t size) noexcept
        {
            const size_t tier = tier_index(size);
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
            cached_reserved_ += size;
        }

        // 归还一个缓存 chunk 给系统; 返回释放字节数
        size_t release_one() noexcept
        {
            for (size_t tier = 0; tier < TIERS; ++tier)
            {
                node* n = free_lists_[tier];
                if (!n)
                {
                    continue;
                }
                free_lists_[tier] = n->next;
                --counts_[tier];
                const size_t size = n->size;
                cached_reserved_ -= size;
                ::operator delete(reinterpret_cast<uint8_t*>(n));
                return size;
            }
            return 0;
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
            cached_reserved_ = 0;
        }

        chunk_pool() noexcept = default;
        ~chunk_pool() noexcept { clear(); }
        chunk_pool(const chunk_pool&) = delete;
        chunk_pool& operator=(const chunk_pool&) = delete;

        chunk_pool(chunk_pool&& o) noexcept
            : free_lists_(o.free_lists_)
            , counts_(o.counts_)
            , cached_reserved_(o.cached_reserved_)
        {
            o.free_lists_.fill(nullptr);
            o.counts_.fill(0);
            o.cached_reserved_ = 0;
        }

        chunk_pool& operator=(chunk_pool&& o) noexcept
        {
            if (this != &o) [[likely]]
            {
                clear();
                free_lists_ = o.free_lists_;
                counts_ = o.counts_;
                cached_reserved_ = o.cached_reserved_;
                o.free_lists_.fill(nullptr);
                o.counts_.fill(0);
                o.cached_reserved_ = 0;
            }
            return *this;
        }

        void swap(chunk_pool& o) noexcept
        {
            std::swap(free_lists_, o.free_lists_);
            std::swap(counts_, o.counts_);
            std::swap(cached_reserved_, o.cached_reserved_);
        }
    };

    struct alignas(16) block_header
    {
        size_t size_;                 // 含标志位
        block_header* prev_physical_;
    };

    struct free_node
    {
        block_header* next_;
        block_header* prev_;
    };

    struct aligned_record
    {
        void* user;
        void* raw;
        size_t size;
        size_t alignment;
    };

    static constexpr size_t SL_BITS = 4;
    static constexpr size_t SL_COUNT = 1 << SL_BITS;
    static constexpr size_t FL_MAX = 32;

    static constexpr size_t HEADER_SIZE = sizeof(block_header);
    static constexpr size_t FREE_NODE_SIZE = sizeof(free_node);
    static constexpr size_t DEFAULT_CHUNK_SIZE = 4096;
    static constexpr size_t ALIGNMENT = base_alignment;
    static constexpr size_t MIN_SPLIT = HEADER_SIZE + FREE_NODE_SIZE;
    static constexpr size_t IN_USE_FLAG = 1;
    static constexpr size_t CACHED_FLAG = 2;
    static constexpr size_t ALIGNED_RAW_FLAG = 4;
    static constexpr size_t FLAGS_MASK = IN_USE_FLAG | CACHED_FLAG | ALIGNED_RAW_FLAG;

    [[nodiscard]] LCF_FORCE_INLINE static bool is_in_use(const block_header* h) noexcept
    {
        return (h->size_ & IN_USE_FLAG) != 0;
    }

    [[nodiscard]] LCF_FORCE_INLINE static bool is_cached(const block_header* h) noexcept
    {
        return (h->size_ & CACHED_FLAG) != 0;
    }

    [[nodiscard]] LCF_FORCE_INLINE static bool is_aligned_raw(const block_header* h) noexcept
    {
        return (h->size_ & ALIGNED_RAW_FLAG) != 0;
    }

    [[nodiscard]] LCF_FORCE_INLINE static bool is_mergeable(const block_header* h) noexcept
    {
        return !is_in_use(h) && !is_cached(h);
    }

    [[nodiscard]] LCF_FORCE_INLINE static size_t block_size(const block_header* h) noexcept
    {
        return h->size_ & ~FLAGS_MASK;
    }

    LCF_FORCE_INLINE static void set_in_use(block_header* h, bool v) noexcept
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

    LCF_FORCE_INLINE static void set_cached(block_header* h, bool v) noexcept
    {
        if (v)
        {
            h->size_ |= CACHED_FLAG;
        }
        else
        {
            h->size_ &= ~CACHED_FLAG;
        }
    }

    LCF_FORCE_INLINE static void set_aligned_raw(block_header* h, bool v) noexcept
    {
        if (v)
        {
            h->size_ |= ALIGNED_RAW_FLAG;
        }
        else
        {
            h->size_ &= ~ALIGNED_RAW_FLAG;
        }
    }

    LCF_FORCE_INLINE static void set_block_size(block_header* h, size_t s) noexcept
    {
        h->size_ = (h->size_ & FLAGS_MASK) | (s & ~FLAGS_MASK);
    }

    LCF_FORCE_INLINE static void size_to_index(size_t size, size_t& fl, size_t& sl) noexcept
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

    LCF_FORCE_INLINE static void size_to_search_index(size_t size, size_t& fl, size_t& sl) noexcept
    {
        if (size >= (size_t{ 1 } << SL_BITS)) [[likely]]
        {
            const size_t f = static_cast<size_t>(std::bit_width(size)) - 1;
            const size_t round = (size_t{ 1 } << (f - SL_BITS)) - 1;
            if (size > SIZE_MAX - round) [[unlikely]]
            {
                fl = FL_MAX - 1;
                sl = SL_COUNT - 1;
                return;
            }
            size += round;
        }
        size_to_index(size, fl, sl);
    }

    [[nodiscard]] LCF_FORCE_INLINE static free_node* get_free_node(block_header* h) noexcept
    {
        return reinterpret_cast<free_node*>(h + 1);
    }

    [[nodiscard]] LCF_FORCE_INLINE static const free_node* get_free_node(const block_header* h) noexcept
    {
        return reinterpret_cast<const free_node*>(h + 1);
    }

    static constexpr size_t SMALL_CLASS_COUNT = 16;
    static constexpr size_t SMALL_MAX_SIZE = 256;
    static constexpr size_t SMALL_CACHE_LIMIT = 512;
    static constexpr size_t SMALL_CACHE_HIGH = 4 * SMALL_CACHE_LIMIT;

    dense<memory_block> memory_chunks_;
    std::array<block_header*, FL_MAX * SL_COUNT> free_lists_{};
    uint32_t fl_bitmap_{ 0 };
    std::array<uint32_t, FL_MAX> sl_bitmaps_{};
    size_t active_reserved_{ 0 };
    size_t live_bytes_{ 0 };
    size_t peak_live_bytes_{ 0 };
    size_t acquire_count_{ 0 };
    size_t release_count_{ 0 };
    size_t soft_delete_count_{ 0 };
    size_t hard_delete_count_{ 0 };
    size_t free_block_count_{ 0 };
    size_t chunk_size_{ DEFAULT_CHUNK_SIZE };
    chunk_pool chunk_pool_;

    std::array<void*, SMALL_CLASS_COUNT> small_heads_{};
    std::array<size_t, SMALL_CLASS_COUNT> small_counts_{};
    size_t cached_block_count_{ 0 };
    size_t next_trim_class_{ 0 };
    block_header* wilderness_{ nullptr };
    dense<aligned_record> aligned_allocations_;

    [[nodiscard]] static constexpr size_t small_class_index(size_t aligned_size) noexcept
    {
        return (aligned_size >> 4) - 1;
    }

    [[nodiscard]] static constexpr size_t align_up(size_t size) noexcept
    {
        return (size + ALIGNMENT - 1) & ~(ALIGNMENT - 1);
    }

    void trim_cache_() noexcept
    {
        size_t scanned = 0;
        while (cached_block_count_ > SMALL_CACHE_LIMIT && scanned < SMALL_CLASS_COUNT)
        {
            const size_t idx = next_trim_class_;
            next_trim_class_ = (next_trim_class_ + 1) % SMALL_CLASS_COUNT;
            ++scanned;

            while (cached_block_count_ > SMALL_CACHE_LIMIT && small_heads_[idx])
            {
                void* cur = small_heads_[idx];
                small_heads_[idx] = *static_cast<void**>(cur);
                --small_counts_[idx];
                --free_block_count_;
                --cached_block_count_;

                uint8_t* bp = static_cast<uint8_t*>(cur) - HEADER_SIZE;
                block_header* block = reinterpret_cast<block_header*>(bp);
                set_in_use(block, false);
                set_cached(block, false);
                merge_adjacent_blocks(block);
            }
        }
    }

    void flush_cache() noexcept
    {
        if (wilderness_)
        {
            size_t w_fl, w_sl;
            size_to_index(block_size(wilderness_), w_fl, w_sl);
            add_to_free_list(wilderness_, w_fl, w_sl);
            wilderness_ = nullptr;
            --free_block_count_;
        }

        for (size_t idx = 0; idx < SMALL_CLASS_COUNT; ++idx)
        {
            while (small_heads_[idx])
            {
                void* ptr = small_heads_[idx];
                small_heads_[idx] = *static_cast<void**>(ptr);
                --free_block_count_;
                --cached_block_count_;

                uint8_t* bp = static_cast<uint8_t*>(ptr) - HEADER_SIZE;
                block_header* block = reinterpret_cast<block_header*>(bp);
                set_in_use(block, false);
                set_cached(block, false);
                merge_adjacent_blocks(block);
            }
        }
        small_counts_.fill(0);
    }

    LCF_FORCE_INLINE
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

    LCF_FORCE_INLINE
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

    LCF_NOINLINE void merge_adjacent_blocks(block_header* block) noexcept
    {
        uint8_t* next_block_ptr = reinterpret_cast<uint8_t*>(block) + HEADER_SIZE + block_size(block);
        block_header* next_block = reinterpret_cast<block_header*>(next_block_ptr);

        if (is_mergeable(next_block)) [[likely]]
        {
            if (next_block == wilderness_)
            {
                wilderness_ = nullptr;
                --free_block_count_;
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
            if (is_mergeable(prev_block)) [[likely]]
            {
                if (prev_block == wilderness_)
                {
                    wilderness_ = nullptr;
                    --free_block_count_;
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
        if (min_size > SIZE_MAX - 2 * HEADER_SIZE) [[unlikely]]
        {
            return;
        }
        size_t need = min_size + 2 * HEADER_SIZE;
        if (chunk_size_ > need && chunk_size_ <= SIZE_MAX - (ALIGNMENT - 1))
        {
            need = chunk_size_;
        }
        if (need > SIZE_MAX - (ALIGNMENT - 1)) [[unlikely]]
        {
            return;
        }
        need = align_up(need);
        size_t new_chunk_size = 0;
        uint8_t* chunk_ptr = chunk_pool_.take(need, new_chunk_size);

        block_header* sentinel = reinterpret_cast<block_header*>(chunk_ptr + new_chunk_size - HEADER_SIZE);
        sentinel->size_ = IN_USE_FLAG;
        sentinel->prev_physical_ = reinterpret_cast<block_header*>(chunk_ptr);

        block_header* header = reinterpret_cast<block_header*>(chunk_ptr);
        header->size_ = new_chunk_size - 2 * HEADER_SIZE;
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
            ++free_block_count_;
        }

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
        active_reserved_ += new_chunk_size;
    }

    LCF_FORCE_INLINE
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

        block->size_ = (block->size_ & FLAGS_MASK) | needed_size;
    }

    [[nodiscard]] LCF_FORCE_INLINE
    void* acquire_block(size_t size) noexcept
    {
        if (size <= SMALL_MAX_SIZE) [[likely]]
        {
            size_t idx = small_class_index(size);
            void* p = small_heads_[idx];
            if (p) [[likely]]
            {
                small_heads_[idx] = *static_cast<void**>(p);
                --small_counts_[idx];
                block_header* cb = reinterpret_cast<block_header*>(
                    static_cast<uint8_t*>(p) - HEADER_SIZE);
                set_cached(cb, false);
                set_in_use(cb, true);
                --free_block_count_;
                --cached_block_count_;
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
                }
                else
                {
                    wilderness_ = nullptr;
                    set_in_use(block, true);
                    --free_block_count_;
                }
                return reinterpret_cast<uint8_t*>(block) + HEADER_SIZE;
            }
            size_t w_fl, w_sl;
            size_to_index(wsize, w_fl, w_sl);
            add_to_free_list(wilderness_, w_fl, w_sl);
            wilderness_ = nullptr;
            --free_block_count_;
        }

        size_t fl, sl;
        size_to_search_index(size, fl, sl);

        uint32_t sl_mask = sl_bitmaps_[fl] >> sl;
        if (sl_mask) [[likely]]
        {
            sl += static_cast<size_t>(std::countr_zero(sl_mask));
        }
        else
        {
            uint32_t fl_mask = (fl + 1 < FL_MAX) ? (fl_bitmap_ >> (fl + 1)) : 0U;
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
                    if (small_heads_[i])
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
        if (bsize < size) [[unlikely]]
        {
            size_t bf, bs;
            size_to_index(bsize, bf, bs);
            add_to_free_list(block, bf, bs);
            return nullptr;
        }
        if (bsize >= size + MIN_SPLIT) [[likely]]
        {
            split_block(block, size);
        }

        set_in_use(block, true);
        return reinterpret_cast<uint8_t*>(block) + HEADER_SIZE;
    }

    LCF_FORCE_INLINE void account_acquire(size_t used_bytes) noexcept
    {
        live_bytes_ += used_bytes;
        if (live_bytes_ > peak_live_bytes_) [[unlikely]]
        {
            peak_live_bytes_ = live_bytes_;
        }
        ++acquire_count_;
    }

    void release_block(block_header* block, bool hard, size_t accounted_bytes) noexcept
    {
        const size_t bsize = block_size(block);
        set_in_use(block, false);
        live_bytes_ -= accounted_bytes;
        ++release_count_;
        if (hard)
        {
            ++hard_delete_count_;
        }
        else
        {
            ++soft_delete_count_;
        }

        if (!hard && bsize <= SMALL_MAX_SIZE) [[likely]]
        {
            void* ptr = reinterpret_cast<uint8_t*>(block) + HEADER_SIZE;
            size_t idx = small_class_index(bsize);
            *static_cast<void**>(ptr) = small_heads_[idx];
            small_heads_[idx] = ptr;
            ++small_counts_[idx];
            set_cached(block, true);
            ++free_block_count_;
            ++cached_block_count_;
            if (cached_block_count_ >= SMALL_CACHE_HIGH) [[unlikely]]
            {
                trim_cache_();
            }
            return;
        }

        if (hard)
        {
            merge_adjacent_blocks(block);
        }
        else
        {
            size_t fl, sl;
            size_to_index(bsize, fl, sl);
            add_to_free_list(block, fl, sl);
        }
    }

    [[nodiscard]] const memory_block* find_chunk(const void* ptr) const noexcept
    {
        const uint8_t* p = static_cast<const uint8_t*>(ptr);
        size_t lo = 0;
        size_t hi = memory_chunks_.size();
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
            return nullptr;
        }
        const memory_block& c = memory_chunks_[lo - 1];
        if (p < c.data_ || p >= c.data_ + c.size_)
        {
            return nullptr;
        }
        return &c;
    }

    [[nodiscard]] block_header* validate_live_block(const void* ptr) const noexcept
    {
        if (!ptr) [[unlikely]]
        {
            return nullptr;
        }
        const memory_block* c = find_chunk(ptr);
        if (!c) [[unlikely]]
        {
            return nullptr;
        }
        const uint8_t* p = static_cast<const uint8_t*>(ptr);
        const uint8_t* chunk_begin = c->data_;
        const uint8_t* chunk_end = c->data_ + c->size_;
        if (p < chunk_begin + HEADER_SIZE || p >= chunk_end - HEADER_SIZE) [[unlikely]]
        {
            return nullptr;
        }
        uint8_t* block_ptr = const_cast<uint8_t*>(p) - HEADER_SIZE;
        block_header* block = reinterpret_cast<block_header*>(block_ptr);
        if (!is_in_use(block)) [[unlikely]]
        {
            return nullptr;
        }
        const size_t bsize = block_size(block);
        if (bsize == 0 || (bsize & (ALIGNMENT - 1)) != 0) [[unlikely]]
        {
            return nullptr;
        }
        if (block_ptr + HEADER_SIZE + bsize > const_cast<uint8_t*>(chunk_end) - HEADER_SIZE) [[unlikely]]
        {
            return nullptr;
        }
        block_header* prev = block->prev_physical_;
        if (prev == nullptr)
        {
            if (block_ptr != const_cast<uint8_t*>(chunk_begin)) [[unlikely]]
            {
                return nullptr;
            }
        }
        else
        {
            uint8_t* prev_ptr = reinterpret_cast<uint8_t*>(prev);
            if (prev_ptr < const_cast<uint8_t*>(chunk_begin) ||
                prev_ptr + HEADER_SIZE > const_cast<uint8_t*>(chunk_end) - HEADER_SIZE) [[unlikely]]
            {
                return nullptr;
            }
            if (prev_ptr + HEADER_SIZE + block_size(prev) != block_ptr) [[unlikely]]
            {
                return nullptr;
            }
        }
        uint8_t* next_ptr = block_ptr + HEADER_SIZE + bsize;
        if (next_ptr < const_cast<uint8_t*>(chunk_end) - HEADER_SIZE) [[likely]]
        {
            block_header* next = reinterpret_cast<block_header*>(next_ptr);
            if (next->prev_physical_ != block) [[unlikely]]
            {
                return nullptr;
            }
        }
        return block;
    }

    [[nodiscard]] const aligned_record* find_aligned(const void* ptr) const noexcept
    {
        size_t lo = 0;
        size_t hi = aligned_allocations_.size();
        while (lo < hi)
        {
            size_t mid = lo + (hi - lo) / 2;
            if (reinterpret_cast<uintptr_t>(aligned_allocations_[mid].user) <
                reinterpret_cast<uintptr_t>(ptr))
            {
                lo = mid + 1;
            }
            else
            {
                hi = mid;
            }
        }
        if (lo >= aligned_allocations_.size()) [[unlikely]]
        {
            return nullptr;
        }
        return aligned_allocations_[lo].user == ptr ? &aligned_allocations_[lo] : nullptr;
    }

    void register_aligned(void* user, void* raw, size_t size, size_t alignment) noexcept
    {
        size_t lo = 0;
        size_t hi = aligned_allocations_.size();
        while (lo < hi)
        {
            size_t mid = lo + (hi - lo) / 2;
            if (reinterpret_cast<uintptr_t>(aligned_allocations_[mid].user) <
                reinterpret_cast<uintptr_t>(user))
            {
                lo = mid + 1;
            }
            else
            {
                hi = mid;
            }
        }
        aligned_allocations_.emplace(aligned_allocations_.begin() + lo,
                                     aligned_record{ user, raw, size, alignment });
    }

    void unregister_aligned(void* user) noexcept
    {
        size_t lo = 0;
        size_t hi = aligned_allocations_.size();
        while (lo < hi)
        {
            size_t mid = lo + (hi - lo) / 2;
            if (reinterpret_cast<uintptr_t>(aligned_allocations_[mid].user) <
                reinterpret_cast<uintptr_t>(user))
            {
                lo = mid + 1;
            }
            else
            {
                hi = mid;
            }
        }
        if (lo < aligned_allocations_.size() && aligned_allocations_[lo].user == user)
        {
            auto it = aligned_allocations_.begin() + lo;
            aligned_allocations_.erase(it, it + 1);
        }
    }

    bool release_aligned(const aligned_record* rec, bool hard) noexcept
    {
        if (!rec) [[unlikely]]
        {
            return false;
        }
        block_header* block = validate_live_block(rec->raw);
        if (!block || !is_aligned_raw(block) || rec->size > block_size(block)) [[unlikely]]
        {
            return false;
        }
        void* const user = rec->user;
        const size_t requested = rec->size;
        unregister_aligned(user);
        set_aligned_raw(block, false);
        release_block(block, hard, requested);
        return true;
    }

    template <size_t MinSize>
    LCF_FORCE_INLINE bool release_validated(void* ptr, bool hard) noexcept
    {
        if (!ptr) [[unlikely]]
        {
            return false;
        }
        if (const aligned_record* rec = find_aligned(ptr))
        {
            if constexpr (MinSize > 0)
            {
                if (rec->size < MinSize) [[unlikely]]
                {
                    return false;
                }
            }
            return release_aligned(rec, hard);
        }
        block_header* block = validate_live_block(ptr);
        if (!block || is_aligned_raw(block)) [[unlikely]]
        {
            return false;
        }
        if constexpr (MinSize > 0)
        {
            if (block_size(block) < MinSize) [[unlikely]]
            {
                return false;
            }
        }
        release_block(block, hard, block_size(block));
        return true;
    }

    [[nodiscard]] size_t reserved_bytes() const noexcept
    {
        return active_reserved_ + chunk_pool_.cached_reserved_;
    }

    [[nodiscard]] size_t reusable_bytes() const noexcept
    {
        size_t total = 0;
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
                    total += block_size(b);
                    b = get_free_node(b)->next_;
                }
            }
        }
        for (size_t idx = 0; idx < SMALL_CLASS_COUNT; ++idx)
        {
            void* p = small_heads_[idx];
            while (p)
            {
                const block_header* b = reinterpret_cast<const block_header*>(
                    static_cast<const uint8_t*>(p) - HEADER_SIZE);
                total += block_size(b);
                p = *static_cast<void**>(p);
            }
        }
        if (wilderness_)
        {
            total += block_size(wilderness_);
        }
        for (size_t tier = 0; tier < chunk_pool::TIERS; ++tier)
        {
            total += chunk_pool_.counts_[tier] *
                (chunk_pool::TIER_SIZES[tier] - 2 * HEADER_SIZE);
        }
        return total;
    }

public:
    explicit memory_pool(size_t chunk_size = DEFAULT_CHUNK_SIZE) noexcept
        : chunk_size_(chunk_size)
    {}

    ~memory_pool() noexcept
    {
        release_all();
    }

    memory_pool(const memory_pool&) = delete;
    memory_pool& operator=(const memory_pool&) = delete;

    memory_pool(memory_pool&& other) noexcept
    {
        swap(other);
    }

    memory_pool& operator=(memory_pool&& other) noexcept
    {
        if (this != &other) [[likely]]
        {
            release_all();
            swap(other);
        }
        return *this;
    }

    void swap(memory_pool& other) noexcept
    {
        memory_chunks_.swap(other.memory_chunks_);
        std::swap(free_lists_, other.free_lists_);
        std::swap(fl_bitmap_, other.fl_bitmap_);
        std::swap(sl_bitmaps_, other.sl_bitmaps_);
        std::swap(active_reserved_, other.active_reserved_);
        std::swap(live_bytes_, other.live_bytes_);
        std::swap(peak_live_bytes_, other.peak_live_bytes_);
        std::swap(acquire_count_, other.acquire_count_);
        std::swap(release_count_, other.release_count_);
        std::swap(soft_delete_count_, other.soft_delete_count_);
        std::swap(hard_delete_count_, other.hard_delete_count_);
        std::swap(free_block_count_, other.free_block_count_);
        std::swap(chunk_size_, other.chunk_size_);
        chunk_pool_.swap(other.chunk_pool_);
        std::swap(small_heads_, other.small_heads_);
        std::swap(small_counts_, other.small_counts_);
        std::swap(cached_block_count_, other.cached_block_count_);
        std::swap(next_trim_class_, other.next_trim_class_);
        std::swap(wilderness_, other.wilderness_);
        aligned_allocations_.swap(other.aligned_allocations_);
    }

    // 分配
    [[nodiscard]] LCF_FORCE_INLINE
    void* acquire(size_t bytes) noexcept
    {
        if (bytes == 0) [[unlikely]]
        {
            return nullptr;
        }
        if (bytes > SIZE_MAX - (ALIGNMENT - 1)) [[unlikely]]
        {
            return nullptr;
        }
        const size_t aligned = align_up(bytes);
        if (aligned > SIZE_MAX - 2 * HEADER_SIZE - (ALIGNMENT - 1)) [[unlikely]]
        {
            return nullptr;
        }
        void* p = acquire_block(aligned);
        if (!p) [[unlikely]]
        {
            return nullptr;
        }
        const block_header* block = reinterpret_cast<const block_header*>(
            static_cast<const uint8_t*>(p) - HEADER_SIZE);
        account_acquire(block_size(block));
        return p;
    }

    [[nodiscard]] LCF_FORCE_INLINE
    void* acquire(size_t bytes, operating_message& error,
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
    [[nodiscard]] LCF_FORCE_INLINE
    void* acquire_sized() noexcept
    {
        static_assert(Size > 0, "acquire_sized<Size> requires Size > 0");
        if constexpr (Size > SIZE_MAX - (ALIGNMENT - 1))
        {
            return nullptr;
        }
        else
        {
            constexpr size_t aligned = (Size + ALIGNMENT - 1) & ~(ALIGNMENT - 1);
            if constexpr (aligned <= SMALL_MAX_SIZE)
            {
                constexpr size_t idx = small_class_index(aligned);
                void* p = small_heads_[idx];
                if (p) [[likely]]
                {
                    small_heads_[idx] = *static_cast<void**>(p);
                    --small_counts_[idx];
                    block_header* cb = reinterpret_cast<block_header*>(
                        static_cast<uint8_t*>(p) - HEADER_SIZE);
                    set_cached(cb, false);
                    set_in_use(cb, true);
                    --free_block_count_;
                    --cached_block_count_;
                    account_acquire(aligned);
                    return p;
                }
            }
            return acquire(Size);
        }
    }

    template <size_t Size>
    [[nodiscard]] LCF_FORCE_INLINE
    void* acquire_sized(operating_message& error,
                        std::source_location site = std::source_location::current()) noexcept
    {
        void* p = acquire_sized<Size>();
        if (!p) [[unlikely]]
        {
            error.write_failure(om_err_out_of_memory.code, msg_level::error, site);
        }
        return p;
    }

    [[nodiscard]] LCF_FORCE_INLINE
    void* acquire_zeroed(size_t bytes) noexcept
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
        if (alignment < base_alignment)
        {
            alignment = base_alignment;
        }
        if (bytes > SIZE_MAX - sizeof(void*) - alignment) [[unlikely]]
        {
            return nullptr;
        }
        const size_t raw_sum = bytes + alignment + sizeof(void*);
        if (raw_sum > SIZE_MAX - (ALIGNMENT - 1)) [[unlikely]]
        {
            return nullptr;
        }
        const size_t raw_size = align_up(raw_sum);
        if (raw_size > SIZE_MAX - 2 * HEADER_SIZE - (ALIGNMENT - 1)) [[unlikely]]
        {
            return nullptr;
        }
        void* raw = acquire_block(raw_size);
        if (!raw) [[unlikely]]
        {
            return nullptr;
        }
        block_header* raw_block = reinterpret_cast<block_header*>(
            static_cast<uint8_t*>(raw) - HEADER_SIZE);
        set_aligned_raw(raw_block, true);

        uintptr_t base = reinterpret_cast<uintptr_t>(raw) + sizeof(void*);
        uintptr_t user = (base + alignment - 1) & ~(static_cast<uintptr_t>(alignment) - 1);
        *reinterpret_cast<void**>(user - sizeof(void*)) = raw;
        register_aligned(reinterpret_cast<void*>(user), raw, bytes, alignment);
        account_acquire(bytes);
        return reinterpret_cast<void*>(user);
    }

    template <typename T, typename... Args>
    [[nodiscard]] LCF_FORCE_INLINE
    T* create(Args&&... args) noexcept
    {
        void* ptr;
        if constexpr (alignof(T) > ALIGNMENT)
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

    // 释放：只处理内存，不析构
    LCF_FORCE_INLINE bool soft_delete(void* ptr) noexcept
    {
        return release_validated<0>(ptr, false);
    }

    LCF_FORCE_INLINE bool hard_delete(void* ptr) noexcept
    {
        return release_validated<0>(ptr, true);
    }

    LCF_FORCE_INLINE bool soft_delete(void* ptr, operating_message& error,
                                  std::source_location site = std::source_location::current()) noexcept
    {
        if (release_validated<0>(ptr, false))
        {
            return true;
        }
        error.write_failure((ptr == nullptr) ? om_err_null_pointer.code : om_err_not_found.code,
                            msg_level::warn, site);
        return false;
    }

    LCF_FORCE_INLINE bool hard_delete(void* ptr, operating_message& error,
                                  std::source_location site = std::source_location::current()) noexcept
    {
        if (release_validated<0>(ptr, true))
        {
            return true;
        }
        error.write_failure((ptr == nullptr) ? om_err_null_pointer.code : om_err_not_found.code,
                            msg_level::warn, site);
        return false;
    }

    template <size_t Size>
    LCF_FORCE_INLINE bool soft_delete_sized(void* ptr) noexcept
    {
        static_assert(Size > 0, "soft_delete_sized<Size> requires Size > 0");
        return release_validated<Size>(ptr, false);
    }

    template <size_t Size>
    LCF_FORCE_INLINE bool hard_delete_sized(void* ptr) noexcept
    {
        static_assert(Size > 0, "hard_delete_sized<Size> requires Size > 0");
        return release_validated<Size>(ptr, true);
    }

    template <typename T>
    LCF_FORCE_INLINE void dispose(T* ptr) noexcept
    {
        if (ptr) [[likely]]
        {
            ptr->~T();
        }
    }

    // 查询
    [[nodiscard]] bool owns(const void* ptr) const noexcept
    {
        if (!ptr) [[unlikely]]
        {
            return false;
        }
        if (find_aligned(ptr))
        {
            return true;
        }
        const block_header* block = validate_live_block(ptr);
        return block != nullptr && !is_aligned_raw(block);
    }

    [[nodiscard]] size_t size_of(const void* ptr) const noexcept
    {
        if (!ptr) [[unlikely]]
        {
            return 0;
        }
        if (const aligned_record* rec = find_aligned(ptr))
        {
            return rec->size;
        }
        const block_header* block = validate_live_block(ptr);
        if (!block || is_aligned_raw(block)) [[unlikely]]
        {
            return 0;
        }
        return block_size(block);
    }

    [[nodiscard]] bool contains(const void* ptr) const noexcept
    {
        return find_chunk(ptr) != nullptr;
    }

    [[nodiscard]] memory_stats stats() const noexcept
    {
        memory_stats s{};
        s.reserved_bytes = reserved_bytes();
        s.live_bytes = live_bytes_;
        s.reusable_bytes = reusable_bytes();
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

    [[nodiscard]] constexpr size_t free_block_count() const noexcept
    {
        return free_block_count_;
    }

    [[nodiscard]] constexpr size_t chunk_size() const noexcept
    {
        return chunk_size_;
    }

    // 池管理
    void reset_stats() noexcept
    {
        acquire_count_ = 0;
        release_count_ = 0;
        soft_delete_count_ = 0;
        hard_delete_count_ = 0;
        peak_live_bytes_ = live_bytes_;
    }

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
        next_trim_class_ = 0;
        wilderness_ = nullptr;
        aligned_allocations_.clear();
        active_reserved_ = 0;
        live_bytes_ = 0;
        free_block_count_ = 0;
        cached_block_count_ = 0;
    }

    void release_all() noexcept
    {
        for (auto& chunk : memory_chunks_)
        {
            ::operator delete(chunk.data_);
            chunk.data_ = nullptr;
            chunk.size_ = 0;
        }
        memory_chunks_.clear();
        chunk_pool_.clear();
        free_lists_.fill(nullptr);
        fl_bitmap_ = 0;
        sl_bitmaps_.fill(0);
        small_heads_.fill(nullptr);
        small_counts_.fill(0);
        next_trim_class_ = 0;
        wilderness_ = nullptr;
        aligned_allocations_.clear();
        active_reserved_ = 0;
        live_bytes_ = 0;
        free_block_count_ = 0;
        cached_block_count_ = 0;
    }

    void increase_capacity(size_t bytes) noexcept
    {
        if (bytes == 0 || bytes <= reserved_bytes()) [[likely]]
        {
            return;
        }
        if (bytes > SIZE_MAX - 2 * HEADER_SIZE - (ALIGNMENT - 1)) [[unlikely]]
        {
            return;
        }
        while (reserved_bytes() < bytes)
        {
            const size_t remaining = bytes - reserved_bytes();
            add_new_chunk(remaining);
        }
    }

    void reduce_capacity(size_t target) noexcept
    {
        if (target >= reserved_bytes()) [[likely]]
        {
            return;
        }

        flush_cache();

        while (reserved_bytes() > target)
        {
            if (chunk_pool_.release_one() == 0)
            {
                break;
            }
        }

        for (auto it = memory_chunks_.begin(); it != memory_chunks_.end(); )
        {
            if (reserved_bytes() <= target)
            {
                break;
            }
            memory_block& chunk = *it;
            block_header* first_block = reinterpret_cast<block_header*>(chunk.data_);

            if (!is_in_use(first_block) && !is_cached(first_block) &&
                block_size(first_block) == chunk.size_ - 2 * HEADER_SIZE)
            {
                size_t fl, sl;
                size_to_index(block_size(first_block), fl, sl);
                remove_from_free_list(first_block, fl, sl);

                active_reserved_ -= chunk.size_;
                uint8_t* raw = chunk.data_;
                chunk.data_ = nullptr;
                chunk.size_ = 0;
                it = memory_chunks_.erase(it, it + 1);
                ::operator delete(raw);
            }
            else
            {
                ++it;
            }
        }
    }

    // 重分配：原位成功返回同一指针；目标为 0 时释放并返回 nullptr
    [[nodiscard]] LCF_FORCE_INLINE
    bool resize_inplace(void* ptr, size_t old_bytes, size_t new_bytes) noexcept
    {
        if (!ptr || new_bytes == 0) [[unlikely]]
        {
            return false;
        }
        block_header* block = validate_live_block(ptr);
        if (!block || is_aligned_raw(block)) [[unlikely]]
        {
            return false;
        }
        const size_t bsize = block_size(block);
        if (old_bytes > bsize) [[unlikely]]
        {
            return false;
        }
        if (new_bytes > SIZE_MAX - (ALIGNMENT - 1)) [[unlikely]]
        {
            return false;
        }
        const size_t new_aligned = align_up(new_bytes);
        if (new_aligned <= bsize) [[likely]]
        {
            return true;
        }

        uint8_t* block_ptr = reinterpret_cast<uint8_t*>(block);
        uint8_t* next_ptr = block_ptr + HEADER_SIZE + bsize;
        block_header* next_block = reinterpret_cast<block_header*>(next_ptr);
        if (!is_mergeable(next_block) ||
            bsize + HEADER_SIZE + block_size(next_block) < new_aligned) [[unlikely]]
        {
            return false;
        }
        if (next_block == wilderness_)
        {
            wilderness_ = nullptr;
            --free_block_count_;
        }
        else
        {
            size_t nf, ns;
            size_to_index(block_size(next_block), nf, ns);
            remove_from_free_list(next_block, nf, ns);
        }
        const size_t merged_size = bsize + HEADER_SIZE + block_size(next_block);
        set_block_size(block, merged_size);
        uint8_t* after_ptr = block_ptr + HEADER_SIZE + merged_size;
        block_header* after_block = reinterpret_cast<block_header*>(after_ptr);
        after_block->prev_physical_ = block;
        if (merged_size >= new_aligned + MIN_SPLIT) [[likely]]
        {
            split_block(block, new_aligned);
        }
        const size_t grown = block_size(block);
        if (grown > bsize)
        {
            live_bytes_ += grown - bsize;
            if (live_bytes_ > peak_live_bytes_) [[unlikely]]
            {
                peak_live_bytes_ = live_bytes_;
            }
        }
        return true;
    }

    [[nodiscard]] LCF_FORCE_INLINE
    void* resize(void* ptr, size_t old_bytes, size_t new_bytes) noexcept
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
        if (resize_inplace(ptr, old_bytes, new_bytes))
        {
            return ptr;
        }
        size_t alignment = base_alignment;
        if (const aligned_record* rec = find_aligned(ptr))
        {
            alignment = rec->alignment;
        }
        void* new_ptr = alignment > base_alignment
            ? acquire_aligned(new_bytes, alignment)
            : acquire(new_bytes);
        if (!new_ptr) [[unlikely]]
        {
            return nullptr;
        }
        const size_t available = size_of(ptr);
        size_t copy_size = old_bytes < new_bytes ? old_bytes : new_bytes;
        if (copy_size > available)
        {
            copy_size = available;
        }
        std::memcpy(new_ptr, ptr, copy_size);
        soft_delete(ptr);
        return new_ptr;
    }

    // 遍历空闲块 (调试/统计用)
    // 回调签名: void(void*, size_t)
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
        if (wilderness_)
        {
            fn(reinterpret_cast<void*>(const_cast<uint8_t*>(
                reinterpret_cast<const uint8_t*>(wilderness_) + HEADER_SIZE)), block_size(wilderness_));
        }
    }
};

inline void swap(memory_pool& a, memory_pool& b) noexcept
{
    a.swap(b);
}

} // namespace memory
