#pragma once
#include <tuple>
#include <array>
#include <limits>
#include "single_class_set.hpp"
#include "part/dense.hpp"
#include "entity.hpp"
// PREFETCH_R 宏: 集中定义于 part/force_inline.hpp

namespace ecs
{

class manager;

// group 家族公共基类: 掩码构建 / 主集合选择 / 块检查 / 压缩 / 版本刷新
//   group / owning_group / reorder_group 共用 (原三份重复实现收敛)
//   check_blocks 涉及 manager 完整类型, out-of-line 定义于 group_impl.hpp
template <typename First, typename... Rest>
class group_base
{
protected:
    static constexpr size_t N = 1 + sizeof...(Rest);
    using AllTypes = std::tuple<First, Rest...>;

    manager*                       mgr_;
    std::array<single_class_set*, N> sets_;
    size_t                         primary_idx_{0};
    dense<uint64_t>              required_masks_;
    uint32_t                       max_block_{0};
    bool                           use_mask_path_{false};
    dense<single_class_set*>     req_sets_;

    group_base(manager* mgr, std::array<single_class_set*, N> sets) noexcept
        : mgr_(mgr), sets_(sets)
    {
        find_smallest();
        build_required_masks_();
    }

    // 必需类型掩码构建 (type id → multi_block_bitmask 布局经 type_id 统一辅助)
    void build_required_masks_() noexcept
    {
        const int ids[N] = { type_id::get_type_id<First>(), type_id::get_type_id<Rest>()... };
        for (size_t i = 0; i < N; ++i)
        {
            const uint32_t b = type_id::mask_block_of(ids[i]);
            if (b > max_block_) max_block_ = b;
        }
        required_masks_.increase_capacity(max_block_ + 1, 0);
        for (size_t i = 0; i < N; ++i)
        {
            required_masks_[type_id::mask_block_of(ids[i])]
                |= 1ULL << type_id::mask_offset_of(ids[i]);
        }
        use_mask_path_ = (N >= 3) || ((max_block_ + 1) <= 5);
        req_sets_.increase_capacity(N, nullptr);
        for (size_t i = 0; i < N; ++i) req_sets_[i] = sets_[i];
    }

    void find_smallest() noexcept
    {
        size_t min_size = std::numeric_limits<size_t>::max();
        primary_idx_ = 0;
        for (size_t i = 0; i < N; ++i)
        {
            if (sets_[i] && sets_[i]->size() < min_size)
            {
                min_size = sets_[i]->size();
                primary_idx_ = i;
            }
        }
    }

    [[nodiscard]] bool all_sets_valid() const noexcept
    {
        for (size_t i = 0; i < N; ++i)
        {
            if (!sets_[i]) return false;
        }
        return true;
    }

    [[nodiscard]] bool check_blocks(uint32_t entity_index) const noexcept;

    // 主集合压缩: 命中条目前压 (物理重排), 返回压缩后条数
    //   owning_group / reorder_group 的 rebuild 共用
    size_t compact_primary() noexcept
    {
        auto* primary = sets_[primary_idx_];
        auto& indices = primary->get_entity_indices();
        const size_t n = primary->size();
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
                    if (req_sets_[k]->sparse_dense_at(eid) == single_class_set::dense_invalid)
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
        return write;
    }

    // 重建后刷新版本缓存 (各子类缓存位置不同, 数组由调用方提供)
    void refresh_versions(std::array<uint64_t, N>& vers) noexcept
    {
        for (size_t i = 0; i < N; ++i)
        {
            if (sets_[i]) vers[i] = sets_[i]->get_pool_version();
        }
    }

