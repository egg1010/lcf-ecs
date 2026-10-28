#pragma once
// list_memory_pool.hpp - 链式内存池
// 小块走 chunk 分档 slot, 大块走 wilderness / 独立申请;
// 软删除与硬删除并列, 大块软删除按大小档缓存并保证同档优先复用.

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <new>
#include <utility>
#include "../dense.hpp"
#include "../force_inline.hpp"
#include "../operating_message.hpp"
#include "memory_common.hpp"
#include "oom_handler.hpp"

namespace memory {

class list_memory_pool
{
public:
    using size_type = size_t;

private:
    friend class layered_allocator;

    // === chunk 布局 ===
    static constexpr size_t CHUNK_SIZE = 4096;
    static constexpr size_t CHUNK_ALIGN = 4096;
    static constexpr uint32_t CHUNK_MAGIC = 0xC4C1C4C1u;
    static constexpr uint32_t BIG_MAGIC = 0xB16B16B1u;
    static constexpr size_t SLOT_BITMAP_BYTES = 32;
    static constexpr size_t SLOT_BITMAP_WORDS = SLOT_BITMAP_BYTES / sizeof(uint32_t);

    struct alignas(64) chunk_header
    {
        char* bump_ptr;
        char* bump_end;
        uint32_t magic;
        uint32_t size_class;
        uint32_t slot_size;
        uint32_t slot_count;
        uint32_t slot_shift;
    };

    static constexpr size_t CHUNK_HEADER_SIZE = sizeof(chunk_header);
    static constexpr size_t DATA_AREA_SIZE = CHUNK_SIZE - CHUNK_HEADER_SIZE - SLOT_BITMAP_BYTES;
    static_assert(sizeof(chunk_header) == 64, "chunk_header must keep DATA_AREA_SIZE stable");
    static_assert(CHUNK_HEADER_SIZE + DATA_AREA_SIZE + SLOT_BITMAP_BYTES == CHUNK_SIZE,
                  "chunk layout must fill exactly one CHUNK_SIZE block");

    static constexpr size_t k_slot_sizes[] = { 16, 32, 64, 128, 256, 512, 1024, 2048, DATA_AREA_SIZE };
    static constexpr size_t SIZE_CLASS_COUNT = sizeof(k_slot_sizes) / sizeof(k_slot_sizes[0]);
    static_assert(SLOT_BITMAP_WORDS * 32 >= DATA_AREA_SIZE / k_slot_sizes[0],
                  "slot bitmap must cover the smallest size class");
    static constexpr size_t SMALL_BLOCK_THRESHOLD = DATA_AREA_SIZE;
    static constexpr size_t ALIGNMENT = base_alignment;

    // === chunk wilderness ===
    static constexpr size_t CHUNK_WILD_SIZE = 1 << 20;

    struct chunk_wilderness
    {
        char* base;
        size_t total_size;
        size_t used_offset;
        chunk_wilderness* next;
    };

    static constexpr size_t CHUNK_WILD_HEADER_ALIGNED =
        (sizeof(chunk_wilderness) + CHUNK_SIZE - 1) & ~(CHUNK_SIZE - 1);

    // === 大块布局 ===
    enum class big_state : uint8_t
    {
        live = 0,
        soft = 1,
        released = 2
    };

    struct alignas(16) big_block_header
    {
        big_block_header* prev;
        big_block_header* next;
        big_block_header* soft_prev;
        big_block_header* soft_next;
        size_t total_size;
        size_t user_size;
        uint32_t magic;
        uint8_t state;
        uint8_t from_wilderness;
        uint8_t aligned_raw;
        uint8_t soft_class;
    };

    static_assert(sizeof(big_block_header) % ALIGNMENT == 0,
                  "big_block_header must keep user area 16-byte aligned");
    static constexpr size_t BIG_HEADER_SIZE = sizeof(big_block_header);

    static constexpr size_t BIG_WILDERNESS_SIZE = 1 << 20;
    static constexpr size_t BIG_WILDERNESS_THRESHOLD = BIG_WILDERNESS_SIZE / 2;

    struct big_wilderness_header
    {
        char* base;
        size_t total_size;
        size_t used_offset;
        big_wilderness_header* next;
    };

    static constexpr size_t BIG_WILD_HEADER_ALIGNED =
        (sizeof(big_wilderness_header) + ALIGNMENT - 1) & ~(ALIGNMENT - 1);

    static constexpr size_t BIG_CLASS_COUNT = 64;

    struct aligned_record
    {
        void* user;
        void* raw;
        size_t size;
        size_t alignment;
    };

    // === 成员 ===
    std::array<void*, SIZE_CLASS_COUNT> global_free_heads_{};
    std::array<chunk_header*, SIZE_CLASS_COUNT> active_chunks_{};
    chunk_wilderness* chunk_wild_head_{ nullptr };

    big_block_header* big_head_{ nullptr };
    std::array<big_block_header*, BIG_CLASS_COUNT> soft_heads_{};
    big_wilderness_header* big_wild_head_{ nullptr };
    dense<big_block_header*> big_blocks_;
    dense<aligned_record> aligned_allocations_;

