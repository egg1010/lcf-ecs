#pragma once
#include <span>
#include <new>
#include <cassert>
#include <cstdint>
#include <cstring>
#include "operating_message.hpp"
#include "entity.hpp"
#include "class_pool.hpp"
#include "type_id.hpp"

namespace ecs
{
class manager;
}

class single_class_set
{
private:
    static constexpr uint64_t sparse_invalid_sentinel = 0xFFFFFFFF00000000ULL;
    class_pool<uint64_t> sparse_combined_;
    class_pool<uint32_t> dense_;
    int type_id_{-1};
    operating_message message;

    void* typed_pool_{nullptr};
    size_t pending_increase_capacity_{0};
    size_t component_size_{0};

    struct pool_ops {
        void (*destroy_pool)(void* pool) noexcept;
        void (*swap_pop)(void* pool, size_t index) noexcept;
        void (*clear_pool)(void* pool) noexcept;
        void (*increase_capacity_pool)(void* pool, size_t cap) noexcept;
        void (*swap_pool)(void* pool, size_t i, size_t j) noexcept;
    } ops_{};

    uint64_t version_{0};
    struct change_tracking_entry
    {
        uint64_t change_version;
        uint64_t added_version;
    };
    class_pool<change_tracking_entry> entity_change_tracking_;
    uint64_t global_change_counter_{0};
    uint64_t global_added_counter_{0};
    bool track_changes_enabled_{true};

    void (*on_add_)(entity, void* component, void* user_data) noexcept = nullptr;
    void* on_add_data_{nullptr};
    void (*on_remove_)(entity, void* component, void* user_data) noexcept = nullptr;
    void* on_remove_data_{nullptr};

    friend class ecs::manager;

    template <typename T>
    void init_typed_storage()
    {
        if (typed_pool_) [[unlikely]] return;
        auto* pool = new (std::nothrow) class_pool<T>();
        if (!pool) [[unlikely]] std::terminate();
        component_size_ = sizeof(T);
        if (pending_increase_capacity_ > 0)
        {
            pool->increase_capacity(pending_increase_capacity_);
            sparse_combined_.increase_capacity(pending_increase_capacity_);
            dense_.increase_capacity(pending_increase_capacity_);
            entity_change_tracking_.increase_capacity(pending_increase_capacity_);
            pending_increase_capacity_ = 0;
        }
        typed_pool_ = pool;
        ops_ = {
            /*.destroy_pool =*/[](void* p) noexcept { delete static_cast<class_pool<T>*>(p); },
            /*.swap_pop =*/[](void* p, size_t index) noexcept {
                auto* pool = static_cast<class_pool<T>*>(p);
                const size_t last = pool->size() - 1;
                if (index != last) [[likely]]
                {
                    if constexpr (std::is_trivially_copyable_v<T>)
                    {
                        std::memcpy(&(*pool)[index], &(*pool)[last], sizeof(T));
                    }
                    else
                    {
                        (*pool)[index].~T();
                        new (&(*pool)[index]) T(std::move((*pool)[last]));
                    }
                }
                pool->pop_back();
            },
            /*.clear_pool =*/[](void* p) noexcept { static_cast<class_pool<T>*>(p)->clear(); },
            /*.increase_capacity_pool =*/[](void* p, size_t cap) noexcept { static_cast<class_pool<T>*>(p)->increase_capacity(cap); },
            /*.swap_pool =*/[](void* p, size_t i, size_t j) noexcept {
                auto* pool = static_cast<class_pool<T>*>(p);
                std::swap((*pool)[i], (*pool)[j]);
            },
        };
    }

    template <typename T>
    [[nodiscard]] class_pool<T>* get_typed_pool() noexcept
    {
        assert(typed_pool_ != nullptr && type_id_ == type_id::get_type_id<T>()
            && "get_typed_pool<T>(): type mismatch or pool not initialized");
        return static_cast<class_pool<T>*>(typed_pool_);
    }

