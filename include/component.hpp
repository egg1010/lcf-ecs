#pragma once
#include <concepts>
#include <tuple>
#include <array>
#include <limits>
#include <type_traits>
#include "single_class_set.hpp"
#include "type_id.hpp"
#include "entity_manager.hpp"
#include "group.hpp"
#include "runtime_view.hpp"

template <typename T>
concept IsEntity = std::same_as<T, entity>;

namespace ecs
{

template <typename... Types>
struct without_t {};

template <typename... Types>
struct with_t {};

template <typename... Types>
inline constexpr without_t<Types...> without{};

template <typename... Types>
inline constexpr with_t<Types...> with{};

template <typename... Types>
using exclude = without_t<Types...>;

template <typename... Types>
using get = with_t<Types...>;

template <typename... Types>
struct owned_t {};

template <typename... Types>
inline constexpr owned_t<Types...> owned{};

template <typename... Types>
struct ordered {};

template <typename T>
concept Component = std::is_copy_constructible_v<std::decay_t<T>>
                  || std::is_move_constructible_v<std::decay_t<T>>;

class manager;

struct component_meta
{
    size_t   size{0};
    uint64_t bit{0};
};

class manager
{
private:
    class_pool<single_class_set> components_c_;
    class_pool<component_meta> component_metas_;
    operating_message component_message;
    entity_manager entity_manager_;

    struct component_signal_event
    {
        uint32_t type;
        uint32_t entity_idx;
        uint32_t component_id;
    };
    static constexpr size_t comp_signal_buffer_size = 256;
    component_signal_event comp_signal_buffer_[comp_signal_buffer_size]{};
    uint32_t comp_signal_write_{0};
    uint32_t comp_signal_read_{0};

    void push_comp_signal(uint32_t type, uint32_t entity_idx, uint32_t component_id) noexcept
    {
        uint32_t next = (comp_signal_write_ + 1) % comp_signal_buffer_size;
        if (next == comp_signal_read_) [[unlikely]]
        {
            return;
        }
        comp_signal_buffer_[comp_signal_write_].type = type;
        comp_signal_buffer_[comp_signal_write_].entity_idx = entity_idx;
        comp_signal_buffer_[comp_signal_write_].component_id = component_id;
        comp_signal_write_ = next;
    }

    void ensure_type_exists(int type_id) noexcept
    {
        if (type_id >= static_cast<int>(components_c_.size())) [[unlikely]]
        {
            for (int i = static_cast<int>(components_c_.size()); i <= type_id; ++i)
            {
                components_c_.emplace_back();
            }
        }
        if (type_id >= static_cast<int>(component_metas_.size())) [[unlikely]]
        {
            for (int i = static_cast<int>(component_metas_.size()); i <= type_id; ++i)
            {
                component_metas_.emplace_back();
            }
        }
    }

    template <typename T>
    void register_component_meta() noexcept
    {
        int type_id = type_id::get_type_id<T>();
        ensure_type_exists(type_id);
        if (component_metas_[type_id].bit == 0) [[unlikely]]
        {
            component_metas_[type_id].size = sizeof(T);
            component_metas_[type_id].bit = 1ULL << (type_id - 1);
        }
    }

    void set_entity_mask_bit(entity entitys, uint64_t bit) noexcept
    {
        if (entitys.is_valid()) [[likely]]
        {
            entity_manager_.set_mask_bit(entitys.parts_.index_, bit);
        }
    }

    void clear_entity_mask_bit(entity entitys, uint64_t bit) noexcept
    {
        if (entitys.is_valid()) [[likely]]
        {
            entity_manager_.clear_mask_bit(entitys.parts_.index_, bit);
        }
    }

public:
    manager() noexcept = default;

    bool is_entity_valid(entity entitys) const noexcept
    {
        return entity_manager_.is_version_valid(entitys);
    }

    void append_preallocated_entities(size_t count) noexcept
    {
        entity_manager_.append_preallocated_entities(count);
    }
    [[nodiscard]] entity create_entity() noexcept
    {
        return entity_manager_.get_entity();
    }
    [[nodiscard]] operating_message& get_operating_message() noexcept
    {
        return component_message;
    }

    manager(manager&&) noexcept = default;
    manager(manager const&) = delete;
    manager& operator=(manager&&) noexcept = default;
    manager& operator=(manager const&) = delete;

    template <typename T>
    operating_message add(entity entitys, T&& component) noexcept
    {
        using DecayedT = std::decay_t<T>;
        int type_id = type_id::get_type_id<DecayedT>();
        register_component_meta<DecayedT>();
        ensure_type_exists(type_id);
        component_message = components_c_[type_id].add(entitys, std::forward<T>(component));
        set_entity_mask_bit(entitys, component_metas_[type_id].bit);
        push_comp_signal(0, entitys.parts_.index_, static_cast<uint32_t>(type_id));
        return component_message;
    }

