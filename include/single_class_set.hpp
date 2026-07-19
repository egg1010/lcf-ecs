#pragma once
#include <span>
#include <new>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#if defined(__AVX2__) || (defined(_MSC_VER) && (defined(_M_IX86) || defined(_M_X64)))
#include <immintrin.h>
#endif
#include "part/operating_message.hpp"
#include "entity.hpp"
#include "part/class_pool.hpp"
#include "part/type_id.hpp"
#include "part/tiered_sort.hpp"


static inline void nt_fill_uint32_(uint32_t* dst, size_t count, uint32_t value) noexcept
{
#if defined(__AVX2__)
    if (count >= 8)
    {
        const __m256i fill = _mm256_set1_epi32(static_cast<int>(value));
        const size_t ymm_count = count / 8;
        for (size_t i = 0; i < ymm_count; ++i)
        {
            _mm256_stream_si256(reinterpret_cast<__m256i*>(dst) + i, fill);
        }
        _mm_sfence();
        const size_t tail_start = ymm_count * 8;
        for (size_t i = tail_start; i < count; ++i)
        {
            dst[i] = value;
        }
        return;
    }
#endif
    for (size_t i = 0; i < count; ++i)
    {
        dst[i] = value;
    }
}

namespace ecs
{
class manager;
template <typename> class query_context;

// 合并存储: dense 索引 + version 同一 cache line, 减少 get_ptr 慢路径 cache miss
struct sparse_entry
{
    uint32_t dense;
    uint32_t version;
};

class single_class_set
{
public:
    static constexpr uint32_t dense_invalid = 0xFFFFFFFFu;

private:
    // paged sparse table config (runtime configurable)
    size_t sparse_page_shift_{10};
    size_t sparse_page_size_{size_t{1} << 10};
    size_t sparse_page_mask_{(size_t{1} << 10) - 1};

    // hot set cache constants
    static constexpr size_t hot_set_capacity_ = 256;

    // flat threshold: sparse_size <= threshold 时用 flat, 否则用 paged
    static constexpr size_t flat_threshold_ = 65536;
    static constexpr size_t flat_page_shift_ = 32;
    static constexpr size_t flat_page_mask_ = SIZE_MAX;

    // flat 模式存储 (合并 dense+version 到同一缓存行)
    sparse_entry* flat_entries_{nullptr};
    size_t flat_capacity_{0};

    // paged 模式存储 (按需分页, 每页 sparse_entry 数组)
    sparse_entry** entry_pages_{nullptr};
    size_t page_dir_capacity_{0};

    bool is_flat_mode_{true};
    size_t sparse_size_{0};

    // hot set cache storage
    struct alignas(32) hot_entry_
    {
        uint32_t entity_index;
        uint32_t dense_index;
        uint32_t version;
        uint64_t pool_version;
    };
    hot_entry_ hot_set_[hot_set_capacity_]{};

    // dense array
    class_pool<uint32_t> dense_;
    // 与 dense_ 同步的 version 数组, 用于遍历时直接读连续内存, 避免 sparse_entry 间接查找
    class_pool<uint32_t> versions_;
    int type_id_{-1};

    void* typed_pool_{nullptr};
    void* typed_pool_data_{nullptr};
    size_t pending_increase_capacity_{0};
    size_t component_size_{0};

    struct pool_ops {
        void (*destroy_pool)(void* pool) noexcept;
        void (*swap_pop)(void* pool, size_t index) noexcept;
        void (*clear_pool)(void* pool) noexcept;
        void (*increase_capacity_pool)(void* pool, size_t cap) noexcept;
        void (*swap_pool)(void* pool, size_t i, size_t j) noexcept;
        void* (*get_pool_data)(void* pool) noexcept;
        bool is_trivially_copyable{false};
        // trivial fast path: memcpy-based swap_pop and swap
        void (*swap_pop_trivial)(void* pool, size_t index, size_t component_size) noexcept;
        void (*swap_pool_trivial)(void* pool, size_t i, size_t j, size_t component_size) noexcept;
        void* (*get_pool_element)(void* pool, size_t index) noexcept;
        size_t (*get_pool_size)(void* pool) noexcept;
        void (*pool_pop_back)(void* pool) noexcept;
    } ops_{};

    uint64_t version_{0};
    struct change_tracking_entry
    {
        uint64_t change_version;
        uint64_t added_version;
    };
    class_pool<change_tracking_entry> entity_change_tracking_;
    uint64_t global_change_counter_{0};
    uint64_t global_added_counter_{0};
    bool track_changes_enabled_{true};

    void (*on_add_)(entity, void* component, void* user_data) noexcept = nullptr;
    void* on_add_data_{nullptr};
    void (*on_remove_)(entity, void* component, void* user_data) noexcept = nullptr;
    void* on_remove_data_{nullptr};
    void (*on_modify_)(entity, void* component, void* user_data) noexcept = nullptr;
    void* on_modify_data_{nullptr};

    friend class ecs::manager;
    template <typename> friend class ecs::query_context;

    // ===== sparse table helpers (flat + paged 混合, 合并 dense+version) =====

    [[nodiscard]] uint32_t sparse_dense_at(uint32_t idx) const noexcept
    {
        if (is_flat_mode_) [[likely]]
        {
            if (idx >= sparse_size_) [[unlikely]]
                return dense_invalid;
            return flat_entries_[idx].dense;
        }
        const size_t page_idx = idx >> sparse_page_shift_;
        if (page_idx >= page_dir_capacity_ || !entry_pages_[page_idx]) [[unlikely]]
            return dense_invalid;
        return entry_pages_[page_idx][idx & sparse_page_mask_].dense;
    }

    [[nodiscard]] uint32_t sparse_version_at(uint32_t idx) const noexcept
    {
        if (is_flat_mode_) [[likely]]
        {
            if (idx >= sparse_size_) [[unlikely]]
                return 0;
            return flat_entries_[idx].version;
        }
        const size_t page_idx = idx >> sparse_page_shift_;
        if (page_idx >= page_dir_capacity_ || !entry_pages_[page_idx]) [[unlikely]]
            return 0;
        return entry_pages_[page_idx][idx & sparse_page_mask_].version;
    }

    [[nodiscard]] uint32_t sparse_dense_at_unchecked(uint32_t idx) const noexcept
    {
        if (is_flat_mode_) [[likely]]
            return flat_entries_[idx].dense;
        return entry_pages_[idx >> sparse_page_shift_][idx & sparse_page_mask_].dense;
    }

    [[nodiscard]] uint32_t sparse_version_at_unchecked(uint32_t idx) const noexcept
    {
        if (is_flat_mode_) [[likely]]
            return flat_entries_[idx].version;
        return entry_pages_[idx >> sparse_page_shift_][idx & sparse_page_mask_].version;
    }

    void sparse_set_at(uint32_t idx, uint32_t dense, uint32_t version) noexcept
    {
        ensure_flat_or_page_(idx);
        if (is_flat_mode_) [[likely]]
        {
            flat_entries_[idx] = {dense, version};
        }
        else
        {
            entry_pages_[idx >> sparse_page_shift_][idx & sparse_page_mask_] = {dense, version};
        }
        if (idx >= sparse_size_)
            sparse_size_ = static_cast<size_t>(idx) + 1;
    }

    void sparse_set_at_unchecked(uint32_t idx, uint32_t dense, uint32_t version) noexcept
    {
        if (is_flat_mode_) [[likely]]
        {
            flat_entries_[idx] = {dense, version};
        }
        else
        {
            entry_pages_[idx >> sparse_page_shift_][idx & sparse_page_mask_] = {dense, version};
        }
        if (idx >= sparse_size_)
            sparse_size_ = static_cast<size_t>(idx) + 1;
    }

    void ensure_flat_or_page_(uint32_t idx) noexcept
    {
        if (is_flat_mode_) [[likely]]
        {
            if (idx >= flat_capacity_) [[unlikely]]
                grow_flat_(idx + 1);
        }
        else
        {
            ensure_page_(idx >> sparse_page_shift_);
        }
    }

    void grow_flat_(size_t min_cap) noexcept
    {
        size_t new_cap = flat_capacity_ ? flat_capacity_ : 256;
        while (new_cap < min_cap)
            new_cap *= 2;
        auto* new_entries = static_cast<sparse_entry*>(
            ::operator new(new_cap * sizeof(sparse_entry), std::align_val_t{32}, std::nothrow));
        if (!new_entries) [[unlikely]]
            std::terminate();
        if (flat_entries_)
        {
            std::memcpy(new_entries, flat_entries_, flat_capacity_ * sizeof(sparse_entry));
            ::operator delete(flat_entries_, flat_capacity_ * sizeof(sparse_entry), std::align_val_t{32});
        }
        const sparse_entry invalid_entry{dense_invalid, 0};
        for (size_t i = flat_capacity_; i < new_cap; ++i)
        {
            new_entries[i] = invalid_entry;
        }
        flat_entries_ = new_entries;
        flat_capacity_ = new_cap;
    }

