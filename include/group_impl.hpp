// group_impl.hpp —— group / owning_group / reorder_group 的 out-of-line 定义
// 公共状态与掩码构建收敛于 group_base (group.hpp), 此处仅保留差异化逻辑
// 依赖 manager 完整定义,由 component.hpp 在 manager 类定义之后 include
#pragma once
#include "group.hpp"
#include "reorder.hpp"
#include "single_class_set.hpp"
#include "part/type_id.hpp"

namespace ecs
{

// check_blocks 依赖 manager 完整类型, 定义于此 (三子类共用基类版本)
template <typename First, typename... Rest>
[[nodiscard]] inline bool group_base<First, Rest...>::check_blocks(uint32_t entity_index) const noexcept
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
inline group<First, Rest...>::group(manager* mgr, std::array<single_class_set*, N> sets) noexcept
    : group_base<First, Rest...>(mgr, sets)
{
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
        // 位掩码路径: check_blocks 过滤 + mapping 构建合并为单遍历
        dense_mappings_.reserve_exact(n);
        for (size_t i = 0; i < n; ++i)
        {
            if (!check_blocks(indices[i])) continue;
            uint32_t eid = indices[i];
            std::array<uint32_t, N> entry{};
            entry[primary_idx_] = static_cast<uint32_t>(i);
            for (size_t k = 0; k < N; ++k)
            {
                if (k == primary_idx_) continue;
                entry[k] = sets_[k]->sparse_dense_at(eid);
            }
            cached_.push_back(static_cast<uint32_t>(i));
            dense_mappings_.push_back(entry);
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
                uint32_t d = req_sets_[k]->sparse_dense_at(eid);
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
    refresh_versions(cached_versions_);
}

template <typename First, typename... Rest>
inline owning_group<First, Rest...>::owning_group(manager* mgr, std::array<single_class_set*, N> sets) noexcept
    : group_base<First, Rest...>(mgr, sets)
{
    rebuild();
}

// 主集合物理压缩: 命中条目前压 (压缩循环收敛于 group_base::compact_primary)
template <typename First, typename... Rest>
inline void owning_group<First, Rest...>::rebuild() noexcept
{
    if (!all_sets_valid()) [[unlikely]]
    {
        owned_size_ = 0;
        return;
    }

    owned_size_ = compact_primary();
    sets_[primary_idx_]->bump_pool_version();
    refresh_versions(cached_versions_);
}

template <typename First, typename... Rest>
inline reorder_group<First, Rest...>::reorder_group(manager* mgr, std::array<single_class_set*, N> sets) noexcept
    : group_base<First, Rest...>(mgr, sets)
{
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

    s->owned_size = compact_primary();
    sets_[primary_idx_]->bump_pool_version();
    refresh_versions(s->cached_versions);
}

} // namespace ecs