    size_t big_block_count_{ 0 };
    size_t big_soft_count_{ 0 };
    size_t reserved_bytes_{ 0 };
    size_t live_bytes_{ 0 };
    size_t free_slot_bytes_{ 0 };
    size_t soft_bytes_{ 0 };
    size_t peak_live_bytes_{ 0 };
    size_t acquire_count_{ 0 };
    size_t release_count_{ 0 };
    size_t soft_delete_count_{ 0 };
    size_t hard_delete_count_{ 0 };

    // === 分档 ===
    [[nodiscard]] static constexpr size_t size_class_for_size(size_t bytes) noexcept
    {
        const size_t idx = static_cast<size_t>(std::bit_width(bytes - 1));
        return idx <= 4 ? 0 : idx - 4;
    }

    [[nodiscard]] static constexpr size_t big_class_for_size(size_t bytes) noexcept
    {
        const size_t idx = static_cast<size_t>(std::bit_width(bytes - 1));
        return idx < BIG_CLASS_COUNT ? idx : BIG_CLASS_COUNT - 1;
    }

    [[nodiscard]] static constexpr size_t slot_count_for_class(size_t cls) noexcept
    {
        return DATA_AREA_SIZE / k_slot_sizes[cls];
    }

    [[nodiscard]] static constexpr size_t slot_shift_for_size(size_t slot_size) noexcept
    {
        if (slot_size == 0 || (slot_size & (slot_size - 1)) != 0)
        {
            return 0;
        }
        return static_cast<size_t>(std::countr_zero(slot_size));
    }

    [[nodiscard]] static constexpr size_t align_up(size_t size) noexcept
    {
        return (size + ALIGNMENT - 1) & ~(ALIGNMENT - 1);
    }

    // === slot 位图 ===
    [[nodiscard]] static uint32_t* slot_bitmap(chunk_header* c) noexcept
    {
        return reinterpret_cast<uint32_t*>(
            reinterpret_cast<char*>(c) + CHUNK_HEADER_SIZE + DATA_AREA_SIZE);
    }

    [[nodiscard]] static const uint32_t* slot_bitmap(const chunk_header* c) noexcept
    {
        return reinterpret_cast<const uint32_t*>(
            reinterpret_cast<const char*>(c) + CHUNK_HEADER_SIZE + DATA_AREA_SIZE);
    }

    [[nodiscard]] static bool slot_bit_test(const chunk_header* c, size_t idx) noexcept
    {
        return (slot_bitmap(c)[idx >> 5] &
                (1U << static_cast<unsigned>(idx & 31))) != 0;
    }

    static void slot_bit_set(chunk_header* c, size_t idx) noexcept
    {
        slot_bitmap(c)[idx >> 5] |= (1U << static_cast<unsigned>(idx & 31));
    }

    static void slot_bit_clear(chunk_header* c, size_t idx) noexcept
    {
        slot_bitmap(c)[idx >> 5] &= ~(1U << static_cast<unsigned>(idx & 31));
    }

    // 返回 SIZE_MAX 表示 ptr 不是该 chunk 的合法 slot 起点
    [[nodiscard]] static size_t slot_index_of(const chunk_header* c, const void* ptr) noexcept
    {
        const uintptr_t data = reinterpret_cast<uintptr_t>(c) + CHUNK_HEADER_SIZE;
        const uintptr_t addr = reinterpret_cast<uintptr_t>(ptr);
        if (addr < data)
        {
            return SIZE_MAX;
        }
        const uintptr_t offset = addr - data;
        if (c->slot_shift == 0)
        {
            return offset == 0 ? 0 : SIZE_MAX;
        }
        const uintptr_t mask = (uintptr_t{ 1 } << c->slot_shift) - 1;
        if ((offset & mask) != 0)
        {
            return SIZE_MAX;
        }
        const uintptr_t idx = offset >> c->slot_shift;
        if (idx >= static_cast<uintptr_t>(c->slot_count))
        {
            return SIZE_MAX;
        }
        return static_cast<size_t>(idx);
    }

    [[nodiscard]] chunk_header* find_chunk_owner(const void* ptr) const noexcept
    {
        if (!ptr)
        {
            return nullptr;
        }
        const uintptr_t addr = reinterpret_cast<uintptr_t>(ptr);
        for (const chunk_wilderness* w = chunk_wild_head_; w; w = w->next)
        {
            const uintptr_t base = reinterpret_cast<uintptr_t>(w->base);
            if (addr < base + CHUNK_WILD_HEADER_ALIGNED || addr >= base + w->used_offset)
            {
                continue;
            }
            // chunk 按 CHUNK_SIZE 对齐切分, 掩码得到的地址必为真实 chunk 起点
            const uintptr_t chunk_addr = addr & ~static_cast<uintptr_t>(CHUNK_SIZE - 1);
            auto* c = reinterpret_cast<chunk_header*>(chunk_addr);
            if (c->magic != CHUNK_MAGIC)
            {
                return nullptr;
            }
            const uintptr_t data_start = chunk_addr + CHUNK_HEADER_SIZE;
            const uintptr_t data_end = data_start + DATA_AREA_SIZE;
            if (addr >= data_start && addr < data_end)
            {
                return c;
            }
            return nullptr;
        }
        return nullptr;
    }

