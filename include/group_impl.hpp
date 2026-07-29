// group_impl.hpp —— group / owning_group / reorder_group 的 out-of-line 定义
// 依赖 manager 完整定义,由 component.hpp 在 manager 类定义之后 include
#pragma once
#include "group.hpp"
#include "reorder.hpp"
#include "single_class_set.hpp"
#include "part/type_id.hpp"

namespace ecs
{

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
    required_masks_.increase_capacity(max_block_ + 1, 0);
    auto fill_mask = [&](int tid) noexcept {
        uint32_t block = block_of(tid);
        uint32_t offset = static_cast<uint32_t>(tid - 1) % 64;
        required_masks_[block] |= (1ULL << offset);
    };
    fill_mask(type_id::get_type_id<First>());
    (fill_mask(type_id::get_type_id<Rest>()), ...);
    use_mask_path_ = (N >= 3) || ((max_block_ + 1) <= 5);
    req_sets_.increase_capacity(N, nullptr);
    for (size_t i = 0; i < N; ++i) req_sets_[i] = sets_[i];
    rebuild();
}

template <typename First, typename... Rest>
inline void group<First, Rest...>::rebuild() noexcept
{
    cached_.clear();
    dense_mappings_.clear();
    if (!all_sets_valid()) [[unlikely]] return;

    auto* primary = sets_[primary_idx_];
    auto& indices = primary->get_entity_indices();
    const size_t n = indices.size();

    // 预分配容量,避免 emplace_back 反复重分配
    cached_.reserve_exact(n);

    if (use_mask_path_)
    {
        // 位掩码路径: 仅 check_blocks 快速过滤, 不构建 mapping
        for (size_t i = 0; i < n; ++i)
        {
            if (check_blocks(indices[i]))
                cached_.push_back(static_cast<uint32_t>(i));
        }
    }
    else
    {
        // sparse 路径: has_all 判断与 mapping 构建合并 (单次遍历)
        dense_mappings_.reserve_exact(n);
        for (size_t i = 0; i < n; ++i)
        {
            if (i + 8 < n) [[likely]]
            {
                uint32_t next_eid = indices[i + 8];
                for (size_t k = 0; k < N; ++k)
                {
                    if (k == primary_idx_) continue;
                    req_sets_[k]->prefetch_sparse_entry(next_eid);
                }
            }
            uint32_t eid = indices[i];
            std::array<uint32_t, N> entry{};
            entry[primary_idx_] = static_cast<uint32_t>(i);
            bool has_all = true;
            for (size_t k = 0; k < N; ++k)
            {
                if (k == primary_idx_) continue;
                uint32_t d = req_sets_[k]->sparse_dense_at_public(eid);
                if (d == single_class_set::dense_invalid)
                {
                    has_all = false;
                    break;
                }
                entry[k] = d;
            }
            if (has_all)
            {
                cached_.push_back(static_cast<uint32_t>(i));
                dense_mappings_.push_back(entry);
            }
        }
    }
    for (size_t i = 0; i < N; ++i)
    {
        if (sets_[i]) cached_versions_[i] = sets_[i]->get_pool_version();
    }

    // mask_path 补充构建 mapping (sparse 路径已在主循环中构建)
    if (use_mask_path_ && !cached_.empty())
    {
        dense_mappings_.reserve_exact(cached_.size());
        dense_mappings_.increase_capacity(cached_.size(), std::array<uint32_t, N>{});
        for (size_t i = 0; i < cached_.size(); ++i)
        {
            auto& entry = dense_mappings_[i];
            uint32_t eid = indices[cached_[i]];
            entry[primary_idx_] = cached_[i];
            for (size_t k = 0; k < N; ++k)
            {
                if (k == primary_idx_) continue;
                if (i + 8 < cached_.size()) [[likely]]
                    sets_[k]->prefetch_sparse_entry(indices[cached_[i + 8]]);
                entry[k] = sets_[k]->sparse_dense_at_public(eid);
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
    required_masks_.increase_capacity(max_block_ + 1, 0);
    auto fill_mask = [&](int tid) noexcept {
        uint32_t block = block_of(tid);
        uint32_t offset = static_cast<uint32_t>(tid - 1) % 64;
        required_masks_[block] |= (1ULL << offset);
    };
    fill_mask(type_id::get_type_id<First>());
    (fill_mask(type_id::get_type_id<Rest>()), ...);
    use_mask_path_ = (N >= 3) || ((max_block_ + 1) <= 5);
    req_sets_.increase_capacity(N, nullptr);
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
            if (read + 8 < n) [[likely]]
            {
                uint32_t next_eid = indices[read + 8];
                for (size_t k = 0; k < N; ++k)
                {
                    if (k == primary_idx_) continue;
                    req_sets_[k]->prefetch_sparse_entry(next_eid);
                }
            }
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
    required_masks_.increase_capacity(max_block_ + 1, 0);
    auto fill_mask = [&](int tid) noexcept {
        uint32_t block = block_of(tid);
        uint32_t offset = static_cast<uint32_t>(tid - 1) % 64;
        required_masks_[block] |= (1ULL << offset);
    };
    fill_mask(type_id::get_type_id<First>());
    (fill_mask(type_id::get_type_id<Rest>()), ...);
    use_mask_path_ = (N >= 3) || ((max_block_ + 1) <= 5);
    req_sets_.increase_capacity(N, nullptr);
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
            if (read + 8 < n) [[likely]]
            {
                uint32_t next_eid = indices[read + 8];
                for (size_t k = 0; k < N; ++k)
                {
                    if (k == primary_idx_) continue;
                    req_sets_[k]->prefetch_sparse_entry(next_eid);
                }
            }
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