    template <size_t... Is>
    [[nodiscard]] bool has_all_impl(entity e, std::index_sequence<Is...>) const noexcept
    {
        return (... && (sets_[Is] && sets_[Is]->template get_ptr_fast<
            std::tuple_element_t<Is, AllTypes>>(e) != nullptr));
    }

public:
    template <typename T, size_t I = 0>
    [[nodiscard]] static constexpr size_t find_type_index() noexcept
    {
        if constexpr (I >= N) return N;
        else if constexpr (std::is_same_v<std::tuple_element_t<I, AllTypes>, T>) return I;
        else return find_type_index<T, I + 1>();
    }
};

template <typename First, typename... Rest>
class group : public group_base<First, Rest...>
{
private:
    using Base = group_base<First, Rest...>;
    using Base::N;
    using typename Base::AllTypes;
    using Base::mgr_;
    using Base::sets_;
    using Base::primary_idx_;
    using Base::required_masks_;
    using Base::max_block_;
    using Base::use_mask_path_;
    using Base::req_sets_;
    using Base::check_blocks;
    using Base::all_sets_valid;
    using Base::refresh_versions;
    using Base::has_all_impl;

    dense<uint32_t>              cached_;
    dense<std::array<uint32_t, N>> dense_mappings_;
    std::array<uint64_t, N>        cached_versions_{};

    void ensure_fresh() noexcept
    {
        for (size_t i = 0; i < N; ++i)
        {
            if (sets_[i] && sets_[i]->get_pool_version() != cached_versions_[i])
            {
                rebuild();
                return;
            }
        }
    }

    template <typename Func>
    void for_each_impl_2(Func&& func) noexcept
    {
        if (cached_.empty()) return;

        using T0 = std::tuple_element_t<0, AllTypes>;
        using T1 = std::tuple_element_t<1, AllTypes>;
        auto* pool0 = sets_[0]->template get_typed_pool_ptr<T0>();
        auto* pool1 = sets_[1]->template get_typed_pool_ptr<T1>();

        const size_t n = cached_.size();
        auto* mappings = dense_mappings_.data();
        auto* primary = sets_[primary_idx_];
        auto& indices = primary->get_entity_indices();

        // 提取原始 data 指针,避免循环内解引用
        auto* d0 = pool0->data();
        auto* d1 = pool1->data();

        // 4x 循环展开,最大化 ILP
        const size_t n4 = n & ~size_t{3};
        size_t i = 0;
        for (; i < n4; i += 4)
        {
            if (i + 12 < n) [[likely]]
            {
                auto& next = mappings[i + 12];
                PREFETCH_R(&d0[next[0]]);
                PREFETCH_R(&d1[next[1]]);
            }

            auto& m0 = mappings[i];
            auto& m1 = mappings[i + 1];
            auto& m2 = mappings[i + 2];
            auto& m3 = mappings[i + 3];

            if constexpr (std::is_invocable_v<Func, entity, First&, Rest&...>)
            {
                uint32_t eid0 = indices[m0[primary_idx_]];
                uint32_t eid1 = indices[m1[primary_idx_]];
                uint32_t eid2 = indices[m2[primary_idx_]];
                uint32_t eid3 = indices[m3[primary_idx_]];
                entity e0(eid0, primary->sparse_version_at(eid0));
                entity e1(eid1, primary->sparse_version_at(eid1));
                entity e2(eid2, primary->sparse_version_at(eid2));
                entity e3(eid3, primary->sparse_version_at(eid3));
                func(e0, d0[m0[0]], d1[m0[1]]);
                func(e1, d0[m1[0]], d1[m1[1]]);
                func(e2, d0[m2[0]], d1[m2[1]]);
                func(e3, d0[m3[0]], d1[m3[1]]);
            }
            else
            {
                func(d0[m0[0]], d1[m0[1]]);
                func(d0[m1[0]], d1[m1[1]]);
                func(d0[m2[0]], d1[m2[1]]);
                func(d0[m3[0]], d1[m3[1]]);
            }
        }
        for (; i < n; ++i)
        {
            auto& m = mappings[i];
            if constexpr (std::is_invocable_v<Func, entity, First&, Rest&...>)
            {
                uint32_t eid = indices[m[primary_idx_]];
                uint32_t ver = primary->sparse_version_at(eid);
                entity e(eid, ver);
                func(e, d0[m[0]], d1[m[1]]);
            }
            else
            {
                func(d0[m[0]], d1[m[1]]);
            }
        }
    }