    template <typename T>
    operating_message add_batch(std::span<const entity> entities, std::span<const T> components) noexcept
    {
        using DecayedT = std::decay_t<T>;
        int type_id = type_id::get_type_id<DecayedT>();
        register_component_meta<DecayedT>();
        ensure_type_exists(type_id);
        component_message = components_c_[type_id].add_batch(entities, components);
        uint64_t bit = component_metas_[type_id].bit;
        for (const auto& e : entities)
        {
            set_entity_mask_bit(e, bit);
        }
        return component_message;
    }

    template <typename T>
    operating_message add_batch(const class_pool<entity>& entities, const class_pool<T>& components) noexcept
    {
        return add_batch(std::span<const entity>(entities.data(), entities.size()),
                         std::span<const T>(components.data(), components.size()));
    }

    template <typename T>
    operating_message add_batch(class_pool<entity>&& entities, class_pool<T>&& components) noexcept
    {
        using DecayedT = std::decay_t<T>;
        int type_id = type_id::get_type_id<DecayedT>();
        register_component_meta<DecayedT>();
        ensure_type_exists(type_id);
        component_message = components_c_[type_id].add_batch(std::move(entities), std::move(components));
        uint64_t bit = component_metas_[type_id].bit;
        for (size_t i = 0; i < entities.size(); ++i)
        {
            set_entity_mask_bit(entities[i], bit);
        }
        return component_message;
    }

    template <IsEntity EE, typename T>
    operating_message add(T&& component, EE entitys) noexcept
    {
        add(entitys, std::forward<T>(component));
        return component_message;
    }
    template <IsEntity EE, typename T>
    manager& addc(T&& component, EE entitys) noexcept
    {
        add(entitys, std::forward<T>(component));
        return *this;
    }

    template <typename T>
    manager& addc(entity entitys, T&& component) noexcept
    {
        add(entitys, std::forward<T>(component));
        return *this;
    }
    template <typename T>
    [[nodiscard]] T* get_ptr(entity entitys) noexcept
    {
        single_class_set* set = get_single_class_set<T>();
        return set ? set->get_ptr<T>(entitys) : nullptr;
    }

    template <typename T>
    [[nodiscard]] const T* get_ptr(entity entitys) const noexcept
    {
        const single_class_set* set = get_single_class_set<T>();
        return set ? set->get_ptr<T>(entitys) : nullptr;
    }

    template <typename T>
    [[nodiscard]] T* get_ptr_fast(entity entitys) noexcept
    {
        single_class_set* set = get_single_class_set<T>();
        return set ? set->get_ptr_fast<T>(entitys) : nullptr;
    }

    template <typename T>
    [[nodiscard]] const T* get_ptr_fast(entity entitys) const noexcept
    {
        const single_class_set* set = get_single_class_set<T>();
        return set ? set->get_ptr_fast<T>(entitys) : nullptr;
    }

    template <typename T>
    operating_message soft_remove(entity entitys) noexcept
    {
        single_class_set* set = get_single_class_set<T>();
        if (set)
        {
            int type_id = type_id::get_type_id<T>();
            if (type_id < static_cast<int>(component_metas_.size()))
            {
                clear_entity_mask_bit(entitys, component_metas_[type_id].bit);
            }
            push_comp_signal(1, entitys.parts_.index_, static_cast<uint32_t>(type_id));
            return set->soft_remove(entitys);
        }
        return component_message;
    }

    template <typename T>
    operating_message hard_remove(entity entitys) noexcept
    {
        single_class_set* set = get_single_class_set<T>();
        if (set)
        {
            int type_id = type_id::get_type_id<T>();
            if (type_id < static_cast<int>(component_metas_.size()))
            {
                clear_entity_mask_bit(entitys, component_metas_[type_id].bit);
            }
            push_comp_signal(1, entitys.parts_.index_, static_cast<uint32_t>(type_id));
            return set->hard_remove(entitys);
        }
        return component_message;
    }

    template <typename T, IsEntity EE>
    manager& hard_removec(EE args) noexcept
    {
        hard_remove<T>(args);
        return *this;
    }

    template <typename T, IsEntity EE>
    manager& soft_removec(EE args) noexcept
    {
        soft_remove<T>(args);
        return *this;
    }

