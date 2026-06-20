#pragma once
#include <span>
#include <new>
#include <cassert>
#include "operating_message.hpp"
#include "entity.hpp"
#include "class_pool.hpp"
#include "type_id.hpp"

namespace ecs
{
class manager;
}


struct sparse_entry 
{
    constexpr sparse_entry() noexcept : dense_index_(0), version_(0) {}

    uint32_t dense_index_{0};   
    uint32_t version_{0};      
    [[nodiscard]] constexpr bool is_valid() const noexcept { return version_ != 0; }
};

class single_class_set
{
private:
    class_pool<sparse_entry> sparse_;
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
                    (*pool)[index].~T();
                    new (&(*pool)[index]) T(std::move((*pool)[last]));
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
        for (size_t i = 0; i < count; ++i)
        {
            const entity& e = entities[i];
            if (!e.is_valid()) [[unlikely]]
            {
                message.write_message(false, "single_class_set::add_batch(): invalid entity index ", std::to_string(e.parts_.index_));
                return message;
            }
            if (e.parts_.index_ > max_index) max_index = e.parts_.index_;
        }

        if (max_index >= sparse_.capacity()) [[unlikely]]
            sparse_.increase_capacity(max_index + 1);

        auto* pool = get_typed_pool<DT>();
        dense_.increase_capacity(dense_.size() + count);
        pool->increase_capacity(pool->size() + count);

        for (size_t i = 0; i < count; ++i)
        {
            const entity& e = entities[i];
            sparse_entry& entry = sparse_.emplace_at(e.parts_.index_);

            if (entry.version_ == e.parts_.version_)
            {
                (*pool)[entry.dense_index_].~DT();
                new (&(*pool)[entry.dense_index_]) DT(get_component(i));
            }
            else
            {
                dense_.emplace_back(e.parts_.index_);
                entry.dense_index_ = static_cast<uint32_t>(dense_.size() - 1);
                entry.version_ = e.parts_.version_;
                pool->emplace_back(get_component(i));
            }
        }

        return message;
    }