    template <typename Func, size_t... Is>
    void for_each_impl(Func&& func, std::index_sequence<Is...>) noexcept
    {
        if (cached_.empty()) return;

        // 提取原始 data 指针,避免循环内解引用
        auto data_ptrs = std::make_tuple(
            sets_[Is]->template get_typed_pool_ptr<std::tuple_element_t<Is, AllTypes>>()->data()...
        );

        const size_t n = cached_.size();
        auto* mappings = dense_mappings_.data();
        auto* primary = sets_[primary_idx_];
        auto& indices = primary->get_entity_indices();

        // 8x 循环展开,最大化 ILP (7+ 组件实测仍优于 4x, 寄存器压力未造成回退)
        const size_t n8 = n & ~size_t{7};
        size_t i = 0;
        for (; i < n8; i += 8)
        {
            if (i + 16 < n) [[likely]]
            {
                auto& next = mappings[i + 16];
                ((void)PREFETCH_R(&std::get<Is>(data_ptrs)[next[Is]]), ...);
            }

            auto& m0 = mappings[i];
            auto& m1 = mappings[i + 1];
            auto& m2 = mappings[i + 2];
            auto& m3 = mappings[i + 3];
            auto& m4 = mappings[i + 4];
            auto& m5 = mappings[i + 5];
            auto& m6 = mappings[i + 6];
            auto& m7 = mappings[i + 7];

            if constexpr (std::is_invocable_v<Func, entity, First&, Rest&...>)
            {
                uint32_t eid0 = indices[m0[primary_idx_]];
                uint32_t eid1 = indices[m1[primary_idx_]];
                uint32_t eid2 = indices[m2[primary_idx_]];
                uint32_t eid3 = indices[m3[primary_idx_]];
                uint32_t eid4 = indices[m4[primary_idx_]];
                uint32_t eid5 = indices[m5[primary_idx_]];
                uint32_t eid6 = indices[m6[primary_idx_]];
                uint32_t eid7 = indices[m7[primary_idx_]];
                entity e0(eid0, primary->sparse_version_at(eid0));
                entity e1(eid1, primary->sparse_version_at(eid1));
                entity e2(eid2, primary->sparse_version_at(eid2));
                entity e3(eid3, primary->sparse_version_at(eid3));
                entity e4(eid4, primary->sparse_version_at(eid4));
                entity e5(eid5, primary->sparse_version_at(eid5));
                entity e6(eid6, primary->sparse_version_at(eid6));
                entity e7(eid7, primary->sparse_version_at(eid7));
                func(e0, std::get<Is>(data_ptrs)[m0[Is]]...);
                func(e1, std::get<Is>(data_ptrs)[m1[Is]]...);
                func(e2, std::get<Is>(data_ptrs)[m2[Is]]...);
                func(e3, std::get<Is>(data_ptrs)[m3[Is]]...);
                func(e4, std::get<Is>(data_ptrs)[m4[Is]]...);
                func(e5, std::get<Is>(data_ptrs)[m5[Is]]...);
                func(e6, std::get<Is>(data_ptrs)[m6[Is]]...);
                func(e7, std::get<Is>(data_ptrs)[m7[Is]]...);
            }
            else
            {
                func(std::get<Is>(data_ptrs)[m0[Is]]...);
                func(std::get<Is>(data_ptrs)[m1[Is]]...);
                func(std::get<Is>(data_ptrs)[m2[Is]]...);
                func(std::get<Is>(data_ptrs)[m3[Is]]...);
                func(std::get<Is>(data_ptrs)[m4[Is]]...);
                func(std::get<Is>(data_ptrs)[m5[Is]]...);
                func(std::get<Is>(data_ptrs)[m6[Is]]...);
                func(std::get<Is>(data_ptrs)[m7[Is]]...);
            }
        }
        const size_t n4 = n & ~size_t{3};
        for (; i < n4; i += 4)
        {
            auto& m0 = mappings[i];
            auto& m1 = mappings[i + 1];
            auto& m2 = mappings[i + 2];
            auto& m3 = mappings[i + 3];

            if constexpr (std::is_invocable_v<Func, entity, First&, Rest&...>)
            {
                uint32_t eid0 = indices[m0[primary_idx_]];
                uint32_t eid1 = indices[m1[primary_idx_]];
                uint32_t eid2 = indices[m2[primary_idx_]];
                uint32_t eid3 = indices[m3[primary_idx_]];
                entity e0(eid0, primary->sparse_version_at(eid0));
                entity e1(eid1, primary->sparse_version_at(eid1));
                entity e2(eid2, primary->sparse_version_at(eid2));
                entity e3(eid3, primary->sparse_version_at(eid3));
                func(e0, std::get<Is>(data_ptrs)[m0[Is]]...);
                func(e1, std::get<Is>(data_ptrs)[m1[Is]]...);
                func(e2, std::get<Is>(data_ptrs)[m2[Is]]...);
                func(e3, std::get<Is>(data_ptrs)[m3[Is]]...);
            }
            else
            {
                func(std::get<Is>(data_ptrs)[m0[Is]]...);
                func(std::get<Is>(data_ptrs)[m1[Is]]...);
                func(std::get<Is>(data_ptrs)[m2[Is]]...);
                func(std::get<Is>(data_ptrs)[m3[Is]]...);
            }
        }
        for (; i < n; ++i)
        {
            auto& m = mappings[i];
            if constexpr (std::is_invocable_v<Func, entity, First&, Rest&...>)
            {
                uint32_t eid = indices[m[primary_idx_]];
                uint32_t ver = primary->sparse_version_at(eid);
                entity e(eid, ver);
                func(e, std::get<Is>(data_ptrs)[m[Is]]...);
            }
            else
            {
                func(std::get<Is>(data_ptrs)[m[Is]]...);
            }
        }
    }

public:
    group(manager* mgr, std::array<single_class_set*, N> sets) noexcept;