    void switch_to_paged_mode_() noexcept
    {
        if (!is_flat_mode_) [[unlikely]]
            return;

        sparse_entry* old_entries = flat_entries_;
        size_t old_cap = flat_capacity_;

        is_flat_mode_ = false;
        page_shift = sparse_page_shift_;
        page_size = sparse_page_size_;
        page_mask = sparse_page_mask_;
        entry_pages_ = nullptr;
        page_dir_capacity_ = 0;
        flat_entries_ = nullptr;
        flat_capacity_ = 0;

        if (dense_.empty())
        {
            if (old_entries)
                ::operator delete(old_entries, old_cap * sizeof(sparse_entry), std::align_val_t{32});
            return;
        }

        size_t max_idx = 0;
        for (size_t i = 0; i < dense_.size(); ++i)
        {
            if (dense_[i] > max_idx) max_idx = dense_[i];
        }
        const size_t needed_pages = (max_idx >> sparse_page_shift_) + 1;
        grow_page_directory_(needed_pages);
        for (size_t pi = 0; pi < needed_pages; ++pi)
        {
            allocate_entry_page_(pi);
        }

        for (size_t i = 0; i < dense_.size(); ++i)
        {
            uint32_t eid = dense_[i];
            if (eid < old_cap)
            {
                entry_pages_[eid >> sparse_page_shift_][eid & sparse_page_mask_] = old_entries[eid];
            }
        }

        if (old_entries)
            ::operator delete(old_entries, old_cap * sizeof(sparse_entry), std::align_val_t{32});

        ++version_;
    }

    void switch_to_flat_mode_() noexcept
    {
        if (is_flat_mode_) [[unlikely]]
            return;

        sparse_entry** old_entry_pages = entry_pages_;
        size_t old_cap = page_dir_capacity_;

        is_flat_mode_ = true;
        page_shift = flat_page_shift_;
        page_size = SIZE_MAX;
        page_mask = flat_page_mask_;
        entry_pages_ = nullptr;
        page_dir_capacity_ = 0;
        flat_entries_ = nullptr;
        flat_capacity_ = 0;

        if (dense_.empty())
        {
            deallocate_entry_pages_(old_entry_pages, old_cap);
            return;
        }

        size_t max_idx = 0;
        for (size_t i = 0; i < dense_.size(); ++i)
        {
            if (dense_[i] > max_idx) max_idx = dense_[i];
        }
        grow_flat_(max_idx + 1);

        for (size_t i = 0; i < dense_.size(); ++i)
        {
            uint32_t eid = dense_[i];
            const size_t page_idx = eid >> sparse_page_shift_;
            if (page_idx < old_cap && old_entry_pages[page_idx])
            {
                flat_entries_[eid] = old_entry_pages[page_idx][eid & sparse_page_mask_];
            }
        }

        deallocate_entry_pages_(old_entry_pages, old_cap);
        ++version_;
    }

    void ensure_page_(size_t page_idx) noexcept
    {
        if (page_idx >= page_dir_capacity_) [[unlikely]]
            grow_page_directory_(page_idx + 1);
        if (!entry_pages_[page_idx]) [[likely]]
        {
            allocate_entry_page_(page_idx);
        }
    }

    void grow_page_directory_(size_t min_pages) noexcept
    {
        size_t new_cap = page_dir_capacity_ ? page_dir_capacity_ : 4;
        while (new_cap < min_pages)
            new_cap *= 2;
        constexpr size_t dir_alignment = 64;
        const size_t dir_bytes = new_cap * sizeof(sparse_entry*);
        auto* new_dir = static_cast<sparse_entry**>(
            ::operator new(dir_bytes, std::align_val_t{dir_alignment}, std::nothrow));
        if (!new_dir) [[unlikely]]
            std::terminate();
        std::memset(new_dir, 0, dir_bytes);
        if (entry_pages_)
        {
            std::memcpy(new_dir, entry_pages_, page_dir_capacity_ * sizeof(sparse_entry*));
            ::operator delete(entry_pages_, page_dir_capacity_ * sizeof(sparse_entry*), std::align_val_t{dir_alignment});
        }
        entry_pages_ = new_dir;
        page_dir_capacity_ = new_cap;
    }

    void allocate_entry_page_(size_t page_idx) noexcept
    {
        const size_t page_bytes = sparse_page_size_ * sizeof(sparse_entry);
        constexpr size_t alignment = 32;
        auto* page = static_cast<sparse_entry*>(
            ::operator new(page_bytes, std::align_val_t{alignment}, std::nothrow));
        if (!page) [[unlikely]]
            std::terminate();
        const sparse_entry invalid_entry{dense_invalid, 0};
        for (size_t i = 0; i < sparse_page_size_; ++i)
        {
            page[i] = invalid_entry;
        }
        entry_pages_[page_idx] = page;
    }

    void deallocate_entry_pages_(sparse_entry** dirs, size_t cap) noexcept
    {
        constexpr size_t dir_alignment = 64;
        constexpr size_t page_alignment = 32;
        const size_t page_bytes = sparse_page_size_ * sizeof(sparse_entry);
        const size_t dir_bytes = cap * sizeof(sparse_entry*);
        if (dirs)
        {
            for (size_t i = 0; i < cap; ++i)
            {
                if (dirs[i])
                    ::operator delete(dirs[i], page_bytes, std::align_val_t{page_alignment});
            }
            ::operator delete(dirs, dir_bytes, std::align_val_t{dir_alignment});
        }
    }

    void deallocate_all_pages_() noexcept
    {
        if (is_flat_mode_)
        {
            if (flat_entries_)
            {
                ::operator delete(flat_entries_, flat_capacity_ * sizeof(sparse_entry), std::align_val_t{32});
                flat_entries_ = nullptr;
            }
            flat_capacity_ = 0;
        }
        else
        {
            deallocate_entry_pages_(entry_pages_, page_dir_capacity_);
            entry_pages_ = nullptr;
            page_dir_capacity_ = 0;
        }
        sparse_size_ = 0;
    }

    void check_mode_switch_() noexcept
    {
        if (is_flat_mode_ && sparse_size_ > flat_threshold_)
        {
            switch_to_paged_mode_();
        }
    }

    // 重建稀疏表: 按新分页大小重新分配所有页,保持 dense 索引有效
    void rebuild_sparse_table_(size_t new_shift) noexcept
    {
        if (new_shift == sparse_page_shift_) [[unlikely]]
            return;
        const size_t new_size = size_t{1} << new_shift;
        const size_t new_mask = new_size - 1;

        // 保存旧状态
        const bool old_flat = is_flat_mode_;
        sparse_entry* old_flat_entries = flat_entries_;
        size_t old_flat_cap = flat_capacity_;
        sparse_entry** old_entry_pages = entry_pages_;
        size_t old_dir_cap = page_dir_capacity_;
        const size_t old_shift = sparse_page_shift_;
        const size_t old_mask = sparse_page_mask_;

        // 重置分页参数
        sparse_page_shift_ = new_shift;
        sparse_page_size_ = new_size;
        sparse_page_mask_ = new_mask;
        is_flat_mode_ = (sparse_size_ <= flat_threshold_);
        if (is_flat_mode_)
        {
            page_shift = flat_page_shift_;
            page_size = SIZE_MAX;
            page_mask = flat_page_mask_;
        }
        else
        {
            page_shift = new_shift;
            page_size = new_size;
            page_mask = new_mask;
        }
        flat_entries_ = nullptr;
        flat_capacity_ = 0;
        entry_pages_ = nullptr;
        page_dir_capacity_ = 0;

        if (dense_.empty())
        {
            if (old_flat)
            {
                if (old_flat_entries)
                    ::operator delete(old_flat_entries, old_flat_cap * sizeof(sparse_entry), std::align_val_t{32});
            }
            else
            {
                deallocate_entry_pages_(old_entry_pages, old_dir_cap);
            }
            sparse_size_ = 0;
            hot_set_clear_();
            return;
        }

        // 计算最大索引
        size_t max_idx = 0;
        for (size_t i = 0; i < dense_.size(); ++i)
        {
            if (dense_[i] > max_idx) max_idx = dense_[i];
        }

        if (is_flat_mode_)
        {
            grow_flat_(max_idx + 1);
        }
        else
        {
            const size_t needed_pages = (max_idx >> new_shift) + 1;
            grow_page_directory_(needed_pages);
            for (size_t pi = 0; pi < needed_pages; ++pi)
            {
                allocate_entry_page_(pi);
            }
        }

        // 从旧存储读取数据写入新存储
        sparse_size_ = 0;
        for (size_t i = 0; i < dense_.size(); ++i)
        {
            uint32_t eid = dense_[i];
            uint32_t v = 0;
            if (old_flat)
            {
                if (eid < old_flat_cap)
                {
                    v = old_flat_entries[eid].version;
                }
            }
            else
            {
                const size_t old_page_idx = eid >> old_shift;
                if (old_page_idx < old_dir_cap && old_entry_pages[old_page_idx])
                {
                    v = old_entry_pages[old_page_idx][eid & old_mask].version;
                }
            }
            // dense index = i
            if (is_flat_mode_)
            {
                flat_entries_[eid] = {static_cast<uint32_t>(i), v};
            }
            else
            {
                entry_pages_[eid >> new_shift][eid & new_mask] = {static_cast<uint32_t>(i), v};
            }
            if (eid >= sparse_size_)
                sparse_size_ = eid + 1;
        }

        // 释放旧存储
        if (old_flat)
        {
            if (old_flat_entries)
                ::operator delete(old_flat_entries, old_flat_cap * sizeof(sparse_entry), std::align_val_t{32});
        }
        else
        {
            deallocate_entry_pages_(old_entry_pages, old_dir_cap);
        }

        ++version_;
    }

