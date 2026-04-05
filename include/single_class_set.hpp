#pragma once
#include <vector>
#include <span>
#include "operating_message.hpp"
#include "void_any.hpp"
#include "entity.hpp"
#include "class_pool.hpp"

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
    class_pool<void_any> object_v_;
    int type_id_{-1};
    operating_message message;

    friend class ecs::manager;

    template <typename T>
    [[nodiscard]] T* fast_get_ptr_by_index(size_t index) noexcept
    {
        if (index >= object_v_.size()) [[unlikely]] return nullptr;
        return object_v_[index].template fast_get_ptr<T>();
    }

    template <typename T>
    [[nodiscard]] const T* fast_get_ptr_by_index(size_t index) const noexcept
    {
        if (index >= object_v_.size()) [[unlikely]] return nullptr;
        return object_v_[index].template fast_get_ptr<T>();
    }

    template <typename T>
    [[nodiscard]] T* get_ptr_unchecked_by_index(size_t index) noexcept
    {
        return object_v_[index].template get_ptr_unchecked<T>();
    }

    template <typename T>
    [[nodiscard]] const T* get_ptr_unchecked_by_index(size_t index) const noexcept
    {
        return object_v_[index].template get_ptr_unchecked<T>();
    }

public:
    void clear() noexcept
    {
        sparse_.clear();
        dense_.clear();
        object_v_.clear();
    }
    single_class_set() noexcept
    {
        sparse_.reserve(500*1000); 
        dense_.reserve(500*1000);
        object_v_.reserve(500*1000);
    }
    template <typename T>
    single_class_set(entity e, T&& object, size_t r_size=500*1000) noexcept
    {
        sparse_.reserve(r_size); 
        dense_.reserve(r_size);
        object_v_.reserve(r_size);
        add(e, std::forward<T>(object));
    }
    template <typename T>
    operating_message add(entity e, T&& object) noexcept
    {   
        if(type_id_ == -1) [[unlikely]]
        {
            using DT = std::decay_t<T>;
            type_id_ = type_id::get_type_id<DT>();
        }
        if(!e.is_valid()) [[unlikely]]
        {
            message.write_message(0, "error ", "single_class_set::add():ID is invalid " + std::to_string(e.index_), ";");
            return message;
        }
        
        sparse_entry& entry = sparse_.emplace_at(e.index_);

        if (entry.is_valid() && (entry.version_ == e.version_)) [[likely]]
        {
            uint32_t idx = entry.dense_index_;
            if (idx < object_v_.size()) [[likely]]
            {
                object_v_[idx] = void_any(std::forward<T>(object));
            } 
            else [[unlikely]]
            {
                message.write_message(0, "error ", "single_class_set::add(): Dense index out of range", ";");
                return message;
            }
        } 
        else [[unlikely]]
        {
            dense_.emplace_back(e.index_);
            uint32_t new_dense_index = static_cast<uint32_t>(dense_.size() - 1);
            entry.dense_index_ = new_dense_index;
            entry.version_ = e.version_; 
            object_v_.emplace_back(std::forward<T>(object));
        }
        return message;       
    }
    
    template <typename T>
    operating_message add_batch(std::span<const entity> entities, std::span<const T> components) noexcept
    {
        if (entities.size() != components.size()) [[unlikely]]
        {
            message.write_message(0, "error ", "single_class_set::add_batch(): Entities and components size mismatch", ";");
            return message;
        }
        
        if (type_id_ == -1) [[unlikely]]
        {
            using DT = std::decay_t<T>;
            type_id_ = type_id::get_type_id<DT>();
        }
        
        size_t max_index = 0;
        bool all_new = true;
        const size_t current_dense_size = dense_.size();
        
        for (const auto& e : entities)
        {
            if (!e.is_valid()) [[unlikely]]
            {
                message.write_message(0, "error ", "single_class_set::add_batch(): Invalid entity index " + std::to_string(e.index_), ";");
                return message;
            }
            if (e.index_ < current_dense_size)
            {
                all_new = false;
            }
            if (e.index_ > max_index)
            {
                max_index = e.index_;
            }
        }
        
        if (max_index >= sparse_.capacity()) [[unlikely]]
        {
            sparse_.reserve(max_index + 1);
        }
        
        dense_.reserve(dense_.size() + entities.size());
        object_v_.reserve(object_v_.size() + entities.size());
        
        if (all_new) [[likely]]
        {
            for (size_t i = 0; i < entities.size(); ++i)
            {
                const entity& e = entities[i];
                const T& component = components[i];
                
                sparse_entry& entry = sparse_.emplace_at(e.index_);
                
                dense_.emplace_back(e.index_);
                uint32_t new_dense_index = static_cast<uint32_t>(dense_.size() - 1);
                entry.dense_index_ = new_dense_index;
                entry.version_ = e.version_;
                object_v_.emplace_back(component);
            }
        }
        else
        {
            for (size_t i = 0; i < entities.size(); ++i)
            {
                const entity& e = entities[i];
                const T& component = components[i];
                
                sparse_entry& entry = sparse_.emplace_at(e.index_);
                
                if (entry.is_valid() && (entry.version_ == e.version_))
                {
                    uint32_t idx = entry.dense_index_;
                    if (idx < object_v_.size()) [[likely]]
                    {
                        object_v_[idx] = void_any(component);
                    }
                    else [[unlikely]]
                    {
                        message.write_message(0, "error ", "single_class_set::add_batch(): Dense index out of range", ";");
                        return message;
                    }
                }
                else
                {
                    dense_.emplace_back(e.index_);
                    uint32_t new_dense_index = static_cast<uint32_t>(dense_.size() - 1);
                    entry.dense_index_ = new_dense_index;
                    entry.version_ = e.version_;
                    object_v_.emplace_back(component);
                }
            }
        }
        
        return message;
    }
    
    template <typename T>
    operating_message add_batch(const std::vector<entity>& entities, const std::vector<T>& components) noexcept
    {
        return add_batch(std::span<const entity>(entities), std::span<const T>(components));
    }
    
    template <typename T>
    operating_message add_batch(std::vector<entity>&& entities, std::vector<T>&& components) noexcept
    {
        if (entities.size() != components.size()) [[unlikely]]
        {
            message.write_message(0, "error ", "single_class_set::add_batch(): Entities and components size mismatch", ";");
            return message;
        }
        
        if (type_id_ == -1) [[unlikely]]
        {
            using DT = std::decay_t<T>;
            type_id_ = type_id::get_type_id<DT>();
        }
        
        size_t max_index = 0;
        for (const auto& e : entities)
        {
            if (!e.is_valid()) [[unlikely]]
            {
                message.write_message(0, "error ", "single_class_set::add_batch(): Invalid entity index " + std::to_string(e.index_), ";");
                return message;
            }
            if (e.index_ > max_index)
            {
                max_index = e.index_;
            }
        }
        
        if (max_index >= sparse_.capacity()) [[unlikely]]
        {
            sparse_.reserve(max_index + 1);
        }
        
        dense_.reserve(dense_.size() + entities.size());
        object_v_.reserve(object_v_.size() + entities.size());
        
        for (size_t i = 0; i < entities.size(); ++i)
        {
            entity& e = entities[i];
            T& component = components[i];
            
            sparse_entry& entry = sparse_.emplace_at(e.index_);
            
            if (entry.is_valid() && (entry.version_ == e.version_)) [[likely]]
            {
                uint32_t idx = entry.dense_index_;
                if (idx < object_v_.size()) [[likely]]
                {
                    object_v_[idx] = void_any(std::move(component));
                }
                else [[unlikely]]
                {
                    message.write_message(0, "error ", "single_class_set::add_batch(): Dense index out of range", ";");
                    return message;
                }
            }
            else [[unlikely]]
            {
                dense_.emplace_back(e.index_);
                uint32_t new_dense_index = static_cast<uint32_t>(dense_.size() - 1);
                entry.dense_index_ = new_dense_index;
                entry.version_ = e.version_;
                object_v_.emplace_back(std::move(component));
            }
        }
        
        return message;
    }
    
    template <typename T>
    [[nodiscard]] T* get_ptr(entity e) noexcept
    {        
        if(!e.is_valid()) [[unlikely]]
        {
            message.write_message(0, "error ", "single_class_set::get():Invalid index " + std::to_string(e.index_), ";");
            return nullptr;
        }
        if(type_id_ != type_id::get_type_id<T>()) [[unlikely]]
        {
            message.write_message(0, "error ", "single_class_set::get():Type mismatch", ";");
            return nullptr;
        }

        if(e.index_ >= sparse_.size()) [[unlikely]]
        {
            message.write_message(0, "error ", "single_class_set::get():Index out of range " + std::to_string(e.index_), ";");
            return nullptr;
        }

        if(sparse_[e.index_].version_ != e.version_) [[unlikely]]
        {
            message.write_message(0, "error ", "single_class_set::get():Entity version mismatch", ";");
            return nullptr;
        }

        auto index = sparse_[e.index_].dense_index_;

        if(index >= object_v_.size()) [[unlikely]]
        {
            message.write_message(0, "error ", "single_class_set::get():Index out of range " + std::to_string(e.index_), ";");
            return nullptr;  
        }

        return object_v_[index].get_ptr<T>();
    }

    operating_message hard_remove(entity e) noexcept
    {
        if (!e.is_valid()) [[unlikely]]
        {
            message.write_message(0, "error ", "single_class_set::hard_remove(): Invalid entity", ";");
            return message;
        }

        if (dense_.empty() || object_v_.empty()) [[unlikely]]
        {
            message.write_message(0, "error ", "single_class_set::hard_remove(): Container is empty", ";");
            return message;
        }
        if(e.index_ >= sparse_.size() || !sparse_[e.index_].is_valid()) [[unlikely]]
        {   
            message.write_message(0, "error ", "single_class_set::hard_remove():ID is invalid " + std::to_string(e.index_), ";");
            return message;
        }
        
        if(sparse_[e.index_].version_ != e.version_) [[unlikely]]
        {
            message.write_message(0, "error ", "single_class_set::hard_remove():Entity version mismatch", ";");
            return message;
        }

        auto index = sparse_[e.index_].dense_index_;
        if(index >= object_v_.size()) [[unlikely]]
        {
            message.write_message(0, "error ", "single_class_set::hard_remove():Index out of range " + std::to_string(e.index_), ";");
            return message;
        }

        auto moved_entity_id = dense_.back();
        dense_[index] = dense_.back();
        if (moved_entity_id != e.index_) [[likely]]
        {
            sparse_[moved_entity_id].dense_index_ = index;
        }
        dense_.pop_back();

        object_v_[index] = std::move(object_v_.back());
        object_v_.pop_back();

        sparse_[e.index_] = sparse_entry{};
        return message;
    }
    operating_message soft_remove(entity e) noexcept
    {
        if (!e.is_valid()) [[unlikely]]
        {
            message.write_message(0, "error ", "single_class_set::soft_remove(): Invalid entity", ";");
            return message;
        }

        if (dense_.empty() || object_v_.empty()) [[unlikely]]
        {
            message.write_message(0, "error ", "single_class_set::soft_remove(): Container is empty", ";");
            return message;
        }
        if(e.index_ >= sparse_.size() || !sparse_[e.index_].is_valid()) [[unlikely]]
        {   
            message.write_message(0, "error ", "single_class_set::soft_remove():ID is invalid " + std::to_string(e.index_), ";");
            return message;
        }
        
        if(sparse_[e.index_].version_ != e.version_) [[unlikely]]
        {
            message.write_message(0, "error ", "single_class_set::soft_remove():Entity version mismatch", ";");
            return message;
        }

        sparse_[e.index_] = sparse_entry{}; 
        return message;
    }

    [[nodiscard]] int& get_type_id() noexcept
    {
        return type_id_;
    }
    
    template <typename T>
    [[nodiscard]] std::span<T* const> get_components_ptr_span() noexcept
    {
        static thread_local std::vector<T*> ptr_cache;
        ptr_cache.clear();
        ptr_cache.reserve(object_v_.size());
        
        for (auto& obj : object_v_)
        {
            ptr_cache.push_back(obj.get_ptr<T>());
        }
        
        return std::span<T* const>(ptr_cache);
    }
    
    template <typename T>
    [[nodiscard]] class_pool<void_any>& get_object_pool() noexcept
    {
        return object_v_;
    }
    

    single_class_set(single_class_set&& other) noexcept
    : sparse_(std::move(other.sparse_))
    , dense_(std::move(other.dense_))
    , object_v_(std::move(other.object_v_))
    , message(std::move(other.message))
    , type_id_(other.type_id_)
    {
        other.type_id_ = -1;
    }
    

    single_class_set& operator=(single_class_set&& other) noexcept
    {
        if (this != &other) [[likely]]
        {
            sparse_ = std::move(other.sparse_);
            dense_ = std::move(other.dense_);
            object_v_ = std::move(other.object_v_);
            message = std::move(other.message);
            type_id_ = other.type_id_;
            other.type_id_ = -1;
        }
        return *this;
    }
    
    [[nodiscard]] operating_message& get_operating_message() noexcept
    {
        return message;
    }

    using iterator = typename class_pool<void_any>::iterator;
    using const_iterator = typename class_pool<void_any>::const_iterator;
    
    [[nodiscard]] iterator begin() noexcept { return object_v_.begin(); }
    [[nodiscard]] iterator end() noexcept { return object_v_.end(); }
    [[nodiscard]] const_iterator begin() const noexcept { return object_v_.begin(); }
    [[nodiscard]] const_iterator end() const noexcept { return object_v_.end(); }
    [[nodiscard]] const_iterator cbegin() const noexcept { return object_v_.cbegin(); }
    [[nodiscard]] const_iterator cend() const noexcept { return object_v_.cend(); }
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
    
    void reserve(size_t capacity) noexcept
    {
        sparse_.reserve(capacity);
        dense_.reserve(capacity);
        object_v_.reserve(capacity);
    }
    
    [[nodiscard]] class_pool<void_any>& get_component_vector() noexcept
    {
        return object_v_;
    }

    [[nodiscard]] class_pool<uint32_t>& get_entity_indices() noexcept
    {
        return dense_;
    }

    [[nodiscard]] const class_pool<uint32_t>& get_entity_indices() const noexcept
    {
        return dense_;
    }

    ~single_class_set() = default;
};