    void rebuild() noexcept;

    [[nodiscard]] size_t size() noexcept { ensure_fresh(); return cached_.size(); }
    [[nodiscard]] bool   empty() noexcept { ensure_fresh(); return cached_.empty(); }

    [[nodiscard]] bool contains(entity e) noexcept
    {
        ensure_fresh();
        return all_sets_valid() && has_all_impl(e, std::index_sequence_for<First, Rest...>{});
    }

    template <typename T>
    [[nodiscard]] T* get(entity e) noexcept
    {
        ensure_fresh();
        constexpr size_t idx = Base::template find_type_index<T>();
        if constexpr (idx < N)
            return sets_[idx]->template get_ptr_fast<T>(e);
        return nullptr;
    }

    [[nodiscard]] entity front() noexcept
    {
        ensure_fresh();
        auto* primary = sets_[primary_idx_];
        auto& indices = primary->get_entity_indices();
        uint32_t eid = indices[cached_[0]];
        return entity(eid, primary->get_version_unchecked(eid));
    }

    [[nodiscard]] entity back() noexcept
    {
        ensure_fresh();
        auto* primary = sets_[primary_idx_];
        auto& indices = primary->get_entity_indices();
        uint32_t eid = indices[cached_[cached_.size() - 1]];
        return entity(eid, primary->get_version_unchecked(eid));
    }

    template <typename Func>
    void for_each(Func&& func) noexcept
    {
        ensure_fresh();
        if constexpr (N == 2)
            for_each_impl_2(std::forward<Func>(func));
        else
            for_each_impl(std::forward<Func>(func), std::index_sequence_for<First, Rest...>{});
    }
};

template <typename First, typename... Rest>
class owning_group : public group_base<First, Rest...>
{
private:
    using Base = group_base<First, Rest...>;
    using Base::N;
    using typename Base::AllTypes;
    using Base::sets_;
    using Base::primary_idx_;
    using Base::all_sets_valid;
    using Base::compact_primary;
    using Base::refresh_versions;
    using Base::has_all_impl;

    size_t                         owned_size_{0};
    std::array<uint64_t, N>        cached_versions_{};

    void ensure_fresh() noexcept
    {
        for (size_t i = 0; i < N; ++i)
        {
            if (sets_[i] && sets_[i]->get_pool_version() != cached_versions_[i])
            {
                rebuild();
                return;
            }
        }
    }