    // ===== hot set cache helpers =====

    void hot_set_update_(uint32_t entity_index, uint32_t dense_index, uint32_t version) noexcept
    {
        const size_t slot = entity_index & (hot_set_capacity_ - 1);
        hot_set_[slot] = {entity_index, dense_index, version, version_};
    }

    void hot_set_invalidate_(uint32_t entity_index) noexcept
    {
        const size_t slot = entity_index & (hot_set_capacity_ - 1);
        if (hot_set_[slot].entity_index == entity_index)
        {
            hot_set_[slot] = {0, 0, 0, 0};
        }
    }

    void hot_set_clear_() noexcept
    {
        std::memset(hot_set_, 0, sizeof(hot_set_));
    }

    // ===== sparse access helpers (由 flat+paged 混合存储提供) =====

    void sparse_set(uint32_t idx, uint32_t version, uint32_t dense) noexcept
    {
        sparse_set_at(idx, dense, version);
    }

    // unchecked: caller guarantees page exists, no hot set update
    void sparse_set_unchecked(uint32_t idx, uint32_t version, uint32_t dense) noexcept
    {
        sparse_set_at_unchecked(idx, dense, version);
    }

    // ===== typed pool helpers =====

    template <typename T>
    void init_typed_storage()
    {
        if (typed_pool_) [[unlikely]] return;
        auto* pool = new (std::nothrow) class_pool<T>();
        if (!pool) [[unlikely]] std::terminate();
        component_size_ = sizeof(T);
        if (pending_increase_capacity_ > 0)
        {
            pool->increase_capacity(pending_increase_capacity_);
            dense_.increase_capacity(pending_increase_capacity_);
            entity_change_tracking_.increase_capacity(pending_increase_capacity_);
            pending_increase_capacity_ = 0;
        }
        typed_pool_ = pool;
        typed_pool_data_ = pool->data();
        ops_ = {
            /*.destroy_pool =*/[](void* p) noexcept { delete static_cast<class_pool<T>*>(p); },
            /*.swap_pop =*/[](void* p, size_t index) noexcept {
                auto* pool = static_cast<class_pool<T>*>(p);
                const size_t last = pool->size() - 1;
                if (index != last) [[likely]]
                {
                    if constexpr (std::is_trivially_copyable_v<T>)
                    {
                        std::memcpy(&(*pool)[index], &(*pool)[last], sizeof(T));
                    }
                    else
                    {
                        (*pool)[index].~T();
                        new (&(*pool)[index]) T(std::move((*pool)[last]));
                    }
                }
                pool->pop_back();
            },
            /*.clear_pool =*/[](void* p) noexcept { static_cast<class_pool<T>*>(p)->clear(); },
            /*.increase_capacity_pool =*/[](void* p, size_t cap) noexcept { static_cast<class_pool<T>*>(p)->increase_capacity(cap); },
            /*.swap_pool =*/[](void* p, size_t i, size_t j) noexcept {
                auto* pool = static_cast<class_pool<T>*>(p);
                std::swap((*pool)[i], (*pool)[j]);
            },
            /*.get_pool_data =*/[](void* p) noexcept -> void* { return static_cast<class_pool<T>*>(p)->data(); },
            /*.is_trivially_copyable =*/std::is_trivially_copyable_v<T>,
            /*.swap_pop_trivial =*/[](void* p, size_t index, size_t comp_size) noexcept {
                auto* pool = static_cast<class_pool<T>*>(p);
                const size_t last = pool->size() - 1;
                if (index != last) [[likely]]
                {
                    auto* data = reinterpret_cast<char*>(pool->data());
                    std::memcpy(data + index * comp_size, data + last * comp_size, comp_size);
                }
                pool->pop_back();
            },
            /*.swap_pool_trivial =*/[](void* p, size_t i, size_t j, size_t comp_size) noexcept {
                auto* pool = static_cast<class_pool<T>*>(p);
                alignas(alignof(std::max_align_t)) char temp[256];
                auto* data = reinterpret_cast<char*>(pool->data());
                std::memcpy(temp, data + i * comp_size, comp_size);
                std::memcpy(data + i * comp_size, data + j * comp_size, comp_size);
                std::memcpy(data + j * comp_size, temp, comp_size);
            },
            /*.get_pool_element =*/[](void* p, size_t index) noexcept -> void* {
                return &(*static_cast<class_pool<T>*>(p))[index];
            },
            /*.get_pool_size =*/[](void* p) noexcept -> size_t {
                return static_cast<class_pool<T>*>(p)->size();
            },
            /*.pool_pop_back =*/[](void* p) noexcept {
                static_cast<class_pool<T>*>(p)->pop_back();
            },
        };
    }

    template <typename T>
    [[nodiscard]] class_pool<T>* get_typed_pool() noexcept
    {
        assert(typed_pool_ != nullptr && type_id_ == type_id::get_type_id<T>()
            && "get_typed_pool<T>(): type mismatch or pool not initialized");
        return static_cast<class_pool<T>*>(typed_pool_);
    }

    template <typename T>
    [[nodiscard]] const class_pool<T>* get_typed_pool() const noexcept
    {
        assert(typed_pool_ != nullptr && type_id_ == type_id::get_type_id<T>()
            && "get_typed_pool<T>(): type mismatch or pool not initialized");
        return static_cast<const class_pool<T>*>(typed_pool_);
    }

    template <typename T>
    [[nodiscard]] T* fast_get_ptr_by_index(size_t index) noexcept
    {
        if (index >= get_typed_pool<T>()->size()) [[unlikely]] return nullptr;
        return &(*get_typed_pool<T>())[index];
    }

    template <typename T>
    [[nodiscard]] const T* fast_get_ptr_by_index(size_t index) const noexcept
    {
        if (index >= get_typed_pool<T>()->size()) [[unlikely]] return nullptr;
        return &(*get_typed_pool<T>())[index];
    }

    template <typename T>
    [[nodiscard]] T* get_ptr_unchecked_by_index(size_t index) noexcept
    {
        return &(*get_typed_pool<T>())[index];
    }

    template <typename T>
    [[nodiscard]] const T* get_ptr_unchecked_by_index(size_t index) const noexcept
    {
        return &(*get_typed_pool<T>())[index];
    }