    template <typename T>
    [[nodiscard]] single_class_set* get_single_class_set() noexcept
    {
        using DecayedT = std::decay_t<T>;
        int type_id = type_id::get_type_id<DecayedT>();
        if (type_id >= static_cast<int>(components_c_.size())) [[unlikely]]
        {
            component_message.write_message(false, "manager::get_single_class_set(): component set does not exist for type_id=", std::to_string(type_id));
            return nullptr;
        }
        return &components_c_[type_id];
    }

    template <typename T>
    [[nodiscard]] const single_class_set* get_single_class_set() const noexcept
    {
        using DecayedT = std::decay_t<T>;
        int type_id = type_id::get_type_id<DecayedT>();
        if (type_id >= static_cast<int>(components_c_.size())) [[unlikely]] return nullptr;
        return &components_c_[type_id];
    }

    template <typename T>
    void reserve_component_capacity(size_t capacity) noexcept
    {
        using DecayedT = std::decay_t<T>;
        int type_id = type_id::get_type_id<DecayedT>();
        ensure_type_exists(type_id);
        components_c_[type_id].increase_capacity(capacity);
    }

    template <typename T>
    [[nodiscard]] class_pool<T>* get_component_vector() noexcept
    {
        single_class_set* set = get_single_class_set<T>();
        return set ? set->get_typed_pool_ptr<T>() : nullptr;
    }

    [[nodiscard]] uint64_t get_entity_mask(entity entitys) const noexcept
    {
        return entity_manager_.get_mask(entitys.parts_.index_);
    }

    [[nodiscard]] const component_meta* get_component_meta(int type_id) const noexcept
    {
        if (type_id < 0 || type_id >= static_cast<int>(component_metas_.size())) [[unlikely]]
            return nullptr;
        return &component_metas_[type_id];
    }

    template <typename T>
    [[nodiscard]] uint64_t get_component_bit() const noexcept
    {
        int type_id = type_id::get_type_id<T>();
        if (type_id >= static_cast<int>(component_metas_.size())) [[unlikely]]
            return 0;
        return component_metas_[type_id].bit;
    }

    [[nodiscard]] entity_manager& get_entity_manager() noexcept
    {
        return entity_manager_;
    }

    template <typename T>
    void delete_type_container() noexcept
    {
        using DecayedT = std::decay_t<T>;
        int type_id = type_id::get_type_id<DecayedT>();
        if (type_id < static_cast<int>(components_c_.size())) [[likely]]
            components_c_[type_id].clear();
    }

    void delete_entity(entity& entitys) noexcept
    {
        if (entitys.is_valid()) [[likely]]
            entity_manager_.destroy_entity(entitys);
    }

    // ======================== single_view ========================
    template <typename T>
    class single_view
    {
    private:
        single_class_set* set_;

    public:
        single_view(single_class_set* set) noexcept : set_(set) {}

        class iterator
        {
        private:
            single_view* view_;
            size_t index_;
        public:
            iterator(single_view* view, size_t index) noexcept : view_(view), index_(index) {}

            [[nodiscard]] entity operator*() const noexcept
            {
                auto& indices = view_->set_->get_entity_indices();
                return entity(indices[index_], view_->set_->get_version_unchecked(indices[index_]));
            }

            iterator& operator++() noexcept { ++index_; return *this; }
            [[nodiscard]] bool operator!=(const iterator& other) const noexcept { return index_ != other.index_; }
        };

        using component_iterator = T*;

        [[nodiscard]] component_iterator component_begin() noexcept
        {
            auto* pool = set_->template get_typed_pool_ptr<T>();
            return pool ? pool->data() : nullptr;
        }
        [[nodiscard]] component_iterator component_end() noexcept
        {
            auto* pool = set_->template get_typed_pool_ptr<T>();
            return pool ? pool->data() + pool->size() : nullptr;
        }

        [[nodiscard]] iterator begin() noexcept { return iterator(this, 0); }
        [[nodiscard]] iterator end() noexcept { return iterator(this, set_ ? set_->size() : 0); }

        [[nodiscard]] size_t size() const noexcept { return set_ ? set_->size() : 0; }
        [[nodiscard]] bool empty() const noexcept { return set_ ? set_->empty() : true; }

        [[nodiscard]] bool contains(entity e) const noexcept
        {
            return set_ && set_->template get_ptr<T>(e) != nullptr;
        }

        template <typename Func>
        void for_each(Func&& func) noexcept
        {
            if (!set_) [[unlikely]] return;
            auto* pool = set_->template get_typed_pool_ptr<T>();
            if (!pool) [[unlikely]] return;

            if constexpr (std::is_invocable_v<Func, entity, T&>)
            {
                auto& indices = set_->get_entity_indices();
                const size_t n = indices.size();
                for (size_t i = 0; i < n; ++i)
                {
                    entity e(indices[i], set_->get_version_unchecked(indices[i]));
                    func(e, (*pool)[i]);
                }
            }
            else
            {
                T* it = pool->data();
                T* end = it + pool->size();
                for (; it != end; ++it)
                    func(*it);
            }
        }
    };

