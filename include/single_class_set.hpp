#pragma once
#include <vector>
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
    sparse_entry():dense_index_(0),version_(0) {}

    uint32_t dense_index_{0};   
    uint32_t version_{0};      
    bool is_valid() const { return version_ != 0; }
};

class Single_class_set
{
private:
    class_pool<sparse_entry> sparse_;
    class_pool<uint32_t> dense_;
    class_pool<void_any> object_v_;
    int type_id_{-1};
    operating_message message;
public:
    void clear()
    {
        sparse_.clear();
        dense_.clear();
        object_v_.clear();
    }
    
    template <typename T>
    Single_class_set(entity e,T&& object,size_t r_size=500*1000)
    {
        sparse_.reserve(r_size);  // 预分配容量，避免频繁扩容
        dense_.reserve(r_size);
        object_v_.reserve(r_size);
        add(e,std::forward<T>(object));
    }
    template <typename T>
    operating_message add(entity e,T&& object)
    {   
        if(type_id_==-1)
        {
            using DT= std::decay_t<T>;
            type_id_=type_id::get_type_id<DT>();
        }
        if(!e.is_valid())
        {
            message.write_message(0,"error ","Single_class_set::add():ID is invalid "+std::to_string(e.index_),";");
            return message;
        }
        if(sparse_.size()<=e.index_)
        {
            sparse_.resize(std::max(sparse_.size() * 2, static_cast<size_t>(e.index_) + 1));
        }
        
        sparse_entry& entry = sparse_[e.index_];

        if (entry.is_valid() && (entry.version_ == e.version_)) 
        {
            uint32_t idx = entry.dense_index_;
            if (idx < object_v_.size()) 
            {
                object_v_[idx] = void_any(std::forward<T>(object));
            } 
            else 
            {
                message.write_message(0, "error ", "Single_class_set::add(): Dense index out of range", ";");
                return message;
            }
        } 
        else 
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
    T* get_ptr(entity e)
    {        
        if(!e.is_valid())
        {
            message.write_message(0,"error ","Single_class_set::get():Invalid index "+std::to_string(e.index_),";");
            return nullptr;
        }
        if(type_id_!=type_id::get_type_id<T>())
        {
            message.write_message(0,"error ","Single_class_set::get():Type mismatch", ";");
            return nullptr;
        }

        if(e.index_ >= sparse_.size())
        {
            message.write_message (0,"error ","Single_class_set::get():Index out of range "+std::to_string(e.index_),";");
            return nullptr;
        }

        if(sparse_[e.index_].version_!=e.version_)
        {
            message.write_message(0,"error ","Single_class_set::get():Entity version mismatch", ";");
            return nullptr;
        }

        auto index = sparse_[e.index_].dense_index_;

        if(index >= object_v_.size())
        {
            message.write_message(0,"error ","Single_class_set::get():Index out of range "+std::to_string(e.index_),";");
            return nullptr;  
        }

        return object_v_[index].get_ptr<T>();
    }

    operating_message hard_remove(entity e)
    {
        if (!e.is_valid()) 
        {
            message.write_message(0, "error ", "Single_class_set::hard_remove(): Invalid entity", ";");
            return message;
        }

        if (dense_.empty() || object_v_.empty()) 
        {
            message.write_message(0, "error ", "Single_class_set::hard_remove(): Container is empty", ";");
            return message;
        }
        if(e.index_ >= sparse_.size()|| !sparse_[e.index_].is_valid())
        {   
            message.write_message(0,"error ","Single_class_set::hard_remove():ID is invalid "+std::to_string(e.index_),";");
            return message;
        }
        
        if(sparse_[e.index_].version_!=e.version_)
        {
            message.write_message(0,"error ","Single_class_set::hard_remove():Entity version mismatch", ";");
            return message;
        }

        auto index = sparse_[e.index_].dense_index_;
        if(index >= object_v_.size())
        {
            message.write_message(0,"error ","Single_class_set::hard_remove():Index out of range "+std::to_string(e.index_),";");
            return message;
        }

        auto moved_entity_id = dense_.back();
        dense_[index] = dense_.back();
        if (moved_entity_id != e.index_) 
        {
            sparse_[moved_entity_id].dense_index_ = index;
        }
        dense_.pop_back();

        object_v_[index]=std::move(object_v_.back());
        object_v_.pop_back();

        sparse_[e.index_] = sparse_entry{};
        return message;
    }
    operating_message soft_remove(entity e)
    {
        if (!e.is_valid()) 
        {
            message.write_message(0, "error ", "Single_class_set::soft_remove(): Invalid entity", ";");
            return message;
        }

        if (dense_.empty() || object_v_.empty()) 
        {
            message.write_message(0, "error ", "Single_class_set::soft_remove(): Container is empty", ";");
            return message;
        }
        if(e.index_ >= sparse_.size()|| !sparse_[e.index_].is_valid())
        {   
            message.write_message(0,"error ","Single_class_set::soft_remove():ID is invalid "+std::to_string(e.index_),";");
            return message;
        }
        
        if(sparse_[e.index_].version_!=e.version_)
        {
            message.write_message(0,"error ","Single_class_set::soft_remove():Entity version mismatch", ";");
            return message;
        }

        sparse_[e.index_]=sparse_entry{}; 
        return message;
    }

    int &get_type_id()
    {
        return type_id_;
    }
    Single_class_set(){}

    Single_class_set(Single_class_set&& other) noexcept
    : sparse_(std::move(other.sparse_))
    , dense_(std::move(other.dense_))
    , object_v_(std::move(other.object_v_))
    , message(std::move(other.message))
    , type_id_(other.type_id_)
    {other.type_id_= -1;}
    

    Single_class_set& operator=(Single_class_set&& other) noexcept
    {
        if (this != &other) 
        {
            sparse_ = std::move(other.sparse_);
            dense_ = std::move(other.dense_);
            object_v_ = std::move(other.object_v_);
            message = std::move(other.message);
            type_id_ = other.type_id_;
            other.type_id_= -1;
        }
        return *this;
    }
    
    operating_message &get_operating_message()
    {
        return message;
    }

    using iterator = typename class_pool<void_any>::iterator;
    using const_iterator = typename class_pool<void_any>::const_iterator;
    
    iterator begin() { return object_v_.begin(); }
    iterator end() { return object_v_.end(); }
    const_iterator begin() const { return object_v_.begin(); }
    const_iterator end() const { return object_v_.end(); }
    const_iterator cbegin() const { return object_v_.cbegin(); }
    const_iterator cend() const { return object_v_.cend(); }
    Single_class_set(const Single_class_set&) = delete;
    Single_class_set& operator=(const Single_class_set&) = delete;

    size_t size() const
    {
        return dense_.size();
    }
    
    void reserve(size_t capacity)
    {
        sparse_.reserve(capacity);
        dense_.reserve(capacity);
        object_v_.reserve(capacity);
    }
    
    class_pool<void_any>& get_component_vector()
    {
        return object_v_;
    }
    ~Single_class_set()=default;
};
