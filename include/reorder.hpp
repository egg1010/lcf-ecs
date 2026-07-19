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
    class_pool<uint64_t>              required_masks_;
    uint32_t                       max_block_{0};
    bool                           use_mask_path_{false};
    class_pool<single_class_set*>     req_sets_;

    reorder_state<N>* st() noexcept { return shared_ ? shared_ : &state_; }

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
            const sparse_entry* re_pri_ver_page = nullptr;
            size_t re_pri_page_idx = SIZE_MAX;
            const sparse_entry* re_other_dense_page = nullptr;
            size_t re_other_page_idx = SIZE_MAX;

            for (size_t i = 0; i < s->owned_size; ++i)
            {
                uint32_t eid = indices[i];

                size_t pid = eid >> primary->page_shift;
                if (pid != re_other_page_idx) [[unlikely]]
                {
                    re_other_dense_page = other_set->get_dense_page(eid);
                    re_other_page_idx = pid;
                }
                uint32_t od = 0xFFFFFFFFu;
                if (re_other_dense_page) [[likely]]
                    od = single_class_set::read_dense_from_page(re_other_dense_page, eid, other_set->page_mask);

                if constexpr (std::is_invocable_v<Func, entity, First&, Rest&...>)
                {
                    if (pid != re_pri_page_idx) [[unlikely]]
                    {
                        re_pri_ver_page = primary->get_version_page(eid);
                        re_pri_page_idx = pid;
                    }
                    uint32_t ver = 0;
                    if (re_pri_ver_page) [[likely]]
                        ver = single_class_set::read_version_from_page(re_pri_ver_page, eid, primary->page_mask);
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
            const sparse_entry* re_pri_ver_page = nullptr;
            size_t re_pri_page_idx = SIZE_MAX;
            const sparse_entry* re_other_dense_page = nullptr;
            size_t re_other_page_idx = SIZE_MAX;

            for (size_t i = 0; i < s->owned_size; ++i)
            {
                uint32_t eid = indices[i];

                size_t pid = eid >> primary->page_shift;
                if (pid != re_other_page_idx) [[unlikely]]
                {
                    re_other_dense_page = other_set->get_dense_page(eid);
                    re_other_page_idx = pid;
                }
                uint32_t od = 0xFFFFFFFFu;
                if (re_other_dense_page) [[likely]]
                    od = single_class_set::read_dense_from_page(re_other_dense_page, eid, other_set->page_mask);

                if constexpr (std::is_invocable_v<Func, entity, First&, Rest&...>)
                {
                    if (pid != re_pri_page_idx) [[unlikely]]
                    {
                        re_pri_ver_page = primary->get_version_page(eid);
                        re_pri_page_idx = pid;
                    }
                    uint32_t ver = 0;
                    if (re_pri_ver_page) [[likely]]
                        ver = single_class_set::read_version_from_page(re_pri_ver_page, eid, primary->page_mask);
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

        // per-set page caches for sparse lookups
        std::array<const sparse_entry*, N> set_dense_pages{};
        std::array<size_t, N> set_page_idxs{};
        set_page_idxs.fill(SIZE_MAX);

        auto get_sparse_cached = [&](single_class_set* s, uint32_t idx, const sparse_entry*& cur_page, size_t& cur_page_idx) -> uint32_t {
            size_t pid = idx >> s->page_shift;
            if (pid != cur_page_idx) [[unlikely]] { cur_page = s->get_dense_page(idx); cur_page_idx = pid; }
            return (cur_page) ? single_class_set::read_dense_from_page(cur_page, idx, s->page_mask) : 0xFFFFFFFFu;
        };

        const sparse_entry* re_pri_ver_page = nullptr;
        size_t re_pri_page_idx = SIZE_MAX;

        for (size_t i = 0; i < s->owned_size; ++i)
        {
            uint32_t eid = indices[i];

            auto comps = std::forward_as_tuple(
                (Is == primary_idx_)
                    ? std::get<Is>(pools)[i]
                    : std::get<Is>(pools)[get_sparse_cached(sets_[Is], eid, set_dense_pages[Is], set_page_idxs[Is])]...
            );

            std::apply([&](auto&... refs) {
                if constexpr (std::is_invocable_v<Func, entity, First&, Rest&...>)
                {
                    size_t pid = eid >> primary->page_shift;
                    if (pid != re_pri_page_idx) [[unlikely]]
                    {
                        re_pri_ver_page = primary->get_version_page(eid);
                        re_pri_page_idx = pid;
                    }
                    uint32_t ver = 0;
                    if (re_pri_ver_page) [[likely]]
                        ver = single_class_set::read_version_from_page(re_pri_ver_page, eid, primary->page_mask);
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