    // ======================== multi_view ========================
    template <typename First, typename... Rest>
    class multi_view
    {
    private:
        static constexpr size_t N = 1 + sizeof...(Rest);
        std::array<single_class_set*, N> sets_;
        size_t primary_idx_{0};

        using AllTypes = std::tuple<First, Rest...>;

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

        template <size_t I>
        [[nodiscard]] auto* get_component(entity e, size_t primary_i) const noexcept
        {
            using T = std::tuple_element_t<I, AllTypes>;
            return I == primary_idx_
                ? sets_[I]->template get_ptr_unchecked_by_index<T>(primary_i)
                : sets_[I]->template get_ptr_fast<T>(e);
        }

        template <typename Func, size_t... Is>
        void for_each_impl(Func&& func, std::index_sequence<Is...>) noexcept
        {
            if (!all_sets_valid()) [[unlikely]] return;

            auto* primary = sets_[primary_idx_];
            auto& indices = primary->get_entity_indices();
            const size_t n = indices.size();

            for (size_t i = 0; i < n; ++i)
            {
                entity e(indices[i], primary->get_version_unchecked(indices[i]));
                auto comps = std::make_tuple(get_component<Is>(e, i)...);

                if ((... && (std::get<Is>(comps) != nullptr))) [[likely]]
                {
                    std::apply([&](auto*... ptrs) {
                        if constexpr (std::is_invocable_v<Func, entity, First&, Rest&...>)
                            func(e, *ptrs...);
                        else
                            func(*ptrs...);
                    }, comps);
                }
            }
        }

        template <size_t... Is>
        [[nodiscard]] bool contains_impl(entity e, std::index_sequence<Is...>) const noexcept
        {
            return (... && (sets_[Is] && sets_[Is]->template get_ptr_fast<std::tuple_element_t<Is, AllTypes>>(e) != nullptr));
        }

    public:
        multi_view(std::array<single_class_set*, N> sets) noexcept
            : sets_(sets)
        {
            find_smallest();
        }

        [[nodiscard]] size_t size() const noexcept
        {
            auto* p = sets_[primary_idx_];
            return p ? p->size() : 0;
        }

        [[nodiscard]] bool empty() const noexcept
        {
            return !all_sets_valid() || sets_[primary_idx_]->empty();
        }

        [[nodiscard]] bool contains(entity e) const noexcept
        {
            return all_sets_valid() && contains_impl(e, std::index_sequence_for<First, Rest...>{});
        }

        template <typename Func>
        void for_each(Func&& func) noexcept
        {
            for_each_impl(std::forward<Func>(func), std::index_sequence_for<First, Rest...>{});
        }
    };

    // ======================== single_view_without ========================
    template <typename T, typename... ExcludeTypes>
    class single_view_without
    {
    private:
        single_class_set* set_;
        manager* mgr_;

    public:
        single_view_without(single_class_set* set, manager* mgr) noexcept : set_(set), mgr_(mgr) {}

        [[nodiscard]] size_t size() const noexcept { return set_ ? set_->size() : 0; }
        [[nodiscard]] bool empty() const noexcept { return set_ ? set_->empty() : true; }

        template <typename Func>
        void for_each(Func&& func) noexcept
        {
            if (!set_) [[unlikely]] return;
            auto* pool = set_->template get_typed_pool_ptr<T>();
            if (!pool) [[unlikely]] return;
            auto& indices = set_->get_entity_indices();
            const size_t n = indices.size();

            for (size_t i = 0; i < n; ++i)
            {
                entity e(indices[i], set_->get_version_unchecked(indices[i]));
                bool should_exclude = (... || (mgr_->get_ptr_fast<ExcludeTypes>(e) != nullptr));

                if (!should_exclude) [[likely]]
                {
                    if constexpr (std::is_invocable_v<Func, entity, T&>)
                        func(e, (*pool)[i]);
                    else
                        func((*pool)[i]);
                }
            }
        }
    };

    // ======================== single_view_with ========================
    template <typename T, typename... GetTypes>
    class single_view_with
    {
    private:
        single_class_set* set_;
        manager* mgr_;

    public:
        single_view_with(single_class_set* set, manager* mgr) noexcept : set_(set), mgr_(mgr) {}