    template <typename T, typename Entities, typename F>
    operating_message add_batch_impl(Entities& entities, size_t count, F&& get_component) noexcept
    {
        operating_message result;
        using DT = std::decay_t<T>;
        const int tid = type_id::get_type_id<DT>();
        if (type_id_ == -1) [[unlikely]]
        {
            type_id_ = tid;
            init_typed_storage<DT>();
        }

        size_t max_index = 0;
        bool all_new = true;
        for (size_t i = 0; i < count; ++i)
        {
            const entity& e = entities[i];
            if (!e.is_valid()) [[unlikely]]
            {
                result.write_message(false, "single_class_set::add_batch(): invalid entity index ", std::to_string(e.parts_.index_));
                return result;
            }
            if (e.parts_.index_ > max_index) max_index = e.parts_.index_;
            if (all_new && e.parts_.index_ < sparse_size_ && sparse_version_at(e.parts_.index_) == e.parts_.version_)
                all_new = false;
        }

        // ensure pages for all indices up to max_index
        if (is_flat_mode_)
        {
            if (max_index >= flat_capacity_)
                grow_flat_(max_index + 1);
        }
        else
        {
            const size_t max_page = max_index >> sparse_page_shift_;
            if (max_page >= page_dir_capacity_) [[unlikely]]
                grow_page_directory_(max_page + 1);
            for (size_t pi = 0; pi <= max_page; ++pi)
            {
                if (!entry_pages_[pi])
                {
                    allocate_entry_page_(pi);
                }
            }
        }
        // update sparse_size_
        if (max_index >= sparse_size_)
            sparse_size_ = max_index + 1;

        auto* pool = get_typed_pool<DT>();

        if (all_new) [[likely]]
        {
            size_t dense_start = dense_.size();
            dense_.increase_capacity(dense_start + count);
            versions_.increase_capacity(dense_start + count);
            pool->increase_capacity(pool->size() + count);
            typed_pool_data_ = pool->data();

            dense_.append_indices_from(entities.data(), count);
            for (size_t i = 0; i < count; ++i)
                versions_.emplace_back_unchecked(entities[i].parts_.version_);

            using component_return_t = decltype(get_component(0));
            if constexpr (std::is_lvalue_reference_v<component_return_t>)
            {
                for (size_t i = 0; i < count; ++i)
                {
                    if (i + 16 < count) [[likely]] PREFETCH_R(&entities[i + 16]);
                    uint32_t idx = entities[i].parts_.index_;
                    sparse_set_unchecked(idx, entities[i].parts_.version_, static_cast<uint32_t>(dense_start + i));
                }
                pool->append_bulk(&get_component(0), count);
            }
            else
            {
                class_pool<DT> temp_components;
                temp_components.increase_capacity(count);
                for (size_t i = 0; i < count; ++i)
                {
                    if (i + 16 < count) [[likely]] PREFETCH_R(&entities[i + 16]);
                    uint32_t idx = entities[i].parts_.index_;
                    sparse_set_unchecked(idx, entities[i].parts_.version_, static_cast<uint32_t>(dense_start + i));
                    temp_components.push_back_unchecked(get_component(i));
                }
                pool->append_bulk_move(temp_components.data(), count);
            }

            entity_change_tracking_.append_generated(count, [this]() noexcept {
                return change_tracking_entry{++global_change_counter_, ++global_added_counter_};
            });
        }
        else
        {
            class_pool<uint32_t> new_positions;
            class_pool<uint32_t> exist_positions;
            new_positions.increase_capacity(count);
            exist_positions.increase_capacity(count);
            for (size_t i = 0; i < count; ++i)
            {
                const entity& e = entities[i];
                uint32_t ver = sparse_version_at(e.parts_.index_);
                if (ver == e.parts_.version_)
                    exist_positions.push_back_unchecked(static_cast<uint32_t>(i));
                else
                    new_positions.push_back_unchecked(static_cast<uint32_t>(i));
            }

            size_t new_count = new_positions.size();
            size_t exist_count = exist_positions.size();

            if (new_count > 0)
            {
                size_t dense_old = dense_.size();
                dense_.increase_capacity(dense_old + new_count);
                versions_.increase_capacity(dense_old + new_count);
                pool->increase_capacity(pool->size() + new_count);
                typed_pool_data_ = pool->data();

                class_pool<uint32_t> new_entity_indices;
                class_pool<uint32_t> new_entity_versions;
                class_pool<DT> new_components;
                new_entity_indices.increase_capacity(new_count);
                new_entity_versions.increase_capacity(new_count);
                new_components.increase_capacity(new_count);
                for (size_t j = 0; j < new_count; ++j)
                {
                    size_t i = new_positions[j];
                    new_entity_indices.push_back_unchecked(entities[i].parts_.index_);
                    new_entity_versions.push_back_unchecked(entities[i].parts_.version_);
                    new_components.push_back_unchecked(get_component(i));
                }
                dense_.append_bulk(new_entity_indices.data(), new_count);
                for (size_t j = 0; j < new_count; ++j)
                    versions_.emplace_back_unchecked(new_entity_versions[j]);
                using component_return_t = decltype(get_component(0));
                if constexpr (std::is_rvalue_reference_v<component_return_t>)
                    pool->append_bulk_move(new_components.data(), new_count);
                else
                    pool->append_bulk(new_components.data(), new_count);

                for (size_t j = 0; j < new_count; ++j)
                {
                    size_t i = new_positions[j];
                    size_t idx = entities[i].parts_.index_;
                    sparse_set_unchecked(static_cast<uint32_t>(idx), entities[i].parts_.version_, static_cast<uint32_t>(dense_old + j));
                }

                entity_change_tracking_.append_generated(new_count, [this]() noexcept {
                    return change_tracking_entry{++global_change_counter_, ++global_added_counter_};
                });
            }

            for (size_t j = 0; j < exist_count; ++j)
            {
                size_t i = exist_positions[j];
                const entity& e = entities[i];
                uint32_t dense_idx = sparse_dense_at(e.parts_.index_);
                (*pool)[dense_idx].~DT();
                new (&(*pool)[dense_idx]) DT(get_component(i));
                entity_change_tracking_.sparse_emplace_at(dense_idx, change_tracking_entry{++global_change_counter_, entity_change_tracking_[dense_idx].added_version});
            }
        }

        ++version_;
        if (typed_pool_ && ops_.get_pool_data) typed_pool_data_ = ops_.get_pool_data(typed_pool_);
        check_mode_switch_();
        return result;
    }

public:
    void clear() noexcept
    {
        deallocate_all_pages_();
        hot_set_clear_();
        dense_.clear();
        versions_.clear();
        entity_change_tracking_.clear();
        if (typed_pool_ && ops_.clear_pool) ops_.clear_pool(typed_pool_);
    }

    single_class_set() noexcept = default;

    explicit single_class_set(size_t capacity) noexcept
    {
        increase_capacity(capacity);
    }

    template <typename T>
    single_class_set(entity e, T&& object, size_t r_size = 1024) noexcept
    {
        increase_capacity(r_size);
        add(e, std::forward<T>(object));
    }

    template <typename T>
    operating_message add(entity e, T&& object) noexcept
    {
        operating_message result;
        using DT = std::decay_t<T>;
        const int tid = type_id::get_type_id<DT>();
        if (type_id_ == -1) [[unlikely]]
        {
            type_id_ = tid;
            init_typed_storage<DT>();
        }
        else if (type_id_ != tid) [[unlikely]]
        {
            result.write_message(false, "single_class_set::add(): type mismatch");
            return result;
        }

        if (!e.is_valid()) [[unlikely]]
        {
            result.write_message(false, "single_class_set::add(): ID is invalid, index=", std::to_string(e.parts_.index_));
            return result;
        }

        auto* pool = get_typed_pool<DT>();

        // fast path: appending at the end
        if (e.parts_.index_ == sparse_size_) [[likely]]
        {
            uint32_t dense_idx = static_cast<uint32_t>(dense_.size());
            sparse_set_at(e.parts_.index_, dense_idx, e.parts_.version_);
            dense_.emplace_back_unchecked(e.parts_.index_);
            versions_.emplace_back_unchecked(e.parts_.version_);
            pool->emplace_back_unchecked(std::forward<T>(object));
            ++version_;
            if (track_changes_enabled_) [[likely]] {
                entity_change_tracking_.emplace_back_unchecked(
                    change_tracking_entry{++global_change_counter_, ++global_added_counter_});
            }
            if (on_add_) [[unlikely]] on_add_(e, &(*pool)[dense_idx], on_add_data_);
            check_mode_switch_();
            return result;
        }

        // slow path: index may be beyond current size or within existing range
        if (e.parts_.index_ >= sparse_size_)
        {
            if (is_flat_mode_)
            {
                ensure_flat_or_page_(e.parts_.index_);
            }
            else
            {
                const size_t target_page = e.parts_.index_ >> sparse_page_shift_;
                for (size_t pi = 0; pi <= target_page; ++pi)
                {
                    ensure_page_(pi);
                }
            }
            sparse_size_ = static_cast<size_t>(e.parts_.index_) + 1;
        }

        uint32_t ver = sparse_version_at(e.parts_.index_);
        uint32_t dense_idx = sparse_dense_at(e.parts_.index_);

        bool is_new_add = (ver != e.parts_.version_);

        if (ver == e.parts_.version_) [[likely]]
        {
            void* old_ptr = &(*pool)[dense_idx];
            if (on_modify_) [[unlikely]]
            {
                on_modify_(e, old_ptr, on_modify_data_);
            }
            else
            {
                if (on_remove_) [[unlikely]] on_remove_(e, old_ptr, on_remove_data_);
            }
            (*pool)[dense_idx].~DT();
            new (&(*pool)[dense_idx]) DT(std::forward<T>(object));
            if (!on_modify_ && on_add_) [[unlikely]] on_add_(e, &(*pool)[dense_idx], on_add_data_);
        }
        else
        {
            dense_.emplace_back(e.parts_.index_);
            dense_idx = static_cast<uint32_t>(dense_.size() - 1);
            ver = e.parts_.version_;
            versions_.emplace_back(ver);
            sparse_set_at(e.parts_.index_, dense_idx, ver);
            pool->emplace_back(std::forward<T>(object));
            if (on_add_) [[unlikely]] on_add_(e, &(*pool)[dense_idx], on_add_data_);
        }
        ++version_;
        if (track_changes_enabled_) [[unlikely]] {
            uint64_t preserved_added = (dense_idx < entity_change_tracking_.size())
                ? entity_change_tracking_[dense_idx].added_version : 0;
            uint64_t new_added = is_new_add ? ++global_added_counter_ : preserved_added;
            entity_change_tracking_.sparse_emplace_at(dense_idx,
                change_tracking_entry{++global_change_counter_, new_added});
        }
        typed_pool_data_ = pool->data();
        check_mode_switch_();
        return result;
    }

