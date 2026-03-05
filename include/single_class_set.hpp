#pragma once
#include <vector>
#include "operating_message.hpp"
#include "void_any.hpp"
#include "entity.hpp"


namespace ecs
{
class manager;
}


class Single_class_set
{
private:
    std::vector<uint32_t> sparse_;
    std::vector<uint32_t> dense_;
    std::vector<Void_any> object_v_;
    int type_id_{-1};
    Operating_message message;
    Void_any_option option_{vao::Absolute_heap_memory};
public:
    void clear()
    {
        sparse_.clear();
        dense_.clear();
        object_v_.clear();
    }
    
    template <typename T>

    Single_class_set(entity e,T&& object,Void_any_option option=vao::Absolute_heap_memory)
    {
        add(e,std::forward<T>(object),option);
    }
    template <typename T>
    Operating_message add(entity e,T&& object,Void_any_option option=vao::Absolute_heap_memory)
    {   
        if(type_id_==-1)
        {
            using DT= std::decay_t<T>;
            type_id_=type_id::get_type_id<DT>();
            option_ = option;
            if(sparse_.empty()) 
            {
                sparse_.resize(e.index_+1);
            }
        }
        if(!e.is_valid())
        {
            message.write_message(0,"error ","Single_class_set::add():ID is invalid "+std::to_string(e.index_),";");
            return message;
        }
        if(sparse_.size()<=e.index_)
        {
            sparse_.resize(e.index_+1, 0);
        }
        auto index=sparse_[e.index_];

        if (index!=0)
        {
            object_v_[index]=Void_any(std::forward<T>(object),option);
            return message;
        }
        dense_.emplace_back(e.index_);
        sparse_[e.index_]=dense_.size()-1;
        object_v_.emplace_back(std::forward<T>(object),option); 
        return message;       
    }
    template <typename T>
    T* get_ptr(entity e)
    {
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

       
        auto index = sparse_[e.index_];

        if(!e.is_valid())
        {
            message.write_message(0,"error ","Single_class_set::get():Invalid index "+std::to_string(e.index_),";");
            return nullptr;
        }

        if(index >= dense_.size())
        {
            message.write_message(0,"error ","Single_class_set::get():Index out of range "+std::to_string(e.index_),";");
            return nullptr;  
        }

    
        return object_v_[index].get_ptr<T>();
    }

    Operating_message remove(entity e)
    {

        if (dense_.empty() || object_v_.empty()) 
        {
            message.write_message(0, "error ", "Single_class_set::remove(): Container is empty", ";");
            return message;
        }
        if(e.index_ >= sparse_.size())
        {   

            message.write_message(0,"error ","Single_class_set::remove():ID is invalid "+std::to_string(e.index_),";");
            return message;
        }
        
        auto index = sparse_[e.index_];

        if(!e.is_valid())
        {

            message.write_message(0,"error ","Single_class_set::remove():ID is invalid "+std::to_string(e.index_),";");
            return message;
        } 


        auto moved_entity_id = dense_.back();
        dense_[index] = dense_.back();
        if (moved_entity_id != e.index_) 
        {
            sparse_[moved_entity_id] = index;
        }
        dense_.pop_back();

        object_v_[index]=std::move(object_v_.back());
        object_v_.pop_back();

        sparse_[e.index_] = 0;
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
    
    Operating_message &get_operating_message()
    {
        return message;
    }


    using iterator = typename std::vector<Void_any>::iterator;
    using const_iterator = typename std::vector<Void_any>::const_iterator;
    
    iterator begin() {return object_v_.begin(); }
    iterator end() { return object_v_.end();}
    const_iterator begin() const { return object_v_.begin();}
    const_iterator end() const { return object_v_.end();}
    const_iterator cbegin() const { return object_v_.cbegin(); }
    const_iterator cend() const { return object_v_.cend(); }
    Single_class_set(const Single_class_set&) = delete;

    Single_class_set& operator=(const Single_class_set&) = delete;

    size_t size() const
    {
        return dense_.size();
    }
    std::vector <Void_any>&get_component_vector()
    {
        return object_v_;
    }
    ~Single_class_set()=default;
};