    template <typename T>
    [[nodiscard]] const class_pool<T>* get_typed_pool() const noexcept
    {
        assert(typed_pool_ != nullptr && type_id_ == type_id::get_type_id<T>()
            && "get_typed_pool<T>(): type mismatch or pool not initialized");
        return static_cast<const class_pool<T>*>(typed_pool_);
    }

    [[nodiscard]] uint32_t sparse_version_at(uint32_t idx) const noexcept
    {
        return static_cast<uint32_t>(sparse_combined_[idx]);
    }

    [[nodiscard]] uint32_t sparse_dense_at(uint32_t idx) const noexcept
    {
        return static_cast<uint32_t>(sparse_combined_[idx] >> 32);
    }

    void sparse_set(uint32_t idx, uint32_t version, uint32_t dense) noexcept
    {
        sparse_combined_[idx] = (static_cast<uint64_t>(dense) << 32) | version;
    }

    template <typename T>
    [[nodiscard]] T* fast_get_ptr_by_index(size_t index) noexcept
    {
        if (index >= get_typed_pool<T>()->size()) [[unlikely]] return nullptr;
        return &(*get_typed_pool<T>())[index];
    }

    template <typename T>
    [[nodiscard]] const T* fast_get_ptr_by_index(size_t index) const noexcept
    {
        if (index >= get_typed_pool<T>()->size()) [[unlikely]] return nullptr;
        return &(*get_typed_pool<T>())[index];
    }

    template <typename T>
    [[nodiscard]] T* get_ptr_unchecked_by_index(size_t index) noexcept
    {
        return &(*get_typed_pool<T>())[index];
    }

    template <typename T>
    [[nodiscard]] const T* get_ptr_unchecked_by_index(size_t index) const noexcept
    {
        return &(*get_typed_pool<T>())[index];
    }