        [[nodiscard]] size_t size() const noexcept { return set_ ? set_->size() : 0; }
        [[nodiscard]] bool empty() const noexcept { return set_ ? set_->empty() : true; }

        template <typename Func>
        void for_each(Func&& func) noexcept
        {
            if (!set_) [[unlikely]] return;
            auto* pool = set_->template get_typed_pool_ptr<T>();
            if (!pool) [[unlikely]] return;
            auto& indices = set_->get_entity_indices();
            const size_t n = indices.size();

            for (size_t i = 0; i < n; ++i)
            {
                entity e(indices[i], set_->get_version_unchecked(indices[i]));
                auto& comp = (*pool)[i];
                auto get_ptrs = std::make_tuple(mgr_->get_ptr_fast<GetTypes>(e)...);
                std::apply([&](auto*... pts) {
                    if constexpr (std::is_invocable_v<Func, entity, T&, GetTypes*...>)
                        func(e, comp, pts...);
                    else
                        func(comp, pts...);
                }, get_ptrs);
            }
        }
    };

    // ======================== or_view ========================
    template <typename A, typename B>
    class or_view
    {
        single_class_set* set_a_;
        single_class_set* set_b_;
    public:
        or_view(single_class_set* a, single_class_set* b) noexcept : set_a_(a), set_b_(b) {}

        template <typename Func>
        void for_each(Func&& func) noexcept
        {
            if (set_a_)
            {
                auto* pool_a = set_a_->template get_typed_pool_ptr<A>();
                if (pool_a)
                {
                    auto& idx_a = set_a_->get_entity_indices();
                    for (size_t i = 0; i < idx_a.size(); ++i)
                    {
                        entity e(idx_a[i], set_a_->get_version_unchecked(idx_a[i]));
                        B* b = set_b_ ? set_b_->template get_ptr_fast<B>(e) : nullptr;
                        if constexpr (std::is_invocable_v<Func, entity, A*, B*>)
                        {
                            func(e, &(*pool_a)[i], b);
                        }
                        else
                        {
                            func(&(*pool_a)[i], b);
                        }
                    }
                }
            }
            if (set_b_)
            {
                auto* pool_b = set_b_->template get_typed_pool_ptr<B>();
                if (pool_b)
                {
                    auto& idx_b = set_b_->get_entity_indices();
                    for (size_t i = 0; i < idx_b.size(); ++i)
                    {
                        entity e(idx_b[i], set_b_->get_version_unchecked(idx_b[i]));
                        if (set_a_ && set_a_->template get_ptr_fast<A>(e)) continue;
                        if constexpr (std::is_invocable_v<Func, entity, A*, B*>)
                        {
                            func(e, nullptr, &(*pool_b)[i]);
                        }
                        else
                        {
                            func(nullptr, &(*pool_b)[i]);
                        }
                    }
                }
            }
        }
    };

    // ======================== filter_view ========================
    template <typename T, typename Pred>
    class filter_view
    {
        manager*       mgr_;
        Pred           pred_;
        class_pool<size_t> filtered_;

        void rebuild_impl() noexcept
        {
            filtered_.clear();
            auto* set = mgr_->template get_single_class_set<T>();
            if (!set) return;
            auto* pool = set->template get_typed_pool_ptr<T>();
            if (!pool) return;
            for (size_t i = 0; i < pool->size(); ++i)
            {
                if (pred_((*pool)[i]))
                    filtered_.emplace_back(i);
            }
        }

    public:
        filter_view(manager* mgr, Pred&& pred) noexcept
            : mgr_(mgr), pred_(std::forward<Pred>(pred))
        {
            rebuild_impl();
        }

        void rebuild() noexcept { rebuild_impl(); }

        [[nodiscard]] size_t size() const noexcept { return filtered_.size(); }
        [[nodiscard]] bool   empty() const noexcept { return filtered_.empty(); }

        template <typename Func>
        void for_each(Func&& func) noexcept
        {
            auto* set = mgr_->template get_single_class_set<T>();
            if (!set) return;
            auto* pool = set->template get_typed_pool_ptr<T>();
            auto& indices = set->get_entity_indices();

            for (size_t i = 0; i < filtered_.size(); ++i)
            {
                size_t dense_index = filtered_[i];
                if constexpr (std::is_invocable_v<Func, entity, T&>)
                {
                    entity e(indices[dense_index], set->get_version_unchecked(indices[dense_index]));
                    func(e, (*pool)[dense_index]);
                }
                else
                {
                    func((*pool)[dense_index]);
                }
            }
        }

        template <typename B> auto and_() noexcept;
        template <typename B> auto or_() noexcept;
    };