    template <typename T>
    operating_message add_batch(std::span<const entity> entities, std::span<const T> components) noexcept
    {
        if (entities.size() != components.size()) [[unlikely]]
        {
            operating_message result;
            result.write_message(false, "single_class_set::add_batch(): entities and components size mismatch");
            return result;
        }
        return add_batch_impl<T>(entities, entities.size(),
            [&components](size_t i) -> const T& { return components[i]; });
    }

    template <typename T>
    operating_message add_batch(const class_pool<entity>& entities, const class_pool<T>& components) noexcept
    {
        if (entities.size() != components.size()) [[unlikely]]
        {
            operating_message result;
            result.write_message(false, "single_class_set::add_batch(): entities and components size mismatch");
            return result;
        }
        return add_batch_impl<T>(entities, entities.size(),
            [&components](size_t i) -> const T& { return components[i]; });
    }

    template <typename T>
    operating_message add_batch(class_pool<entity>&& entities, class_pool<T>&& components) noexcept
    {
        if (entities.size() != components.size()) [[unlikely]]
        {
            operating_message result;
            result.write_message(false, "single_class_set::add_batch(): entities and components size mismatch");
            return result;
        }
        return add_batch_impl<T>(entities, entities.size(),
            [&components](size_t i) -> T&& { return std::move(components[i]); });
    }

    template <typename T>
    [[nodiscard]] T* get_ptr(entity e) noexcept
    {
        if (!e.is_valid() || type_id_ != type_id::get_type_id<T>()) [[unlikely]]
        {
            return nullptr;
        }
        // hot set fast path
        const size_t slot = e.parts_.index_ & (hot_set_capacity_ - 1);
        const auto& entry = hot_set_[slot];
        if (entry.entity_index == e.parts_.index_ && entry.version == e.parts_.version_ && entry.pool_version == version_) [[likely]]
        {
            return &(*get_typed_pool<T>())[entry.dense_index];
        }
        // paged sparse lookup
        if (e.parts_.index_ >= sparse_size_) [[unlikely]]
            return nullptr;
        const uint32_t dense = sparse_dense_at(e.parts_.index_);
        if (dense == dense_invalid) [[unlikely]]
            return nullptr;
        const uint32_t ver = sparse_version_at(e.parts_.index_);
        if (ver != e.parts_.version_) [[unlikely]]
            return nullptr;
        // update hot set
        hot_set_[slot] = {e.parts_.index_, dense, ver, version_};
        return &(*get_typed_pool<T>())[dense];
    }

    template <typename T>
    [[nodiscard]] const T* get_ptr(entity e) const noexcept
    {
        if (!e.is_valid() || type_id_ != type_id::get_type_id<T>()) [[unlikely]]
            return nullptr;
        // hot set fast path
        const size_t slot = e.parts_.index_ & (hot_set_capacity_ - 1);
        const auto& entry = hot_set_[slot];
        if (entry.entity_index == e.parts_.index_ && entry.version == e.parts_.version_ && entry.pool_version == version_) [[likely]]
        {
            return &(*get_typed_pool<T>())[entry.dense_index];
        }
        // paged sparse lookup
        if (e.parts_.index_ >= sparse_size_) [[unlikely]]
            return nullptr;
        const uint32_t dense = sparse_dense_at(e.parts_.index_);
        if (dense == dense_invalid) [[unlikely]]
            return nullptr;
        const uint32_t ver = sparse_version_at(e.parts_.index_);
        if (ver != e.parts_.version_) [[unlikely]]
            return nullptr;
        return &(*get_typed_pool<T>())[dense];
    }

    template <typename T>
    [[nodiscard]] T* get_ptr_fast(entity e) noexcept
    {
        // hot set fast path
        const size_t slot = e.parts_.index_ & (hot_set_capacity_ - 1);
        const auto& entry = hot_set_[slot];
        if (entry.entity_index == e.parts_.index_ && entry.version == e.parts_.version_ && entry.pool_version == version_) [[likely]]
        {
            return &(*get_typed_pool<T>())[entry.dense_index];
        }
        // paged sparse lookup
        if (e.parts_.index_ >= sparse_size_) [[unlikely]]
            return nullptr;
        const uint32_t dense = sparse_dense_at(e.parts_.index_);
        if (dense == dense_invalid) [[unlikely]]
            return nullptr;
        const uint32_t ver = sparse_version_at(e.parts_.index_);
        if (ver != e.parts_.version_) [[unlikely]]
            return nullptr;
        // update hot set
        hot_set_[slot] = {e.parts_.index_, dense, ver, version_};
        return &(*get_typed_pool<T>())[dense];
    }

    // inline fast path: uses cached typed_pool_data_ to avoid get_typed_pool indirection
    // 热集 miss 时: 分离存储只需1次加载 dense (flat 模式) 或1次加载 (paged 模式)
    template <typename T>
    [[nodiscard]] T* get_ptr_fast_inline(entity e) noexcept
    {
        if (!typed_pool_data_) [[unlikely]]
            return get_ptr_fast<T>(e);
        const size_t slot = e.parts_.index_ & (hot_set_capacity_ - 1);
        const auto& entry = hot_set_[slot];
        if (entry.entity_index == e.parts_.index_ && entry.version == e.parts_.version_ && entry.pool_version == version_) [[likely]]
        {
            return static_cast<T*>(typed_pool_data_) + entry.dense_index;
        }
        if (e.parts_.index_ >= sparse_size_) [[unlikely]]
            return nullptr;
        const uint32_t dense = sparse_dense_at(e.parts_.index_);
        if (dense == dense_invalid) [[unlikely]]
            return nullptr;
        const uint32_t ver = sparse_version_at(e.parts_.index_);
        if (ver != e.parts_.version_) [[unlikely]]
            return nullptr;
        hot_set_[slot] = {e.parts_.index_, dense, ver, version_};
        return static_cast<T*>(typed_pool_data_) + dense;
    }

    template <typename T>
    [[nodiscard]] const T* get_ptr_fast_inline(entity e) const noexcept
    {
        if (!typed_pool_data_) [[unlikely]]
            return get_ptr_fast<T>(e);
        const size_t slot = e.parts_.index_ & (hot_set_capacity_ - 1);
        const auto& entry = hot_set_[slot];
        if (entry.entity_index == e.parts_.index_ && entry.version == e.parts_.version_ && entry.pool_version == version_) [[likely]]
        {
            return static_cast<const T*>(typed_pool_data_) + entry.dense_index;
        }
        if (e.parts_.index_ >= sparse_size_) [[unlikely]]
            return nullptr;
        const uint32_t dense = sparse_dense_at(e.parts_.index_);
        if (dense == dense_invalid) [[unlikely]]
            return nullptr;
        const uint32_t ver = sparse_version_at(e.parts_.index_);
        if (ver != e.parts_.version_) [[unlikely]]
            return nullptr;
        return static_cast<const T*>(typed_pool_data_) + dense;
    }

    template <typename T>
    [[nodiscard]] const T* get_ptr_fast(entity e) const noexcept
    {
        // hot set fast path
        const size_t slot = e.parts_.index_ & (hot_set_capacity_ - 1);
        const auto& entry = hot_set_[slot];
        if (entry.entity_index == e.parts_.index_ && entry.version == e.parts_.version_ && entry.pool_version == version_) [[likely]]
        {
            return &(*get_typed_pool<T>())[entry.dense_index];
        }
        // paged sparse lookup
        if (e.parts_.index_ >= sparse_size_) [[unlikely]]
            return nullptr;
        const uint32_t dense = sparse_dense_at(e.parts_.index_);
        if (dense == dense_invalid) [[unlikely]]
            return nullptr;
        const uint32_t ver = sparse_version_at(e.parts_.index_);
        if (ver != e.parts_.version_) [[unlikely]]
            return nullptr;
        return &(*get_typed_pool<T>())[dense];
    }

    template <typename T>
    [[nodiscard]] T* get_ptr_raw(entity e) noexcept
    {
        return &(*get_typed_pool<T>())[sparse_dense_at(e.parts_.index_)];
    }

    template <typename T>
    [[nodiscard]] const T* get_ptr_raw(entity e) const noexcept
    {
        return &(*get_typed_pool<T>())[sparse_dense_at(e.parts_.index_)];
    }

    void prefetch_component(uint32_t entity_index) const noexcept
    {
        if (is_flat_mode_)
        {
            if (entity_index < flat_capacity_)
                PREFETCH_R(&flat_entries_[entity_index]);
        }
        else
        {
            const size_t page_idx = entity_index >> sparse_page_shift_;
            if (page_idx < page_dir_capacity_ && entry_pages_[page_idx])
                PREFETCH_R(&entry_pages_[page_idx][entity_index & sparse_page_mask_]);
        }
    }