    template <typename T, typename Entities, typename F>
    operating_message add_batch_impl(Entities& entities, size_t count, F&& get_component) noexcept
    {
        using DT = std::decay_t<T>;
        const int tid = type_id::get_type_id<DT>();
        if (type_id_ == -1) [[unlikely]]
        {
            type_id_ = tid;
            init_typed_storage<DT>();
        }

        size_t max_index = 0;
        bool all_new = true;
        for (size_t i = 0; i < count; ++i)
        {
            const entity& e = entities[i];
            if (!e.is_valid()) [[unlikely]]
            {
                message.write_message(false, "single_class_set::add_batch(): invalid entity index ", std::to_string(e.parts_.index_));
                return message;
            }
            if (e.parts_.index_ > max_index) max_index = e.parts_.index_;
            if (all_new && e.parts_.index_ < sparse_combined_.size() && sparse_version_at(e.parts_.index_) == e.parts_.version_)
                all_new = false;
        }

        if (max_index >= sparse_combined_.capacity()) [[unlikely]]
            sparse_combined_.increase_capacity(max_index + 1);

        size_t old_size = sparse_combined_.size();
        for (size_t i = old_size; i <= max_index; ++i)
        {
            sparse_combined_.emplace_at(i, sparse_invalid_sentinel);
        }

        auto* pool = get_typed_pool<DT>();

        if (all_new) [[likely]]
        {
            size_t dense_start = dense_.size();
            dense_.increase_capacity(dense_start + count);
            pool->increase_capacity(pool->size() + count);

            dense_.append_indices_from(entities.data(), count);

            using component_return_t = decltype(get_component(0));
            if constexpr (std::is_lvalue_reference_v<component_return_t>)
            {
                for (size_t i = 0; i < count; ++i)
                {
                    PREFETCH_R(&entities[i + 16]);
                    uint32_t idx = entities[i].parts_.index_;
                    sparse_set(idx, entities[i].parts_.version_, static_cast<uint32_t>(dense_start + i));
                }
                pool->append_bulk(&get_component(0), count);
            }
            else
            {
                class_pool<DT> temp_components;
                temp_components.increase_capacity(count);
                for (size_t i = 0; i < count; ++i)
                {
                    PREFETCH_R(&entities[i + 16]);
                    uint32_t idx = entities[i].parts_.index_;
                    sparse_set(idx, entities[i].parts_.version_, static_cast<uint32_t>(dense_start + i));
                    temp_components.push_back_unchecked(get_component(i));
                }
                pool->append_bulk_move(temp_components.data(), count);
            }

            entity_change_tracking_.append_generated(count, [this]() noexcept {
                return change_tracking_entry{++global_change_counter_, ++global_added_counter_};
            });
        }
        else
        {
            class_pool<uint32_t> new_positions;
            class_pool<uint32_t> exist_positions;
            new_positions.increase_capacity(count);
            exist_positions.increase_capacity(count);
            for (size_t i = 0; i < count; ++i)
            {
                const entity& e = entities[i];
                uint32_t ver = sparse_version_at(e.parts_.index_);
                if (ver == e.parts_.version_)
                    exist_positions.push_back_unchecked(static_cast<uint32_t>(i));
                else
                    new_positions.push_back_unchecked(static_cast<uint32_t>(i));
            }

            size_t new_count = new_positions.size();
            size_t exist_count = exist_positions.size();

            if (new_count > 0)
            {
                size_t dense_old = dense_.size();
                dense_.increase_capacity(dense_old + new_count);
                pool->increase_capacity(pool->size() + new_count);

                class_pool<uint32_t> new_entity_indices;
                class_pool<DT> new_components;
                new_entity_indices.increase_capacity(new_count);
                new_components.increase_capacity(new_count);
                for (size_t j = 0; j < new_count; ++j)
                {
                    size_t i = new_positions[j];
                    new_entity_indices.push_back_unchecked(entities[i].parts_.index_);
                    new_components.push_back_unchecked(get_component(i));
                }
                dense_.append_bulk(new_entity_indices.data(), new_count);
                using component_return_t = decltype(get_component(0));
                if constexpr (std::is_rvalue_reference_v<component_return_t>)
                    pool->append_bulk_move(new_components.data(), new_count);
                else
                    pool->append_bulk(new_components.data(), new_count);

                for (size_t j = 0; j < new_count; ++j)
                {
                    size_t i = new_positions[j];
                    size_t idx = entities[i].parts_.index_;
                    sparse_set(static_cast<uint32_t>(idx), entities[i].parts_.version_, static_cast<uint32_t>(dense_old + j));
                }

                entity_change_tracking_.append_generated(new_count, [this]() noexcept {
                    return change_tracking_entry{++global_change_counter_, ++global_added_counter_};
                });
            }

            for (size_t j = 0; j < exist_count; ++j)
            {
                size_t i = exist_positions[j];
                const entity& e = entities[i];
                uint32_t dense_idx = sparse_dense_at(e.parts_.index_);
                (*pool)[dense_idx].~DT();
                new (&(*pool)[dense_idx]) DT(get_component(i));
                entity_change_tracking_.sparse_emplace_at(dense_idx, change_tracking_entry{++global_change_counter_, entity_change_tracking_[dense_idx].added_version});
            }
        }

        ++version_;
        return message;
    }

public:
    void clear() noexcept
    {
        sparse_combined_.clear();
        dense_.clear();
        entity_change_tracking_.clear();
        if (typed_pool_ && ops_.clear_pool) ops_.clear_pool(typed_pool_);
    }

    single_class_set() noexcept = default;

    explicit single_class_set(size_t capacity) noexcept
    {
        increase_capacity(capacity);
    }

    template <typename T>
    single_class_set(entity e, T&& object, size_t r_size = 1024) noexcept
    {
        increase_capacity(r_size);
        add(e, std::forward<T>(object));
    }

