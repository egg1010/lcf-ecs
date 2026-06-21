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
#include "reorder.hpp"
#include "runtime_view.hpp"

template <typename T>
concept IsEntity = std::same_as<T, entity>;

namespace ecs
{

// ======================== radix sort helper ========================
template <typename T>
constexpr bool is_radix_sortable_v =
    std::is_integral_v<T> || std::is_floating_point_v<T>;

template <typename T>
[[nodiscard]] inline auto radix_key(T val) noexcept
{
    if constexpr (std::is_integral_v<T>)
    {
        using U = std::make_unsigned_t<T>;
        U u = static_cast<U>(val);
        if constexpr (std::is_signed_v<T>)
            u ^= (U{1} << (sizeof(U) * 8 - 1));
        return u;
    }
    else if constexpr (std::is_same_v<T, float>)
    {
        uint32_t u;
        std::memcpy(&u, &val, sizeof(u));
        u ^= (u >> 31) ? 0xFFFFFFFF : 0x80000000;
        return u;
    }
    else if constexpr (std::is_same_v<T, double>)
    {
        uint64_t u;
        std::memcpy(&u, &val, sizeof(u));
        u ^= (u >> 63) ? 0xFFFFFFFFFFFFFFFF : 0x8000000000000000;
        return u;
    }
}

template <typename KeyType>
inline void radix_sort_entries(void* entries_data, size_t n) noexcept
    requires is_radix_sortable_v<KeyType>
{
    if (n <= 1) return;
    using U = decltype(radix_key(std::declval<KeyType>()));
    constexpr size_t key_bytes = sizeof(U);
    constexpr size_t radix_bits = 8;
    constexpr size_t bucket_count = 1 << radix_bits;
    constexpr size_t passes = key_bytes; // 8 bits per pass

    struct sort_entry { KeyType key; size_t index; };
    auto* entries = static_cast<sort_entry*>(entries_data);
    auto* temp = static_cast<sort_entry*>(::operator new(n * sizeof(sort_entry), std::align_val_t{alignof(sort_entry)}, std::nothrow));
    if (!temp) [[unlikely]] return;

    for (size_t pass = 0; pass < passes; ++pass)
    {
        size_t count[bucket_count] = {};
        size_t shift = pass * radix_bits;

        for (size_t i = 0; i < n; ++i)
        {
            U k = radix_key(entries[i].key);
            ++count[(k >> shift) & (bucket_count - 1)];
        }

        size_t total = 0;
        for (size_t i = 0; i < bucket_count; ++i)
        {
            size_t c = count[i];
            count[i] = total;
            total += c;
        }

        for (size_t i = 0; i < n; ++i)
        {
            U k = radix_key(entries[i].key);
            size_t bucket = (k >> shift) & (bucket_count - 1);
            new (&temp[count[bucket]]) sort_entry(std::move(entries[i]));
            ++count[bucket];
        }

        std::swap(entries, temp);
    }

    // 如果最终结果在 temp 中，拷贝回 entries_data
    if (passes % 2 == 0)
    {
        for (size_t i = 0; i < n; ++i)
            new (&static_cast<sort_entry*>(entries_data)[i]) sort_entry(std::move(entries[i]));
    }

    ::operator delete(temp, n * sizeof(sort_entry), std::align_val_t{alignof(sort_entry)});
}

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
    static constexpr size_t comp_signal_buffer_size = 1024;
    component_signal_event comp_signal_buffer_[comp_signal_buffer_size]{};
    uint32_t comp_signal_write_{0};
    uint32_t comp_signal_read_{0};
    bool comp_signal_enabled_{true};