    // === 大块登记表 ===
    void register_big_block(big_block_header* bh) noexcept
    {
        size_t lo = 0;
        size_t hi = big_blocks_.size();
        while (lo < hi)
        {
            size_t mid = lo + (hi - lo) / 2;
            if (reinterpret_cast<uintptr_t>(big_blocks_[mid]) <
                reinterpret_cast<uintptr_t>(bh))
            {
                lo = mid + 1;
            }
            else
            {
                hi = mid;
            }
        }
        big_blocks_.emplace(big_blocks_.begin() + lo, bh);
    }

    void unregister_big_block(big_block_header* bh) noexcept
    {
        size_t lo = 0;
        size_t hi = big_blocks_.size();
        while (lo < hi)
        {
            size_t mid = lo + (hi - lo) / 2;
            if (reinterpret_cast<uintptr_t>(big_blocks_[mid]) <
                reinterpret_cast<uintptr_t>(bh))
            {
                lo = mid + 1;
            }
            else
            {
                hi = mid;
            }
        }
        if (lo < big_blocks_.size() && big_blocks_[lo] == bh)
        {
            auto it = big_blocks_.begin() + lo;
            big_blocks_.erase(it, it + 1);
        }
    }

    [[nodiscard]] const big_block_header* find_big_owner(const void* ptr) const noexcept
    {
        if (!ptr)
        {
            return nullptr;
        }
        const uintptr_t addr = reinterpret_cast<uintptr_t>(ptr);
        if (addr < BIG_HEADER_SIZE)
        {
            return nullptr;
        }
        const uintptr_t target = addr - BIG_HEADER_SIZE;
        size_t lo = 0;
        size_t hi = big_blocks_.size();
        while (lo < hi)
        {
            size_t mid = lo + (hi - lo) / 2;
            if (reinterpret_cast<uintptr_t>(big_blocks_[mid]) < target)
            {
                lo = mid + 1;
            }
            else
            {
                hi = mid;
            }
        }
        if (lo >= big_blocks_.size())
        {
            return nullptr;
        }
        const big_block_header* bh = big_blocks_[lo];
        if (reinterpret_cast<uintptr_t>(bh) != target || bh->magic != BIG_MAGIC)
        {
            return nullptr;
        }
        return bh;
    }

    [[nodiscard]] big_block_header* find_big_owner(void* ptr) noexcept
    {
        return const_cast<big_block_header*>(
            static_cast<const list_memory_pool*>(this)->find_big_owner(
                static_cast<const void*>(ptr)));
    }

    // === 对齐分配登记表 ===
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
        if (lo >= aligned_allocations_.size())
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

    void release_slot(chunk_header* c, size_t idx, bool hard) noexcept
    {
        live_bytes_ -= c->slot_size;
        ++release_count_;
        if (hard)
        {
            ++hard_delete_count_;
        }
        else
        {
            ++soft_delete_count_;
        }
        slot_bit_clear(c, idx);
        void* ptr = reinterpret_cast<char*>(c) + CHUNK_HEADER_SIZE +
            idx * static_cast<size_t>(c->slot_size);
        auto* node = static_cast<void**>(ptr);
        node[0] = global_free_heads_[c->size_class];
        node[1] = c;
        global_free_heads_[c->size_class] = ptr;
        free_slot_bytes_ += c->slot_size;
    }