    template <typename T>
    operating_message& add(entity e, T&& object) noexcept
    {
        using DT = std::decay_t<T>;
        const int tid = type_id::get_type_id<DT>();
        if (type_id_ == -1) [[unlikely]]
        {
            type_id_ = tid;
            init_typed_storage<DT>();
        }
        else if (type_id_ != tid) [[unlikely]]
        {
            message.write_message(false, "single_class_set::add(): type mismatch");
            return message;
        }

        if (!e.is_valid()) [[unlikely]]
        {
            message.write_message(false, "single_class_set::add(): ID is invalid, index=", std::to_string(e.parts_.index_));
            return message;
        }

        auto* pool = get_typed_pool<DT>();
        size_t old_size = sparse_combined_.size();

        if (e.parts_.index_ == old_size) [[likely]]
        {
            uint32_t dense_idx = static_cast<uint32_t>(dense_.size());
            uint64_t combined = (static_cast<uint64_t>(dense_idx) << 32) | e.parts_.version_;
            sparse_combined_.emplace_back_unchecked(combined);
            dense_.emplace_back_unchecked(e.parts_.index_);
            pool->emplace_back_unchecked(std::forward<T>(object));
            ++version_;
            if (track_changes_enabled_) [[likely]] {
                entity_change_tracking_.emplace_back_unchecked(
                    change_tracking_entry{++global_change_counter_, ++global_added_counter_});
            }
            if (on_add_) [[unlikely]] on_add_(e, &(*pool)[dense_idx], on_add_data_);
            return message;
        }

        uint64_t& combined = sparse_combined_.emplace_at(e.parts_.index_);
        uint32_t ver = static_cast<uint32_t>(combined);
        uint32_t dense_idx = static_cast<uint32_t>(combined >> 32);

        for (size_t i = old_size; i < e.parts_.index_; ++i)
        {
            sparse_combined_.emplace_at(i, sparse_invalid_sentinel);
        }

        bool is_new_add = (ver != e.parts_.version_);

        if (ver == e.parts_.version_) [[likely]]
        {
            (*pool)[dense_idx].~DT();
            new (&(*pool)[dense_idx]) DT(std::forward<T>(object));
        }
        else
        {
            dense_.emplace_back(e.parts_.index_);
            dense_idx = static_cast<uint32_t>(dense_.size() - 1);
            ver = e.parts_.version_;
            combined = (static_cast<uint64_t>(dense_idx) << 32) | ver;
            pool->emplace_back(std::forward<T>(object));
        }
        ++version_;
        if (track_changes_enabled_) [[unlikely]] {
            uint64_t preserved_added = (dense_idx < entity_change_tracking_.size())
                ? entity_change_tracking_[dense_idx].added_version : 0;
            uint64_t new_added = is_new_add ? ++global_added_counter_ : preserved_added;
            entity_change_tracking_.sparse_emplace_at(dense_idx,
                change_tracking_entry{++global_change_counter_, new_added});
        }
        if (on_add_) [[unlikely]] on_add_(e, &(*pool)[dense_idx], on_add_data_);
        return message;       
    }
    
    template <typename T>
    operating_message add_batch(std::span<const entity> entities, std::span<const T> components) noexcept
    {
        if (entities.size() != components.size()) [[unlikely]]
        {
            message.write_message(false, "single_class_set::add_batch(): entities and components size mismatch");
            return message;
        }
        return add_batch_impl<T>(entities, entities.size(),
            [&components](size_t i) -> const T& { return components[i]; });
    }
    
    template <typename T>
    operating_message add_batch(const class_pool<entity>& entities, const class_pool<T>& components) noexcept
    {
        if (entities.size() != components.size()) [[unlikely]]
        {
            message.write_message(false, "single_class_set::add_batch(): entities and components size mismatch");
            return message;
        }
        return add_batch_impl<T>(entities, entities.size(),
            [&components](size_t i) -> const T& { return components[i]; });
    }

    template <typename T>
    operating_message add_batch(class_pool<entity>&& entities, class_pool<T>&& components) noexcept
    {
        if (entities.size() != components.size()) [[unlikely]]
        {
            message.write_message(false, "single_class_set::add_batch(): entities and components size mismatch");
            return message;
        }
        return add_batch_impl<T>(entities, entities.size(),
            [&components](size_t i) -> T&& { return std::move(components[i]); });
    }
    
