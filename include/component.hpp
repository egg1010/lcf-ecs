#pragma once
#include <concepts>
#include <tuple>
#include <array>
#include <limits>
#include <type_traits>
#include "single_class_set.hpp"
#include "part/type_id.hpp"
#include "entity_manager.hpp"
#include "group.hpp"
#include "reorder.hpp"
#include "runtime_view.hpp"
#include "part/radix_sort_helper.hpp"
#include "view_tags.hpp"

template <typename T>
concept IsEntity = std::same_as<T, entity>;

namespace ecs
{

class manager;
class command_buffer;

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
    size_t default_component_capacity_{0};

    struct component_signal_event
    {
        uint32_t type;
        uint32_t entity_idx;
        uint32_t component_id;
    };
    static constexpr size_t comp_signal_buffer_size = 1024;
    static_assert((comp_signal_buffer_size & (comp_signal_buffer_size - 1)) == 0,
                  "comp_signal_buffer_size must be power of 2");
    component_signal_event comp_signal_buffer_[comp_signal_buffer_size]{};
    uint32_t comp_signal_write_{0};
    uint32_t comp_signal_read_{0};
    class_pool<component_signal_event> comp_signal_overflow_chain_;
    size_t comp_signal_overflow_read_{0};
    uint64_t comp_signal_overflow_count_{0};
    bool comp_signal_enabled_{true};
    bool comp_signal_flushing_{false};
    bool track_changes_enabled_default_{true};

    void push_comp_signal(uint32_t type, uint32_t entity_idx, uint32_t component_id) noexcept
    {
        if (!comp_signal_enabled_) [[unlikely]] return;
        uint32_t next = (comp_signal_write_ + 1) & (comp_signal_buffer_size - 1);
        if (next == comp_signal_read_) [[unlikely]]
        {
            ++comp_signal_overflow_count_;
            comp_signal_overflow_chain_.emplace_back(component_signal_event{type, entity_idx, component_id});
            return;
        }
        comp_signal_buffer_[comp_signal_write_] = {type, entity_idx, component_id};
        comp_signal_write_ = next;
    }

