#pragma once
#include <tuple>
#include <array>
#include <limits>
#include "single_class_set.hpp"
#include "part/dense.hpp"
#include "entity.hpp"

#ifndef PREFETCH_R
#define PREFETCH_R(ptr) DENSE_PREFETCH_R(ptr)
#endif

namespace ecs
{

class manager;

// ======================== group ========================
template <typename First, typename... Rest>
class group
{
private:
    static constexpr size_t N = 1 + sizeof...(Rest);
    using AllTypes = std::tuple<First, Rest...>;

    manager*                       mgr_;
    std::array<single_class_set*, N> sets_;
    size_t                         primary_idx_{0};
    dense<uint32_t>              cached_;
    dense<std::array<uint32_t, N>> dense_mappings_;
    std::array<uint64_t, N>        cached_versions_{};
    dense<uint64_t>              required_masks_;
    uint32_t                       max_block_{0};
    bool                           use_mask_path_{false};
    dense<single_class_set*>     req_sets_;

    [[nodiscard]] bool check_blocks(uint32_t entity_index) const noexcept;

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

        for (size_t i = 0; i < n; ++i)
        {
            if (i + 8 < n) [[likely]]
            {
                auto& next = mappings[i + 8];
                PREFETCH_R(&(*pool0)[next[0]]);
                PREFETCH_R(&(*pool1)[next[1]]);
            }

            auto& m = mappings[i];

            if constexpr (std::is_invocable_v<Func, entity, First&, Rest&...>)
            {
                uint32_t eid = indices[m[primary_idx_]];
                uint32_t ver = primary->sparse_version_at_public(eid);
                entity e(eid, ver);
                func(e, (*pool0)[m[0]], (*pool1)[m[1]]);
            }
            else
            {
                func((*pool0)[m[0]], (*pool1)[m[1]]);
            }
        }
    }