    template <typename T>
    [[nodiscard]] T* get_ptr(entity e) noexcept
    {
        if (!e.is_valid() || type_id_ != type_id::get_type_id<T>() ||
            e.parts_.index_ >= sparse_combined_.size()) [[unlikely]]
        {
            return nullptr;
        }
        const uint64_t combined = sparse_combined_[e.parts_.index_];
        if (static_cast<uint32_t>(combined) != e.parts_.version_) [[unlikely]]
            return nullptr;
        return &(*get_typed_pool<T>())[static_cast<uint32_t>(combined >> 32)];
    }

    template <typename T>
    [[nodiscard]] const T* get_ptr(entity e) const noexcept
    {
        if (!e.is_valid() || type_id_ != type_id::get_type_id<T>() ||
            e.parts_.index_ >= sparse_combined_.size()) [[unlikely]]
            return nullptr;
        const uint64_t combined = sparse_combined_[e.parts_.index_];
        if (static_cast<uint32_t>(combined) != e.parts_.version_) [[unlikely]]
            return nullptr;
        return &(*get_typed_pool<T>())[static_cast<uint32_t>(combined >> 32)];
    }

    template <typename T>
    [[nodiscard]] T* get_ptr_fast(entity e) noexcept
    {
        if (e.parts_.index_ >= sparse_combined_.size()) [[unlikely]]
            return nullptr;
        const uint64_t combined = sparse_combined_[e.parts_.index_];
        if (static_cast<uint32_t>(combined) != e.parts_.version_) [[unlikely]]
            return nullptr;
        return &(*get_typed_pool<T>())[static_cast<uint32_t>(combined >> 32)];
    }

    template <typename T>
    [[nodiscard]] const T* get_ptr_fast(entity e) const noexcept
    {
        if (e.parts_.index_ >= sparse_combined_.size()) [[unlikely]]
            return nullptr;
        const uint64_t combined = sparse_combined_[e.parts_.index_];
        if (static_cast<uint32_t>(combined) != e.parts_.version_) [[unlikely]]
            return nullptr;
        return &(*get_typed_pool<T>())[static_cast<uint32_t>(combined >> 32)];
    }

    template <typename T>
    [[nodiscard]] T* get_ptr_raw(entity e) noexcept
    {
        return &(*get_typed_pool<T>())[sparse_dense_at(e.parts_.index_)];
    }

    template <typename T>
    [[nodiscard]] const T* get_ptr_raw(entity e) const noexcept
    {
        return &(*get_typed_pool<T>())[sparse_dense_at(e.parts_.index_)];
    }

    void prefetch_component(uint32_t entity_index) const noexcept
    {
        PREFETCH_R(&sparse_combined_[entity_index]);
    }

    void prefetch_ptr(entity e) const noexcept
    {
        PREFETCH_R(&sparse_combined_[e.parts_.index_]);
    }

    void prefetch_ptr_batch(const entity* entities, size_t count) const noexcept
    {
        for (size_t i = 0; i < count; ++i)
            PREFETCH_R(&sparse_combined_[entities[i].parts_.index_]);
    }

    template <typename T>
    void prefetch_ptr_data(entity e) const noexcept
    {
        if (e.parts_.index_ < sparse_combined_.size()) [[likely]]
        {
            const uint64_t combined = sparse_combined_[e.parts_.index_];
            const uint32_t dense = static_cast<uint32_t>(combined >> 32);
            PREFETCH_R(&(*get_typed_pool<T>())[dense]);
        }
    }

