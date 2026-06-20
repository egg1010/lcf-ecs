#pragma once
#include <tuple>
#include <array>
#include <limits>
#include "single_class_set.hpp"
#include "class_pool.hpp"
#include "entity.hpp"

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
    class_pool<size_t>             cached_;
    std::array<uint64_t, N>        cached_versions_{};

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

    template <size_t I>
    [[nodiscard]] auto& get_component_unchecked(entity e, size_t primary_dense_idx) const noexcept
    {
        using T = std::tuple_element_t<I, AllTypes>;
        if constexpr (I == 0 && N == 1)
        {
            return (*sets_[I]->template get_typed_pool_ptr<T>())[primary_dense_idx];
        }
        else
        {
            size_t dense_idx = (I == primary_idx_)
                ? primary_dense_idx
                : sets_[I]->get_sparse()[e.parts_.index_].dense_index_;
            return (*sets_[I]->template get_typed_pool_ptr<T>())[dense_idx];
        }
    }

    template <typename Func, size_t... Is>
    void for_each_impl(Func&& func, std::index_sequence<Is...>) noexcept
    {
        if (cached_.empty()) return;

        auto* primary = sets_[primary_idx_];
        auto& indices = primary->get_entity_indices();

        for (size_t i = 0; i < cached_.size(); ++i)
        {
            size_t dense_idx = cached_[i];
            entity e(indices[dense_idx], primary->get_version_unchecked(indices[dense_idx]));

            auto comps = std::forward_as_tuple(
                get_component_unchecked<Is>(e, dense_idx)...
            );

            std::apply([&](auto&... refs) {
                if constexpr (std::is_invocable_v<Func, entity, First&, Rest&...>)
                {
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
    group(manager* mgr, std::array<single_class_set*, N> sets) noexcept
        : mgr_(mgr), sets_(sets)
    {
        find_smallest();
        rebuild();
    }

    void rebuild() noexcept
    {
        cached_.clear();
        if (!all_sets_valid()) [[unlikely]] return;

        auto* primary = sets_[primary_idx_];
        auto& indices = primary->get_entity_indices();
        const size_t n = indices.size();

        for (size_t i = 0; i < n; ++i)
        {
            entity e(indices[i], primary->get_version_unchecked(indices[i]));
            if (contains_impl(e, std::index_sequence_for<First, Rest...>{}))
            {
                cached_.emplace_back(i);
            }
        }
        for (size_t i = 0; i < N; ++i)
        {
            if (sets_[i]) cached_versions_[i] = sets_[i]->get_pool_version();
        }
    }

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
        size_t dense_idx = cached_[0];
        return entity(indices[dense_idx], primary->get_version_unchecked(indices[dense_idx]));
    }

    [[nodiscard]] entity back() noexcept
    {
        ensure_fresh();
        auto* primary = sets_[primary_idx_];
        auto& indices = primary->get_entity_indices();
        size_t dense_idx = cached_[cached_.size() - 1];
        return entity(indices[dense_idx], primary->get_version_unchecked(indices[dense_idx]));
    }

    template <typename Func>
    void for_each(Func&& func) noexcept
    {
        ensure_fresh();
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

    template <size_t I>
    [[nodiscard]] auto& get_component_unchecked(entity e, size_t primary_dense_idx) const noexcept
    {
        using T = std::tuple_element_t<I, AllTypes>;
        size_t dense_idx = (I == primary_idx_)
            ? primary_dense_idx
            : sets_[I]->get_sparse()[e.parts_.index_].dense_index_;
        return (*sets_[I]->template get_typed_pool_ptr<T>())[dense_idx];
    }

    template <size_t... Is>
    [[nodiscard]] bool has_all_impl(entity e, std::index_sequence<Is...>) const noexcept
    {
        return (... && (sets_[Is] && sets_[Is]->template get_ptr_fast<
            std::tuple_element_t<Is, AllTypes>>(e) != nullptr));
    }

    template <typename Func, size_t... Is>
    void for_each_impl(Func&& func, std::index_sequence<Is...>) noexcept
    {
        if (owned_size_ == 0) return;

        auto* primary = sets_[primary_idx_];
        auto& indices = primary->get_entity_indices();

        for (size_t i = 0; i < owned_size_; ++i)
        {
            entity e(indices[i], primary->get_version_unchecked(indices[i]));

            auto comps = std::forward_as_tuple(
                get_component_unchecked<Is>(e, i)...
            );

            std::apply([&](auto&... refs) {
                if constexpr (std::is_invocable_v<Func, entity, First&, Rest&...>)
                {
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
    owning_group(manager* mgr, std::array<single_class_set*, N> sets) noexcept
        : mgr_(mgr), sets_(sets)
    {
        find_smallest();
        rebuild();
    }

    void rebuild() noexcept
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
        for (size_t read = 0; read < n; ++read)
        {
            entity e(indices[read], primary->get_version_unchecked(indices[read]));
            if (has_all_impl(e, std::index_sequence_for<First, Rest...>{}))
            {
                if (read != write)
                {
                    primary->swap_dense_and_pool(read, write);
                }
                ++write;
            }
        }
        owned_size_ = write;
        for (size_t i = 0; i < N; ++i)
        {
            if (sets_[i]) cached_versions_[i] = sets_[i]->get_pool_version();
        }
    }

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
        for_each_impl(std::forward<Func>(func), std::index_sequence_for<First, Rest...>{});
    }
};

}