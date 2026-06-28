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
    use_mask_path_ = ((type_id::get_type_id<First>() <= 64) && ... && (type_id::get_type_id<Rest>() <= 64));
    required_mask_ = (mgr_->template get_component_bit<First>() | ... | mgr_->template get_component_bit<Rest>());
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
            entity e(indices[i], primary->get_version_unchecked(indices[i]));
            if ((mgr_->get_entity_mask(e) & required_mask_) == required_mask_)
                cached_.emplace_back(static_cast<uint32_t>(i));
        }
    }
    else
    {
        for (size_t i = 0; i < n; ++i)
        {
            uint32_t idx = indices[i];
            uint32_t ver = primary->get_version_unchecked(idx);
            bool all_in = true;
            for (size_t k = 0; k < N; ++k)
            {
                if (k == primary_idx_) continue;
                if (!single_class_set::sparse_contains_version(sets_[k], idx, ver))
                {
                    all_in = false;
                    break;
                }
            }
            if (all_in)
                cached_.emplace_back(static_cast<uint32_t>(i));
        }
    }
    for (size_t i = 0; i < N; ++i)
    {
        if (sets_[i]) cached_versions_[i] = sets_[i]->get_pool_version();
    }

    dense_mappings_.clear();
    dense_mappings_.resize(cached_.size(), std::array<uint32_t, N>{});
    std::array<const uint64_t*, N> sparse_arrays{};
    for (size_t k = 0; k < N; ++k)
        sparse_arrays[k] = sets_[k]->get_sparse_combined().data();
    for (size_t i = 0; i < cached_.size(); ++i)
    {
        auto& entry = dense_mappings_[i];
        uint32_t eid = indices[cached_[i]];
        for (size_t k = 0; k < N; ++k)
        {
            if (k == primary_idx_)
                entry[k] = cached_[i];
            else
                entry[k] = static_cast<uint32_t>(sparse_arrays[k][eid] >> 32);
        }
    }
}

template <typename First, typename... Rest>
inline owning_group<First, Rest...>::owning_group(manager* mgr, std::array<single_class_set*, N> sets) noexcept
    : mgr_(mgr), sets_(sets)
{
    find_smallest();
    use_mask_path_ = ((type_id::get_type_id<First>() <= 64) && ... && (type_id::get_type_id<Rest>() <= 64));
    required_mask_ = (mgr_->template get_component_bit<First>() | ... | mgr_->template get_component_bit<Rest>());
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
            entity e(indices[read], primary->get_version_unchecked(indices[read]));
            if ((mgr_->get_entity_mask(e) & required_mask_) == required_mask_)
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
            uint32_t idx = indices[read];
            uint32_t ver = primary->get_version_unchecked(idx);
            bool all_in = true;
            for (size_t k = 0; k < N; ++k)
            {
                if (k == primary_idx_) continue;
                if (!single_class_set::sparse_contains_version(sets_[k], idx, ver))
                {
                    all_in = false;
                    break;
                }
            }
            if (all_in)
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
    use_mask_path_ = ((type_id::get_type_id<First>() <= 64) && ... && (type_id::get_type_id<Rest>() <= 64));
    required_mask_ = (mgr_->template get_component_bit<First>() | ... | mgr_->template get_component_bit<Rest>());
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
            entity e(indices[read], primary->get_version_unchecked(indices[read]));
            if ((mgr_->get_entity_mask(e) & required_mask_) == required_mask_)
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
            uint32_t idx = indices[read];
            uint32_t ver = primary->get_version_unchecked(idx);
            bool all_in = true;
            for (size_t k = 0; k < N; ++k)
            {
                if (k == primary_idx_) continue;
                if (!single_class_set::sparse_contains_version(sets_[k], idx, ver))
                {
                    all_in = false;
                    break;
                }
            }
            if (all_in)
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

    for (size_t i = 0; i < N; ++i)
    {
        if (sets_[i]) s->cached_versions[i] = sets_[i]->get_pool_version();
    }
}

} // namespace ecs