public:
    void clear() noexcept
    {
        sparse_.clear();
        dense_.clear();
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
    operating_message add(entity e, T&& object) noexcept
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
        sparse_entry& entry = sparse_.emplace_at(e.parts_.index_);

        if (entry.version_ == e.parts_.version_) [[likely]]
        {
            (*pool)[entry.dense_index_].~DT();
            new (&(*pool)[entry.dense_index_]) DT(std::forward<T>(object));
        }
        else
        {
            dense_.emplace_back(e.parts_.index_);
            entry.dense_index_ = static_cast<uint32_t>(dense_.size() - 1);
            entry.version_ = e.parts_.version_;
            pool->emplace_back(std::forward<T>(object));
        }
        ++version_;
        if (on_add_) [[unlikely]] on_add_(e, &(*pool)[entry.dense_index_], on_add_data_);
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
            e.parts_.index_ >= sparse_.size() || sparse_[e.parts_.index_].version_ != e.parts_.version_) [[unlikely]]
        {
            return nullptr;
        }
        return &(*get_typed_pool<T>())[sparse_[e.parts_.index_].dense_index_];
    }

    template <typename T>
    [[nodiscard]] const T* get_ptr(entity e) const noexcept
    {
        if (!e.is_valid() || type_id_ != type_id::get_type_id<T>() ||
            e.parts_.index_ >= sparse_.size() || sparse_[e.parts_.index_].version_ != e.parts_.version_) [[unlikely]]
            return nullptr;
        return &(*get_typed_pool<T>())[sparse_[e.parts_.index_].dense_index_];
    }

    [[nodiscard]] uint32_t get_version(uint32_t entity_index) const noexcept
    {
        if (entity_index >= sparse_.size()) [[unlikely]] return 0;
        return sparse_[entity_index].version_;
    }

    [[nodiscard]] uint32_t get_version_unchecked(uint32_t entity_index) const noexcept
    {
        return sparse_[entity_index].version_;
    }

    [[nodiscard]] uint64_t get_pool_version() const noexcept
    {
        return version_;
    }

    template <typename T>
    [[nodiscard]] T* get_ptr_fast(entity e) noexcept
    {
        if (e.parts_.index_ >= sparse_.size() || sparse_[e.parts_.index_].version_ != e.parts_.version_) [[unlikely]]
            return nullptr;
        return &(*get_typed_pool<T>())[sparse_[e.parts_.index_].dense_index_];
    }

    template <typename T>
    [[nodiscard]] const T* get_ptr_fast(entity e) const noexcept
    {
        if (e.parts_.index_ >= sparse_.size() || sparse_[e.parts_.index_].version_ != e.parts_.version_) [[unlikely]]
            return nullptr;
        return &(*get_typed_pool<T>())[sparse_[e.parts_.index_].dense_index_];
    }

    operating_message hard_remove(entity e) noexcept
    {
        if (!e.is_valid() || e.parts_.index_ >= sparse_.size() || sparse_[e.parts_.index_].version_ != e.parts_.version_) [[unlikely]]
        {
            message.write_message(false, "single_class_set::hard_remove(): invalid entity or version mismatch, index=", std::to_string(e.parts_.index_));
            return message;
        }

        auto index = sparse_[e.parts_.index_].dense_index_;

        void* comp_ptr = typed_pool_ ? static_cast<char*>(typed_pool_) + index * component_size_ : nullptr;
        if (on_remove_ && comp_ptr) [[unlikely]] on_remove_(e, comp_ptr, on_remove_data_);

        auto moved_entity_id = dense_.back();
        dense_[index] = dense_.back();
        if (moved_entity_id != e.parts_.index_) [[likely]]
        {
            sparse_[moved_entity_id].dense_index_ = index;
        }
        dense_.pop_back();

        if (typed_pool_ && ops_.swap_pop) ops_.swap_pop(typed_pool_, index);

        sparse_[e.parts_.index_] = sparse_entry{};
        ++version_;
        return message;
    }

    operating_message soft_remove(entity e) noexcept
    {
        if (!e.is_valid() || e.parts_.index_ >= sparse_.size() || sparse_[e.parts_.index_].version_ != e.parts_.version_) [[unlikely]]
        {
            message.write_message(false, "single_class_set::soft_remove(): invalid entity or version mismatch, index=", std::to_string(e.parts_.index_));
            return message;
        }

        sparse_[e.parts_.index_] = sparse_entry{};
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
    : sparse_(std::move(other.sparse_))
    , dense_(std::move(other.dense_))
    , type_id_(other.type_id_)
    , message(std::move(other.message))
    , typed_pool_(other.typed_pool_)
    , pending_increase_capacity_(other.pending_increase_capacity_)
    , component_size_(other.component_size_)
    , ops_(other.ops_)
    , version_(other.version_)
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

            sparse_ = std::move(other.sparse_);
            dense_ = std::move(other.dense_);
            typed_pool_ = other.typed_pool_;
            ops_ = other.ops_;
            pending_increase_capacity_ = other.pending_increase_capacity_;
            component_size_ = other.component_size_;
            message = std::move(other.message);
            type_id_ = other.type_id_;
            version_ = other.version_;
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
        sparse_.increase_capacity(capacity);
        dense_.increase_capacity(capacity);
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

    void swap_dense_and_pool(size_t i, size_t j) noexcept
    {
        if (i == j) [[unlikely]] return;
        uint32_t tmp = dense_[i];
        dense_[i] = dense_[j];
        dense_[j] = tmp;
        sparse_[dense_[i]].dense_index_ = static_cast<uint32_t>(i);
        sparse_[dense_[j]].dense_index_ = static_cast<uint32_t>(j);
        if (typed_pool_ && ops_.swap_pool) [[likely]]
        {
            ops_.swap_pool(typed_pool_, i, j);
        }
    }

    [[nodiscard]] class_pool<sparse_entry>& get_sparse() noexcept
    {
        return sparse_;
    }

    [[nodiscard]] const class_pool<sparse_entry>& get_sparse() const noexcept
    {
        return sparse_;
    }

    ~single_class_set() noexcept
    {
        if (typed_pool_ && ops_.destroy_pool) ops_.destroy_pool(typed_pool_);
    }
};
