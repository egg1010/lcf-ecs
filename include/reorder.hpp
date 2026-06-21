#pragma once
#include <tuple>
#include <array>
#include <limits>
#include "single_class_set.hpp"
#include "entity.hpp"

namespace ecs
{

class manager;

template <typename... Types>
struct reorder_t {};

template <typename... Types>
inline constexpr reorder_t<Types...> reorder{};

template <size_t N>
struct reorder_state
{
    size_t                  owned_size{0};
    std::array<uint64_t, N> cached_versions{};
};

template <typename First, typename... Rest>
class reorder_group
{
private:
    static constexpr size_t N = 1 + sizeof...(Rest);
    using AllTypes = std::tuple<First, Rest...>;

    manager*                       mgr_;
    std::array<single_class_set*, N> sets_;
    size_t                         primary_idx_{0};
    reorder_state<N>               state_;
    reorder_state<N>*              shared_{nullptr};
    uint64_t                       required_mask_{0};

    reorder_state<N>* st() noexcept { return shared_ ? shared_ : &state_; }

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
        auto* s = st();
        for (size_t i = 0; i < N; ++i)
        {
            if (sets_[i] && sets_[i]->get_pool_version() != s->cached_versions[i])
            {
                rebuild();
                return;
            }
        }
    }

    template <typename Func>
    void for_each_impl_2(Func&& func) noexcept
    {
        auto* s = st();
        if (s->owned_size == 0) return;

        using T0 = std::tuple_element_t<0, AllTypes>;
        using T1 = std::tuple_element_t<1, AllTypes>;
        auto* pool0_data = sets_[0]->template get_typed_pool_ptr<T0>()->data();
        auto* pool1_data = sets_[1]->template get_typed_pool_ptr<T1>()->data();
        auto* primary = sets_[primary_idx_];
        auto& indices = primary->get_entity_indices();
        const size_t other_idx = (primary_idx_ == 0) ? 1 : 0;
        auto* other_set = sets_[other_idx];
        auto* other_sparse = other_set->get_sparse_combined().data();
        auto* primary_sparse = primary->get_sparse_combined().data();

        if (primary_idx_ == 0)
        {
            for (size_t i = 0; i < s->owned_size; ++i)
            {
                if (i + 8 < s->owned_size) [[likely]]
                {
                    uint32_t next_eid = indices[i + 8];
                    PREFETCH_R(&other_sparse[next_eid]);
                }
                uint32_t eid = indices[i];
                uint32_t od = static_cast<uint32_t>(other_sparse[eid] >> 32);

                if constexpr (std::is_invocable_v<Func, entity, First&, Rest&...>)
                {
                    entity e(eid, static_cast<uint32_t>(primary_sparse[eid]));
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
            for (size_t i = 0; i < s->owned_size; ++i)
            {
                if (i + 8 < s->owned_size) [[likely]]
                {
                    uint32_t next_eid = indices[i + 8];
                    PREFETCH_R(&other_sparse[next_eid]);
                }
                uint32_t eid = indices[i];
                uint32_t od = static_cast<uint32_t>(other_sparse[eid] >> 32);

                if constexpr (std::is_invocable_v<Func, entity, First&, Rest&...>)
                {
                    entity e(eid, static_cast<uint32_t>(primary_sparse[eid]));
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
        auto* s = st();
        if (s->owned_size == 0) return;

        auto pools = std::make_tuple(
            sets_[Is]->template get_typed_pool_ptr<std::tuple_element_t<Is, AllTypes>>()->data()...
        );

        auto* primary = sets_[primary_idx_];
        auto& indices = primary->get_entity_indices();
        auto* primary_sparse = primary->get_sparse_combined().data();

        std::array<const uint64_t*, N> sparse_arrays{};
        for (size_t k = 0; k < N; ++k)
            sparse_arrays[k] = sets_[k]->get_sparse_combined().data();

        for (size_t i = 0; i < s->owned_size; ++i)
        {
            if (i + 8 < s->owned_size) [[likely]]
            {
                uint32_t next_eid = indices[i + 8];
                for (size_t k = 0; k < N; ++k)
                {
                    if (k != primary_idx_)
                        PREFETCH_R(&sparse_arrays[k][next_eid]);
                }
            }

            uint32_t eid = indices[i];

            auto comps = std::forward_as_tuple(
                (Is == primary_idx_)
                    ? std::get<Is>(pools)[i]
                    : std::get<Is>(pools)[static_cast<uint32_t>(sparse_arrays[Is][eid] >> 32)]...
            );

            std::apply([&](auto&... refs) {
                if constexpr (std::is_invocable_v<Func, entity, First&, Rest&...>)
                {
                    entity e(eid, static_cast<uint32_t>(primary_sparse[eid]));
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
    reorder_group(manager* mgr, std::array<single_class_set*, N> sets) noexcept;

    void rebuild() noexcept;

    void share_with(reorder_group& other) noexcept
    {
        shared_ = other.st();
        primary_idx_ = other.primary_idx_;
    }

    [[nodiscard]] size_t size() noexcept { ensure_fresh(); return st()->owned_size; }
    [[nodiscard]] bool   empty() noexcept { ensure_fresh(); return st()->owned_size == 0; }

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
        auto* s = st();
        return entity(indices[s->owned_size - 1], primary->get_version_unchecked(indices[s->owned_size - 1]));
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