    template <typename Func, size_t... Is>
    void for_each_impl(Func&& func, std::index_sequence<Is...>) noexcept
    {
        if (cached_.empty()) return;

        auto pools = std::make_tuple(
            sets_[Is]->template get_typed_pool_ptr<std::tuple_element_t<Is, AllTypes>>()...
        );

        const size_t n = cached_.size();
        auto* mappings = dense_mappings_.data();
        auto* primary = sets_[primary_idx_];
        auto& indices = primary->get_entity_indices();

        for (size_t i = 0; i < n; ++i)
        {
            if (i + 8 < n) [[likely]]
            {
                auto& next = mappings[i + 8];
                ((void)PREFETCH_R(&(*std::get<Is>(pools))[next[Is]]), ...);
            }

            auto& m = mappings[i];
            auto comps = std::forward_as_tuple(
                (*std::get<Is>(pools))[m[Is]]...
            );

            std::apply([&](auto&... refs) {
                if constexpr (std::is_invocable_v<Func, entity, First&, Rest&...>)
                {
                    uint32_t eid = indices[m[primary_idx_]];
                    uint32_t ver = primary->sparse_version_at_public(eid);
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

    template <size_t... Is>
    [[nodiscard]] bool contains_impl(entity e, std::index_sequence<Is...>) const noexcept
    {
        return (... && (sets_[Is] && sets_[Is]->template get_ptr_fast<
            std::tuple_element_t<Is, AllTypes>>(e) != nullptr));
    }

public:
    group(manager* mgr, std::array<single_class_set*, N> sets) noexcept;

    void rebuild() noexcept;

    [[nodiscard]] size_t size() noexcept { ensure_fresh(); return cached_.size(); }
    [[nodiscard]] bool   empty() noexcept { ensure_fresh(); return cached_.empty(); }

    [[nodiscard]] bool contains(entity e) noexcept
    {
        ensure_fresh();
        return all_sets_valid() && contains_impl(e, std::index_sequence_for<First, Rest...>{});
    }

    template <typename T, size_t I = 0>
    [[nodiscard]] static constexpr size_t find_type_index() noexcept
    {
        if constexpr (I >= N) return N;
        else if constexpr (std::is_same_v<std::tuple_element_t<I, AllTypes>, T>) return I;
        else return find_type_index<T, I + 1>();
    }

    template <typename T>
    [[nodiscard]] T* get(entity e) noexcept
    {
        ensure_fresh();
        constexpr size_t idx = find_type_index<T>();
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

// ======================== owning_group ========================
template <typename First, typename... Rest>
class owning_group
{
private:
    static constexpr size_t N = 1 + sizeof...(Rest);
    using AllTypes = std::tuple<First, Rest...>;

    manager*                       mgr_;
    std::array<single_class_set*, N> sets_;
    size_t                         primary_idx_{0};
    size_t                         owned_size_{0};
    std::array<uint64_t, N>        cached_versions_{};
    dense<uint64_t>              required_masks_;
    uint32_t                       max_block_{0};
    bool                           use_mask_path_{false};
    dense<single_class_set*>     req_sets_;

    [[nodiscard]] bool check_blocks(uint32_t entity_index) const noexcept;

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
            for (size_t i = 0; i < owned_size_; ++i)
            {
                uint32_t eid = indices[i];
                uint32_t od = other_set->sparse_dense_at_public(eid);
                if constexpr (std::is_invocable_v<Func, entity, First&, Rest&...>)
                {
                    uint32_t ver = primary->sparse_version_at_public(eid);
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
            for (size_t i = 0; i < owned_size_; ++i)
            {
                uint32_t eid = indices[i];
                uint32_t od = other_set->sparse_dense_at_public(eid);
                if constexpr (std::is_invocable_v<Func, entity, First&, Rest&...>)
                {
                    uint32_t ver = primary->sparse_version_at_public(eid);
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
            uint32_t eid = indices[i];

            std::array<uint32_t, N> dense_idx;
            for (size_t k = 0; k < N; ++k)
            {
                if (k == primary_idx_)
                    dense_idx[k] = static_cast<uint32_t>(i);
                else
                    dense_idx[k] = sets_[k]->sparse_dense_at_public(eid);
            }

            auto comps = std::forward_as_tuple(
                std::get<Is>(pools)[dense_idx[Is]]...
            );

            std::apply([&](auto&... refs) {
                if constexpr (std::is_invocable_v<Func, entity, First&, Rest&...>)
                {
                    uint32_t ver = primary->sparse_version_at_public(eid);
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

    template <size_t... Is>
    [[nodiscard]] bool has_all_impl(entity e, std::index_sequence<Is...>) const noexcept
    {
        return (... && (sets_[Is] && sets_[Is]->template get_ptr_fast<
            std::tuple_element_t<Is, AllTypes>>(e) != nullptr));
    }

    template <size_t... Is>
    [[nodiscard]] bool contains_impl(entity e, std::index_sequence<Is...>) const noexcept
    {
        return (... && (sets_[Is] && sets_[Is]->template get_ptr_fast<
            std::tuple_element_t<Is, AllTypes>>(e) != nullptr));
    }

public:
    owning_group(manager* mgr, std::array<single_class_set*, N> sets) noexcept;

    void rebuild() noexcept;

    [[nodiscard]] size_t size() noexcept { ensure_fresh(); return owned_size_; }
    [[nodiscard]] bool   empty() noexcept { ensure_fresh(); return owned_size_ == 0; }

    [[nodiscard]] bool contains(entity e) noexcept
    {
        ensure_fresh();
        return all_sets_valid() && contains_impl(e, std::index_sequence_for<First, Rest...>{});
    }

    template <typename T, size_t I = 0>
    [[nodiscard]] static constexpr size_t find_type_index() noexcept
    {
        if constexpr (I >= N) return N;
        else if constexpr (std::is_same_v<std::tuple_element_t<I, AllTypes>, T>) return I;
        else return find_type_index<T, I + 1>();
    }

    template <typename T>
    [[nodiscard]] T* get(entity e) noexcept
    {
        ensure_fresh();
        constexpr size_t idx = find_type_index<T>();
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