    // ======================== filter_and_view ========================
    template <typename T, typename B, typename Pred>
    class filter_and_view
    {
        manager*       mgr_;
        Pred           pred_;
        class_pool<size_t> filtered_;

        void rebuild_impl() noexcept
        {
            filtered_.clear();
            auto* set_a = mgr_->template get_single_class_set<T>();
            auto* set_b = mgr_->template get_single_class_set<B>();
            if (!set_a || !set_b) return;
            auto* pool_a = set_a->template get_typed_pool_ptr<T>();
            if (!pool_a) return;
            auto& indices = set_a->get_entity_indices();
            for (size_t i = 0; i < pool_a->size(); ++i)
            {
                if (!pred_((*pool_a)[i])) continue;
                entity e(indices[i], set_a->get_version_unchecked(indices[i]));
                if (set_b->template get_ptr_fast<B>(e))
                    filtered_.emplace_back(i);
            }
        }

    public:
        filter_and_view(manager* mgr, Pred&& pred) noexcept
            : mgr_(mgr), pred_(std::forward<Pred>(pred))
        {
            rebuild_impl();
        }

        void rebuild() noexcept { rebuild_impl(); }

        [[nodiscard]] size_t size() const noexcept { return filtered_.size(); }
        [[nodiscard]] bool   empty() const noexcept { return filtered_.empty(); }

        template <typename Func>
        void for_each(Func&& func) noexcept
        {
            auto* set_a = mgr_->template get_single_class_set<T>();
            auto* set_b = mgr_->template get_single_class_set<B>();
            if (!set_a || !set_b) return;
            auto* pool_a = set_a->template get_typed_pool_ptr<T>();
            auto& indices = set_a->get_entity_indices();

            for (size_t i = 0; i < filtered_.size(); ++i)
            {
                size_t dense_index = filtered_[i];
                entity e(indices[dense_index], set_a->get_version_unchecked(indices[dense_index]));
                B* b = set_b->template get_ptr_fast<B>(e);
                if (!b) continue;
                if constexpr (std::is_invocable_v<Func, entity, T&, B&>)
                {
                    func(e, (*pool_a)[dense_index], *b);
                }
                else
                {
                    func((*pool_a)[dense_index], *b);
                }
            }
        }
    };

    // ======================== filter_or_view ========================
    template <typename T, typename B, typename Pred>
    class filter_or_view
    {
        manager*       mgr_;
        Pred           pred_;
        class_pool<size_t> filtered_;

        void rebuild_impl() noexcept
        {
            filtered_.clear();
            auto* set_a = mgr_->template get_single_class_set<T>();
            if (!set_a) return;
            auto* pool_a = set_a->template get_typed_pool_ptr<T>();
            if (!pool_a) return;
            for (size_t i = 0; i < pool_a->size(); ++i)
            {
                if (pred_((*pool_a)[i]))
                    filtered_.emplace_back(i);
            }
        }

    public:
        filter_or_view(manager* mgr, Pred&& pred) noexcept
            : mgr_(mgr), pred_(std::forward<Pred>(pred))
        {
            rebuild_impl();
        }

        void rebuild() noexcept { rebuild_impl(); }

        [[nodiscard]] size_t size() const noexcept { return filtered_.size(); }
        [[nodiscard]] bool   empty() const noexcept { return filtered_.empty(); }

        template <typename Func>
        void for_each(Func&& func) noexcept
        {
            auto* set_a = mgr_->template get_single_class_set<T>();
            auto* set_b = mgr_->template get_single_class_set<B>();

            if (set_a)
            {
                auto* pool_a = set_a->template get_typed_pool_ptr<T>();
                if (pool_a)
                {
                    auto& idx_a = set_a->get_entity_indices();
                    for (size_t i = 0; i < filtered_.size(); ++i)
                    {
                        size_t dense_index = filtered_[i];
                        entity e(idx_a[dense_index], set_a->get_version_unchecked(idx_a[dense_index]));
                        B* b = set_b ? set_b->template get_ptr_fast<B>(e) : nullptr;
                        if constexpr (std::is_invocable_v<Func, entity, T*, B*>)
                        {
                            func(e, &(*pool_a)[dense_index], b);
                        }
                        else
                        {
                            func(&(*pool_a)[dense_index], b);
                        }
                    }
                }
            }

            if (set_b)
            {
                auto* pool_b = set_b->template get_typed_pool_ptr<B>();
                if (pool_b)
                {
                    auto& idx_b = set_b->get_entity_indices();
                    for (size_t i = 0; i < idx_b.size(); ++i)
                    {
                        entity e(idx_b[i], set_b->get_version_unchecked(idx_b[i]));
                        if (set_a)
                        {
                            T* a = set_a->template get_ptr_fast<T>(e);
                            if (a && pred_(*a)) continue;
                        }
                        if constexpr (std::is_invocable_v<Func, entity, T*, B*>)
                        {
                            func(e, nullptr, &(*pool_b)[i]);
                        }
                        else
                        {
                            func(nullptr, &(*pool_b)[i]);
                        }
                    }
                }
            }
        }
    };