    void prefetch_ptr(entity e) const noexcept
    {
        if (is_flat_mode_)
        {
            if (e.parts_.index_ < flat_capacity_)
                PREFETCH_R(&flat_entries_[e.parts_.index_]);
        }
        else
        {
            const size_t page_idx = e.parts_.index_ >> sparse_page_shift_;
            if (page_idx < page_dir_capacity_ && entry_pages_[page_idx])
                PREFETCH_R(&entry_pages_[page_idx][e.parts_.index_ & sparse_page_mask_]);
        }
    }

    void prefetch_ptr_batch(const entity* entities, size_t count) const noexcept
    {
        for (size_t i = 0; i < count; ++i)
        {
            const uint32_t idx = entities[i].parts_.index_;
            if (is_flat_mode_)
            {
                if (idx < flat_capacity_)
                    PREFETCH_R(&flat_entries_[idx]);
            }
            else
            {
                const size_t page_idx = idx >> sparse_page_shift_;
                if (page_idx < page_dir_capacity_ && entry_pages_[page_idx])
                    PREFETCH_R(&entry_pages_[page_idx][idx & sparse_page_mask_]);
            }
        }
    }

    template <typename T>
    void prefetch_ptr_data(entity e) const noexcept
    {
        if (e.parts_.index_ >= sparse_size_) [[unlikely]]
            return;
        const uint32_t dense = sparse_dense_at(e.parts_.index_);
        if (dense == dense_invalid) [[unlikely]]
            return;
        PREFETCH_R(&(*get_typed_pool<T>())[dense]);
    }

    // 排序预取批量查询: 大批量时按 entity index 排序后顺序访问 sparse 表, 减少 cache miss
    // 使用 radix_sort_entries 直接排序结构体数组, 消除间接寻址
    // 返回 false 表示分配失败, 调用方回退到 chunk 路径
    template <typename T>
    bool get_ptr_batch_sorted_(const entity* entities, T** results, size_t count) noexcept
    {
        auto* pool = get_typed_pool<T>();

        // 条目: { key=entity_index, index=(version<<32 | orig_i) }
        struct batch_sort_entry
        {
            uint32_t key;
            size_t index;
        };

        constexpr size_t align = 64;
        auto* entries = static_cast<batch_sort_entry*>(
            ::operator new(count * sizeof(batch_sort_entry), std::align_val_t{align}, std::nothrow));
        if (!entries) [[unlikely]]
            return false;

        // 填充: 顺序访问 entities[], 无效 entity 标记 key=UINT32_MAX 排到末尾
        for (size_t i = 0; i < count; ++i)
        {
            const entity& e = entities[i];
            if (!e.is_valid() || e.parts_.index_ >= sparse_size_) [[unlikely]]
            {
                entries[i].key = UINT32_MAX;
                entries[i].index = i;
            }
            else
            {
                entries[i].key = e.parts_.index_;
                entries[i].index = (static_cast<size_t>(e.parts_.version_) << 32) | i;
            }
        }

        // 按 entity_index 排序 (radix sort, O(n))
        radix_sort_entries<uint32_t>(entries, count);

        // 顺序处理: entries[] 顺序读, sparse 表顺序访问, 无间接寻址
        constexpr size_t pf_dist = 8;
        for (size_t i = 0; i < count; ++i)
        {
            if (i + pf_dist < count) [[likely]]
            {
                uint32_t pf_idx = entries[i + pf_dist].key;
                if (pf_idx != UINT32_MAX)
                {
                    if (is_flat_mode_)
                    {
                        if (pf_idx < flat_capacity_)
                            PREFETCH_R(&flat_entries_[pf_idx]);
                    }
                    else
                    {
                        const size_t pf_page_idx = pf_idx >> sparse_page_shift_;
                        if (pf_page_idx < page_dir_capacity_ && entry_pages_[pf_page_idx])
                            PREFETCH_R(&entry_pages_[pf_page_idx][pf_idx & sparse_page_mask_]);
                    }
                }
            }

            uint32_t eidx = entries[i].key;
            if (eidx == UINT32_MAX) [[unlikely]]
            {
                results[entries[i].index & 0xFFFFFFFF] = nullptr;
                continue;
            }

            size_t orig_i = entries[i].index & 0xFFFFFFFF;
            uint32_t ver = static_cast<uint32_t>(entries[i].index >> 32);

            uint32_t dense = sparse_dense_at(eidx);
            if (dense == dense_invalid) [[unlikely]]
            {
                results[orig_i] = nullptr;
                continue;
            }
            uint32_t stored_ver = sparse_version_at(eidx);
            if (stored_ver != ver) [[unlikely]]
            {
                results[orig_i] = nullptr;
                continue;
            }
            results[orig_i] = &(*pool)[dense];
        }

        ::operator delete(entries, count * sizeof(batch_sort_entry), std::align_val_t{align});
        return true;
    }

    template <typename T>
    void get_ptr_batch(const entity* entities, T** results, size_t count) noexcept
    {
        if (type_id_ != type_id::get_type_id<T>()) [[unlikely]]
        {
            for (size_t i = 0; i < count; ++i) results[i] = nullptr;
            return;
        }

        // 大批量 + sparse 表超出 L3 缓存时走排序预取路径
        // sparse_size_ * 8 > 16MB 时随机访问 cache miss 率高, 排序后顺序访问更优
        // 小规模 sparse 表可被 L3 缓存, chunk 路径 + 预取已足够快
        if (count > 4096 && sparse_size_ > 2000000) [[unlikely]]
        {
            if (get_ptr_batch_sorted_<T>(entities, results, count))
                return;
        }

        auto* pool = get_typed_pool<T>();

        constexpr size_t chunk = 16;
        uint32_t dense_buf[chunk];

        for (size_t base = 0; base < count; base += chunk)
        {
            size_t n = base + chunk <= count ? chunk : count - base;

            for (size_t j = 0; j < n; ++j)
            {
                size_t i = base + j;
                const entity& e = entities[i];
                // prefetch next batch
                if (i + 8 < count) [[likely]]
                {
                    const uint32_t pf_idx = entities[i + 8].parts_.index_;
                    if (is_flat_mode_)
                    {
                        if (pf_idx < flat_capacity_)
                            PREFETCH_R(&flat_entries_[pf_idx]);
                    }
                    else
                    {
                        const size_t pf_page_idx = pf_idx >> sparse_page_shift_;
                        if (pf_page_idx < page_dir_capacity_ && entry_pages_[pf_page_idx])
                            PREFETCH_R(&entry_pages_[pf_page_idx][pf_idx & sparse_page_mask_]);
                    }
                }

                if (!e.is_valid() || e.parts_.index_ >= sparse_size_) [[unlikely]]
                {
                    dense_buf[j] = UINT32_MAX;
                    continue;
                }
                uint32_t dense = sparse_dense_at(e.parts_.index_);
                if (dense == dense_invalid) [[unlikely]]
                {
                    dense_buf[j] = UINT32_MAX;
                    continue;
                }
                uint32_t ver = sparse_version_at(e.parts_.index_);
                if (ver != e.parts_.version_) [[unlikely]]
                {
                    dense_buf[j] = UINT32_MAX;
                    continue;
                }
                dense_buf[j] = dense;
                PREFETCH_R(&(*pool)[dense]);
            }

            for (size_t j = 0; j < n; ++j)
            {
                uint32_t dense = dense_buf[j];
                if (dense == UINT32_MAX) [[unlikely]]
                {
                    results[base + j] = nullptr;
                    continue;
                }
                results[base + j] = &(*pool)[dense];
            }
        }
    }

    [[nodiscard]] uint32_t get_version(uint32_t entity_index) const noexcept
    {
        if (entity_index >= sparse_size_) [[unlikely]] return 0;
        return sparse_version_at(entity_index);
    }

    [[nodiscard]] uint32_t get_version_unchecked(uint32_t entity_index) const noexcept
    {
        return sparse_version_at(entity_index);
    }

    [[nodiscard]] uint64_t get_pool_version() const noexcept
    {
        return version_;
    }

    [[nodiscard]] uint64_t get_entity_change_version(size_t dense_index) const noexcept
    {
        if (dense_index >= entity_change_tracking_.size()) [[unlikely]] return 0;
        return entity_change_tracking_[dense_index].change_version;
    }

    [[nodiscard]] uint64_t get_entity_added_version(size_t dense_index) const noexcept
    {
        if (dense_index >= entity_change_tracking_.size()) [[unlikely]] return 0;
        return entity_change_tracking_[dense_index].added_version;
    }

    [[nodiscard]] uint64_t get_global_added_counter() const noexcept
    {
        return global_added_counter_;
    }

    [[nodiscard]] uint64_t get_global_change_counter() const noexcept
    {
        return global_change_counter_;
    }