    template <typename Func>
    void for_each_impl_2(Func&& func) noexcept
    {
        if (owned_size_ == 0) return;

        using T0 = std::tuple_element_t<0, AllTypes>;
        using T1 = std::tuple_element_t<1, AllTypes>;
        auto* pool0_data = sets_[0]->template get_typed_pool_ptr<T0>()->data();
        auto* pool1_data = sets_[1]->template get_typed_pool_ptr<T1>()->data();
        auto* primary = sets_[primary_idx_];
        auto& indices = primary->get_entity_indices();
        const size_t other_idx = (primary_idx_ == 0) ? 1 : 0;
        auto* other_set = sets_[other_idx];

        if (primary_idx_ == 0)
        {
            // 4x 循环展开,最大化 ILP
            const size_t n4 = owned_size_ & ~size_t{3};
            size_t i = 0;
            for (; i < n4; i += 4)
            {
                if (i + 12 < owned_size_) [[likely]]
                {
                    uint32_t next_eid = indices[i + 12];
                    primary->prefetch_sparse_entry(next_eid);
                    other_set->prefetch_sparse_entry(next_eid);
                }
                uint32_t eid0 = indices[i];
                uint32_t eid1 = indices[i + 1];
                uint32_t eid2 = indices[i + 2];
                uint32_t eid3 = indices[i + 3];
                uint32_t od0 = other_set->sparse_dense_at(eid0);
                uint32_t od1 = other_set->sparse_dense_at(eid1);
                uint32_t od2 = other_set->sparse_dense_at(eid2);
                uint32_t od3 = other_set->sparse_dense_at(eid3);
                if constexpr (std::is_invocable_v<Func, entity, First&, Rest&...>)
                {
                    entity e0(eid0, primary->sparse_version_at(eid0));
                    entity e1(eid1, primary->sparse_version_at(eid1));
                    entity e2(eid2, primary->sparse_version_at(eid2));
                    entity e3(eid3, primary->sparse_version_at(eid3));
                    func(e0, pool0_data[i],     pool1_data[od0]);
                    func(e1, pool0_data[i + 1], pool1_data[od1]);
                    func(e2, pool0_data[i + 2], pool1_data[od2]);
                    func(e3, pool0_data[i + 3], pool1_data[od3]);
                }
                else
                {
                    func(pool0_data[i],     pool1_data[od0]);
                    func(pool0_data[i + 1], pool1_data[od1]);
                    func(pool0_data[i + 2], pool1_data[od2]);
                    func(pool0_data[i + 3], pool1_data[od3]);
                }
            }
            for (; i < owned_size_; ++i)
            {
                uint32_t eid = indices[i];
                uint32_t od = other_set->sparse_dense_at(eid);
                if constexpr (std::is_invocable_v<Func, entity, First&, Rest&...>)
                {
                    uint32_t ver = primary->sparse_version_at(eid);
                    entity e(eid, ver);
                    func(e, pool0_data[i], pool1_data[od]);
                }
                else
                {
                    func(pool0_data[i], pool1_data[od]);
                }
            }
        }
        else
        {
            // 4x 循环展开,最大化 ILP
            const size_t n4 = owned_size_ & ~size_t{3};
            size_t i = 0;
            for (; i < n4; i += 4)
            {
                if (i + 12 < owned_size_) [[likely]]
                {
                    uint32_t next_eid = indices[i + 12];
                    primary->prefetch_sparse_entry(next_eid);
                    other_set->prefetch_sparse_entry(next_eid);
                }
                uint32_t eid0 = indices[i];
                uint32_t eid1 = indices[i + 1];
                uint32_t eid2 = indices[i + 2];
                uint32_t eid3 = indices[i + 3];
                uint32_t od0 = other_set->sparse_dense_at(eid0);
                uint32_t od1 = other_set->sparse_dense_at(eid1);
                uint32_t od2 = other_set->sparse_dense_at(eid2);
                uint32_t od3 = other_set->sparse_dense_at(eid3);
                if constexpr (std::is_invocable_v<Func, entity, First&, Rest&...>)
                {
                    entity e0(eid0, primary->sparse_version_at(eid0));
                    entity e1(eid1, primary->sparse_version_at(eid1));
                    entity e2(eid2, primary->sparse_version_at(eid2));
                    entity e3(eid3, primary->sparse_version_at(eid3));
                    func(e0, pool0_data[od0], pool1_data[i]);
                    func(e1, pool0_data[od1], pool1_data[i + 1]);
                    func(e2, pool0_data[od2], pool1_data[i + 2]);
                    func(e3, pool0_data[od3], pool1_data[i + 3]);
                }
                else
                {
                    func(pool0_data[od0], pool1_data[i]);
                    func(pool0_data[od1], pool1_data[i + 1]);
                    func(pool0_data[od2], pool1_data[i + 2]);
                    func(pool0_data[od3], pool1_data[i + 3]);
                }
            }
            for (; i < owned_size_; ++i)
            {
                uint32_t eid = indices[i];
                uint32_t od = other_set->sparse_dense_at(eid);
                if constexpr (std::is_invocable_v<Func, entity, First&, Rest&...>)
                {
                    uint32_t ver = primary->sparse_version_at(eid);
                    entity e(eid, ver);
                    func(e, pool0_data[od], pool1_data[i]);
                }
                else
                {
                    func(pool0_data[od], pool1_data[i]);
                }
            }
        }
    }