    // ======================== view() 工厂方法 ========================
    template <typename T>
    [[nodiscard]] single_view<T> view() noexcept
    {
        return single_view<T>(get_single_class_set<T>());
    }

    template <typename First, typename Second, typename... Rest>
    [[nodiscard]] multi_view<First, Second, Rest...> view() noexcept
    {
        return multi_view<First, Second, Rest...>(
            std::array<single_class_set*, 2 + sizeof...(Rest)>{
                get_single_class_set<First>(),
                get_single_class_set<Second>(),
                get_single_class_set<Rest>()...
            });
    }

    template <typename T, typename... ExcludeTypes>
    [[nodiscard]] single_view_without<T, ExcludeTypes...> view(without_t<ExcludeTypes...>) noexcept
    {
        return single_view_without<T, ExcludeTypes...>(get_single_class_set<T>(), this);
    }

    template <typename T, typename... GetTypes>
    [[nodiscard]] single_view_with<T, GetTypes...> view(with_t<GetTypes...>) noexcept
    {
        return single_view_with<T, GetTypes...>(get_single_class_set<T>(), this);
    }

    // ======================== view_or / view_filtered 工厂方法 ========================
    template <typename A, typename B>
    [[nodiscard]] or_view<A, B> view_or() noexcept
    {
        return or_view<A, B>(get_single_class_set<A>(), get_single_class_set<B>());
    }

    template <typename T, typename Pred>
    [[nodiscard]] filter_view<T, Pred> view_filtered(Pred&& pred) noexcept
    {
        return filter_view<T, Pred>(this, std::forward<Pred>(pred));
    }

    // ======================== group() 工厂方法 ========================
    template <typename First, typename... Rest>
    [[nodiscard]] ecs::group<First, Rest...> group() noexcept
    {
        return ecs::group<First, Rest...>(this, std::array<single_class_set*, 1 + sizeof...(Rest)>{
            get_single_class_set<First>(),
            get_single_class_set<Rest>()...
        });
    }

    template <typename First, typename... Rest>
    [[nodiscard]] ecs::owning_group<First, Rest...> group(owned_t<First>) noexcept
    {
        return ecs::owning_group<First, Rest...>(this, std::array<single_class_set*, 1 + sizeof...(Rest)>{
            get_single_class_set<First>(),
            get_single_class_set<Rest>()...
        });
    }

    // ======================== runtime_view 工厂方法 ========================
    [[nodiscard]] ecs::runtime_view runtime_view_create(class_pool<int> required_ids,
                                                    class_pool<int> excluded_ids = {}) noexcept
    {
        return ecs::runtime_view(this, ecs::runtime_query(this, std::move(required_ids), std::move(excluded_ids)));
    }

    [[nodiscard]] single_class_set* get_single_class_set_by_id(int type_id) noexcept
    {
        if (type_id < 0 || type_id >= static_cast<int>(components_c_.size())) [[unlikely]]
            return nullptr;
        return &components_c_[type_id];
    }

    // ======================== 生命周期信号 API ========================

    void set_on_entity_created(void (*fn)(entity, void*) noexcept, void* user_data = nullptr) noexcept
    {
        entity_manager_.on_entity_created_ = fn;
        entity_manager_.on_entity_created_data_ = user_data;
    }
    void set_on_entity_destroyed(void (*fn)(entity, void*) noexcept, void* user_data = nullptr) noexcept
    {
        entity_manager_.on_entity_destroyed_ = fn;
        entity_manager_.on_entity_destroyed_data_ = user_data;
    }

    template <typename T>
    void set_on_add(void (*fn)(entity, void*, void*) noexcept, void* user_data = nullptr) noexcept
    {
        ensure_type_exists(type_id::get_type_id<T>());
        single_class_set* set = get_single_class_set<T>();
        if (set)
        {
            set->on_add_ = fn;
            set->on_add_data_ = user_data;
        }
    }
    template <typename T>
    void set_on_remove(void (*fn)(entity, void*, void*) noexcept, void* user_data = nullptr) noexcept
    {
        ensure_type_exists(type_id::get_type_id<T>());
        single_class_set* set = get_single_class_set<T>();
        if (set)
        {
            set->on_remove_ = fn;
            set->on_remove_data_ = user_data;
        }
    }