    template <typename T>
    void get_ptr_batch(const entity* entities, T** results, size_t count) noexcept
    {
        if (type_id_ != type_id::get_type_id<T>()) [[unlikely]]
        {
            for (size_t i = 0; i < count; ++i) results[i] = nullptr;
            return;
        }
        auto* pool = get_typed_pool<T>();
        size_t sparse_sz = sparse_combined_.size();

        constexpr size_t chunk = 16;
        uint32_t dense_buf[chunk];

        for (size_t base = 0; base < count; base += chunk)
        {
            size_t n = base + chunk <= count ? chunk : count - base;

            for (size_t j = 0; j < n; ++j)
            {
                size_t i = base + j;
                if (i + 8 < count) [[likely]]
                    PREFETCH_R(&sparse_combined_[entities[i + 8].parts_.index_]);

                const entity& e = entities[i];
                if (!e.is_valid() || e.parts_.index_ >= sparse_sz) [[unlikely]]
                {
                    dense_buf[j] = UINT32_MAX;
                    continue;
                }
                uint64_t combined = sparse_combined_[e.parts_.index_];
                if (static_cast<uint32_t>(combined) != e.parts_.version_) [[unlikely]]
                {
                    dense_buf[j] = UINT32_MAX;
                    continue;
                }
                uint32_t dense = static_cast<uint32_t>(combined >> 32);
                dense_buf[j] = dense;
                PREFETCH_R(&(*pool)[dense]);
            }

            for (size_t j = 0; j < n; ++j)
            {
                uint32_t dense = dense_buf[j];
                if (dense == UINT32_MAX) [[unlikely]]
                {
                    results[base + j] = nullptr;
                    continue;
                }
                results[base + j] = &(*pool)[dense];
            }
        }
    }

    [[nodiscard]] uint32_t get_version(uint32_t entity_index) const noexcept
    {
        if (entity_index >= sparse_combined_.size()) [[unlikely]] return 0;
        return sparse_version_at(entity_index);
    }

    [[nodiscard]] uint32_t get_version_unchecked(uint32_t entity_index) const noexcept
    {
        return sparse_version_at(entity_index);
    }

    [[nodiscard]] uint64_t get_pool_version() const noexcept
    {
        return version_;
    }

    [[nodiscard]] uint64_t get_entity_change_version(size_t dense_index) const noexcept
    {
        if (dense_index >= entity_change_tracking_.size()) [[unlikely]] return 0;
        return entity_change_tracking_[dense_index].change_version;
    }

    [[nodiscard]] uint64_t get_entity_added_version(size_t dense_index) const noexcept
    {
        if (dense_index >= entity_change_tracking_.size()) [[unlikely]] return 0;
        return entity_change_tracking_[dense_index].added_version;
    }

    [[nodiscard]] uint64_t get_global_added_counter() const noexcept
    {
        return global_added_counter_;
    }

    [[nodiscard]] uint64_t get_global_change_counter() const noexcept
    {
        return global_change_counter_;
    }

    operating_message hard_remove(entity e) noexcept
    {
        if (!e.is_valid() || e.parts_.index_ >= sparse_combined_.size() || sparse_version_at(e.parts_.index_) != e.parts_.version_) [[unlikely]]
        {
            message.write_message(false, "single_class_set::hard_remove(): invalid entity or version mismatch, index=", std::to_string(e.parts_.index_));
            return message;
        }

        auto index = sparse_dense_at(e.parts_.index_);

        void* comp_ptr = typed_pool_ ? static_cast<char*>(typed_pool_) + index * component_size_ : nullptr;
        if (on_remove_ && comp_ptr) [[unlikely]] on_remove_(e, comp_ptr, on_remove_data_);

        auto moved_entity_id = dense_.back();
        dense_[index] = dense_.back();
        if (moved_entity_id != e.parts_.index_) [[likely]]
        {
            sparse_set(moved_entity_id, sparse_version_at(moved_entity_id), static_cast<uint32_t>(index));
        }
        dense_.pop_back();

        if (index < entity_change_tracking_.size() && entity_change_tracking_.size() > 0)
        {
            if (index < entity_change_tracking_.size() - 1)
            {
                entity_change_tracking_[index] = entity_change_tracking_.back();
            }
            entity_change_tracking_.pop_back();
        }

        if (typed_pool_ && ops_.swap_pop) ops_.swap_pop(typed_pool_, index);

        sparse_set(e.parts_.index_, 0, 0);
        ++version_;
        return message;
    }