    void ensure_type_exists(int type_id) noexcept
    {
        if (type_id >= static_cast<int>(components_c_.size())) [[unlikely]]
        {
            for (int i = static_cast<int>(components_c_.size()); i <= type_id; ++i)
            {
                components_c_.emplace_back();
                single_class_set& new_set = components_c_.back();
                new_set.track_changes_enabled_ = track_changes_enabled_default_;
                if (default_component_capacity_ > 0)
                {
                    new_set.increase_capacity(default_component_capacity_);
                }
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
        if (type_id < static_cast<int>(component_metas_.size()) &&
            component_metas_[type_id].size != 0) [[likely]]
        {
            return;
        }
        ensure_type_exists(type_id);
        if (component_metas_[type_id].size == 0) [[unlikely]]
        {
            component_metas_[type_id].size = sizeof(T);
            // type_id <= 64 进 mask 快路径;>64 不进 mask,走 sparse 交集
            if (type_id <= 64)
            {
                component_metas_[type_id].bit = 1ULL << (type_id - 1);
            }
        }
    }

    void set_entity_mask_bit(entity entitys, uint64_t bit) noexcept
    {
        if (bit == 0) return;
        if (entitys.is_valid()) [[likely]]
        {
            entity_manager_.set_mask_bit(entitys.parts_.index_, bit);
        }
    }

    void clear_entity_mask_bit(entity entitys, uint64_t bit) noexcept
    {
        if (bit == 0) return;
        if (entitys.is_valid()) [[likely]]
        {
            entity_manager_.clear_mask_bit(entitys.parts_.index_, bit);
        }
    }

    template <typename T>
    void add_component_without_message(entity entitys, T&& component) noexcept
    {
        using DecayedT = std::decay_t<T>;
        int type_id = type_id::get_type_id<DecayedT>();
        register_component_meta<DecayedT>();
        components_c_[type_id].add(entitys, std::forward<T>(component));
        set_entity_mask_bit(entitys, component_metas_[type_id].bit);
        if (!components_c_[type_id].on_add_) push_comp_signal(0, entitys.parts_.index_, static_cast<uint32_t>(type_id));
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
        if (count > default_component_capacity_) default_component_capacity_ = count;
        for (size_t i = 0; i < components_c_.size(); ++i)
        {
            components_c_[i].increase_capacity(count);
        }
    }
    void disable_comp_signals() noexcept { comp_signal_enabled_ = false; }
    void enable_comp_signals() noexcept { comp_signal_enabled_ = true; }
    void disable_track_changes() noexcept {
        track_changes_enabled_default_ = false;
        for (size_t i = 0; i < components_c_.size(); ++i)
            components_c_[i].track_changes_enabled_ = false;
    }
    void enable_track_changes() noexcept {
        track_changes_enabled_default_ = true;
        for (size_t i = 0; i < components_c_.size(); ++i)
            components_c_[i].track_changes_enabled_ = true;
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
    operating_message& add(entity entitys, T&& component) noexcept
    {
        using DecayedT = std::decay_t<T>;
        int type_id = type_id::get_type_id<DecayedT>();
        register_component_meta<DecayedT>();
        component_message = components_c_[type_id].add(entitys, std::forward<T>(component));
        set_entity_mask_bit(entitys, component_metas_[type_id].bit);
        if (!components_c_[type_id].on_add_) push_comp_signal(0, entitys.parts_.index_, static_cast<uint32_t>(type_id));
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
    operating_message& add(T&& component, EE entitys) noexcept
    {
        add_component_without_message(entitys, std::forward<T>(component));
        return component_message;
    }
    template <IsEntity EE, typename T>
    manager& addc(T&& component, EE entitys) noexcept
    {
        add_component_without_message(entitys, std::forward<T>(component));
        return *this;
    }

    template <typename T>
    manager& addc(entity entitys, T&& component) noexcept
    {
        add_component_without_message(entitys, std::forward<T>(component));
        return *this;
    }
    template <typename T>
    [[nodiscard]] T* get_ptr(entity entitys) noexcept
    {
        if (!entitys.is_valid()) [[unlikely]] return nullptr;
        single_class_set* set = get_single_class_set<T>();
        return set ? set->get_ptr_fast<T>(entitys) : nullptr;
    }

    template <typename T>
    [[nodiscard]] const T* get_ptr(entity entitys) const noexcept
    {
        if (!entitys.is_valid()) [[unlikely]] return nullptr;
        const single_class_set* set = get_single_class_set<T>();
        return set ? set->get_ptr_fast<T>(entitys) : nullptr;
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
    void prefetch_ptr(entity entitys) const noexcept
    {
        const single_class_set* set = get_single_class_set<T>();
        if (set) set->prefetch_ptr(entitys);
    }

    template <typename T>
    void prefetch_ptr_batch(const entity* entities, size_t count) const noexcept
    {
        const single_class_set* set = get_single_class_set<T>();
        if (set) set->prefetch_ptr_batch(entities, count);
    }

    template <typename T>
    void prefetch_ptr_data(entity entitys) const noexcept
    {
        const single_class_set* set = get_single_class_set<T>();
        if (set) set->prefetch_ptr_data<T>(entitys);
    }

    template <typename T>
    void get_ptr_batch(const entity* entities, T** results, size_t count) noexcept
    {
        single_class_set* set = get_single_class_set<T>();
        if (set) set->get_ptr_batch<T>(entities, results, count);
        else
        {
            for (size_t i = 0; i < count; ++i) results[i] = nullptr;
        }
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
            // soft_remove 仅逻辑隐藏,组件未析构,不触发 on_remove_ 也不入队
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
            // 即时回调与延迟队列互斥:注册了 on_remove_ 则同步触发,否则入队
            if (!set->on_remove_) push_comp_signal(1, entitys.parts_.index_, static_cast<uint32_t>(type_id));
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
        if (!entitys.is_valid()) [[unlikely]] return;
        // 先触发实体身上所有组件的 on_remove_ 与 comp_signal(顺序:组件 remove 先于 entity destroyed)
        uint64_t mask = entity_manager_.get_mask(entitys.parts_.index_);
        for (size_t i = 0; i < components_c_.size(); ++i)
        {
            // type_id<=64 用 mask 位图加速跳过未持有的 set;>64 仍需 contains_entity 检查
            if (i >= 1 && i <= 64 && !(mask & (1ULL << (i - 1)))) [[likely]] continue;
            single_class_set& set = components_c_[i];
            if (set.contains_entity(entitys))
            {
                if (!set.on_remove_) push_comp_signal(1, entitys.parts_.index_, static_cast<uint32_t>(i));
                set.hard_remove(entitys);
            }
        }
        entity_manager_.destroy_entity(entitys);
    }

    template <typename T, typename Compare>
    void sort_entities_by_component(Compare&& cmp) noexcept
    {
        single_class_set* set = get_single_class_set<T>();
        if (!set || set->size() <= 1) [[unlikely]] return;
        auto* pool = set->template get_typed_pool_ptr<T>();
        if (!pool) [[unlikely]] return;

        const size_t n = set->size();
        class_pool<size_t> indices;
        indices.increase_capacity(n);
        for (size_t i = 0; i < n; ++i) indices.emplace_back(i);

        T* pool_data = pool->data();
        size_t* idx_data = indices.data();

        if constexpr (is_radix_sortable_v<T> &&
                       std::is_same_v<std::decay_t<Compare>, std::less<T>>)
        {
            class_pool<size_t> temp_buf;
            temp_buf.increase_capacity(n);
            temp_buf.resize(n, size_t{0});
            radix_sort_indices<T>(idx_data, pool_data, n, temp_buf.data());
        }
        else
        {
            std::sort(idx_data, idx_data + n, [pool_data, &cmp](size_t a, size_t b) {
                return cmp(pool_data[a], pool_data[b]);
            });
        }

        set->template reorder_dense_by_indices<T>(indices);
    }

    template <typename T, typename Other, typename Compare>
    void reorder_by_component(Compare&& cmp) noexcept
    {
        single_class_set* set_t = get_single_class_set<T>();
        single_class_set* set_other = get_single_class_set<Other>();
        if (!set_t || !set_other || set_t->size() <= 1) [[unlikely]] return;
        auto* pool_t = set_t->template get_typed_pool_ptr<T>();
        auto* pool_other = set_other->template get_typed_pool_ptr<Other>();
        if (!pool_t || !pool_other) [[unlikely]] return;

        const size_t n = set_t->size();
        class_pool<size_t> indices;
        indices.increase_capacity(n);
        for (size_t i = 0; i < n; ++i)
            indices.emplace_back(i);

        auto& t_indices = set_t->get_entity_indices();
        auto& other_sparse_pool = set_other->get_sparse_combined();
        const size_t other_sparse_size = other_sparse_pool.size();
        auto* other_sparse = other_sparse_pool.data();
        auto* other_pool_data = pool_other->data();
        size_t* idx_data = indices.data();
        Other default_other{};

        std::sort(idx_data, idx_data + n,
            [t_indices_ptr = t_indices.data(), other_sparse, other_sparse_size, other_pool_data, &default_other, &cmp](size_t a, size_t b) {
                uint32_t eid_a = t_indices_ptr[a];
                uint32_t eid_b = t_indices_ptr[b];
                uint32_t od_a = (eid_a < other_sparse_size) ? static_cast<uint32_t>(other_sparse[eid_a] >> 32) : UINT32_MAX;
                uint32_t od_b = (eid_b < other_sparse_size) ? static_cast<uint32_t>(other_sparse[eid_b] >> 32) : UINT32_MAX;
                Other& ra = (od_a != UINT32_MAX) ? other_pool_data[od_a] : default_other;
                Other& rb = (od_b != UINT32_MAX) ? other_pool_data[od_b] : default_other;
                return cmp(ra, rb);
            });

        set_t->template reorder_dense_by_indices<T>(indices);
    }

    template <typename T, typename Compare>
    void sort_component_container(Compare&& cmp) noexcept
    {
        sort_entities_by_component<T>(std::forward<Compare>(cmp));
    }

#include "single_view.inc.hpp"
#include "multi_view.inc.hpp"
#include "composite_views.inc.hpp"

    // ======================== view() 工厂方法 ========================
    template <typename T>
    [[nodiscard]] single_view<T> view() noexcept
    {
        return single_view<T>(get_single_class_set<T>(), this);
    }

    template <typename First, typename Second, typename... Rest>
    [[nodiscard]] multi_view<First, Second, Rest...> view() noexcept
    {
        return multi_view<First, Second, Rest...>(
            std::array<single_class_set*, 2 + sizeof...(Rest)>{
                get_single_class_set<First>(),
                get_single_class_set<Second>(),
                get_single_class_set<Rest>()...
            }, this);
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

    // ======================== view_or / view_any_of / view_filtered 工厂方法 ========================
    template <typename A, typename B>
    [[nodiscard]] or_view<A, B> view_or() noexcept
    {
        return or_view<A, B>(get_single_class_set<A>(), get_single_class_set<B>());
    }

    template <typename... Types>
    [[nodiscard]] auto view_any_of() noexcept
    {
        return any_of_view<Types...>(std::array<single_class_set*, sizeof...(Types)>{
            get_single_class_set<Types>()...
        });
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

    template <typename First, typename... Rest>
    [[nodiscard]] ecs::reorder_group<First, Rest...> group(reorder_t<First>) noexcept
    {
        return ecs::reorder_group<First, Rest...>(this, std::array<single_class_set*, 1 + sizeof...(Rest)>{
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

    // 运行时 term 查询(支持 OR/OPTIONAL/NOT 与读写标注)
    [[nodiscard]] ecs::runtime_view runtime_view_create_from_terms(class_pool<ecs::runtime_term> terms) noexcept
    {
        return ecs::runtime_view(this, ecs::runtime_query(this, std::move(terms)));
    }

    // ======================== command_buffer 工厂方法 ========================
    [[nodiscard]] ecs::command_buffer create_command_buffer() noexcept;

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
    template <typename T>
    void set_on_modify(void (*fn)(entity, void*, void*) noexcept, void* user_data = nullptr) noexcept
    {
        ensure_type_exists(type_id::get_type_id<T>());
        single_class_set* set = get_single_class_set<T>();
        if (set)
        {
            set->on_modify_ = fn;
            set->on_modify_data_ = user_data;
        }
    }

    void enable_entity_signals() noexcept { entity_manager_.enable_entity_signals(); }
    void disable_entity_signals() noexcept { entity_manager_.disable_entity_signals(); }

    [[nodiscard]] uint64_t entity_signal_overflow_count() const noexcept
    {
        return entity_manager_.signal_overflow_count();
    }
    [[nodiscard]] uint64_t comp_signal_overflow_count() const noexcept
    {
        return comp_signal_overflow_count_;
    }
    void reset_entity_signal_overflow_count() noexcept { entity_manager_.reset_signal_overflow_count(); }
    void reset_comp_signal_overflow_count() noexcept { comp_signal_overflow_count_ = 0; }

    void reserve_entity_signal_capacity(size_t n) noexcept { entity_manager_.reserve_signal_capacity(n); }
    void reserve_comp_signal_capacity(size_t n) noexcept { comp_signal_overflow_chain_.increase_capacity(n); }

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
        // 防 flush 递归重入
        if (comp_signal_flushing_) [[unlikely]] return;
        comp_signal_flushing_ = true;
        // 循环上限防止 handler 内追加导致无限循环
        uint64_t budget = comp_signal_buffer_size * 4 + comp_signal_overflow_chain_.size();
        while (budget > 0 && comp_signal_read_ != comp_signal_write_)
        {
            auto& ev = comp_signal_buffer_[comp_signal_read_];
            handler(ev.type, ev.entity_idx, ev.component_id);
            comp_signal_read_ = (comp_signal_read_ + 1) & (comp_signal_buffer_size - 1);
            --budget;
        }
        while (budget > 0 && comp_signal_overflow_read_ < comp_signal_overflow_chain_.size())
        {
            auto& ev = comp_signal_overflow_chain_[comp_signal_overflow_read_];
            handler(ev.type, ev.entity_idx, ev.component_id);
            ++comp_signal_overflow_read_;
            --budget;
        }
        if (comp_signal_overflow_read_ == comp_signal_overflow_chain_.size() && comp_signal_overflow_chain_.size() > 0)
        {
            comp_signal_overflow_chain_.clear();
            comp_signal_overflow_read_ = 0;
        }
        comp_signal_flushing_ = false;
    }
    [[nodiscard]] bool has_pending_component_signals() const noexcept
    {
        return comp_signal_read_ != comp_signal_write_ || comp_signal_overflow_read_ < comp_signal_overflow_chain_.size();
    }

    ~manager() = default;
};

} // namespace ecs

#include "group_impl.hpp"
#include "runtime_view_impl.hpp"
#include "command_buffer.hpp"

namespace ecs
{
// out-of-line:依赖 command_buffer 完整定义
inline command_buffer manager::create_command_buffer() noexcept
{
    return command_buffer(this);
}
} // namespace ecs