    template <typename Func, size_t... Is>
    void for_each_impl(Func&& func, std::index_sequence<Is...>) noexcept
    {
        if (owned_size_ == 0) return;

        auto pools = std::make_tuple(
            sets_[Is]->template get_typed_pool_ptr<std::tuple_element_t<Is, AllTypes>>()->data()...
        );

        auto* primary = sets_[primary_idx_];
        auto& indices = primary->get_entity_indices();

        for (size_t i = 0; i < owned_size_; ++i)
        {
            if (i + 8 < owned_size_) [[likely]]
                primary->prefetch_sparse_entry(indices[i + 8]);

            uint32_t eid = indices[i];

            std::array<uint32_t, N> dense_idx;
            for (size_t k = 0; k < N; ++k)
            {
                if (k == primary_idx_)
                    dense_idx[k] = static_cast<uint32_t>(i);
                else
                {
                    if (i + 8 < owned_size_) [[likely]]
                        sets_[k]->prefetch_sparse_entry(indices[i + 8]);
                    dense_idx[k] = sets_[k]->sparse_dense_at(eid);
                }
            }

            auto comps = std::forward_as_tuple(
                std::get<Is>(pools)[dense_idx[Is]]...
            );

            std::apply([&](auto&... refs) {
                if constexpr (std::is_invocable_v<Func, entity, First&, Rest&...>)
                {
                    uint32_t ver = primary->sparse_version_at(eid);
                    entity e(eid, ver);
                    func(e, refs...);
                }
                else
                {
                    func(refs...);
                }
            }, comps);
        }
    }

public:
    owning_group(manager* mgr, std::array<single_class_set*, N> sets) noexcept;

    void rebuild() noexcept;

    [[nodiscard]] size_t size() noexcept { ensure_fresh(); return owned_size_; }
    [[nodiscard]] bool   empty() noexcept { ensure_fresh(); return owned_size_ == 0; }

    [[nodiscard]] bool contains(entity e) noexcept
    {
        ensure_fresh();
        return all_sets_valid() && has_all_impl(e, std::index_sequence_for<First, Rest...>{});
    }

    template <typename T>
    [[nodiscard]] T* get(entity e) noexcept
    {
        ensure_fresh();
        constexpr size_t idx = Base::template find_type_index<T>();
        if constexpr (idx < N)
            return sets_[idx]->template get_ptr_fast<T>(e);
        return nullptr;
    }

    [[nodiscard]] entity front() noexcept
    {
        ensure_fresh();
        auto* primary = sets_[primary_idx_];
        auto& indices = primary->get_entity_indices();
        return entity(indices[0], primary->get_version_unchecked(indices[0]));
    }

    [[nodiscard]] entity back() noexcept
    {
        ensure_fresh();
        auto* primary = sets_[primary_idx_];
        auto& indices = primary->get_entity_indices();
        return entity(indices[owned_size_ - 1], primary->get_version_unchecked(indices[owned_size_ - 1]));
    }

    template <typename Func>
    void for_each(Func&& func) noexcept
    {
        ensure_fresh();
        if constexpr (N == 2)
            for_each_impl_2(std::forward<Func>(func));
        else
            for_each_impl(std::forward<Func>(func), std::index_sequence_for<First, Rest...>{});
    }
};

}