    template <typename Func>
    void flush_entity_signals(Func&& handler) noexcept
    {
        entity_manager_.flush_signals(std::forward<Func>(handler));
    }
    [[nodiscard]] bool has_pending_entity_signals() const noexcept
    {
        return entity_manager_.has_pending_signals();
    }

    template <typename Func>
    void flush_component_signals(Func&& handler) noexcept
    {
        while (comp_signal_read_ != comp_signal_write_)
        {
            auto& ev = comp_signal_buffer_[comp_signal_read_];
            handler(ev.type, ev.entity_idx, ev.component_id);
            comp_signal_read_ = (comp_signal_read_ + 1) % comp_signal_buffer_size;
        }
    }
    [[nodiscard]] bool has_pending_component_signals() const noexcept
    {
        return comp_signal_read_ != comp_signal_write_;
    }

    ~manager() = default;
};

// ======================== runtime_query / runtime_view out-of-line 定义 ========================

inline runtime_query::runtime_query(manager* mgr, class_pool<int> required_ids,
                                     class_pool<int> excluded_ids) noexcept
    : required_ids_(std::move(required_ids))
{
    if (required_ids_.empty()) [[unlikely]] return;

    size_t min_size = std::numeric_limits<size_t>::max();
    for (int tid : required_ids_)
    {
        const auto* meta = mgr->get_component_meta(tid);
        if (meta && meta->bit != 0)
        {
            req_mask_ |= meta->bit;
        }
        auto* set = mgr->get_single_class_set_by_id(tid);
        if (set && set->size() < min_size)
        {
            min_size = set->size();
            primary_set_ = set;
        }
    }

    for (int tid : excluded_ids)
    {
        const auto* meta = mgr->get_component_meta(tid);
        if (meta && meta->bit != 0)
        {
            exc_mask_ |= meta->bit;
        }
    }
}

inline bool runtime_view::all_sets_valid() const noexcept
{
    for (int tid : query_.required_ids_)
    {
        if (!mgr_->get_single_class_set_by_id(tid)) return false;
    }
    return true;
}

inline bool runtime_view::contains(entity e) noexcept
{
    ensure_fresh();
    if (!all_sets_valid()) [[unlikely]] return false;
    uint64_t mask = mgr_->get_entity_mask(e);
    if ((mask & query_.req_mask_) != query_.req_mask_) return false;
    if (query_.exc_mask_ != 0 && (mask & query_.exc_mask_) != 0) return false;
    return true;
}

inline void runtime_view::rebuild() noexcept
{
    size_t min_size = std::numeric_limits<size_t>::max();
    query_.primary_set_ = nullptr;
    for (int tid : query_.required_ids_)
    {
        auto* set = mgr_->get_single_class_set_by_id(tid);
        if (set && set->size() < min_size)
        {
            min_size = set->size();
            query_.primary_set_ = set;
        }
    }
    if (query_.primary_set_)
    {
        cached_primary_version_ = query_.primary_set_->get_pool_version();
    }
}

template <typename T>
inline T* runtime_view::get_ptr(entity e) noexcept
{
    ensure_fresh();
    return mgr_->template get_ptr_fast<T>(e);
}

template <typename Func>
inline void runtime_view::for_each(Func&& func) noexcept
{
    ensure_fresh();
    if (query_.primary_set_ == nullptr || !all_sets_valid()) [[unlikely]] return;

    auto* primary = query_.primary_set_;
    auto& indices = primary->get_entity_indices();
    const size_t n = indices.size();

    for (size_t i = 0; i < n; ++i)
    {
        uint32_t idx = indices[i];
        uint64_t mask = mgr_->get_entity_mask(entity(idx, primary->get_version_unchecked(idx)));

        if ((mask & query_.req_mask_) != query_.req_mask_)
        {
            continue;
        }
        if (query_.exc_mask_ != 0 && (mask & query_.exc_mask_) != 0)
        {
            continue;
        }

        entity e(idx, primary->get_version_unchecked(idx));

        if constexpr (std::is_invocable_v<Func, entity>)
        {
            func(e);
        }
        else
        {
            func();
        }
    }
}

// ======================== filter_view 的 and_ / or_ 方法 ========================
template <typename T, typename Pred>
template <typename B>
auto manager::filter_view<T, Pred>::and_() noexcept
{
    return filter_and_view<T, B, Pred>(mgr_, std::move(pred_));
}

template <typename T, typename Pred>
template <typename B>
auto manager::filter_view<T, Pred>::or_() noexcept
{
    return filter_or_view<T, B, Pred>(mgr_, std::move(pred_));
}

} // namespace ecs