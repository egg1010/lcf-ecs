// group_impl.hpp —— group / owning_group / reorder_group 的 out-of-line 定义
// 依赖 manager 完整定义,由 component.hpp 在 manager 类定义之后 include
#pragma once
#include "group.hpp"
#include "reorder.hpp"
#include "single_class_set.hpp"
#include "part/type_id.hpp"

namespace ecs
{

// ======================== group / owning_group / reorder_group out-of-line 定义 ========================

template <typename First, typename... Rest>
inline group<First, Rest...>::group(manager* mgr, std::array<single_class_set*, N> sets) noexcept
    : mgr_(mgr), sets_(sets)
{
    find_smallest();
    auto block_of = [](int tid) noexcept -> uint32_t {
        return static_cast<uint32_t>(tid - 1) / 64;
    };
    max_block_ = block_of(type_id::get_type_id<First>());
    auto update_block = [&](int tid) noexcept {
        uint32_t b = block_of(tid);
        if (b > max_block_) max_block_ = b;
    };
    (update_block(type_id::get_type_id<Rest>()), ...);
    required_masks_.resize(max_block_ + 1, 0);
    auto fill_mask = [&](int tid) noexcept {
        uint32_t block = block_of(tid);
        uint32_t offset = static_cast<uint32_t>(tid - 1) % 64;
        required_masks_[block] |= (1ULL << offset);
    };
    fill_mask(type_id::get_type_id<First>());
    (fill_mask(type_id::get_type_id<Rest>()), ...);
    use_mask_path_ = (N >= 3) || ((max_block_ + 1) <= 5);
    req_sets_.resize(N, nullptr);
    for (size_t i = 0; i < N; ++i) req_sets_[i] = sets_[i];
    rebuild();
}

template <typename First, typename... Rest>
inline void group<First, Rest...>::rebuild() noexcept
{
    cached_.clear();
    if (!all_sets_valid()) [[unlikely]] return;

    auto* primary = sets_[primary_idx_];
    auto& indices = primary->get_entity_indices();
    const size_t n = indices.size();

    if (use_mask_path_)
    {
        for (size_t i = 0; i < n; ++i)
        {
            if (check_blocks(indices[i]))
                cached_.emplace_back(static_cast<uint32_t>(i));
        }
    }
    else
    {
        for (size_t i = 0; i < n; ++i)
        {
            uint32_t eid = indices[i];
            bool has_all = true;
            for (size_t k = 0; k < N; ++k)
            {
                if (k == primary_idx_) continue;
                if (req_sets_[k]->sparse_dense_at_public(eid) == single_class_set::dense_invalid)
                {
                    has_all = false;
                    break;
                }
            }
            if (has_all)
                cached_.emplace_back(static_cast<uint32_t>(i));
        }
    }
    for (size_t i = 0; i < N; ++i)
    {
        if (sets_[i]) cached_versions_[i] = sets_[i]->get_pool_version();
    }

    dense_mappings_.clear();
    dense_mappings_.resize(cached_.size(), std::array<uint32_t, N>{});
    const sparse_entry* grp_set_dense_pages[N] = {};
    size_t grp_set_page_idxs[N];
    for (size_t k = 0; k < N; ++k) grp_set_page_idxs[k] = SIZE_MAX;
    for (size_t i = 0; i < cached_.size(); ++i)
    {
        auto& entry = dense_mappings_[i];
        uint32_t eid = indices[cached_[i]];
        size_t pid = eid >> primary->page_shift;
        for (size_t k = 0; k < N; ++k)
        {
            if (k == primary_idx_)
                entry[k] = cached_[i];
            else
            {
                if (pid != grp_set_page_idxs[k]) [[unlikely]]
                {
                    grp_set_dense_pages[k] = sets_[k]->get_dense_page(eid);
                    grp_set_page_idxs[k] = pid;
                }
                entry[k] = 0xFFFFFFFFu;
                if (grp_set_dense_pages[k]) [[likely]]
                    entry[k] = single_class_set::read_dense_from_page(grp_set_dense_pages[k], eid, sets_[k]->page_mask);
            }
        }
    }
}

template <typename First, typename... Rest>
inline owning_group<First, Rest...>::owning_group(manager* mgr, std::array<single_class_set*, N> sets) noexcept
    : mgr_(mgr), sets_(sets)
{
    find_smallest();
    auto block_of = [](int tid) noexcept -> uint32_t {
        return static_cast<uint32_t>(tid - 1) / 64;
    };
    max_block_ = block_of(type_id::get_type_id<First>());
    auto update_block = [&](int tid) noexcept {
        uint32_t b = block_of(tid);
        if (b > max_block_) max_block_ = b;
    };
    (update_block(type_id::get_type_id<Rest>()), ...);
    required_masks_.resize(max_block_ + 1, 0);
    auto fill_mask = [&](int tid) noexcept {
        uint32_t block = block_of(tid);
        uint32_t offset = static_cast<uint32_t>(tid - 1) % 64;
        required_masks_[block] |= (1ULL << offset);
    };
    fill_mask(type_id::get_type_id<First>());
    (fill_mask(type_id::get_type_id<Rest>()), ...);
    use_mask_path_ = (N >= 3) || ((max_block_ + 1) <= 5);
    req_sets_.resize(N, nullptr);
    for (size_t i = 0; i < N; ++i) req_sets_[i] = sets_[i];
    rebuild();
}

template <typename First, typename... Rest>
inline void owning_group<First, Rest...>::rebuild() noexcept
{
    if (!all_sets_valid()) [[unlikely]]
    {
        owned_size_ = 0;
        return;
    }

    auto* primary = sets_[primary_idx_];
    auto& indices = primary->get_entity_indices();
    size_t n = primary->size();

    size_t write = 0;
    if (use_mask_path_)
    {
        for (size_t read = 0; read < n; ++read)
        {
            if (check_blocks(indices[read]))
            {
                if (read != write)
                {
                    primary->swap_dense_and_pool(read, write);
                }
                ++write;
            }
        }
    }
    else
    {
        for (size_t read = 0; read < n; ++read)
        {
            uint32_t eid = indices[read];
            bool has_all = true;
            for (size_t k = 0; k < N; ++k)
            {
                if (k == primary_idx_) continue;
                if (req_sets_[k]->sparse_dense_at_public(eid) == single_class_set::dense_invalid)
                {
                    has_all = false;
                    break;
                }
            }
            if (has_all)
            {
                if (read != write)
                {
                    primary->swap_dense_and_pool(read, write);
                }
                ++write;
            }
        }
    }
    owned_size_ = write;
    primary->bump_pool_version();

    for (size_t i = 0; i < N; ++i)
    {
        if (sets_[i]) cached_versions_[i] = sets_[i]->get_pool_version();
    }
}

template <typename First, typename... Rest>
inline reorder_group<First, Rest...>::reorder_group(manager* mgr, std::array<single_class_set*, N> sets) noexcept
    : mgr_(mgr), sets_(sets)
{
    find_smallest();
    auto block_of = [](int tid) noexcept -> uint32_t {
        return static_cast<uint32_t>(tid - 1) / 64;
    };
    max_block_ = block_of(type_id::get_type_id<First>());
    auto update_block = [&](int tid) noexcept {
        uint32_t b = block_of(tid);
        if (b > max_block_) max_block_ = b;
    };
    (update_block(type_id::get_type_id<Rest>()), ...);
    required_masks_.resize(max_block_ + 1, 0);
    auto fill_mask = [&](int tid) noexcept {
        uint32_t block = block_of(tid);
        uint32_t offset = static_cast<uint32_t>(tid - 1) % 64;
        required_masks_[block] |= (1ULL << offset);
    };
    fill_mask(type_id::get_type_id<First>());
    (fill_mask(type_id::get_type_id<Rest>()), ...);
    use_mask_path_ = (N >= 3) || ((max_block_ + 1) <= 5);
    req_sets_.resize(N, nullptr);
    for (size_t i = 0; i < N; ++i) req_sets_[i] = sets_[i];
    rebuild();
}

template <typename First, typename... Rest>
inline void reorder_group<First, Rest...>::rebuild() noexcept
{
    auto* s = st();
    if (!all_sets_valid()) [[unlikely]]
    {
        s->owned_size = 0;
        return;
    }

    auto* primary = sets_[primary_idx_];
    auto& indices = primary->get_entity_indices();
    size_t n = primary->size();

    size_t write = 0;
    if (use_mask_path_)
    {
        for (size_t read = 0; read < n; ++read)
        {
            if (check_blocks(indices[read]))
            {
                if (read != write)
                {
                    primary->swap_dense_and_pool(read, write);
                }
                ++write;
            }
        }
    }
    else
    {
        for (size_t read = 0; read < n; ++read)
        {
            uint32_t eid = indices[read];
            bool has_all = true;
            for (size_t k = 0; k < N; ++k)
            {
                if (k == primary_idx_) continue;
                if (req_sets_[k]->sparse_dense_at_public(eid) == single_class_set::dense_invalid)
                {
                    has_all = false;
                    break;
                }
            }
            if (has_all)
            {
                if (read != write)
                {
                    primary->swap_dense_and_pool(read, write);
                }
                ++write;
            }
        }
    }
    s->owned_size = write;
    primary->bump_pool_version();

    for (size_t i = 0; i < N; ++i)
    {
        if (sets_[i]) s->cached_versions[i] = sets_[i]->get_pool_version();
    }
}

template <typename First, typename... Rest>
[[nodiscard]] inline bool group<First, Rest...>::check_blocks(uint32_t entity_index) const noexcept
{
    for (uint32_t b = 0; b <= max_block_; ++b)
    {
        if (required_masks_[b] == 0) continue;
        uint64_t mask = mgr_->get_entity_block_by_idx(entity_index, b);
        if ((mask & required_masks_[b]) != required_masks_[b])
            return false;
    }
    return true;
}

template <typename First, typename... Rest>
[[nodiscard]] inline bool owning_group<First, Rest...>::check_blocks(uint32_t entity_index) const noexcept
{
    for (uint32_t b = 0; b <= max_block_; ++b)
    {
        if (required_masks_[b] == 0) continue;
        uint64_t mask = mgr_->get_entity_block_by_idx(entity_index, b);
        if ((mask & required_masks_[b]) != required_masks_[b])
            return false;
    }
    return true;
}

template <typename First, typename... Rest>
[[nodiscard]] inline bool reorder_group<First, Rest...>::check_blocks(uint32_t entity_index) const noexcept
{
    for (uint32_t b = 0; b <= max_block_; ++b)
    {
        if (required_masks_[b] == 0) continue;
        uint64_t mask = mgr_->get_entity_block_by_idx(entity_index, b);
        if ((mask & required_masks_[b]) != required_masks_[b])
            return false;
    }
    return true;
}

} // namespace ecs