    operating_message hard_remove(entity e) noexcept
    {
        operating_message result;
        if (!e.is_valid() || e.parts_.index_ >= sparse_size_ || sparse_version_at(e.parts_.index_) != e.parts_.version_) [[unlikely]]
        {
            result.write_message(false, "single_class_set::hard_remove(): invalid entity or version mismatch, index=", std::to_string(e.parts_.index_));
            return result;
        }

        auto index = sparse_dense_at(e.parts_.index_);

        void* comp_ptr = typed_pool_ ? static_cast<char*>(typed_pool_) + index * component_size_ : nullptr;
        if (on_remove_ && comp_ptr) [[unlikely]] on_remove_(e, comp_ptr, on_remove_data_);

        auto moved_entity_id = dense_.back();
        dense_[index] = dense_.back();
        if (index < versions_.size() && !versions_.empty())
        {
            if (index < versions_.size() - 1)
                versions_[index] = versions_.back();
            versions_.pop_back();
        }

        if (moved_entity_id != e.parts_.index_) [[likely]]
        {
            sparse_set_unchecked(moved_entity_id, sparse_version_at_unchecked(moved_entity_id), static_cast<uint32_t>(index));
        }
        dense_.pop_back();

        if (index < entity_change_tracking_.size() && entity_change_tracking_.size() > 0)
        {
            if (index < entity_change_tracking_.size() - 1)
            {
                entity_change_tracking_[index] = entity_change_tracking_.back();
            }
            entity_change_tracking_.pop_back();
        }

        if (typed_pool_)
        {
            if (ops_.is_trivially_copyable && typed_pool_data_ && ops_.get_pool_size && ops_.pool_pop_back)
            {
                // inline trivial swap_pop: memcpy last→index + pop_back
                const size_t last = ops_.get_pool_size(typed_pool_) - 1;
                if (index != last) [[likely]]
                {
                    auto* data = static_cast<char*>(typed_pool_data_);
                    std::memcpy(data + index * component_size_, data + last * component_size_, component_size_);
                }
                ops_.pool_pop_back(typed_pool_);
            }
            else if (ops_.swap_pop)
            {
                ops_.swap_pop(typed_pool_, index);
            }
        }

        sparse_set_at_unchecked(e.parts_.index_, dense_invalid, 0);
        ++version_;
        return result;
    }

    operating_message soft_remove(entity e) noexcept
    {
        operating_message result;
        if (!e.is_valid() || e.parts_.index_ >= sparse_size_ || sparse_version_at(e.parts_.index_) != e.parts_.version_) [[unlikely]]
        {
            result.write_message(false, "single_class_set::soft_remove(): invalid entity or version mismatch, index=", std::to_string(e.parts_.index_));
            return result;
        }

        sparse_set_at_unchecked(e.parts_.index_, dense_invalid, 0);
        ++version_;
        return result;
    }

    [[nodiscard]] bool contains_entity(entity e) const noexcept
    {
        if (!e.is_valid() || e.parts_.index_ >= sparse_size_) [[unlikely]] return false;
        const uint32_t dense = sparse_dense_at(e.parts_.index_);
        if (dense == dense_invalid) [[unlikely]] return false;
        return sparse_version_at(e.parts_.index_) == e.parts_.version_;
    }

    [[nodiscard]] int& get_type_id() noexcept
    {
        return type_id_;
    }

    template <typename T>
    [[nodiscard]] class_pool<T>* get_typed_pool_ptr() noexcept
    {
        if (type_id_ != type_id::get_type_id<T>()) [[unlikely]] return nullptr;
        return static_cast<class_pool<T>*>(typed_pool_);
    }

    template <typename T>
    [[nodiscard]] const class_pool<T>* get_typed_pool_ptr() const noexcept
    {
        if (type_id_ != type_id::get_type_id<T>()) [[unlikely]] return nullptr;
        return static_cast<const class_pool<T>*>(typed_pool_);
    }

    single_class_set(single_class_set&& other) noexcept
    : sparse_page_shift_(other.sparse_page_shift_)
    , sparse_page_size_(other.sparse_page_size_)
    , sparse_page_mask_(other.sparse_page_mask_)
    , flat_entries_(other.flat_entries_)
    , flat_capacity_(other.flat_capacity_)
    , entry_pages_(other.entry_pages_)
    , page_dir_capacity_(other.page_dir_capacity_)
    , is_flat_mode_(other.is_flat_mode_)
    , sparse_size_(other.sparse_size_)
    , dense_(std::move(other.dense_))
    , type_id_(other.type_id_)
    , typed_pool_(other.typed_pool_)
    , pending_increase_capacity_(other.pending_increase_capacity_)
    , component_size_(other.component_size_)
    , ops_(other.ops_)
    , version_(other.version_)
    , entity_change_tracking_(std::move(other.entity_change_tracking_))
    , global_change_counter_(other.global_change_counter_)
    , global_added_counter_(other.global_added_counter_)
    , track_changes_enabled_(other.track_changes_enabled_)
    , on_add_(other.on_add_)
    , on_add_data_(other.on_add_data_)
    , on_remove_(other.on_remove_)
    , on_remove_data_(other.on_remove_data_)
    , on_modify_(other.on_modify_)
    , on_modify_data_(other.on_modify_data_)
    , page_shift(other.page_shift)
    , page_size(other.page_size)
    , page_mask(other.page_mask)
    {
        std::memcpy(hot_set_, other.hot_set_, sizeof(hot_set_));
        other.flat_entries_ = nullptr;
        other.flat_capacity_ = 0;
        other.entry_pages_ = nullptr;
        other.page_dir_capacity_ = 0;
        other.sparse_size_ = 0;
        other.typed_pool_ = nullptr;
        other.ops_ = {};
        other.type_id_ = -1;
        other.pending_increase_capacity_ = 0;
        other.component_size_ = 0;
        other.version_ = 0;
        other.global_change_counter_ = 0;
        other.global_added_counter_ = 0;
        other.on_add_ = nullptr;
        other.on_add_data_ = nullptr;
        other.on_remove_ = nullptr;
        other.on_remove_data_ = nullptr;
        other.on_modify_ = nullptr;
        other.on_modify_data_ = nullptr;
    }

    single_class_set& operator=(single_class_set&& other) noexcept
    {
        if (this != &other) [[likely]]
        {
            if (typed_pool_ && ops_.destroy_pool) ops_.destroy_pool(typed_pool_);
            deallocate_all_pages_();

            sparse_page_shift_ = other.sparse_page_shift_;
            sparse_page_size_ = other.sparse_page_size_;
            sparse_page_mask_ = other.sparse_page_mask_;
            flat_entries_ = other.flat_entries_;
            flat_capacity_ = other.flat_capacity_;
            entry_pages_ = other.entry_pages_;
            page_dir_capacity_ = other.page_dir_capacity_;
            is_flat_mode_ = other.is_flat_mode_;
            sparse_size_ = other.sparse_size_;
            std::memcpy(hot_set_, other.hot_set_, sizeof(hot_set_));
            dense_ = std::move(other.dense_);
            typed_pool_ = other.typed_pool_;
            ops_ = other.ops_;
            pending_increase_capacity_ = other.pending_increase_capacity_;
            component_size_ = other.component_size_;
            type_id_ = other.type_id_;
            version_ = other.version_;
            entity_change_tracking_ = std::move(other.entity_change_tracking_);
            global_change_counter_ = other.global_change_counter_;
            global_added_counter_ = other.global_added_counter_;
            track_changes_enabled_ = other.track_changes_enabled_;
            on_add_ = other.on_add_;
            on_add_data_ = other.on_add_data_;
            on_remove_ = other.on_remove_;
            on_remove_data_ = other.on_remove_data_;
            on_modify_ = other.on_modify_;
            on_modify_data_ = other.on_modify_data_;
            page_shift = other.page_shift;
            page_size = other.page_size;
            page_mask = other.page_mask;

            other.flat_entries_ = nullptr;
            other.flat_capacity_ = 0;
            other.entry_pages_ = nullptr;
            other.page_dir_capacity_ = 0;
            other.sparse_size_ = 0;
            other.typed_pool_ = nullptr;
            other.ops_ = {};
            other.type_id_ = -1;
            other.pending_increase_capacity_ = 0;
            other.component_size_ = 0;
            other.version_ = 0;
            other.global_change_counter_ = 0;
            other.global_added_counter_ = 0;
            other.on_add_ = nullptr;
            other.on_add_data_ = nullptr;
            other.on_remove_ = nullptr;
            other.on_remove_data_ = nullptr;
            other.on_modify_ = nullptr;
            other.on_modify_data_ = nullptr;
        }
        return *this;
    }

    single_class_set(const single_class_set&) = delete;
    single_class_set& operator=(const single_class_set&) = delete;

    [[nodiscard]] constexpr size_t size() const noexcept
    {
        return dense_.size();
    }

    [[nodiscard]] constexpr bool empty() const noexcept
    {
        return dense_.empty();
    }