    void push_comp_signal(uint32_t type, uint32_t entity_idx, uint32_t component_id) noexcept
    {
        if (!comp_signal_enabled_) [[likely]] return;
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
    void disable_comp_signals() noexcept { comp_signal_enabled_ = false; }
    void enable_comp_signals() noexcept { comp_signal_enabled_ = true; }
    void disable_track_changes() noexcept {
        for (size_t i = 0; i < components_c_.size(); ++i)
            components_c_[i].track_changes_enabled_ = false;
    }
    void enable_track_changes() noexcept {
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

    template <typename T, typename Compare>
    void sort_entities_by_component(Compare&& cmp) noexcept
    {
        single_class_set* set = get_single_class_set<T>();
        if (!set || set->size() <= 1) [[unlikely]] return;
        auto* pool = set->template get_typed_pool_ptr<T>();
        if (!pool) [[unlikely]] return;

        class_pool<size_t> indices;
        indices.increase_capacity(set->size());
        for (size_t i = 0; i < set->size(); ++i) indices.emplace_back(i);

        T* pool_data = pool->data();
        size_t* idx_data = indices.data();
        std::sort(idx_data, idx_data + indices.size(), [pool_data, &cmp](size_t a, size_t b) {
            return cmp(pool_data[a], pool_data[b]);
        });

        for (size_t i = 0; i < indices.size(); ++i)
        {
            if (idx_data[i] == i) continue;
            size_t curr = i;
            size_t next = idx_data[curr];
            while (next != i)
            {
                set->swap_dense_and_pool(curr, next);
                idx_data[curr] = curr;
                curr = next;
                next = idx_data[curr];
            }
            idx_data[curr] = curr;
        }
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

        class_pool<size_t> indices;
        class_pool<Other> other_values;
        const size_t n = set_t->size();
        indices.increase_capacity(n);
        other_values.increase_capacity(n);
        auto& t_indices = set_t->get_entity_indices();
        auto* other_sparse = set_other->get_sparse_combined().data();
        auto* other_pool_data = pool_other->data();
        for (size_t i = 0; i < n; ++i)
        {
            indices.emplace_back(i);
            uint32_t eid = t_indices[i];
            uint32_t other_dense = static_cast<uint32_t>(other_sparse[eid] >> 32);
            other_values.emplace_back(other_dense != UINT32_MAX ? other_pool_data[other_dense] : Other{});
        }

        size_t* idx_data = indices.data();
        Other* other_data = other_values.data();
        std::sort(idx_data, idx_data + indices.size(), [other_data, &cmp](size_t a, size_t b) {
            return cmp(other_data[a], other_data[b]);
        });

        for (size_t i = 0; i < indices.size(); ++i)
        {
            if (idx_data[i] == i) continue;
            size_t curr = i;
            size_t next = idx_data[curr];
            while (next != i)
            {
                set_t->swap_dense_and_pool(curr, next);
                idx_data[curr] = curr;
                curr = next;
                next = idx_data[curr];
            }
            idx_data[curr] = curr;
        }
    }

    template <typename T, typename Compare>
    void sort_component_container(Compare&& cmp) noexcept
    {
        single_class_set* set = get_single_class_set<T>();
        if (!set || set->size() <= 1) [[unlikely]] return;
        auto* pool = set->template get_typed_pool_ptr<T>();
        if (!pool) [[unlikely]] return;
        std::sort(pool->data(), pool->data() + pool->size(), std::forward<Compare>(cmp));
        ++set->version_;
    }

    // ======================== single_view ========================
    template <typename T>
    class single_view
    {
    private:
        single_class_set* set_;
        manager* mgr_;

        void resolve_set() noexcept
        {
            if (mgr_) set_ = mgr_->template get_single_class_set<T>();
        }

    public:
        single_view(single_class_set* set, manager* mgr = nullptr) noexcept : set_(set), mgr_(mgr) {}

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

        [[nodiscard]] T* get_component_for_entity(entity e) noexcept
        {
            return set_ ? set_->template get_ptr_fast<T>(e) : nullptr;
        }

        [[nodiscard]] entity get_first_entity() const noexcept
        {
            if (!set_ || set_->size() == 0) [[unlikely]] return entity{};
            auto& indices = set_->get_entity_indices();
            return entity(indices[0], set_->get_version_unchecked(indices[0]));
        }

        [[nodiscard]] entity get_last_entity() const noexcept
        {
            if (!set_ || set_->size() == 0) [[unlikely]] return entity{};
            auto& indices = set_->get_entity_indices();
            size_t last = indices.size() - 1;
            return entity(indices[last], set_->get_version_unchecked(indices[last]));
        }

        [[nodiscard]] entity get_entity_at_index(size_t index) const noexcept
        {
            if (!set_ || index >= set_->size()) [[unlikely]] return entity{};
            auto& indices = set_->get_entity_indices();
            return entity(indices[index], set_->get_version_unchecked(indices[index]));
        }

        [[nodiscard]] T* get_component_at_index(size_t index) noexcept
        {
            if (!set_ || index >= set_->size()) [[unlikely]] return nullptr;
            auto* pool = set_->template get_typed_pool_ptr<T>();
            return pool ? &(*pool)[index] : nullptr;
        }

        template <typename Func>
        void for_each(Func&& func) noexcept
        {
            resolve_set();
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

        // ======================== single_view::paged_view ========================
        class paged_view
        {
        private:
            single_view* base_;
            size_t offset_;
            size_t limit_;

        public:
            paged_view(single_view* base, size_t offset, size_t limit) noexcept
                : base_(base), offset_(offset), limit_(limit) {}

            [[nodiscard]] size_t size() const noexcept
            {
                size_t base_sz = base_->size();
                if (offset_ >= base_sz) return 0;
                size_t rem = base_sz - offset_;
                return rem < limit_ ? rem : limit_;
            }

            [[nodiscard]] bool empty() const noexcept { return size() == 0; }

            template <typename Func>
            void for_each(Func&& func) noexcept
            {
                size_t skipped = 0;
                size_t processed = 0;
                base_->for_each([&](auto&... args) {
                    if (skipped < offset_)
                    {
                        ++skipped;
                        return;
                    }
                    if (processed >= limit_) return;
                    ++processed;
                    func(args...);
                });
            }
        };

        auto page(size_t offset, size_t limit) noexcept
        {
            return paged_view(this, offset, limit);
        }

        // ======================== single_view::sorted_component_view ========================
        template <typename Compare>
        class sorted_component_view
        {
        private:
            single_view* base_;
            Compare cmp_;
            class_pool<size_t> sorted_indices_;
            uint64_t last_version_{0};
            bool needs_rebuild_{true};

            void rebuild() noexcept
            {
                sorted_indices_.clear();
                if (!base_->set_) [[unlikely]] return;
                auto* pool = base_->set_->template get_typed_pool_ptr<T>();
                if (!pool) [[unlikely]] return;

                const size_t n = pool->size();
                T* pool_data = pool->data();

                struct sort_entry { T key; size_t index; };
                class_pool<sort_entry> entries;
                entries.resize(n, {});

                for (size_t i = 0; i < n; ++i)
                {
                    entries[i].key = pool_data[i];
                    entries[i].index = i;
                }

                std::sort(entries.data(), entries.data() + n, [this](const sort_entry& a, const sort_entry& b) {
                    return cmp_(a.key, b.key);
                });

                sorted_indices_.resize(n, size_t{0});
                for (size_t i = 0; i < n; ++i)
                    sorted_indices_[i] = entries[i].index;

                last_version_ = base_->set_->get_pool_version();
                needs_rebuild_ = false;
            }

            void ensure_fresh() noexcept
            {
                if (base_->set_ && base_->set_->get_pool_version() != last_version_)
                    rebuild();
            }

        public:
            sorted_component_view(single_view* base, Compare cmp) noexcept
                : base_(base), cmp_(std::move(cmp))
            {
                rebuild();
            }

            [[nodiscard]] size_t size() const noexcept { return sorted_indices_.size(); }
            [[nodiscard]] bool empty() const noexcept { return sorted_indices_.empty(); }

            template <typename Func>
            void for_each(Func&& func) noexcept
            {
                ensure_fresh();
                if (sorted_indices_.empty()) return;

                auto* pool = base_->set_->template get_typed_pool_ptr<T>();
                if (!pool) [[unlikely]] return;
                auto& indices = base_->set_->get_entity_indices();
                auto* sparse_combined = base_->set_->get_sparse_combined().data();

                if constexpr (std::is_invocable_v<Func, entity, T&>)
                {
                    for (size_t i = 0; i < sorted_indices_.size(); ++i)
                    {
                        if (i + 32 < sorted_indices_.size()) [[likely]]
                        {
                            size_t next_idx = sorted_indices_[i + 32];
                            PREFETCH_R(&(*pool)[next_idx]);
                            PREFETCH_R(&sparse_combined[indices[next_idx]]);
                        }
                        size_t idx = sorted_indices_[i];
                        entity e(indices[idx], static_cast<uint32_t>(sparse_combined[indices[idx]]));
                        func(e, (*pool)[idx]);
                    }
                }
                else
                {
                    for (size_t i = 0; i < sorted_indices_.size(); ++i)
                    {
                        if (i + 32 < sorted_indices_.size()) [[likely]]
                            PREFETCH_R(&(*pool)[sorted_indices_[i + 32]]);
                        func((*pool)[sorted_indices_[i]]);
                    }
                }
            }
        };

        template <typename Compare>
        auto sorted_by_component(Compare&& cmp) noexcept
        {
            return sorted_component_view<Compare>(this, std::forward<Compare>(cmp));
        }

        // ======================== single_view::grouped_component_view ========================
        template <typename KeyType, typename KeyFunc>
        class grouped_component_view
        {
        private:
            single_view* base_;
            KeyFunc key_func_;
            class_pool<size_t> sorted_indices_;
            class_pool<KeyType> group_keys_;
            class_pool<size_t> group_starts_;
            uint64_t last_version_{0};
            bool needs_rebuild_{true};

            void rebuild() noexcept
            {
                sorted_indices_.clear();
                group_keys_.clear();
                group_starts_.clear();
                if (!base_->set_) [[unlikely]] return;
                auto* pool = base_->set_->template get_typed_pool_ptr<T>();
                if (!pool) [[unlikely]] return;

                const size_t n = pool->size();
                T* pool_data = pool->data();

                struct sort_entry { KeyType key; size_t index; };
                class_pool<sort_entry> entries;
                entries.resize(n, {});

                for (size_t i = 0; i < n; ++i)
                {
                    entries[i].key = key_func_(pool_data[i]);
                    entries[i].index = i;
                }

                if constexpr (is_radix_sortable_v<KeyType>)
                {
                    radix_sort_entries<KeyType>(entries.data(), n);
                }
                else
                {
                    std::sort(entries.data(), entries.data() + n, [](const sort_entry& a, const sort_entry& b) {
                        return a.key < b.key;
                    });
                }

                sorted_indices_.resize(n, size_t{0});
                group_keys_.resize(n, KeyType{});
                for (size_t i = 0; i < n; ++i)
                {
                    sorted_indices_[i] = entries[i].index;
                    group_keys_[i] = entries[i].key;
                }

                group_starts_.emplace_back(0);
                for (size_t i = 1; i < n; ++i)
                {
                    if (group_keys_[i] != group_keys_[i - 1])
                        group_starts_.emplace_back(i);
                }

                last_version_ = base_->set_->get_pool_version();
                needs_rebuild_ = false;
            }

            void ensure_fresh() noexcept
            {
                if (base_->set_ && base_->set_->get_pool_version() != last_version_)
                    rebuild();
            }

        public:
            grouped_component_view(single_view* base, KeyFunc key_func) noexcept
                : base_(base), key_func_(std::move(key_func))
            {
                rebuild();
            }

            [[nodiscard]] size_t size() const noexcept { return sorted_indices_.size(); }
            [[nodiscard]] bool empty() const noexcept { return sorted_indices_.empty(); }
            [[nodiscard]] size_t group_count() const noexcept { return group_starts_.size(); }

            template <typename Func>
            void for_each(Func&& func) noexcept
            {
                ensure_fresh();
                if (sorted_indices_.empty()) return;

                auto* pool = base_->set_->template get_typed_pool_ptr<T>();
                if (!pool) [[unlikely]] return;
                auto& indices = base_->set_->get_entity_indices();
                auto* sparse_combined = base_->set_->get_sparse_combined().data();

                if constexpr (std::is_invocable_v<Func, entity, T&>)
                {
                    for (size_t i = 0; i < sorted_indices_.size(); ++i)
                    {
                        if (i + 32 < sorted_indices_.size()) [[likely]]
                        {
                            size_t next_idx = sorted_indices_[i + 32];
                            PREFETCH_R(&(*pool)[next_idx]);
                            PREFETCH_R(&sparse_combined[indices[next_idx]]);
                        }
                        size_t idx = sorted_indices_[i];
                        entity e(indices[idx], static_cast<uint32_t>(sparse_combined[indices[idx]]));
                        func(e, (*pool)[idx]);
                    }
                }
                else
                {
                    for (size_t i = 0; i < sorted_indices_.size(); ++i)
                    {
                        if (i + 32 < sorted_indices_.size()) [[likely]]
                            PREFETCH_R(&(*pool)[sorted_indices_[i + 32]]);
                        func((*pool)[sorted_indices_[i]]);
                    }
                }
            }

            template <typename Func>
            void for_each_group(Func&& func) noexcept
            {
                ensure_fresh();
                if (sorted_indices_.empty()) return;

                for (size_t g = 0; g < group_starts_.size(); ++g)
                {
                    size_t start = group_starts_[g];
                    size_t end = (g + 1 < group_starts_.size()) ? group_starts_[g + 1] : sorted_indices_.size();
                    KeyType key = group_keys_[start];
                    func(key, start, end);
                }
            }
        };

        template <typename KeyFunc>
        auto sorted_by_component_value(KeyFunc&& key_func) noexcept
        {
            using KeyType = std::invoke_result_t<KeyFunc, T&>;
            return grouped_component_view<KeyType, KeyFunc>(this, std::forward<KeyFunc>(key_func));
        }

        // ======================== single_view::changed_view ========================
        class changed_view
        {
        private:
            single_view* base_;
            uint64_t last_version_{0};
            class_pool<size_t> changed_indices_;
            bool needs_rebuild_{true};

            void rebuild() noexcept
            {
                changed_indices_.clear();
                base_->resolve_set();
                if (!base_->set_) [[unlikely]]
                {
                    needs_rebuild_ = false;
                    return;
                }

                uint64_t cur = base_->set_->get_pool_version();
                if (cur == last_version_)
                {
                    needs_rebuild_ = false;
                    return;
                }
                last_version_ = cur;

                auto* pool = base_->set_->template get_typed_pool_ptr<T>();
                if (!pool) [[unlikely]] return;
                const size_t n = pool->size();
                for (size_t i = 0; i < n; ++i)
                {
                    changed_indices_.emplace_back(i);
                }

                needs_rebuild_ = false;
            }

            void ensure_fresh() noexcept
            {
                if (!needs_rebuild_) return;
                rebuild();
            }

        public:
            changed_view(single_view* base) noexcept : base_(base)
            {
                rebuild();
            }

            [[nodiscard]] size_t size() const noexcept { return changed_indices_.size(); }
            [[nodiscard]] bool empty() const noexcept { return changed_indices_.empty(); }

            void reset_tracking() noexcept
            {
                last_version_ = 0;
                changed_indices_.clear();
                needs_rebuild_ = true;
            }

            template <typename Func>
            void for_each(Func&& func) noexcept
            {
                ensure_fresh();
                needs_rebuild_ = true;
                if (changed_indices_.empty()) return;

                auto* pool = base_->set_->template get_typed_pool_ptr<T>();
                if (!pool) [[unlikely]] return;
                auto& indices = base_->set_->get_entity_indices();

                if constexpr (std::is_invocable_v<Func, entity, T&>)
                {
                    for (size_t i = 0; i < changed_indices_.size(); ++i)
                    {
                        size_t idx = changed_indices_[i];
                        entity e(indices[idx], base_->set_->get_version_unchecked(indices[idx]));
                        func(e, (*pool)[idx]);
                    }
                }
                else
                {
                    for (size_t i = 0; i < changed_indices_.size(); ++i)
                    {
                        func((*pool)[changed_indices_[i]]);
                    }
                }
            }
        };

        auto track_changes() noexcept
        {
            return changed_view(this);
        }

        // ======================== single_view::filter_changed ========================
        class filter_changed_view
        {
        private:
            single_view base_;
            class_pool<uint64_t> last_observed_versions_;
            class_pool<size_t> changed_indices_;
            bool needs_rebuild_{true};
            uint64_t last_pool_version_{0};

            void rebuild() noexcept
            {
                changed_indices_.clear();
                base_.resolve_set();
                if (!base_.set_) [[unlikely]] { needs_rebuild_ = false; return; }
                auto* pool = base_.set_->template get_typed_pool_ptr<T>();
                if (!pool) [[unlikely]] { needs_rebuild_ = false; return; }

                const size_t n = pool->size();
                // 仅扩展，不覆盖已追踪的版本号
                while (last_observed_versions_.size() < n)
                    last_observed_versions_.emplace_back(0);

                for (size_t i = 0; i < n; ++i)
                {
                    uint64_t cur = base_.set_->get_entity_change_version(i);
                    if (i >= last_observed_versions_.size() || cur != last_observed_versions_[i])
                    {
                        if (i < last_observed_versions_.size())
                            last_observed_versions_[i] = cur;
                        else
                            last_observed_versions_.emplace_at(i, cur);
                        changed_indices_.emplace_back(i);
                    }
                }
                last_pool_version_ = base_.set_->get_pool_version();
                needs_rebuild_ = false;
            }

        public:
            filter_changed_view(single_view base) noexcept : base_(base) { rebuild(); }

            [[nodiscard]] size_t size() const noexcept { return changed_indices_.size(); }
            [[nodiscard]] bool empty() const noexcept { return changed_indices_.empty(); }

            void reset_tracking() noexcept
            {
                last_observed_versions_.clear();
                changed_indices_.clear();
                needs_rebuild_ = true;
            }

            template <typename Func>
            void for_each(Func&& func) noexcept
            {
                base_.resolve_set();
                if (base_.set_ && base_.set_->get_pool_version() != last_pool_version_)
                    needs_rebuild_ = true;
                if (needs_rebuild_) rebuild();
                needs_rebuild_ = true;
                if (changed_indices_.empty()) return;

                auto* pool = base_.set_->template get_typed_pool_ptr<T>();
                if (!pool) [[unlikely]] return;
                auto& indices = base_.set_->get_entity_indices();

                if constexpr (std::is_invocable_v<Func, entity, T&>)
                {
                    for (size_t i = 0; i < changed_indices_.size(); ++i)
                    {
                        size_t idx = changed_indices_[i];
                        entity e(indices[idx], base_.set_->get_version_unchecked(indices[idx]));
                        func(e, (*pool)[idx]);
                    }
                }
                else
                {
                    for (size_t i = 0; i < changed_indices_.size(); ++i)
                    {
                        func((*pool)[changed_indices_[i]]);
                    }
                }
            }
        };

        auto filter_changed() noexcept
        {
            return filter_changed_view(*this);
        }

        // ======================== single_view::filter_added ========================
        class filter_added_view
        {
        private:
            single_view base_;
            class_pool<uint64_t> last_observed_added_;
            class_pool<size_t> added_indices_;
            bool needs_rebuild_{true};
            uint64_t last_pool_version_{0};
            uint64_t baseline_added_counter_{0};

            void rebuild() noexcept
            {
                added_indices_.clear();
                base_.resolve_set();
                if (!base_.set_) [[unlikely]] { needs_rebuild_ = false; return; }
                auto* pool = base_.set_->template get_typed_pool_ptr<T>();
                if (!pool) [[unlikely]] { needs_rebuild_ = false; return; }

                const size_t n = pool->size();
                while (last_observed_added_.size() < n)
                    last_observed_added_.emplace_back(0);

                for (size_t i = 0; i < n; ++i)
                {
                    uint64_t cur = base_.set_->get_entity_added_version(i);
                    if (cur > baseline_added_counter_ && (i >= last_observed_added_.size() || cur != last_observed_added_[i]))
                    {
                        if (i < last_observed_added_.size())
                            last_observed_added_[i] = cur;
                        else
                            last_observed_added_.emplace_at(i, cur);
                        added_indices_.emplace_back(i);
                    }
                }
                last_pool_version_ = base_.set_->get_pool_version();
                needs_rebuild_ = false;
            }

        public:
            filter_added_view(single_view base) noexcept : base_(base)
            {
                base_.resolve_set();
                if (base_.set_)
                    baseline_added_counter_ = base_.set_->get_global_added_counter();
                rebuild();
            }

            [[nodiscard]] size_t size() const noexcept { return added_indices_.size(); }
            [[nodiscard]] bool empty() const noexcept { return added_indices_.empty(); }

            void reset_tracking() noexcept
            {
                last_observed_added_.clear();
                added_indices_.clear();
                needs_rebuild_ = true;
                baseline_added_counter_ = 0;
            }

            template <typename Func>
            void for_each(Func&& func) noexcept
            {
                base_.resolve_set();
                if (base_.set_ && base_.set_->get_pool_version() != last_pool_version_)
                    needs_rebuild_ = true;
                if (needs_rebuild_) rebuild();
                needs_rebuild_ = true;
                if (added_indices_.empty()) return;

                auto* pool = base_.set_->template get_typed_pool_ptr<T>();
                if (!pool) [[unlikely]] return;
                auto& indices = base_.set_->get_entity_indices();

                if constexpr (std::is_invocable_v<Func, entity, T&>)
                {
                    for (size_t i = 0; i < added_indices_.size(); ++i)
                    {
                        size_t idx = added_indices_[i];
                        entity e(indices[idx], base_.set_->get_version_unchecked(indices[idx]));
                        func(e, (*pool)[idx]);
                    }
                }
                else
                {
                    for (size_t i = 0; i < added_indices_.size(); ++i)
                    {
                        func((*pool)[added_indices_[i]]);
                    }
                }
            }
        };

        auto filter_added() noexcept
        {
            return filter_added_view(*this);
        }

        // ======================== single_view::exactly_one ========================
        [[nodiscard]] T& exactly_one() noexcept
        {
            resolve_set();
            assert(set_ && set_->size() == 1 && "exactly_one(): expected exactly 1 entity");
            auto* pool = set_->template get_typed_pool_ptr<T>();
            return (*pool)[0];
        }

        [[nodiscard]] const T& exactly_one() const noexcept
        {
            assert(set_ && set_->size() == 1 && "exactly_one(): expected exactly 1 entity");
            auto* pool = set_->template get_typed_pool_ptr<T>();
            return (*pool)[0];
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
        manager* mgr_{nullptr};
        class_pool<std::array<uint32_t, N>> dense_mappings_soa_;
        class_pool<uint32_t> cached_entity_versions_;
        uint64_t cached_versions_[N]{};
        bool mappings_valid_{false};

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

        void rebuild_mappings() noexcept
        {
            auto* primary = sets_[primary_idx_];
            auto& indices = primary->get_entity_indices();
            const size_t n = indices.size();

            dense_mappings_soa_.clear();
            dense_mappings_soa_.resize(n, {});
            cached_entity_versions_.clear();
            cached_entity_versions_.resize(n, 0);

            auto* sparse_combined = primary->get_sparse_combined().data();
            for (size_t i = 0; i < n; ++i)
            {
                uint32_t idx = indices[i];
                auto& m = dense_mappings_soa_[i];
                m[primary_idx_] = static_cast<uint32_t>(i);
                cached_entity_versions_[i] = static_cast<uint32_t>(sparse_combined[idx]);
                for (size_t k = 0; k < N; ++k)
                {
                    if (k == primary_idx_) continue;
                    if (sets_[k]->get_version(idx) == primary->get_version_unchecked(idx))
                        m[k] = sets_[k]->get_dense_at(idx);
                    else
                        m[k] = UINT32_MAX;
                }
            }

            for (size_t k = 0; k < N; ++k)
                cached_versions_[k] = sets_[k]->get_pool_version();
            mappings_valid_ = true;
        }

        void ensure_mappings() noexcept
        {
            bool need_rebuild = !mappings_valid_;
            if (!need_rebuild)
            {
                for (size_t k = 0; k < N; ++k)
                {
                    if (cached_versions_[k] != sets_[k]->get_pool_version())
                    {
                        need_rebuild = true;
                        break;
                    }
                }
            }
            if (need_rebuild)
                rebuild_mappings();
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
        [[nodiscard]] std::tuple_element_t<I, AllTypes>* get_component_mapped(size_t primary_i) const noexcept
        {
            using T = std::tuple_element_t<I, AllTypes>;
            uint32_t dense_idx = dense_mappings_soa_[primary_i][I];
            if (dense_idx == UINT32_MAX) [[unlikely]] return nullptr;
            return sets_[I]->template get_ptr_unchecked_by_index<T>(dense_idx);
        }

        template <size_t I>
        [[nodiscard]] auto* get_component(entity e, size_t primary_i) const noexcept
        {
            using T = std::tuple_element_t<I, AllTypes>;
            return I == primary_idx_
                ? sets_[I]->template get_ptr_unchecked_by_index<T>(primary_i)
                : sets_[I]->template get_ptr_fast<T>(e);
        }

        template <typename T, typename Tuple>
        [[nodiscard]] static constexpr size_t find_type_index() noexcept
        {
            return find_type_index_impl<T, Tuple, 0>();
        }

        template <typename T, typename Tuple, size_t I>
        [[nodiscard]] static constexpr size_t find_type_index_impl() noexcept
        {
            if constexpr (std::is_same_v<T, std::tuple_element_t<I, Tuple>>) return I;
            else return find_type_index_impl<T, Tuple, I + 1>();
        }

        template <typename Func>
        void for_each_impl_2(Func&& func) noexcept
        {
            if (!all_sets_valid()) [[unlikely]] return;

            ensure_mappings();

            auto* primary = sets_[primary_idx_];
            auto& indices = primary->get_entity_indices();
            const size_t n = indices.size();

            auto* pool0 = sets_[0]->template get_typed_pool_ptr<std::tuple_element_t<0, AllTypes>>();
            auto* pool1 = sets_[1]->template get_typed_pool_ptr<std::tuple_element_t<1, AllTypes>>();
            auto* raw = reinterpret_cast<uint32_t*>(dense_mappings_soa_.data());
            auto* raw_end = raw + n * 2;

            if constexpr (std::is_invocable_v<Func, entity, First&, Rest&...>)
            {
                auto* versions = cached_entity_versions_.data();
                for (const uint32_t* p = raw; p < raw_end; p += 2)
                {
                    if (p + 32 < raw_end) [[likely]]
                    {
                        PREFETCH_R(&(*pool0)[p[32]]);
                        PREFETCH_R(&(*pool1)[p[33]]);
                    }
                    if (p[0] == UINT32_MAX || p[1] == UINT32_MAX) [[unlikely]] continue;
                    size_t i = static_cast<size_t>(p - raw) >> 1;
                    PREFETCH_R(&versions[i + 32]);
                    entity e(indices[i], versions[i]);
                    func(e, (*pool0)[p[0]], (*pool1)[p[1]]);
                }
            }
            else
            {
                for (const uint32_t* p = raw; p < raw_end; p += 2)
                {
                    if (p + 32 < raw_end) [[likely]]
                    {
                        PREFETCH_R(&(*pool0)[p[32]]);
                        PREFETCH_R(&(*pool1)[p[33]]);
                    }
                    if (p[0] == UINT32_MAX || p[1] == UINT32_MAX) [[unlikely]] continue;
                    func((*pool0)[p[0]], (*pool1)[p[1]]);
                }
            }
        }

        template <typename Func>
        void for_each_impl_3(Func&& func) noexcept
        {
            if (!all_sets_valid()) [[unlikely]] return;

            ensure_mappings();

            auto* primary = sets_[primary_idx_];
            auto& indices = primary->get_entity_indices();
            const size_t n = indices.size();

            auto* pool0 = sets_[0]->template get_typed_pool_ptr<std::tuple_element_t<0, AllTypes>>();
            auto* pool1 = sets_[1]->template get_typed_pool_ptr<std::tuple_element_t<1, AllTypes>>();
            auto* pool2 = sets_[2]->template get_typed_pool_ptr<std::tuple_element_t<2, AllTypes>>();
            auto* raw = reinterpret_cast<uint32_t*>(dense_mappings_soa_.data());
            auto* raw_end = raw + n * 3;

            if constexpr (std::is_invocable_v<Func, entity, First&, Rest&...>)
            {
                auto* versions = cached_entity_versions_.data();
                for (const uint32_t* p = raw; p < raw_end; p += 3)
                {
                    if (p + 48 < raw_end) [[likely]]
                    {
                        PREFETCH_R(&(*pool0)[p[48]]);
                        PREFETCH_R(&(*pool1)[p[49]]);
                        PREFETCH_R(&(*pool2)[p[50]]);
                    }
                    if (p[0] == UINT32_MAX || p[1] == UINT32_MAX || p[2] == UINT32_MAX) [[unlikely]] continue;
                    size_t i = static_cast<size_t>(p - raw) / 3;
                    PREFETCH_R(&versions[i + 32]);
                    entity e(indices[i], versions[i]);
                    func(e, (*pool0)[p[0]], (*pool1)[p[1]], (*pool2)[p[2]]);
                }
            }
            else
            {
                for (const uint32_t* p = raw; p < raw_end; p += 3)
                {
                    if (p + 48 < raw_end) [[likely]]
                    {
                        PREFETCH_R(&(*pool0)[p[48]]);
                        PREFETCH_R(&(*pool1)[p[49]]);
                        PREFETCH_R(&(*pool2)[p[50]]);
                    }
                    if (p[0] == UINT32_MAX || p[1] == UINT32_MAX || p[2] == UINT32_MAX) [[unlikely]] continue;
                    func((*pool0)[p[0]], (*pool1)[p[1]], (*pool2)[p[2]]);
                }
            }
        }

        template <typename Func>
        void for_each_impl_4(Func&& func) noexcept
        {
            if (!all_sets_valid()) [[unlikely]] return;

            ensure_mappings();

            auto* primary = sets_[primary_idx_];
            auto& indices = primary->get_entity_indices();
            const size_t n = indices.size();

            auto* pool0 = sets_[0]->template get_typed_pool_ptr<std::tuple_element_t<0, AllTypes>>();
            auto* pool1 = sets_[1]->template get_typed_pool_ptr<std::tuple_element_t<1, AllTypes>>();
            auto* pool2 = sets_[2]->template get_typed_pool_ptr<std::tuple_element_t<2, AllTypes>>();
            auto* pool3 = sets_[3]->template get_typed_pool_ptr<std::tuple_element_t<3, AllTypes>>();
            auto* raw = reinterpret_cast<uint32_t*>(dense_mappings_soa_.data());
            auto* raw_end = raw + n * 4;

            if constexpr (std::is_invocable_v<Func, entity, First&, Rest&...>)
            {
                auto* versions = cached_entity_versions_.data();
                for (const uint32_t* p = raw; p < raw_end; p += 4)
                {
                    if (p + 64 < raw_end) [[likely]]
                    {
                        PREFETCH_R(&(*pool0)[p[64]]);
                        PREFETCH_R(&(*pool1)[p[65]]);
                        PREFETCH_R(&(*pool2)[p[66]]);
                        PREFETCH_R(&(*pool3)[p[67]]);
                    }
                    if (p[0] == UINT32_MAX || p[1] == UINT32_MAX || p[2] == UINT32_MAX || p[3] == UINT32_MAX) [[unlikely]] continue;
                    size_t i = static_cast<size_t>(p - raw) >> 2;
                    PREFETCH_R(&versions[i + 32]);
                    entity e(indices[i], versions[i]);
                    func(e, (*pool0)[p[0]], (*pool1)[p[1]], (*pool2)[p[2]], (*pool3)[p[3]]);
                }
            }
            else
            {
                for (const uint32_t* p = raw; p < raw_end; p += 4)
                {
                    if (p + 64 < raw_end) [[likely]]
                    {
                        PREFETCH_R(&(*pool0)[p[64]]);
                        PREFETCH_R(&(*pool1)[p[65]]);
                        PREFETCH_R(&(*pool2)[p[66]]);
                        PREFETCH_R(&(*pool3)[p[67]]);
                    }
                    if (p[0] == UINT32_MAX || p[1] == UINT32_MAX || p[2] == UINT32_MAX || p[3] == UINT32_MAX) [[unlikely]] continue;
                    func((*pool0)[p[0]], (*pool1)[p[1]], (*pool2)[p[2]], (*pool3)[p[3]]);
                }
            }
        }

        template <typename Func, size_t... Is>
        void for_each_impl(Func&& func, std::index_sequence<Is...>) noexcept
        {
            if (!all_sets_valid()) [[unlikely]] return;

            ensure_mappings();

            auto* primary = sets_[primary_idx_];
            auto& indices = primary->get_entity_indices();
            const size_t n = indices.size();

            auto pools = std::make_tuple(
                sets_[Is]->template get_typed_pool_ptr<std::tuple_element_t<Is, AllTypes>>()...
            );
            auto* raw = reinterpret_cast<uint32_t*>(dense_mappings_soa_.data());

            constexpr size_t stride = N;
            constexpr size_t chunk = 16;
            uint32_t valid[chunk];

            if constexpr (std::is_invocable_v<Func, entity, First&, Rest&...>)
            {
                auto* versions = cached_entity_versions_.data();
                for (size_t base = 0; base < n; base += chunk)
                {
                    size_t end = (base + chunk < n) ? base + chunk : n;
                    size_t valid_count = 0;

                    for (size_t i = base; i < end; ++i)
                    {
                        const uint32_t* m = raw + i * stride;
                        bool all_valid = ((m[Is] != UINT32_MAX) && ...);
                        if (!all_valid) continue;
                        (PREFETCH_R(&(*std::get<Is>(pools))[m[Is]]), ...);
                        PREFETCH_R(&versions[i]);
                        valid[valid_count++] = static_cast<uint32_t>(i);
                    }

                    for (size_t v = 0; v < valid_count; ++v)
                    {
                        size_t i = valid[v];
                        const uint32_t* m = raw + i * stride;
                        entity e(indices[i], versions[i]);
                        func(e, (*std::get<Is>(pools))[m[Is]]...);
                    }
                }
            }
            else
            {
                for (size_t base = 0; base < n; base += chunk)
                {
                    size_t end = (base + chunk < n) ? base + chunk : n;
                    size_t valid_count = 0;

                    for (size_t i = base; i < end; ++i)
                    {
                        const uint32_t* m = raw + i * stride;
                        bool all_valid = ((m[Is] != UINT32_MAX) && ...);
                        if (!all_valid) continue;
                        (PREFETCH_R(&(*std::get<Is>(pools))[m[Is]]), ...);
                        valid[valid_count++] = static_cast<uint32_t>(i);
                    }

                    for (size_t v = 0; v < valid_count; ++v)
                    {
                        size_t i = valid[v];
                        const uint32_t* m = raw + i * stride;
                        func((*std::get<Is>(pools))[m[Is]]...);
                    }
                }
            }
        }

        template <size_t... Is>
        [[nodiscard]] bool contains_impl(entity e, std::index_sequence<Is...>) const noexcept
        {
            return (... && (sets_[Is] && sets_[Is]->template get_ptr_fast<std::tuple_element_t<Is, AllTypes>>(e) != nullptr));
        }

    public:
        multi_view(std::array<single_class_set*, N> sets, manager* mgr = nullptr) noexcept
            : sets_(sets), mgr_(mgr)
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

        template <typename T>
        [[nodiscard]] T* get_component_for_entity(entity e) noexcept
        {
            constexpr size_t idx = find_type_index<T, AllTypes>();
            return sets_[idx] ? sets_[idx]->template get_ptr_fast<T>(e) : nullptr;
        }

        [[nodiscard]] entity get_first_entity() const noexcept
        {
            if (!all_sets_valid()) [[unlikely]] return entity{};
            auto* primary = sets_[primary_idx_];
            auto& indices = primary->get_entity_indices();
            for (size_t i = 0; i < indices.size(); ++i)
            {
                entity e(indices[i], primary->get_version_unchecked(indices[i]));
                if (contains_impl(e, std::index_sequence_for<First, Rest...>{})) return e;
            }
            return entity{};
        }

        [[nodiscard]] entity get_last_entity() const noexcept
        {
            if (!all_sets_valid()) [[unlikely]] return entity{};
            auto* primary = sets_[primary_idx_];
            auto& indices = primary->get_entity_indices();
            for (size_t i = indices.size(); i > 0; --i)
            {
                size_t idx = i - 1;
                entity e(indices[idx], primary->get_version_unchecked(indices[idx]));
                if (contains_impl(e, std::index_sequence_for<First, Rest...>{})) return e;
            }
            return entity{};
        }

        [[nodiscard]] entity get_entity_at_index(size_t index) const noexcept
        {
            if (!all_sets_valid()) [[unlikely]] return entity{};
            auto* primary = sets_[primary_idx_];
            if (index >= primary->size()) [[unlikely]] return entity{};
            auto& indices = primary->get_entity_indices();
            return entity(indices[index], primary->get_version_unchecked(indices[index]));
        }

        template <typename Func>
        void for_each(Func&& func) noexcept
        {
            if constexpr (N == 2)
                for_each_impl_2(std::forward<Func>(func));
            else if constexpr (N == 3)
                for_each_impl_3(std::forward<Func>(func));
            else if constexpr (N == 4)
                for_each_impl_4(std::forward<Func>(func));
            else
                for_each_impl(std::forward<Func>(func), std::index_sequence_for<First, Rest...>{});
        }

        template <typename... Optionals>
        class with_optionals
        {
        private:
            template <typename T>
            using set_ptr = single_class_set*;

            multi_view* base_;
            manager* mgr_;
            std::tuple<set_ptr<Optionals>...> optional_sets_;

        public:
            with_optionals(multi_view* base, manager* mgr,
                           std::tuple<set_ptr<Optionals>...> opt_sets) noexcept
                : base_(base), mgr_(mgr), optional_sets_(opt_sets) {}

            template <typename U>
            auto include_optional_component() noexcept
            {
                auto* set = mgr_->template get_single_class_set<U>();
                return with_optionals<Optionals..., U>(
                    base_, mgr_,
                    std::tuple_cat(optional_sets_, std::make_tuple(set)));
            }

            template <typename Func>
            void for_each(Func&& func) noexcept
            {
                for_each_impl(std::forward<Func>(func),
                              std::index_sequence_for<Optionals...>{});
            }

        private:
            template <typename Func, size_t... OIs>
            void for_each_impl(Func&& func, std::index_sequence<OIs...>) noexcept
            {
                base_->for_each([&](entity e, auto&... comps) {
                    auto opt_ptrs = std::make_tuple(
                        std::get<OIs>(optional_sets_)->template get_ptr_fast<
                            std::tuple_element_t<OIs, std::tuple<Optionals...>>
                        >(e)...
                    );
                    std::apply([&](auto*... opts) {
                        if constexpr (std::is_invocable_v<Func, entity, decltype(comps)..., decltype(opts)...>)
                        {
                            func(e, comps..., opts...);
                        }
                        else
                        {
                            func(comps..., opts...);
                        }
                    }, opt_ptrs);
                });
            }
        };

        template <typename U>
        auto include_optional_component() noexcept
        {
            return with_optionals<U>(this, mgr_,
                std::make_tuple(mgr_->template get_single_class_set<U>()));
        }

        // ======================== paged_view ========================
        class paged_view
        {
        private:
            multi_view* base_;
            size_t offset_;
            size_t limit_;

        public:
            paged_view(multi_view* base, size_t offset, size_t limit) noexcept
                : base_(base), offset_(offset), limit_(limit) {}

            [[nodiscard]] size_t size() const noexcept
            {
                size_t base_sz = base_->size();
                if (offset_ >= base_sz) return 0;
                size_t rem = base_sz - offset_;
                return rem < limit_ ? rem : limit_;
            }

            [[nodiscard]] bool empty() const noexcept { return size() == 0; }

            template <typename Func>
            void for_each(Func&& func) noexcept
            {
                size_t skipped = 0;
                size_t processed = 0;
                base_->for_each([&](auto&... args) {
                    if (skipped < offset_)
                    {
                        ++skipped;
                        return;
                    }
                    if (processed >= limit_) return;
                    ++processed;
                    func(args...);
                });
            }
        };

        auto page(size_t offset, size_t limit) noexcept
        {
            return paged_view(this, offset, limit);
        }

        // ======================== sorted_component_view ========================
        template <typename Compare, size_t SortIdx>
        class sorted_component_view
        {
        private:
            multi_view* base_;
            Compare cmp_;
            class_pool<size_t> sorted_indices_;
            class_pool<uint64_t> last_versions_;
            bool needs_rebuild_{true};

            using SortType = std::tuple_element_t<SortIdx, AllTypes>;

            void rebuild() noexcept
            {
                sorted_indices_.clear();
                if (!base_->all_sets_valid()) [[unlikely]] return;

                base_->ensure_mappings();
                auto* primary = base_->sets_[base_->primary_idx_];
                auto& indices = primary->get_entity_indices();
                const size_t n = indices.size();

                struct sort_entry { SortType key; size_t index; };
                class_pool<sort_entry> entries;
                entries.resize(n, {});

                auto* raw = reinterpret_cast<uint32_t*>(base_->dense_mappings_soa_.data());
                auto* sort_pool = base_->sets_[SortIdx]->template get_typed_pool_ptr<SortType>()->data();

                for (size_t i = 0; i < n; ++i)
                {
                    uint32_t key_dense = raw[i * N + SortIdx];
                    entries[i].key = (key_dense != UINT32_MAX) ? sort_pool[key_dense] : SortType{};
                    entries[i].index = i;
                }

                std::sort(entries.data(), entries.data() + n, [this](const sort_entry& a, const sort_entry& b) {
                    return cmp_(a.key, b.key);
                });

                sorted_indices_.resize(n, size_t{0});
                for (size_t i = 0; i < n; ++i)
                    sorted_indices_[i] = entries[i].index;

                for (size_t i = 0; i < N; ++i)
                {
                    if (base_->sets_[i])
                        last_versions_[i] = base_->sets_[i]->get_pool_version();
                }
                needs_rebuild_ = false;
            }

            void ensure_fresh() noexcept
            {
                for (size_t i = 0; i < N; ++i)
                {
                    if (base_->sets_[i] && base_->sets_[i]->get_pool_version() != last_versions_[i])
                    {
                        rebuild();
                        return;
                    }
                }
            }

            template <typename Func, size_t... Is>
            void for_each_impl(Func&& func, std::index_sequence<Is...>) noexcept
            {
                ensure_fresh();
                if (sorted_indices_.empty()) return;

                base_->ensure_mappings();
                auto* primary = base_->sets_[base_->primary_idx_];
                auto& indices = primary->get_entity_indices();
                auto* sparse_combined = primary->get_sparse_combined().data();
                auto* raw = reinterpret_cast<uint32_t*>(base_->dense_mappings_soa_.data());
                constexpr size_t stride = N;

                auto pools = std::make_tuple(
                    base_->sets_[Is]->template get_typed_pool_ptr<std::tuple_element_t<Is, AllTypes>>()...
                );

                for (size_t i = 0; i < sorted_indices_.size(); ++i)
                {
                    if (i + 32 < sorted_indices_.size()) [[likely]]
                    {
                        size_t next_pi = sorted_indices_[i + 32];
                        const uint32_t* next_m = raw + next_pi * stride;
                        (PREFETCH_R(&(*std::get<Is>(pools))[next_m[Is]]), ...);
                        PREFETCH_R(&sparse_combined[indices[next_pi]]);
                    }
                    size_t primary_i = sorted_indices_[i];
                    const uint32_t* m = raw + primary_i * stride;
                    if (((m[Is] != UINT32_MAX) && ...)) [[likely]]
                    {
                        entity e(indices[primary_i], static_cast<uint32_t>(sparse_combined[indices[primary_i]]));
                        if constexpr (std::is_invocable_v<Func, entity, First&, Rest&...>)
                            func(e, (*std::get<Is>(pools))[m[Is]]...);
                        else
                            func((*std::get<Is>(pools))[m[Is]]...);
                    }
                }
            }

        public:
            sorted_component_view(multi_view* base, Compare cmp) noexcept
                : base_(base), cmp_(std::move(cmp))
            {
                last_versions_.resize(N, 0);
                rebuild();
            }

            [[nodiscard]] size_t size() const noexcept { return sorted_indices_.size(); }
            [[nodiscard]] bool empty() const noexcept { return sorted_indices_.empty(); }

            template <typename Func>
            void for_each(Func&& func) noexcept
            {
                for_each_impl(std::forward<Func>(func), std::index_sequence_for<First, Rest...>{});
            }
        };

        template <typename T, typename Compare>
        auto sorted_by_component(Compare&& cmp) noexcept
        {
            constexpr size_t idx = find_type_index<T, AllTypes>();
            return sorted_component_view<Compare, idx>(this, std::forward<Compare>(cmp));
        }

        // ======================== grouped_component_view ========================
        template <typename KeyType, typename KeyFunc>
        class grouped_component_view
        {
        private:
            multi_view* base_;
            KeyFunc key_func_;
            class_pool<size_t> sorted_indices_;
            class_pool<KeyType> group_keys_;
            class_pool<size_t> group_starts_;
            class_pool<uint64_t> last_versions_;
            bool needs_rebuild_{true};

            void rebuild() noexcept
            {
                sorted_indices_.clear();
                group_keys_.clear();
                group_starts_.clear();
                if (!base_->all_sets_valid()) [[unlikely]] return;

                base_->ensure_mappings();
                auto* primary = base_->sets_[base_->primary_idx_];
                auto& indices = primary->get_entity_indices();
                const size_t n = indices.size();

                struct sort_entry { KeyType key; size_t index; };
                class_pool<sort_entry> entries;
                entries.resize(n, {});

                auto* raw = reinterpret_cast<uint32_t*>(base_->dense_mappings_soa_.data());
                auto* first_pool = base_->sets_[0]->template get_typed_pool_ptr<First>()->data();

                for (size_t i = 0; i < n; ++i)
                {
                    uint32_t first_dense = raw[i * N + 0];
                    entries[i].key = (first_dense != UINT32_MAX) ? key_func_(first_pool[first_dense]) : KeyType{};
                    entries[i].index = i;
                }

                if constexpr (is_radix_sortable_v<KeyType>)
                {
                    radix_sort_entries<KeyType>(entries.data(), n);
                }
                else
                {
                    std::sort(entries.data(), entries.data() + n, [](const sort_entry& a, const sort_entry& b) {
                        return a.key < b.key;
                    });
                }

                sorted_indices_.resize(n, size_t{0});
                group_keys_.resize(n, KeyType{});
                for (size_t i = 0; i < n; ++i)
                {
                    sorted_indices_[i] = entries[i].index;
                    group_keys_[i] = entries[i].key;
                }

                group_starts_.emplace_back(0);
                for (size_t i = 1; i < n; ++i)
                {
                    if (group_keys_[i] != group_keys_[i - 1])
                        group_starts_.emplace_back(i);
                }

                for (size_t i = 0; i < N; ++i)
                {
                    if (base_->sets_[i])
                        last_versions_[i] = base_->sets_[i]->get_pool_version();
                }
                needs_rebuild_ = false;
            }

            void ensure_fresh() noexcept
            {
                for (size_t i = 0; i < N; ++i)
                {
                    if (base_->sets_[i] && base_->sets_[i]->get_pool_version() != last_versions_[i])
                    {
                        rebuild();
                        return;
                    }
                }
            }

            template <typename Func, size_t... Is>
            void for_each_impl(Func&& func, std::index_sequence<Is...>) noexcept
            {
                ensure_fresh();
                if (sorted_indices_.empty()) return;

                base_->ensure_mappings();
                auto* primary = base_->sets_[base_->primary_idx_];
                auto& indices = primary->get_entity_indices();
                auto* sparse_combined = primary->get_sparse_combined().data();
                auto* raw = reinterpret_cast<uint32_t*>(base_->dense_mappings_soa_.data());
                constexpr size_t stride = N;

                auto pools = std::make_tuple(
                    base_->sets_[Is]->template get_typed_pool_ptr<std::tuple_element_t<Is, AllTypes>>()...
                );

                for (size_t i = 0; i < sorted_indices_.size(); ++i)
                {
                    if (i + 32 < sorted_indices_.size()) [[likely]]
                    {
                        size_t next_pi = sorted_indices_[i + 32];
                        const uint32_t* next_m = raw + next_pi * stride;
                        (PREFETCH_R(&(*std::get<Is>(pools))[next_m[Is]]), ...);
                        PREFETCH_R(&sparse_combined[indices[next_pi]]);
                    }
                    size_t primary_i = sorted_indices_[i];
                    const uint32_t* m = raw + primary_i * stride;
                    if (((m[Is] != UINT32_MAX) && ...)) [[likely]]
                    {
                        entity e(indices[primary_i], static_cast<uint32_t>(sparse_combined[indices[primary_i]]));
                        if constexpr (std::is_invocable_v<Func, entity, First&, Rest&...>)
                            func(e, (*std::get<Is>(pools))[m[Is]]...);
                        else
                            func((*std::get<Is>(pools))[m[Is]]...);
                    }
                }
            }

        public:
            grouped_component_view(multi_view* base, KeyFunc key_func) noexcept
                : base_(base), key_func_(std::move(key_func))
            {
                last_versions_.resize(N, 0);
                rebuild();
            }

            [[nodiscard]] size_t size() const noexcept { return sorted_indices_.size(); }
            [[nodiscard]] bool empty() const noexcept { return sorted_indices_.empty(); }
            [[nodiscard]] size_t group_count() const noexcept { return group_starts_.size(); }

            template <typename Func>
            void for_each(Func&& func) noexcept
            {
                for_each_impl(std::forward<Func>(func), std::index_sequence_for<First, Rest...>{});
            }

            template <typename Func>
            void for_each_group(Func&& func) noexcept
            {
                ensure_fresh();
                if (sorted_indices_.empty()) return;

                for (size_t g = 0; g < group_starts_.size(); ++g)
                {
                    size_t start = group_starts_[g];
                    size_t end = (g + 1 < group_starts_.size()) ? group_starts_[g + 1] : sorted_indices_.size();
                    KeyType key = group_keys_[start];
                    func(key, start, end);
                }
            }
        };

        template <typename KeyFunc>
        auto sorted_by_component_value(KeyFunc&& key_func) noexcept
        {
            using KeyType = std::invoke_result_t<KeyFunc, First&>;
            return grouped_component_view<KeyType, KeyFunc>(this, std::forward<KeyFunc>(key_func));
        }

        // ======================== changed_view ========================
        class changed_view
        {
        private:
            multi_view* base_;
            class_pool<uint64_t> last_versions_;
            class_pool<size_t> changed_indices_;
            bool needs_rebuild_{true};

            void rebuild() noexcept
            {
                changed_indices_.clear();
                if (!base_->all_sets_valid()) [[unlikely]]
                {
                    needs_rebuild_ = false;
                    return;
                }

                bool any_changed = false;
                for (size_t i = 0; i < N; ++i)
                {
                    if (base_->sets_[i])
                    {
                        uint64_t cur = base_->sets_[i]->get_pool_version();
                        if (cur != last_versions_[i])
                        {
                            last_versions_[i] = cur;
                            any_changed = true;
                        }
                    }
                }

                if (!any_changed)
                {
                    needs_rebuild_ = false;
                    return;
                }

                auto* primary = base_->sets_[base_->primary_idx_];
                auto& indices = primary->get_entity_indices();
                const size_t n = indices.size();

                for (size_t i = 0; i < n; ++i)
                {
                    entity e(indices[i], primary->get_version_unchecked(indices[i]));
                    if (base_->contains_impl(e, std::index_sequence_for<First, Rest...>{}))
                        changed_indices_.emplace_back(i);
                }
                needs_rebuild_ = false;
            }

            void ensure_fresh() noexcept
            {
                if (!needs_rebuild_) return;
                rebuild();
            }

            template <typename Func, size_t... Is>
            void for_each_impl(Func&& func, std::index_sequence<Is...>) noexcept
            {
                ensure_fresh();
                needs_rebuild_ = true;
                if (changed_indices_.empty()) return;

                auto* primary = base_->sets_[base_->primary_idx_];
                auto& indices = primary->get_entity_indices();

                for (size_t i = 0; i < changed_indices_.size(); ++i)
                {
                    size_t primary_i = changed_indices_[i];
                    entity e(indices[primary_i], primary->get_version_unchecked(indices[primary_i]));

                    auto comps = std::make_tuple(
                        base_->template get_component<Is>(e, primary_i)...
                    );

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

        public:
            changed_view(multi_view* base) noexcept : base_(base)
            {
                last_versions_.resize(N, 0);
                rebuild();
            }

            [[nodiscard]] size_t size() const noexcept { return changed_indices_.size(); }
            [[nodiscard]] bool empty() const noexcept { return changed_indices_.empty(); }

            void reset_tracking() noexcept
            {
                for (size_t i = 0; i < N; ++i)
                    last_versions_[i] = 0;
                changed_indices_.clear();
                needs_rebuild_ = true;
            }

            template <typename Func>
            void for_each(Func&& func) noexcept
            {
                for_each_impl(std::forward<Func>(func), std::index_sequence_for<First, Rest...>{});
            }
        };

        auto track_changes() noexcept
        {
            return changed_view(this);
        }

        // ======================== multi_view::filter_changed ========================
        template <size_t TrackIdx>
        class filter_changed_view
        {
        private:
            multi_view base_;
            class_pool<uint64_t> last_observed_versions_;
            class_pool<size_t> changed_indices_;
            bool needs_rebuild_{true};

            using TrackType = std::tuple_element_t<TrackIdx, std::tuple<First, Rest...>>;

            void rebuild() noexcept
            {
                changed_indices_.clear();
                if (!base_.all_sets_valid()) [[unlikely]] { needs_rebuild_ = false; return; }
                if (!base_.sets_[TrackIdx]) [[unlikely]] { needs_rebuild_ = false; return; }

                auto* primary = base_.sets_[base_.primary_idx_];
                auto& indices = primary->get_entity_indices();
                const size_t n = indices.size();
                // 仅扩展，不覆盖已追踪的版本号
                while (last_observed_versions_.size() < n)
                    last_observed_versions_.emplace_back(0);

                auto* track_set = base_.sets_[TrackIdx];

                for (size_t i = 0; i < n; ++i)
                {
                    entity e(indices[i], primary->get_version_unchecked(indices[i]));
                    if (!base_.contains_impl(e, std::index_sequence_for<First, Rest...>{})) continue;

                    uint32_t dense_idx = track_set->get_dense_at(e.parts_.index_);
                    uint64_t cur = track_set->get_entity_change_version(dense_idx);
                    if (i >= last_observed_versions_.size() || cur != last_observed_versions_[i])
                    {
                        if (i < last_observed_versions_.size())
                            last_observed_versions_[i] = cur;
                        else
                            last_observed_versions_.emplace_at(i, cur);
                        changed_indices_.emplace_back(i);
                    }
                }
                needs_rebuild_ = false;
            }

            template <typename Func, size_t... Is>
            void for_each_impl(Func&& func, std::index_sequence<Is...>) noexcept
            {
                if (needs_rebuild_) rebuild();
                needs_rebuild_ = true;
                if (changed_indices_.empty()) return;

                auto* primary = base_.sets_[base_.primary_idx_];
                auto& indices = primary->get_entity_indices();

                for (size_t i = 0; i < changed_indices_.size(); ++i)
                {
                    size_t primary_i = changed_indices_[i];
                    entity e(indices[primary_i], primary->get_version_unchecked(indices[primary_i]));
                    auto comps = std::make_tuple(base_.template get_component<Is>(e, primary_i)...);
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

        public:
            filter_changed_view(multi_view base) noexcept : base_(base) { rebuild(); }

            [[nodiscard]] size_t size() const noexcept { return changed_indices_.size(); }
            [[nodiscard]] bool empty() const noexcept { return changed_indices_.empty(); }

            void reset_tracking() noexcept
            {
                last_observed_versions_.clear();
                changed_indices_.clear();
                needs_rebuild_ = true;
            }

            template <typename Func>
            void for_each(Func&& func) noexcept
            {
                for_each_impl(std::forward<Func>(func), std::index_sequence_for<First, Rest...>{});
            }
        };

        template <typename T>
        auto filter_changed() noexcept
        {
            constexpr size_t idx = find_type_index<T, std::tuple<First, Rest...>>();
            return filter_changed_view<idx>(*this);
        }

        auto filter_any_changed() noexcept
        {
            return filter_changed_view<0>(*this);
        }

        // ======================== multi_view::filter_added ========================
        template <size_t TrackIdx>
        class filter_added_view
        {
        private:
            multi_view base_;
            class_pool<uint64_t> last_observed_added_;
            class_pool<size_t> added_indices_;
            bool needs_rebuild_{true};
            uint64_t baseline_added_counter_{0};

            using TrackType = std::tuple_element_t<TrackIdx, std::tuple<First, Rest...>>;

            void rebuild() noexcept
            {
                added_indices_.clear();
                if (!base_.all_sets_valid()) [[unlikely]] { needs_rebuild_ = false; return; }
                if (!base_.sets_[TrackIdx]) [[unlikely]] { needs_rebuild_ = false; return; }

                auto* primary = base_.sets_[base_.primary_idx_];
                auto& indices = primary->get_entity_indices();
                const size_t n = indices.size();
                while (last_observed_added_.size() < n)
                    last_observed_added_.emplace_back(0);

                auto* track_set = base_.sets_[TrackIdx];

                for (size_t i = 0; i < n; ++i)
                {
                    entity e(indices[i], primary->get_version_unchecked(indices[i]));
                    if (!base_.contains_impl(e, std::index_sequence_for<First, Rest...>{})) continue;

                    uint32_t dense_idx = track_set->get_dense_at(e.parts_.index_);
                    uint64_t cur = track_set->get_entity_added_version(dense_idx);
                    if (cur > baseline_added_counter_ && (i >= last_observed_added_.size() || cur != last_observed_added_[i]))
                    {
                        if (i < last_observed_added_.size())
                            last_observed_added_[i] = cur;
                        else
                            last_observed_added_.emplace_at(i, cur);
                        added_indices_.emplace_back(i);
                    }
                }
                needs_rebuild_ = false;
            }

            template <typename Func, size_t... Is>
            void for_each_impl(Func&& func, std::index_sequence<Is...>) noexcept
            {
                if (needs_rebuild_) rebuild();
                needs_rebuild_ = true;
                if (added_indices_.empty()) return;

                auto* primary = base_.sets_[base_.primary_idx_];
                auto& indices = primary->get_entity_indices();

                for (size_t i = 0; i < added_indices_.size(); ++i)
                {
                    size_t primary_i = added_indices_[i];
                    entity e(indices[primary_i], primary->get_version_unchecked(indices[primary_i]));
                    auto comps = std::make_tuple(base_.template get_component<Is>(e, primary_i)...);
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

        public:
            filter_added_view(multi_view base) noexcept : base_(base)
            {
                if (base_.all_sets_valid() && base_.sets_[TrackIdx])
                    baseline_added_counter_ = base_.sets_[TrackIdx]->get_global_added_counter();
                rebuild();
            }

            [[nodiscard]] size_t size() const noexcept { return added_indices_.size(); }
            [[nodiscard]] bool empty() const noexcept { return added_indices_.empty(); }

            void reset_tracking() noexcept
            {
                last_observed_added_.clear();
                added_indices_.clear();
                needs_rebuild_ = true;
                baseline_added_counter_ = 0;
            }

            template <typename Func>
            void for_each(Func&& func) noexcept
            {
                for_each_impl(std::forward<Func>(func), std::index_sequence_for<First, Rest...>{});
            }
        };

        template <typename T>
        auto filter_added() noexcept
        {
            constexpr size_t idx = find_type_index<T, std::tuple<First, Rest...>>();
            return filter_added_view<idx>(*this);
        }

        // ======================== multi_view::exactly_one ========================
        [[nodiscard]] std::tuple<First&, Rest&...> exactly_one() noexcept
        {
            assert(all_sets_valid() && "exactly_one(): sets not valid");
            auto* primary = sets_[primary_idx_];
            auto& indices = primary->get_entity_indices();
            size_t match_count = 0;
            size_t last_match = 0;

            for (size_t i = 0; i < indices.size(); ++i)
            {
                entity e(indices[i], primary->get_version_unchecked(indices[i]));
                if (contains_impl(e, std::index_sequence_for<First, Rest...>{}))
                {
                    last_match = i;
                    ++match_count;
                    if (match_count > 1) break;
                }
            }
            assert(match_count == 1 && "exactly_one(): expected exactly 1 matching entity");

            entity e(indices[last_match], primary->get_version_unchecked(indices[last_match]));
            return std::forward_as_tuple(
                *sets_[find_type_index<First, std::tuple<First, Rest...>>()]->template get_ptr_fast<First>(e),
                *sets_[find_type_index<Rest, std::tuple<First, Rest...>>()]->template get_ptr_fast<Rest>(e)...
            );
        }

        // ======================== multi_view::find_one ========================
        [[nodiscard]] std::tuple<First*, Rest*...> find_one(entity e) noexcept
        {
            if (!all_sets_valid()) [[unlikely]]
                return std::make_tuple(static_cast<First*>(nullptr), static_cast<Rest*>(nullptr)...);
            if (!contains_impl(e, std::index_sequence_for<First, Rest...>{}))
                return std::make_tuple(static_cast<First*>(nullptr), static_cast<Rest*>(nullptr)...);
            return std::make_tuple(
                sets_[find_type_index<First, std::tuple<First, Rest...>>()]->template get_ptr_fast<First>(e),
                sets_[find_type_index<Rest, std::tuple<First, Rest...>>()]->template get_ptr_fast<Rest>(e)...
            );
        }

        // ======================== multi_view::iter_over_entities ========================
        template <typename EntitySpan>
        class entity_specific_view
        {
        private:
            multi_view base_;
            std::remove_reference_t<EntitySpan> entities_;

        public:
            entity_specific_view(multi_view base, EntitySpan entities) noexcept
                : base_(base), entities_(std::forward<EntitySpan>(entities)) {}

            template <typename Func, size_t... Is>
            void for_each_impl(Func&& func, std::index_sequence<Is...>) noexcept
            {
                for (auto e : entities_)
                {
                    if (!base_.contains_impl(e, std::index_sequence_for<First, Rest...>{})) continue;
                    std::apply([&](auto*... ptrs) {
                        if constexpr (std::is_invocable_v<Func, entity, First&, Rest&...>)
                            func(e, *ptrs...);
                        else
                            func(*ptrs...);
                    }, std::make_tuple(
                        base_.sets_[Is]->template get_ptr_fast<std::tuple_element_t<Is, std::tuple<First, Rest...>>>(e)...
                    ));
                }
            }

            template <typename Func>
            void for_each(Func&& func) noexcept
            {
                for_each_impl(std::forward<Func>(func), std::index_sequence_for<First, Rest...>{});
            }
        };

        template <typename EntitySpan>
        auto iter_over_entities(EntitySpan&& entities) noexcept
        {
            return entity_specific_view<EntitySpan>(*this, std::forward<EntitySpan>(entities));
        }
    };

    // ======================== single_view_without ========================
    template <typename T, typename... ExcludeTypes>
    class single_view_without
    {
    private:
        single_class_set* set_;
        manager* mgr_;
        uint64_t exclude_mask_{0};

    public:
        single_view_without(single_class_set* set, manager* mgr) noexcept
            : set_(set), mgr_(mgr)
        {
            if (mgr_)
                exclude_mask_ = (... | mgr_->template get_component_bit<ExcludeTypes>());
        }

        [[nodiscard]] size_t size() const noexcept { return set_ ? set_->size() : 0; }
        [[nodiscard]] bool empty() const noexcept { return set_ ? set_->empty() : true; }

        [[nodiscard]] bool contains(entity e) const noexcept
        {
            if (!set_ || !set_->template get_ptr<T>(e)) return false;
            return (mgr_->get_entity_mask(e) & exclude_mask_) == 0;
        }

        [[nodiscard]] T* get_component_for_entity(entity e) noexcept
        {
            return set_ ? set_->template get_ptr_fast<T>(e) : nullptr;
        }

        [[nodiscard]] entity get_first_entity() const noexcept
        {
            if (!set_) [[unlikely]] return entity{};
            auto* pool = set_->template get_typed_pool_ptr<T>();
            if (!pool) [[unlikely]] return entity{};
            auto& indices = set_->get_entity_indices();
            for (size_t i = 0; i < indices.size(); ++i)
            {
                entity e(indices[i], set_->get_version_unchecked(indices[i]));
                if ((mgr_->get_entity_mask(e) & exclude_mask_) == 0) return e;
            }
            return entity{};
        }

        template <typename Func>
        void for_each(Func&& func) noexcept
        {
            if (!set_) [[unlikely]] return;
            auto* pool = set_->template get_typed_pool_ptr<T>();
            if (!pool) [[unlikely]] return;
            auto& indices = set_->get_entity_indices();
            const size_t n = indices.size();

            if constexpr (std::is_invocable_v<Func, entity, T&>)
            {
                auto* sparse_combined = set_->get_sparse_combined().data();
                auto& em = mgr_->get_entity_manager();
                for (size_t i = 0; i < n; ++i)
                {
                    if (i + 32 < n) [[likely]]
                    {
                        PREFETCH_R(&(*pool)[i + 32]);
                        PREFETCH_R(&sparse_combined[indices[i + 32]]);
                    }
                    uint32_t idx = indices[i];
                    uint64_t mask = em.get_mask(idx);
                    if ((mask & exclude_mask_) != 0) [[unlikely]] continue;
                    entity e(idx, static_cast<uint32_t>(sparse_combined[idx]));
                    func(e, (*pool)[i]);
                }
            }
            else
            {
                auto& em = mgr_->get_entity_manager();
                for (size_t i = 0; i < n; ++i)
                {
                    if (i + 32 < n) [[likely]]
                        PREFETCH_R(&(*pool)[i + 32]);
                    uint32_t idx = indices[i];
                    uint64_t mask = em.get_mask(idx);
                    if ((mask & exclude_mask_) != 0) [[unlikely]] continue;
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

        [[nodiscard]] bool contains(entity e) const noexcept
        {
            return set_ && set_->template get_ptr<T>(e) != nullptr;
        }

        [[nodiscard]] T* get_component_for_entity(entity e) noexcept
        {
            return set_ ? set_->template get_ptr_fast<T>(e) : nullptr;
        }

        template <typename U>
        [[nodiscard]] U* get_optional_component_for_entity(entity e) noexcept
        {
            return mgr_ ? mgr_->template get_ptr_fast<U>(e) : nullptr;
        }

        [[nodiscard]] entity get_first_entity() const noexcept
        {
            if (!set_ || set_->size() == 0) [[unlikely]] return entity{};
            auto& indices = set_->get_entity_indices();
            return entity(indices[0], set_->get_version_unchecked(indices[0]));
        }

        template <typename Func>
        void for_each(Func&& func) noexcept
        {
            if (!set_) [[unlikely]] return;
            auto* pool = set_->template get_typed_pool_ptr<T>();
            if (!pool) [[unlikely]] return;
            auto& indices = set_->get_entity_indices();
            const size_t n = indices.size();
            auto* sparse_combined = set_->get_sparse_combined().data();
            for (size_t i = 0; i < n; ++i)
            {
                if (i + 32 < n) [[likely]]
                {
                    PREFETCH_R(&(*pool)[i + 32]);
                    PREFETCH_R(&sparse_combined[indices[i + 32]]);
                }
                uint32_t idx = indices[i];
                entity e(idx, static_cast<uint32_t>(sparse_combined[idx]));
                auto& comp = (*pool)[i];
                if constexpr (sizeof...(GetTypes) == 0)
                {
                    if constexpr (std::is_invocable_v<Func, entity, T&>)
                        func(e, comp);
                    else
                        func(comp);
                }
                else
                {
                    auto get_ptrs = std::make_tuple(mgr_->template get_ptr_fast<GetTypes>(e)...);
                    std::apply([&](auto*... pts) {
                        if constexpr (std::is_invocable_v<Func, entity, T&, GetTypes*...>)
                            func(e, comp, pts...);
                        else
                            func(comp, pts...);
                    }, get_ptrs);
                }
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

        [[nodiscard]] size_t size() const noexcept
        {
            size_t s = 0;
            if (set_a_) s += set_a_->size();
            if (set_b_) s += set_b_->size();
            return s;
        }

        [[nodiscard]] bool empty() const noexcept
        {
            return (!set_a_ || set_a_->empty()) && (!set_b_ || set_b_->empty());
        }

        [[nodiscard]] bool contains(entity e) const noexcept
        {
            return (set_a_ && set_a_->template get_ptr_fast<A>(e) != nullptr)
                || (set_b_ && set_b_->template get_ptr_fast<B>(e) != nullptr);
        }

        [[nodiscard]] entity get_first_entity() const noexcept
        {
            if (set_a_ && set_a_->size() > 0)
            {
                auto& idx = set_a_->get_entity_indices();
                return entity(idx[0], set_a_->get_version_unchecked(idx[0]));
            }
            if (set_b_ && set_b_->size() > 0)
            {
                auto& idx = set_b_->get_entity_indices();
                return entity(idx[0], set_b_->get_version_unchecked(idx[0]));
            }
            return entity{};
        }

        template <typename Func>
        void for_each(Func&& func) noexcept
        {
            if (set_a_)
            {
                auto* pool_a = set_a_->template get_typed_pool_ptr<A>();
                if (pool_a)
                {
                    auto& idx_a = set_a_->get_entity_indices();
                    auto* a_sparse = set_a_->get_sparse_combined().data();
                    auto* b_sparse = set_b_ ? set_b_->get_sparse_combined().data() : nullptr;
                    auto* pool_b = set_b_ ? set_b_->template get_typed_pool_ptr<B>() : nullptr;
                    size_t b_sparse_size = set_b_ ? set_b_->get_sparse_combined().size() : 0;
                    for (size_t i = 0; i < idx_a.size(); ++i)
                    {
                        uint32_t eid = idx_a[i];
                        entity e(eid, static_cast<uint32_t>(a_sparse[eid]));
                        B* b = nullptr;
                        if (b_sparse && pool_b && eid < b_sparse_size)
                        {
                            uint32_t bd = static_cast<uint32_t>(b_sparse[eid] >> 32);
                            if (bd != UINT32_MAX) b = &(*pool_b)[bd];
                        }
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
                    auto* b_sparse = set_b_->get_sparse_combined().data();
                    auto* a_sparse = set_a_ ? set_a_->get_sparse_combined().data() : nullptr;
                    size_t a_sparse_size = set_a_ ? set_a_->get_sparse_combined().size() : 0;
                    for (size_t i = 0; i < idx_b.size(); ++i)
                    {
                        uint32_t eid = idx_b[i];
                        if (a_sparse && eid < a_sparse_size)
                        {
                            uint32_t ad = static_cast<uint32_t>(a_sparse[eid] >> 32);
                            if (ad != UINT32_MAX) continue;
                        }
                        entity e(eid, static_cast<uint32_t>(b_sparse[eid]));
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

    // ======================== any_of_view ========================
    template <typename... Types>
    class any_of_view
    {
    private:
        static constexpr size_t N = sizeof...(Types);
        std::array<single_class_set*, N> sets_;

        template <size_t I>
        using type_at = std::tuple_element_t<I, std::tuple<Types...>>;

        [[nodiscard]] size_t max_entity_index() const noexcept
        {
            size_t max_idx = 0;
            for (auto* s : sets_)
            {
                if (s)
                {
                    size_t sz = s->get_sparse_combined().size();
                    if (sz > max_idx) max_idx = sz;
                }
            }
            return max_idx;
        }

    public:
        any_of_view(std::array<single_class_set*, N> s) noexcept : sets_(s) {}

        [[nodiscard]] size_t size() const noexcept
        {
            size_t total = 0;
            for (auto* s : sets_) if (s) total += s->size();
            return total;
        }

        [[nodiscard]] bool empty() const noexcept
        {
            for (auto* s : sets_) if (s && s->size() > 0) return false;
            return true;
        }

        template <size_t... Is>
        void for_each_impl(auto&& func, std::index_sequence<Is...>) noexcept
        {
            size_t max_idx = max_entity_index();
            if (max_idx == 0) return;
            class_pool<uint64_t> visited;
            visited.resize((max_idx >> 6) + 1, 0);

            for (size_t set_idx = 0; set_idx < N; ++set_idx)
            {
                auto* set = sets_[set_idx];
                if (!set) continue;
                auto& indices = set->get_entity_indices();
                const size_t n = indices.size();

                for (size_t i = 0; i < n; ++i)
                {
                    uint32_t idx = indices[i];
                    size_t word = idx >> 6;
                    uint64_t bit = uint64_t{1} << (idx & 63);
                    if (visited[word] & bit) continue;
                    visited[word] |= bit;

                    entity e(idx, set->get_version_unchecked(idx));

                    auto comp_ptrs = std::make_tuple(
                        [&]() -> type_at<Is>* {
                            if (Is == set_idx) return nullptr;
                            return sets_[Is] ? sets_[Is]->template get_ptr_fast<type_at<Is>>(e) : nullptr;
                        }()...
                    );

                    std::apply([&](auto*... ptrs) {
                        if constexpr (std::is_invocable_v<decltype(func), entity, type_at<Is>*...>)
                            func(e, ptrs...);
                        else
                            func(ptrs...);
                    }, comp_ptrs);
                }
            }
        }

        template <typename Func>
        void for_each(Func&& func) noexcept
        {
            for_each_impl(std::forward<Func>(func), std::index_sequence_for<Types...>{});
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
            auto* set = mgr_->template get_single_class_set<T>();
            if (!set) return;
            auto* pool = set->template get_typed_pool_ptr<T>();
            if (!pool) return;
            size_t pool_size = pool->size();
            filtered_.clear();
            filtered_.increase_capacity(pool_size);
            for (size_t i = 0; i < pool_size; ++i)
            {
                if (pred_((*pool)[i]))
                    filtered_.push_back_unchecked(i);
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

        [[nodiscard]] bool contains(entity e) const noexcept
        {
            auto* set = mgr_->template get_single_class_set<T>();
            if (!set) return false;
            auto* pool = set->template get_typed_pool_ptr<T>();
            if (!pool) return false;
            if (e.parts_.index_ >= set->get_sparse_combined().size()) return false;
            uint64_t combined = set->get_sparse_combined()[e.parts_.index_];
            if (static_cast<uint32_t>(combined) != e.parts_.version_) return false;
            size_t dense_idx = static_cast<uint32_t>(combined >> 32);
            for (size_t i = 0; i < filtered_.size(); ++i)
            {
                if (filtered_[i] == dense_idx) return pred_((*pool)[dense_idx]);
            }
            return false;
        }

        [[nodiscard]] T* get_component_for_entity(entity e) noexcept
        {
            auto* set = mgr_->template get_single_class_set<T>();
            return set ? set->template get_ptr_fast<T>(e) : nullptr;
        }

        [[nodiscard]] entity get_first_entity() const noexcept
        {
            if (filtered_.empty()) [[unlikely]] return entity{};
            auto* set = mgr_->template get_single_class_set<T>();
            if (!set) return entity{};
            auto& indices = set->get_entity_indices();
            size_t dense_idx = filtered_[0];
            return entity(indices[dense_idx], set->get_version_unchecked(indices[dense_idx]));
        }

        [[nodiscard]] entity get_entity_at_index(size_t index) const noexcept
        {
            if (index >= filtered_.size()) [[unlikely]] return entity{};
            auto* set = mgr_->template get_single_class_set<T>();
            if (!set) return entity{};
            auto& indices = set->get_entity_indices();
            size_t dense_idx = filtered_[index];
            return entity(indices[dense_idx], set->get_version_unchecked(indices[dense_idx]));
        }

        [[nodiscard]] T* get_component_at_index(size_t index) noexcept
        {
            if (index >= filtered_.size()) [[unlikely]] return nullptr;
            auto* set = mgr_->template get_single_class_set<T>();
            if (!set) return nullptr;
            auto* pool = set->template get_typed_pool_ptr<T>();
            return pool ? &(*pool)[filtered_[index]] : nullptr;
        }

        template <typename Func>
        void for_each(Func&& func) noexcept
        {
            auto* set = mgr_->template get_single_class_set<T>();
            if (!set) return;
            auto* pool = set->template get_typed_pool_ptr<T>();
            auto& indices = set->get_entity_indices();
            auto* sparse_combined = set->get_sparse_combined().data();
            size_t n = filtered_.size();
            size_t* filtered_data = filtered_.data();

            for (size_t i = 0; i < n; ++i)
            {
                if (i + 32 < n) [[likely]]
                {
                    size_t next_idx = filtered_data[i + 32];
                    PREFETCH_R(&(*pool)[next_idx]);
                    PREFETCH_R(&sparse_combined[indices[next_idx]]);
                }
                size_t dense_index = filtered_data[i];
                if constexpr (std::is_invocable_v<Func, entity, T&>)
                {
                    entity e(indices[dense_index], static_cast<uint32_t>(sparse_combined[indices[dense_index]]));
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
        class_pool<uint64_t> filtered_;

        void rebuild_impl() noexcept
        {
            filtered_.clear();
            auto* set_a = mgr_->template get_single_class_set<T>();
            auto* set_b = mgr_->template get_single_class_set<B>();
            if (!set_a || !set_b) return;
            auto* pool_a = set_a->template get_typed_pool_ptr<T>();
            auto* pool_b = set_b->template get_typed_pool_ptr<B>();
            if (!pool_a || !pool_b) return;
            auto& indices = set_a->get_entity_indices();
            auto* b_sparse = set_b->get_sparse_combined().data();
            size_t b_sparse_size = set_b->get_sparse_combined().size();
            for (size_t i = 0; i < pool_a->size(); ++i)
            {
                if (!pred_((*pool_a)[i])) continue;
                uint32_t eid = indices[i];
                if (eid >= b_sparse_size) continue;
                uint32_t b_dense = static_cast<uint32_t>(b_sparse[eid] >> 32);
                if (b_dense != UINT32_MAX)
                {
                    filtered_.emplace_back((static_cast<uint64_t>(i) << 32) | b_dense);
                }
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

        [[nodiscard]] bool contains(entity e) const noexcept
        {
            auto* set_a = mgr_->template get_single_class_set<T>();
            auto* set_b = mgr_->template get_single_class_set<B>();
            if (!set_a || !set_b) return false;
            auto* pool_a = set_a->template get_typed_pool_ptr<T>();
            if (!pool_a) return false;
            if (e.parts_.index_ >= set_a->get_sparse_combined().size()) return false;
            uint64_t combined = set_a->get_sparse_combined()[e.parts_.index_];
            if (static_cast<uint32_t>(combined) != e.parts_.version_) return false;
            size_t dense_idx = static_cast<uint32_t>(combined >> 32);
            if (!pred_((*pool_a)[dense_idx])) return false;
            if (!set_b->template get_ptr_fast<B>(e)) return false;
            for (size_t i = 0; i < filtered_.size(); ++i)
            {
                if (static_cast<size_t>(filtered_[i] >> 32) == dense_idx) return true;
            }
            return false;
        }

        [[nodiscard]] T* get_component_for_entity(entity e) noexcept
        {
            auto* set = mgr_->template get_single_class_set<T>();
            return set ? set->template get_ptr_fast<T>(e) : nullptr;
        }

        [[nodiscard]] B* get_optional_component_for_entity(entity e) noexcept
        {
            auto* set = mgr_->template get_single_class_set<B>();
            return set ? set->template get_ptr_fast<B>(e) : nullptr;
        }

        [[nodiscard]] entity get_first_entity() const noexcept
        {
            if (filtered_.empty()) [[unlikely]] return entity{};
            auto* set_a = mgr_->template get_single_class_set<T>();
            if (!set_a) return entity{};
            auto& indices = set_a->get_entity_indices();
            size_t dense_idx = static_cast<size_t>(filtered_[0] >> 32);
            return entity(indices[dense_idx], set_a->get_version_unchecked(indices[dense_idx]));
        }

        [[nodiscard]] entity get_entity_at_index(size_t index) const noexcept
        {
            if (index >= filtered_.size()) [[unlikely]] return entity{};
            auto* set_a = mgr_->template get_single_class_set<T>();
            if (!set_a) return entity{};
            auto& indices = set_a->get_entity_indices();
            size_t dense_idx = static_cast<size_t>(filtered_[index] >> 32);
            return entity(indices[dense_idx], set_a->get_version_unchecked(indices[dense_idx]));
        }

        template <typename Func>
        void for_each(Func&& func) noexcept
        {
            auto* set_a = mgr_->template get_single_class_set<T>();
            auto* set_b = mgr_->template get_single_class_set<B>();
            if (!set_a || !set_b) return;
            auto* pool_a = set_a->template get_typed_pool_ptr<T>();
            auto* pool_b = set_b->template get_typed_pool_ptr<B>();
            if (!pool_a || !pool_b) return;
            auto& indices = set_a->get_entity_indices();
            auto* sparse_combined = set_a->get_sparse_combined().data();

            for (size_t i = 0; i < filtered_.size(); ++i)
            {
                uint64_t packed = filtered_[i];
                size_t a_dense = static_cast<size_t>(packed >> 32);
                size_t b_dense = static_cast<size_t>(packed & 0xFFFFFFFF);
                if constexpr (std::is_invocable_v<Func, entity, T&, B&>)
                {
                    entity e(indices[a_dense], static_cast<uint32_t>(sparse_combined[indices[a_dense]]));
                    func(e, (*pool_a)[a_dense], (*pool_b)[b_dense]);
                }
                else
                {
                    func((*pool_a)[a_dense], (*pool_b)[b_dense]);
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
        class_pool<uint64_t> filtered_;
        class_pool<size_t> filtered_b_;

        void rebuild_impl() noexcept
        {
            filtered_.clear();
            filtered_b_.clear();
            auto* set_a = mgr_->template get_single_class_set<T>();
            auto* set_b = mgr_->template get_single_class_set<B>();
            if (!set_a) return;
            auto* pool_a = set_a->template get_typed_pool_ptr<T>();
            if (!pool_a) return;
            auto& indices = set_a->get_entity_indices();
            auto* b_sparse = set_b ? set_b->get_sparse_combined().data() : nullptr;
            size_t b_sparse_size = set_b ? set_b->get_sparse_combined().size() : 0;

            for (size_t i = 0; i < pool_a->size(); ++i)
            {
                if (!pred_((*pool_a)[i])) continue;
                uint32_t eid = indices[i];
                uint64_t b_dense = UINT32_MAX;
                if (b_sparse && eid < b_sparse_size)
                {
                    uint32_t bd = static_cast<uint32_t>(b_sparse[eid] >> 32);
                    if (bd != UINT32_MAX) b_dense = bd;
                }
                filtered_.emplace_back((static_cast<uint64_t>(i) << 32) | b_dense);
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
                        filtered_b_.emplace_back(i);
                    }
                }
            }
        }

    public:
        filter_or_view(manager* mgr, Pred&& pred) noexcept
            : mgr_(mgr), pred_(std::forward<Pred>(pred))
        {
            rebuild_impl();
        }

        void rebuild() noexcept { rebuild_impl(); }

        [[nodiscard]] size_t size() const noexcept { return filtered_.size() + filtered_b_.size(); }
        [[nodiscard]] bool   empty() const noexcept { return filtered_.empty() && filtered_b_.empty(); }

        [[nodiscard]] bool contains(entity e) const noexcept
        {
            auto* set_a = mgr_->template get_single_class_set<T>();
            auto* set_b = mgr_->template get_single_class_set<B>();
            if (!set_a) return false;
            auto* pool_a = set_a->template get_typed_pool_ptr<T>();
            if (!pool_a) return false;
            if (e.parts_.index_ >= set_a->get_sparse_combined().size()) return false;
            uint64_t combined = set_a->get_sparse_combined()[e.parts_.index_];
            if (static_cast<uint32_t>(combined) != e.parts_.version_) return false;
            size_t dense_idx = static_cast<uint32_t>(combined >> 32);
            if (pred_((*pool_a)[dense_idx]))
            {
                for (size_t i = 0; i < filtered_.size(); ++i)
                {
                    if (static_cast<size_t>(filtered_[i] >> 32) == dense_idx) return true;
                }
            }
            if (set_b && set_b->template get_ptr_fast<B>(e)) return true;
            return false;
        }

        [[nodiscard]] entity get_first_entity() const noexcept
        {
            if (!filtered_.empty())
            {
                auto* set_a = mgr_->template get_single_class_set<T>();
                if (set_a)
                {
                    auto& indices = set_a->get_entity_indices();
                    size_t dense_idx = static_cast<size_t>(filtered_[0] >> 32);
                    return entity(indices[dense_idx], set_a->get_version_unchecked(indices[dense_idx]));
                }
            }
            if (!filtered_b_.empty())
            {
                auto* set_b = mgr_->template get_single_class_set<B>();
                if (set_b)
                {
                    auto& idx = set_b->get_entity_indices();
                    return entity(idx[filtered_b_[0]], set_b->get_version_unchecked(idx[filtered_b_[0]]));
                }
            }
            return entity{};
        }

        template <typename Func>
        void for_each(Func&& func) noexcept
        {
            auto* set_a = mgr_->template get_single_class_set<T>();
            auto* set_b = mgr_->template get_single_class_set<B>();

            if (set_a && !filtered_.empty())
            {
                auto* pool_a = set_a->template get_typed_pool_ptr<T>();
                if (pool_a)
                {
                    auto& idx_a = set_a->get_entity_indices();
                    for (size_t i = 0; i < filtered_.size(); ++i)
                    {
                        uint64_t packed = filtered_[i];
                        size_t a_dense = static_cast<size_t>(packed >> 32);
                        size_t b_dense = static_cast<size_t>(packed & 0xFFFFFFFF);
                        entity e(idx_a[a_dense], set_a->get_version_unchecked(idx_a[a_dense]));
                        B* b = (b_dense != UINT32_MAX && set_b) ? &(*set_b->template get_typed_pool_ptr<B>())[b_dense] : nullptr;
                        if constexpr (std::is_invocable_v<Func, entity, T*, B*>)
                        {
                            func(e, &(*pool_a)[a_dense], b);
                        }
                        else
                        {
                            func(&(*pool_a)[a_dense], b);
                        }
                    }
                }
            }

            if (set_b && !filtered_b_.empty())
            {
                auto* pool_b = set_b->template get_typed_pool_ptr<B>();
                if (pool_b)
                {
                    auto& idx_b = set_b->get_entity_indices();
                    for (size_t i = 0; i < filtered_b_.size(); ++i)
                    {
                        size_t b_dense = filtered_b_[i];
                        entity e(idx_b[b_dense], set_b->get_version_unchecked(idx_b[b_dense]));
                        if constexpr (std::is_invocable_v<Func, entity, T*, B*>)
                        {
                            func(e, nullptr, &(*pool_b)[b_dense]);
                        }
                        else
                        {
                            func(nullptr, &(*pool_b)[b_dense]);
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

// ======================== group / owning_group / reorder_group out-of-line 定义 ========================

template <typename First, typename... Rest>
inline group<First, Rest...>::group(manager* mgr, std::array<single_class_set*, N> sets) noexcept
    : mgr_(mgr), sets_(sets)
{
    find_smallest();
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

    for (size_t i = 0; i < n; ++i)
    {
        entity e(indices[i], primary->get_version_unchecked(indices[i]));
        if ((mgr_->get_entity_mask(e) & required_mask_) == required_mask_)
            cached_.emplace_back(static_cast<uint32_t>(i));
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
    s->owned_size = write;

    for (size_t i = 0; i < N; ++i)
    {
        if (sets_[i]) s->cached_versions[i] = sets_[i]->get_pool_version();
    }
}

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
    uint64_t mask = mgr_->get_entity_manager().get_mask(e.parts_.index_);
    if ((mask & query_.req_mask_) != query_.req_mask_) return false;
    if (query_.exc_mask_ != 0 && (mask & query_.exc_mask_) != 0) return false;
    return true;
}

inline entity runtime_view::get_first_entity() noexcept
{
    ensure_fresh();
    if (query_.primary_set_ == nullptr || !all_sets_valid()) [[unlikely]] return entity{};
    auto* primary = query_.primary_set_;
    auto& indices = primary->get_entity_indices();
    auto* sparse_combined = primary->get_sparse_combined().data();
    auto& em = mgr_->get_entity_manager();
    for (size_t i = 0; i < indices.size(); ++i)
    {
        uint32_t idx = indices[i];
        uint64_t mask = em.get_mask(idx);
        if ((mask & query_.req_mask_) != query_.req_mask_) continue;
        if (query_.exc_mask_ != 0 && (mask & query_.exc_mask_) != 0) continue;
        return entity(idx, static_cast<uint32_t>(sparse_combined[idx]));
    }
    return entity{};
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
    auto* sparse_combined = primary->get_sparse_combined().data();
    const size_t n = indices.size();
    auto& em = mgr_->get_entity_manager();

    for (size_t i = 0; i < n; ++i)
    {
        if (i + 32 < n) [[likely]]
            PREFETCH_R(&sparse_combined[indices[i + 32]]);
        uint32_t idx = indices[i];
        uint64_t mask = em.get_mask(idx);

        if ((mask & query_.req_mask_) != query_.req_mask_)
        {
            continue;
        }
        if (query_.exc_mask_ != 0 && (mask & query_.exc_mask_) != 0)
        {
            continue;
        }

        entity e(idx, static_cast<uint32_t>(sparse_combined[idx]));

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