    template <size_t MinSize>
    bool release_validated(void* ptr, bool hard) noexcept
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
            big_block_header* raw_bh = find_big_owner(rec->raw);
            if (!raw_bh ||
                raw_bh->state != static_cast<uint8_t>(big_state::live) ||
                !raw_bh->aligned_raw ||
                raw_bh->user_size < rec->size) [[unlikely]]
            {
                return false;
            }
            unregister_aligned(ptr);
            release_big(raw_bh, hard);
            return true;
        }
        if (chunk_header* c = find_chunk_owner(ptr))
        {
            const size_t idx = slot_index_of(c, ptr);
            if (idx == SIZE_MAX || !slot_bit_test(c, idx)) [[unlikely]]
            {
                return false;
            }
            if constexpr (MinSize > 0)
            {
                if (c->slot_size < MinSize) [[unlikely]]
                {
                    return false;
                }
            }
            release_slot(c, idx, hard);
            return true;
        }
        big_block_header* bh = find_big_owner(ptr);
        if (!bh || bh->state != static_cast<uint8_t>(big_state::live) || bh->aligned_raw) [[unlikely]]
        {
            return false;
        }
        if constexpr (MinSize > 0)
        {
            if (bh->user_size < MinSize) [[unlikely]]
            {
                return false;
            }
        }
        release_big(bh, hard);
        return true;
    }

    // === wilderness ===
    [[nodiscard]] chunk_wilderness* allocate_chunk_wilderness() noexcept
    {
        void* raw = ::operator new(CHUNK_WILD_SIZE, std::align_val_t{ CHUNK_ALIGN }, std::nothrow);
        if (!raw) [[unlikely]]
        {
            handle_oom(CHUNK_WILD_SIZE, "list_memory_pool::allocate_chunk_wilderness");
        }
        auto* w = static_cast<chunk_wilderness*>(raw);
        w->base = static_cast<char*>(raw);
        w->total_size = CHUNK_WILD_SIZE;
        w->used_offset = CHUNK_WILD_HEADER_ALIGNED;
        w->next = chunk_wild_head_;
        chunk_wild_head_ = w;
        reserved_bytes_ += CHUNK_WILD_SIZE;
        return w;
    }

    [[nodiscard]] chunk_header* allocate_chunk() noexcept
    {
        chunk_wilderness* w = chunk_wild_head_;
        while (w && w->total_size - w->used_offset < CHUNK_SIZE)
        {
            w = w->next;
        }
        if (!w) [[unlikely]]
        {
            w = allocate_chunk_wilderness();
        }
        char* p = w->base + w->used_offset;
        w->used_offset += CHUNK_SIZE;
        return reinterpret_cast<chunk_header*>(p);
    }

    void init_chunk_for_class(chunk_header* c, size_t cls) noexcept
    {
        c->magic = CHUNK_MAGIC;
        c->size_class = static_cast<uint32_t>(cls);
        c->slot_size = static_cast<uint32_t>(k_slot_sizes[cls]);
        c->slot_count = static_cast<uint32_t>(slot_count_for_class(cls));
        c->slot_shift = static_cast<uint32_t>(slot_shift_for_size(k_slot_sizes[cls]));
        c->bump_ptr = reinterpret_cast<char*>(c) + CHUNK_HEADER_SIZE;
        c->bump_end = c->bump_ptr + static_cast<size_t>(c->slot_count) * c->slot_size;
        std::memset(slot_bitmap(c), 0, SLOT_BITMAP_BYTES);
    }

    [[nodiscard]] chunk_header* ensure_active_chunk(size_t cls) noexcept
    {
        chunk_header* c = active_chunks_[cls];
        if (c && c->bump_ptr < c->bump_end) [[likely]]
        {
            return c;
        }
        c = allocate_chunk();
        init_chunk_for_class(c, cls);
        active_chunks_[cls] = c;
        return c;
    }

    // === 大块软缓存 ===
    void soft_list_push(big_block_header* bh) noexcept
    {
        const size_t cls = big_class_for_size(bh->user_size);
        bh->soft_class = static_cast<uint8_t>(cls);
        bh->soft_prev = nullptr;
        bh->soft_next = soft_heads_[cls];
        if (soft_heads_[cls])
        {
            soft_heads_[cls]->soft_prev = bh;
        }
        soft_heads_[cls] = bh;
    }

    void soft_list_remove(big_block_header* bh) noexcept
    {
        const size_t cls = bh->soft_class;
        if (bh->soft_prev)
        {
            bh->soft_prev->soft_next = bh->soft_next;
        }
        else
        {
            soft_heads_[cls] = bh->soft_next;
        }
        if (bh->soft_next)
        {
            bh->soft_next->soft_prev = bh->soft_prev;
        }
        bh->soft_prev = nullptr;
        bh->soft_next = nullptr;
    }

    // 同档请求只从本档软缓存中找容量足够的块
    [[nodiscard]] big_block_header* take_soft_big(size_t cls, size_t bytes) noexcept
    {
        big_block_header* bh = soft_heads_[cls];
        while (bh && bh->user_size < bytes)
        {
            bh = bh->soft_next;
        }
        if (!bh)
        {
            return nullptr;
        }
        soft_list_remove(bh);
        soft_bytes_ -= bh->user_size;
        --big_soft_count_;
        bh->user_size = bytes;
        bh->state = static_cast<uint8_t>(big_state::live);
        bh->aligned_raw = 0;
        return bh;
    }

    void link_big_block(big_block_header* bh, size_t bytes, size_t total,
                        bool from_wilderness) noexcept
    {
        bh->prev = nullptr;
        bh->next = big_head_;
        if (big_head_)
        {
            big_head_->prev = bh;
        }
        big_head_ = bh;
        bh->soft_prev = nullptr;
        bh->soft_next = nullptr;
        bh->total_size = total;
        bh->user_size = bytes;
        bh->magic = BIG_MAGIC;
        bh->state = static_cast<uint8_t>(big_state::live);
        bh->from_wilderness = from_wilderness ? 1 : 0;
        bh->aligned_raw = 0;
        bh->soft_class = 0;
        ++big_block_count_;
        register_big_block(bh);
    }

    void unlink_big_block(big_block_header* bh) noexcept
    {
        if (bh->prev)
        {
            bh->prev->next = bh->next;
        }
        else
        {
            big_head_ = bh->next;
        }
        if (bh->next)
        {
            bh->next->prev = bh->prev;
        }
    }

    [[nodiscard]] big_block_header* carve_big_wilderness(size_t total) noexcept
    {
        big_wilderness_header* w = big_wild_head_;
        while (w)
        {
            if (w->used_offset <= w->total_size && total <= w->total_size - w->used_offset)
            {
                break;
            }
            w = w->next;
        }
        if (!w)
        {
            void* raw = ::operator new(BIG_WILDERNESS_SIZE,
                                       std::align_val_t{ CHUNK_ALIGN }, std::nothrow);
            if (!raw) [[unlikely]]
            {
                return nullptr;
            }
            w = static_cast<big_wilderness_header*>(raw);
            w->base = static_cast<char*>(raw);
            w->total_size = BIG_WILDERNESS_SIZE;
            w->used_offset = BIG_WILD_HEADER_ALIGNED;
            w->next = big_wild_head_;
            big_wild_head_ = w;
            reserved_bytes_ += BIG_WILDERNESS_SIZE;
        }
        char* p = w->base + w->used_offset;
        w->used_offset += total;
        return reinterpret_cast<big_block_header*>(p);
    }

    [[nodiscard]] big_block_header* allocate_new_big(size_t bytes) noexcept
    {
        if (bytes > SIZE_MAX - (ALIGNMENT - 1)) [[unlikely]]
        {
            return nullptr;
        }
        const size_t aligned = align_up(bytes);
        if (aligned > SIZE_MAX - BIG_HEADER_SIZE) [[unlikely]]
        {
            return nullptr;
        }
        const size_t total = BIG_HEADER_SIZE + aligned;

        if (total < BIG_WILDERNESS_THRESHOLD)
        {
            if (big_block_header* bh = carve_big_wilderness(total))
            {
                link_big_block(bh, bytes, total, true);
                return bh;
            }
        }

        void* raw = ::operator new(total, std::align_val_t{ CHUNK_ALIGN }, std::nothrow);
        if (!raw) [[unlikely]]
        {
            handle_oom(total, "list_memory_pool::allocate_new_big");
        }
        reserved_bytes_ += total;
        auto* bh = static_cast<big_block_header*>(raw);
        link_big_block(bh, bytes, total, false);
        return bh;
    }

    [[nodiscard]] big_block_header* acquire_big(size_t bytes, bool aligned_raw) noexcept
    {
        big_block_header* bh = take_soft_big(big_class_for_size(bytes), bytes);
        if (!bh) [[unlikely]]
        {
            bh = allocate_new_big(bytes);
            if (!bh)
            {
                return nullptr;
            }
        }
        if (aligned_raw)
        {
            bh->aligned_raw = 1;
        }
        return bh;
    }

    // 硬删除独立大块直接归还系统; wilderness 内大块仅解除跟踪
    void release_big(big_block_header* bh, bool hard) noexcept
    {
        const size_t bytes = bh->user_size;
        live_bytes_ -= bytes;
        ++release_count_;
        bh->aligned_raw = 0;

        if (!hard)
        {
            ++soft_delete_count_;
            bh->state = static_cast<uint8_t>(big_state::soft);
            soft_list_push(bh);
            soft_bytes_ += bytes;
            ++big_soft_count_;
            return;
        }

        ++hard_delete_count_;
        bh->state = static_cast<uint8_t>(big_state::released);
        unlink_big_block(bh);
        unregister_big_block(bh);
        --big_block_count_;
        if (!bh->from_wilderness)
        {
            const size_t total = bh->total_size;
            reserved_bytes_ -= total;
            ::operator delete(bh, total, std::align_val_t{ CHUNK_ALIGN });
        }
    }

    void account_acquire(size_t bytes) noexcept
    {
        live_bytes_ += bytes;
        if (live_bytes_ > peak_live_bytes_) [[unlikely]]
        {
            peak_live_bytes_ = live_bytes_;
        }
        ++acquire_count_;
    }

    [[nodiscard]] void* take_slot(size_t cls) noexcept
    {
        void* p = global_free_heads_[cls];
        if (p) [[likely]]
        {
            auto* node = static_cast<void**>(p);
            global_free_heads_[cls] = node[0];
            auto* c = static_cast<chunk_header*>(node[1]);
            const size_t idx = slot_index_of(c, p);
            if (idx == SIZE_MAX) [[unlikely]]
            {
                return nullptr;
            }
            slot_bit_set(c, idx);
            free_slot_bytes_ -= c->slot_size;
            return p;
        }

        chunk_header* c = ensure_active_chunk(cls);
        if (c->bump_ptr >= c->bump_end) [[unlikely]]
        {
            return nullptr;
        }
        void* ptr = c->bump_ptr;
        c->bump_ptr += c->slot_size;
        const size_t idx = slot_index_of(c, ptr);
        if (idx == SIZE_MAX) [[unlikely]]
        {
            return nullptr;
        }
        slot_bit_set(c, idx);
        return ptr;
    }

    [[nodiscard]] size_t reusable_bytes() const noexcept
    {
        size_t total = free_slot_bytes_;
        for (size_t cls = 0; cls < SIZE_CLASS_COUNT; ++cls)
        {
            const chunk_header* c = active_chunks_[cls];
            if (c && c->bump_ptr < c->bump_end)
            {
                total += static_cast<size_t>(c->bump_end - c->bump_ptr);
            }
        }
        total += soft_bytes_;
        for (const chunk_wilderness* w = chunk_wild_head_; w; w = w->next)
        {
            total += w->total_size - w->used_offset;
        }
        for (const big_wilderness_header* w = big_wild_head_; w; w = w->next)
        {
            total += w->total_size - w->used_offset;
        }
        return total;
    }