    void increase_capacity(size_t capacity) noexcept
    {
        dense_.increase_capacity(capacity);
        entity_change_tracking_.increase_capacity(capacity);
        if (typed_pool_ && ops_.increase_capacity_pool)
        {
            ops_.increase_capacity_pool(typed_pool_, capacity);
            if (ops_.get_pool_data) typed_pool_data_ = ops_.get_pool_data(typed_pool_);
        }
        else
        {
            if (capacity > pending_increase_capacity_) pending_increase_capacity_ = capacity;
        }
        if (is_flat_mode_)
        {
            if (capacity > flat_capacity_)
                grow_flat_(capacity);
        }
        else
        {
            const size_t needed_pages = (capacity >> sparse_page_shift_) + 1;
            if (needed_pages > page_dir_capacity_)
                grow_page_directory_(needed_pages);
            for (size_t i = 0; i < needed_pages; ++i)
            {
                if (!entry_pages_[i])
                {
                    allocate_entry_page_(i);
                }
            }
        }
    }

    [[nodiscard]] class_pool<uint32_t>& get_entity_indices() noexcept
    {
        return dense_;
    }

    [[nodiscard]] const class_pool<uint32_t>& get_entity_indices() const noexcept
    {
        return dense_;
    }

    // 与 dense_ 同步的 version 数组, 遍历时直接读连续内存
    [[nodiscard]] class_pool<uint32_t>& get_entity_versions() noexcept
    {
        return versions_;
    }

    [[nodiscard]] const class_pool<uint32_t>& get_entity_versions() const noexcept
    {
        return versions_;
    }

    // sparse intersection check for >64 types slow path
    [[nodiscard]] static bool sparse_contains_version(const single_class_set* set,
                                                     uint32_t idx,
                                                     uint32_t version) noexcept
    {
        if (!set) return false;
        if (idx >= set->sparse_size_) return false;
        uint32_t dense = set->sparse_dense_at(idx);
        uint32_t ver = set->sparse_version_at(idx);
        return dense != dense_invalid && ver == version;
    }

    [[nodiscard]] uint32_t get_dense_at(uint32_t entity_index) const noexcept
    {
        return sparse_dense_at(entity_index);
    }

    void swap_dense_and_pool(size_t i, size_t j) noexcept
    {
        if (i == j) [[unlikely]] return;
        uint32_t tmp = dense_[i];
        dense_[i] = dense_[j];
        dense_[j] = tmp;
        if (i < versions_.size() && j < versions_.size())
        {
            uint32_t tmp_v = versions_[i];
            versions_[i] = versions_[j];
            versions_[j] = tmp_v;
        }
        sparse_set_unchecked(dense_[i], sparse_version_at_unchecked(dense_[i]), static_cast<uint32_t>(i));
        sparse_set_unchecked(dense_[j], sparse_version_at_unchecked(dense_[j]), static_cast<uint32_t>(j));
        if (i < entity_change_tracking_.size() && j < entity_change_tracking_.size())
        {
            change_tracking_entry tmp = entity_change_tracking_[i];
            entity_change_tracking_[i] = entity_change_tracking_[j];
            entity_change_tracking_[j] = tmp;
        }
        if (typed_pool_) [[likely]]
        {
            if (ops_.is_trivially_copyable && typed_pool_data_ && component_size_ <= 256)
            {
                // inline trivial swap: memcpy-based
                auto* data = static_cast<char*>(typed_pool_data_);
                alignas(alignof(std::max_align_t)) char temp[256];
                std::memcpy(temp, data + i * component_size_, component_size_);
                std::memcpy(data + i * component_size_, data + j * component_size_, component_size_);
                std::memcpy(data + j * component_size_, temp, component_size_);
            }
            else if (ops_.swap_pool)
            {
                ops_.swap_pool(typed_pool_, i, j);
            }
        }
    }

    template <typename T>
    void reorder_dense_by_indices(const class_pool<size_t>& sorted_indices) noexcept
    {
        const size_t n = dense_.size();
        if (n <= 1 || sorted_indices.size() < n) [[unlikely]] return;

        class_pool<uint32_t> new_dense;
        new_dense.increase_capacity(n);
        for (size_t i = 0; i < n; ++i)
            new_dense.emplace_back(dense_[sorted_indices[i]]);

        class_pool<uint32_t> new_versions;
        if (versions_.size() >= n)
        {
            new_versions.increase_capacity(n);
            for (size_t i = 0; i < n; ++i)
                new_versions.emplace_back(versions_[sorted_indices[i]]);
        }

        class_pool<T>* typed_pool = get_typed_pool_ptr<T>();
        if (typed_pool)
        {
            class_pool<T> new_pool;
            new_pool.increase_capacity(n);
            for (size_t i = 0; i < n; ++i)
                new_pool.emplace_back(std::move((*typed_pool)[sorted_indices[i]]));
            *typed_pool = std::move(new_pool);
            if (ops_.get_pool_data) typed_pool_data_ = ops_.get_pool_data(typed_pool_);
        }

        class_pool<change_tracking_entry> new_tracking;
        if (entity_change_tracking_.size() >= n)
        {
            new_tracking.increase_capacity(n);
            for (size_t i = 0; i < n; ++i)
            {
                new_tracking.emplace_back(entity_change_tracking_[sorted_indices[i]]);
            }
            entity_change_tracking_ = std::move(new_tracking);
        }

        for (size_t i = 0; i < n; ++i)
            sparse_set_unchecked(new_dense[i], sparse_version_at_unchecked(new_dense[i]), static_cast<uint32_t>(i));

        dense_ = std::move(new_dense);
        if (new_versions.size() > 0)
            versions_ = std::move(new_versions);
        ++version_;
    }

    // public sparse access (分离存储)
    [[nodiscard]] uint32_t sparse_dense_at_public(uint32_t idx) const noexcept
    {
        return sparse_dense_at(idx);
    }

    [[nodiscard]] uint32_t sparse_version_at_public(uint32_t idx) const noexcept
    {
        return sparse_version_at(idx);
    }

    [[nodiscard]] size_t get_sparse_size() const noexcept
    {
        return sparse_size_;
    }

    [[nodiscard]] size_t get_page_directory_capacity() const noexcept
    {
        return page_dir_capacity_;
    }

    // 合并后 dense+version 同页, 返回 entry 页目录指针
    [[nodiscard]] const sparse_entry* const* get_entry_pages() const noexcept
    {
        return entry_pages_;
    }

    [[nodiscard]] bool is_flat_mode() const noexcept
    {
        return is_flat_mode_;
    }

    // flat 模式返回 flat_entries_, paged 模式返回对应页指针
    [[nodiscard]] const sparse_entry* get_flat_entries() const noexcept
    {
        return flat_entries_;
    }

    [[nodiscard]] size_t get_flat_capacity() const noexcept
    {
        return flat_capacity_;
    }

    // 分页大小配置 (运行时可修改)
    [[nodiscard]] size_t get_page_size_shift() const noexcept
    {
        return sparse_page_shift_;
    }

    void set_page_size_shift(size_t shift) noexcept
    {
        if (shift < 6 || shift > 20) [[unlikely]]
            return;
        rebuild_sparse_table_(shift);
    }

    // 实例级分页参数 (flat 模式下 page_shift=32, page_mask=SIZE_MAX)
    size_t page_shift{flat_page_shift_};
    size_t page_size{SIZE_MAX};
    size_t page_mask{flat_page_mask_};

    // traversal helper: get entry page pointer for a given entity index
    // dense+version 合并存储, get_dense_page 与 get_version_page 返回同一指针
    [[nodiscard]] const sparse_entry* get_dense_page(uint32_t entity_index) const noexcept
    {
        if (is_flat_mode_) [[likely]]
        {
            if (entity_index >= flat_capacity_) [[unlikely]]
                return nullptr;
            return flat_entries_;
        }
        const size_t page_idx = entity_index >> sparse_page_shift_;
        if (page_idx >= page_dir_capacity_) [[unlikely]]
            return nullptr;
        return entry_pages_[page_idx];
    }

    [[nodiscard]] const sparse_entry* get_version_page(uint32_t entity_index) const noexcept
    {
        return get_dense_page(entity_index);
    }

    // traversal helper: read dense index from a known entry page pointer
    [[nodiscard]] static uint32_t read_dense_from_page(const sparse_entry* page, uint32_t entity_index, size_t mask) noexcept
    {
        return page[entity_index & mask].dense;
    }

    // traversal helper: read version from a known entry page pointer
    [[nodiscard]] static uint32_t read_version_from_page(const sparse_entry* page, uint32_t entity_index, size_t mask) noexcept
    {
        return page[entity_index & mask].version;
    }

    void clear_hot_set() noexcept
    {
        hot_set_clear_();
    }

    void bump_pool_version() noexcept
    {
        ++version_;
    }

    ~single_class_set() noexcept
    {
        if (typed_pool_ && ops_.destroy_pool) ops_.destroy_pool(typed_pool_);
        deallocate_all_pages_();
    }
};

} // namespace ecs