    operating_message soft_remove(entity e) noexcept
    {
        if (!e.is_valid() || e.parts_.index_ >= sparse_combined_.size() || sparse_version_at(e.parts_.index_) != e.parts_.version_) [[unlikely]]
        {
            message.write_message(false, "single_class_set::soft_remove(): invalid entity or version mismatch, index=", std::to_string(e.parts_.index_));
            return message;
        }

        sparse_set(e.parts_.index_, 0, 0);
        ++version_;
        return message;
    }

    [[nodiscard]] int& get_type_id() noexcept
    {
        return type_id_;
    }

    template <typename T>
    [[nodiscard]] class_pool<T>* get_typed_pool_ptr() noexcept
    {
        if (type_id_ != type_id::get_type_id<T>()) [[unlikely]] return nullptr;
        return static_cast<class_pool<T>*>(typed_pool_);
    }

    template <typename T>
    [[nodiscard]] const class_pool<T>* get_typed_pool_ptr() const noexcept
    {
        if (type_id_ != type_id::get_type_id<T>()) [[unlikely]] return nullptr;
        return static_cast<const class_pool<T>*>(typed_pool_);
    }

    single_class_set(single_class_set&& other) noexcept
    : sparse_combined_(std::move(other.sparse_combined_))
    , dense_(std::move(other.dense_))
    , type_id_(other.type_id_)
    , message(std::move(other.message))
    , typed_pool_(other.typed_pool_)
    , pending_increase_capacity_(other.pending_increase_capacity_)
    , component_size_(other.component_size_)
    , ops_(other.ops_)
    , version_(other.version_)
    , entity_change_tracking_(std::move(other.entity_change_tracking_))
    , global_change_counter_(other.global_change_counter_)
    , global_added_counter_(other.global_added_counter_)
    , track_changes_enabled_(other.track_changes_enabled_)
    , on_add_(other.on_add_)
    , on_add_data_(other.on_add_data_)
    , on_remove_(other.on_remove_)
    , on_remove_data_(other.on_remove_data_)
    {
        other.typed_pool_ = nullptr;
        other.ops_ = {};
        other.type_id_ = -1;
        other.pending_increase_capacity_ = 0;
        other.component_size_ = 0;
        other.version_ = 0;
        other.global_change_counter_ = 0;
        other.global_added_counter_ = 0;
        other.on_add_ = nullptr;
        other.on_add_data_ = nullptr;
        other.on_remove_ = nullptr;
        other.on_remove_data_ = nullptr;
    }

    single_class_set& operator=(single_class_set&& other) noexcept
    {
        if (this != &other) [[likely]]
        {
            if (typed_pool_ && ops_.destroy_pool) ops_.destroy_pool(typed_pool_);

            sparse_combined_ = std::move(other.sparse_combined_);
            dense_ = std::move(other.dense_);
            typed_pool_ = other.typed_pool_;
            ops_ = other.ops_;
            pending_increase_capacity_ = other.pending_increase_capacity_;
            component_size_ = other.component_size_;
            message = std::move(other.message);
            type_id_ = other.type_id_;
            version_ = other.version_;
            entity_change_tracking_ = std::move(other.entity_change_tracking_);
            global_change_counter_ = other.global_change_counter_;
            global_added_counter_ = other.global_added_counter_;
            track_changes_enabled_ = other.track_changes_enabled_;
            on_add_ = other.on_add_;
            on_add_data_ = other.on_add_data_;
            on_remove_ = other.on_remove_;
            on_remove_data_ = other.on_remove_data_;

            other.typed_pool_ = nullptr;
            other.ops_ = {};
            other.type_id_ = -1;
            other.pending_increase_capacity_ = 0;
            other.component_size_ = 0;
            other.version_ = 0;
            other.global_change_counter_ = 0;
            other.global_added_counter_ = 0;
            other.on_add_ = nullptr;
            other.on_add_data_ = nullptr;
            other.on_remove_ = nullptr;
            other.on_remove_data_ = nullptr;
        }
        return *this;
    }
    