public:
    // === 构造/析构 ===
    list_memory_pool() noexcept = default;

    ~list_memory_pool() noexcept
    {
        release_all();
    }

    list_memory_pool(const list_memory_pool&) = delete;
    list_memory_pool& operator=(const list_memory_pool&) = delete;

    list_memory_pool(list_memory_pool&& other) noexcept
    {
        swap(other);
    }

    list_memory_pool& operator=(list_memory_pool&& other) noexcept
    {
        if (this != &other) [[likely]]
        {
            release_all();
            swap(other);
        }
        return *this;
    }

    void swap(list_memory_pool& other) noexcept
    {
        std::swap(global_free_heads_, other.global_free_heads_);
        std::swap(active_chunks_, other.active_chunks_);
        std::swap(chunk_wild_head_, other.chunk_wild_head_);
        std::swap(big_head_, other.big_head_);
        std::swap(soft_heads_, other.soft_heads_);
        std::swap(big_wild_head_, other.big_wild_head_);
        big_blocks_.swap(other.big_blocks_);
        aligned_allocations_.swap(other.aligned_allocations_);
        std::swap(big_block_count_, other.big_block_count_);
        std::swap(big_soft_count_, other.big_soft_count_);
        std::swap(reserved_bytes_, other.reserved_bytes_);
        std::swap(live_bytes_, other.live_bytes_);
        std::swap(free_slot_bytes_, other.free_slot_bytes_);
        std::swap(soft_bytes_, other.soft_bytes_);
        std::swap(peak_live_bytes_, other.peak_live_bytes_);
        std::swap(acquire_count_, other.acquire_count_);
        std::swap(release_count_, other.release_count_);
        std::swap(soft_delete_count_, other.soft_delete_count_);
        std::swap(hard_delete_count_, other.hard_delete_count_);
    }

    // === 分配 ===
    [[nodiscard]] void* acquire(size_t bytes) noexcept
    {
        if (bytes == 0) [[unlikely]]
        {
            return nullptr;
        }
        if (bytes > SMALL_BLOCK_THRESHOLD)
        {
            big_block_header* bh = acquire_big(bytes, false);
            if (!bh) [[unlikely]]
            {
                return nullptr;
            }
            account_acquire(bytes);
            return reinterpret_cast<char*>(bh) + BIG_HEADER_SIZE;
        }
        const size_t cls = size_class_for_size(bytes);
        void* p = take_slot(cls);
        if (!p) [[unlikely]]
        {
            return nullptr;
        }
        account_acquire(k_slot_sizes[cls]);
        return p;
    }

    [[nodiscard]] void* acquire(size_t bytes, operating_message& error,
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
        if constexpr (Size > SMALL_BLOCK_THRESHOLD)
        {
            return acquire(Size);
        }
        else
        {
            constexpr size_t cls = size_class_for_size(Size);
            void* p = take_slot(cls);
            if (!p) [[unlikely]]
            {
                return nullptr;
            }
            account_acquire(k_slot_sizes[cls]);
            return p;
        }
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

    [[nodiscard]] void* acquire_zeroed(size_t bytes) noexcept
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
        if (alignment < ALIGNMENT)
        {
            alignment = ALIGNMENT;
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
        big_block_header* bh = acquire_big(raw_size, true);
        if (!bh) [[unlikely]]
        {
            return nullptr;
        }
        void* raw = reinterpret_cast<char*>(bh) + BIG_HEADER_SIZE;
        const uintptr_t base = reinterpret_cast<uintptr_t>(raw) + sizeof(void*);
        const uintptr_t user = (base + alignment - 1) & ~(static_cast<uintptr_t>(alignment) - 1);
        *reinterpret_cast<void**>(user - sizeof(void*)) = raw;
        register_aligned(reinterpret_cast<void*>(user), raw, bytes, alignment);
        account_acquire(raw_size);
        return reinterpret_cast<void*>(user);
    }

    template <typename T, typename... Args>
    [[nodiscard]] FORCE_INLINE T* create(Args&&... args) noexcept
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

    // === 释放: 只处理内存, 不析构 ===
    bool soft_delete(void* ptr) noexcept
    {
        return release_validated<0>(ptr, false);
    }

    bool hard_delete(void* ptr) noexcept
    {
        return release_validated<0>(ptr, true);
    }

    bool soft_delete(void* ptr, operating_message& error,
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

    bool hard_delete(void* ptr, operating_message& error,
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
    FORCE_INLINE bool soft_delete_sized(void* ptr) noexcept
    {
        static_assert(Size > 0, "soft_delete_sized<Size> requires Size > 0");
        return release_validated<Size>(ptr, false);
    }

    template <size_t Size>
    FORCE_INLINE bool hard_delete_sized(void* ptr) noexcept
    {
        static_assert(Size > 0, "hard_delete_sized<Size> requires Size > 0");
        return release_validated<Size>(ptr, true);
    }

    template <typename T>
    FORCE_INLINE void dispose(T* ptr) noexcept
    {
        if (ptr) [[likely]]
        {
            ptr->~T();
        }
    }

    // === 查询 ===
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
        if (const chunk_header* c = find_chunk_owner(ptr))
        {
            const size_t idx = slot_index_of(c, ptr);
            return idx != SIZE_MAX && slot_bit_test(c, idx);
        }
        const big_block_header* bh = find_big_owner(ptr);
        return bh && bh->state == static_cast<uint8_t>(big_state::live) && !bh->aligned_raw;
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
        if (const chunk_header* c = find_chunk_owner(ptr))
        {
            const size_t idx = slot_index_of(c, ptr);
            return (idx != SIZE_MAX && slot_bit_test(c, idx)) ? c->slot_size : 0;
        }
        const big_block_header* bh = find_big_owner(ptr);
        if (!bh || bh->state != static_cast<uint8_t>(big_state::live) || bh->aligned_raw)
        {
            return 0;
        }
        return bh->user_size;
    }

    [[nodiscard]] bool contains(const void* ptr) const noexcept
    {
        if (!ptr) [[unlikely]]
        {
            return false;
        }
        if (find_chunk_owner(ptr))
        {
            return true;
        }
        const uintptr_t addr = reinterpret_cast<uintptr_t>(ptr);
        for (const big_block_header* bh = big_head_; bh; bh = bh->next)
        {
            const uintptr_t begin = reinterpret_cast<uintptr_t>(bh);
            if (addr >= begin && addr < begin + bh->total_size)
            {
                return true;
            }
        }
        for (const big_wilderness_header* w = big_wild_head_; w; w = w->next)
        {
            const uintptr_t begin = reinterpret_cast<uintptr_t>(w->base);
            if (addr >= begin + BIG_WILD_HEADER_ALIGNED && addr < begin + w->used_offset)
            {
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] memory_stats stats() const noexcept
    {
        memory_stats s{};
        s.reserved_bytes = reserved_bytes_;
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

    // === 池管理 ===
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
        big_block_header* bh = big_head_;
        while (bh)
        {
            big_block_header* next = bh->next;
            if (!bh->from_wilderness)
            {
                const size_t total = bh->total_size;
                reserved_bytes_ -= total;
                ::operator delete(bh, total, std::align_val_t{ CHUNK_ALIGN });
            }
            bh = next;
        }
        big_head_ = nullptr;
        soft_heads_.fill(nullptr);
        big_blocks_.clear();
        big_block_count_ = 0;
        big_soft_count_ = 0;
        soft_bytes_ = 0;

        global_free_heads_.fill(nullptr);
        active_chunks_.fill(nullptr);
        free_slot_bytes_ = 0;

        for (chunk_wilderness* w = chunk_wild_head_; w; w = w->next)
        {
            w->used_offset = CHUNK_WILD_HEADER_ALIGNED;
        }
        for (big_wilderness_header* w = big_wild_head_; w; w = w->next)
        {
            w->used_offset = BIG_WILD_HEADER_ALIGNED;
        }

        aligned_allocations_.clear();
        live_bytes_ = 0;
    }

    void release_all() noexcept
    {
        chunk_wilderness* cwild = chunk_wild_head_;
        while (cwild)
        {
            chunk_wilderness* next = cwild->next;
            ::operator delete(cwild->base, CHUNK_WILD_SIZE, std::align_val_t{ CHUNK_ALIGN });
            cwild = next;
        }
        chunk_wild_head_ = nullptr;

        big_block_header* bh = big_head_;
        while (bh)
        {
            big_block_header* next = bh->next;
            if (!bh->from_wilderness)
            {
                const size_t total = bh->total_size;
                ::operator delete(bh, total, std::align_val_t{ CHUNK_ALIGN });
            }
            bh = next;
        }
        big_head_ = nullptr;

        big_wilderness_header* bwild = big_wild_head_;
        while (bwild)
        {
            big_wilderness_header* next = bwild->next;
            ::operator delete(bwild->base, BIG_WILDERNESS_SIZE, std::align_val_t{ CHUNK_ALIGN });
            bwild = next;
        }
        big_wild_head_ = nullptr;

        global_free_heads_.fill(nullptr);
        active_chunks_.fill(nullptr);
        soft_heads_.fill(nullptr);
        big_blocks_.clear();
        aligned_allocations_.clear();
        big_block_count_ = 0;
        big_soft_count_ = 0;
        reserved_bytes_ = 0;
        live_bytes_ = 0;
        free_slot_bytes_ = 0;
        soft_bytes_ = 0;
    }

    void increase_capacity(size_t bytes) noexcept
    {
        if (bytes == 0 || bytes <= reserved_bytes_) [[likely]]
        {
            return;
        }
        while (reserved_bytes_ < bytes)
        {
            (void)allocate_chunk_wilderness();
        }
    }

    void reduce_capacity(size_t target) noexcept
    {
        if (target >= reserved_bytes_) [[likely]]
        {
            return;
        }

        // 软删除的独立大块可直接归还系统
        big_block_header* bh = big_head_;
        while (bh && reserved_bytes_ > target)
        {
            big_block_header* next = bh->next;
            if (bh->state == static_cast<uint8_t>(big_state::soft) && !bh->from_wilderness)
            {
                soft_list_remove(bh);
                soft_bytes_ -= bh->user_size;
                --big_soft_count_;
                unlink_big_block(bh);
                unregister_big_block(bh);
                --big_block_count_;
                const size_t total = bh->total_size;
                reserved_bytes_ -= total;
                ::operator delete(bh, total, std::align_val_t{ CHUNK_ALIGN });
            }
            bh = next;
        }

        chunk_wilderness** cp = &chunk_wild_head_;
        while (*cp && reserved_bytes_ > target)
        {
            chunk_wilderness* w = *cp;
            if (w->used_offset <= CHUNK_WILD_HEADER_ALIGNED)
            {
                *cp = w->next;
                reserved_bytes_ -= w->total_size;
                ::operator delete(w->base, CHUNK_WILD_SIZE, std::align_val_t{ CHUNK_ALIGN });
            }
            else
            {
                cp = &w->next;
            }
        }

        big_wilderness_header** bp = &big_wild_head_;
        while (*bp && reserved_bytes_ > target)
        {
            big_wilderness_header* w = *bp;
            if (w->used_offset <= BIG_WILD_HEADER_ALIGNED)
            {
                *bp = w->next;
                reserved_bytes_ -= w->total_size;
                ::operator delete(w->base, BIG_WILDERNESS_SIZE, std::align_val_t{ CHUNK_ALIGN });
            }
            else
            {
                bp = &w->next;
            }
        }
    }

    // === 重分配 ===
    [[nodiscard]] bool resize_inplace(void* ptr, size_t old_bytes, size_t new_bytes) noexcept
    {
        if (!ptr || new_bytes == 0) [[unlikely]]
        {
            return false;
        }
        const size_t available = size_of(ptr);
        if (available == 0 || old_bytes > available) [[unlikely]]
        {
            return false;
        }
        if (find_aligned(ptr)) [[unlikely]]
        {
            return false;
        }
        if (const chunk_header* c = find_chunk_owner(ptr))
        {
            return new_bytes <= c->slot_size;
        }
        big_block_header* bh = find_big_owner(ptr);
        if (!bh || bh->state != static_cast<uint8_t>(big_state::live) || bh->aligned_raw) [[unlikely]]
        {
            return false;
        }
        if (new_bytes <= bh->user_size) [[likely]]
        {
            return true;
        }
        const size_t capacity = bh->total_size - BIG_HEADER_SIZE;
        if (new_bytes > capacity) [[unlikely]]
        {
            return false;
        }
        live_bytes_ += new_bytes - bh->user_size;
        bh->user_size = new_bytes;
        if (live_bytes_ > peak_live_bytes_) [[unlikely]]
        {
            peak_live_bytes_ = live_bytes_;
        }
        return true;
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
        if (resize_inplace(ptr, old_bytes, new_bytes))
        {
            return ptr;
        }
        size_t alignment = ALIGNMENT;
        if (const aligned_record* rec = find_aligned(ptr))
        {
            alignment = rec->alignment;
        }
        void* new_ptr = alignment > ALIGNMENT
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
        if (copy_size > 0)
        {
            std::memcpy(new_ptr, ptr, copy_size);
        }
        soft_delete(ptr);
        return new_ptr;
    }

    // 遍历可复用块 (调试/统计用)
    // 回调签名: void(void*, size_t)
    template <typename Fn>
    void iterate_free(Fn&& fn) const noexcept
    {
        for (size_t cls = 0; cls < SIZE_CLASS_COUNT; ++cls)
        {
            void* p = global_free_heads_[cls];
            while (p)
            {
                void* next = *static_cast<void**>(p);
                fn(p, k_slot_sizes[cls]);
                p = next;
            }
        }
        for (const big_block_header* bh = big_head_; bh; bh = bh->next)
        {
            if (bh->state == static_cast<uint8_t>(big_state::soft))
            {
                fn(reinterpret_cast<void*>(const_cast<char*>(
                    reinterpret_cast<const char*>(bh) + BIG_HEADER_SIZE)), bh->user_size);
            }
        }
    }
};

inline void swap(list_memory_pool& a, list_memory_pool& b) noexcept
{
    a.swap(b);
}

} // namespace memory
