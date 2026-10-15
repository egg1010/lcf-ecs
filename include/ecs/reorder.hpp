#pragma once
#include <tuple>
#include <array>
#include <limits>
#include "group.hpp"

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
class reorder_group : public group_base<First, Rest...>
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

    reorder_state<N>               state_;
    reorder_state<N>*              shared_{nullptr};

    reorder_state<N>* st() noexcept { return shared_ ? shared_ : &state_; }

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

        if (primary_idx_ == 0)
        {
            for (size_t i = 0; i < s->owned_size; ++i)
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
            for (size_t i = 0; i < s->owned_size; ++i)
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
        auto* s = st();
        if (s->owned_size == 0) return;

        auto pools = std::make_tuple(
            sets_[Is]->template get_typed_pool_ptr<std::tuple_element_t<Is, AllTypes>>()->data()...
        );

        auto* primary = sets_[primary_idx_];
        auto& indices = primary->get_entity_indices();

        for (size_t i = 0; i < s->owned_size; ++i)
        {
            uint32_t eid = indices[i];

            auto comps = std::forward_as_tuple(
                (Is == primary_idx_)
                    ? std::get<Is>(pools)[i]
                    : std::get<Is>(pools)[sets_[Is]->sparse_dense_at(eid)]...
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