    [[nodiscard]] operating_message& get_operating_message() noexcept
    {
        return message;
    }

    single_class_set(const single_class_set&) = delete;
    single_class_set& operator=(const single_class_set&) = delete;

    [[nodiscard]] constexpr size_t size() const noexcept
    {
        return dense_.size();
    }

    [[nodiscard]] constexpr bool empty() const noexcept
    {
        return dense_.empty();
    }
    
    void increase_capacity(size_t capacity) noexcept
    {
        sparse_combined_.increase_capacity(capacity);
        dense_.increase_capacity(capacity);
        entity_change_tracking_.increase_capacity(capacity);
        if (typed_pool_ && ops_.increase_capacity_pool)
        {
            ops_.increase_capacity_pool(typed_pool_, capacity);
        }
        else
        {
            if (capacity > pending_increase_capacity_) pending_increase_capacity_ = capacity;
        }
    }

    [[nodiscard]] class_pool<uint32_t>& get_entity_indices() noexcept
    {
        return dense_;
    }

    [[nodiscard]] const class_pool<uint32_t>& get_entity_indices() const noexcept
    {
        return dense_;
    }

    [[nodiscard]] const class_pool<uint64_t>& get_sparse_combined() const noexcept
    {
        return sparse_combined_;
    }

    [[nodiscard]] uint32_t get_dense_at(uint32_t entity_index) const noexcept
    {
        return sparse_dense_at(entity_index);
    }

    void swap_dense_and_pool(size_t i, size_t j) noexcept
    {
        if (i == j) [[unlikely]] return;
        uint32_t tmp = dense_[i];
        dense_[i] = dense_[j];
        dense_[j] = tmp;
        sparse_set(dense_[i], sparse_version_at(dense_[i]), static_cast<uint32_t>(i));
        sparse_set(dense_[j], sparse_version_at(dense_[j]), static_cast<uint32_t>(j));
        if (i < entity_change_tracking_.size() && j < entity_change_tracking_.size())
        {
            change_tracking_entry tmp = entity_change_tracking_[i];
            entity_change_tracking_[i] = entity_change_tracking_[j];
            entity_change_tracking_[j] = tmp;
        }
        if (typed_pool_ && ops_.swap_pool) [[likely]]
        {
            ops_.swap_pool(typed_pool_, i, j);
        }
    }

    template <typename T>
    void reorder_dense_by_indices(const class_pool<size_t>& sorted_indices) noexcept
    {
        const size_t n = dense_.size();
        if (n <= 1 || sorted_indices.size() < n) [[unlikely]] return;

        class_pool<uint32_t> new_dense;
        new_dense.increase_capacity(n);
        for (size_t i = 0; i < n; ++i)
            new_dense.emplace_back(dense_[sorted_indices[i]]);

        class_pool<T>* typed_pool = get_typed_pool_ptr<T>();
        if (typed_pool)
        {
            class_pool<T> new_pool;
            new_pool.increase_capacity(n);
            for (size_t i = 0; i < n; ++i)
                new_pool.emplace_back(std::move((*typed_pool)[sorted_indices[i]]));
            *typed_pool = std::move(new_pool);
        }

        class_pool<change_tracking_entry> new_tracking;
        if (entity_change_tracking_.size() >= n)
        {
            new_tracking.increase_capacity(n);
            for (size_t i = 0; i < n; ++i)
            {
                new_tracking.emplace_back(entity_change_tracking_[sorted_indices[i]]);
            }
            entity_change_tracking_ = std::move(new_tracking);
        }

        for (size_t i = 0; i < n; ++i)
            sparse_set(new_dense[i], sparse_version_at(new_dense[i]), static_cast<uint32_t>(i));

        dense_ = std::move(new_dense);
        ++version_;
    }

    ~single_class_set() noexcept
    {
        if (typed_pool_ && ops_.destroy_pool) ops_.destroy_pool(typed_pool_);
    }
};