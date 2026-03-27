#pragma once
#include <typeinfo>
#include "type_id.hpp"
#include <new>
#include <type_traits>
#include <cstddef>

class void_any
{
private:
    void* ptr_{nullptr};
    int any_type_id_{-1};
    void (*deleter_)(void*){nullptr};
    void* (*cloner_)(const void*){nullptr};  

    template<typename T>
    void construct_from(T&& object) noexcept
    {
        using DecayedT = std::decay_t<T>;
        void* new_ptr = ::operator new(sizeof(DecayedT), std::nothrow);
        if (!new_ptr) 
        {
            return;
        }
        new (new_ptr) DecayedT(std::forward<T>(object));

        any_type_id_ = type_id::get_type_id<DecayedT>();
        ptr_ = new_ptr;
        deleter_ = [](void* p) 
        {
            static_cast<DecayedT*>(p)->~DecayedT();
            ::operator delete(p);
        };
        cloner_ = [](const void* p) -> void*  
        {
            void* new_ptr = ::operator new(sizeof(DecayedT), std::nothrow);
            if (!new_ptr) return nullptr;
            new (new_ptr) DecayedT(*static_cast<const DecayedT*>(p));
            return new_ptr;
        };
    }

public:
    void_any() noexcept = default;

    template<typename T>
    void_any(T&& object) noexcept
    {
        construct_from(std::forward<T>(object));
    }

    ~void_any() 
    {
        if (deleter_ && ptr_) 
        {
            deleter_(ptr_);
        }
    }

    template<typename T>
    void set(T&& object) noexcept
    {
        if (deleter_ && ptr_) 
        {
            deleter_(ptr_);
        }
        
        ptr_ = nullptr;
        any_type_id_ = -1;
        deleter_ = nullptr;
        cloner_ = nullptr;
        
        construct_from(std::forward<T>(object));
    }

    template<typename T>
    T get() noexcept
    { 
        T* p = get_ptr<T>();
        if (!p)
        {
            return T{};
        }
        else
        {
            return *p;
        }
    }

    template<typename T>
    T* get_ptr() noexcept
    {   
        using DT = std::decay_t<T>;
        if(type_id::get_type_id<DT>() != any_type_id_ || any_type_id_ == -1)
        {
            return nullptr;
        }

        return static_cast<T*>(ptr_);
    }

    constexpr int get_type_id() const 
    {
        return any_type_id_;
    }

    void_any(const void_any& other) noexcept 
    {
        if (other.ptr_) 
        {
            ptr_ = other.cloner_(other.ptr_);
            if (ptr_) 
            {
                any_type_id_ = other.any_type_id_;
                deleter_ = other.deleter_;
                cloner_ = other.cloner_;
            }
        }
    }
    
    void_any& operator=(const void_any& other) noexcept 
    {
        if (this != &other) 
        {
            if (deleter_ && ptr_) 
            {
                deleter_(ptr_);
            } 
            
            ptr_ = nullptr;
            any_type_id_ = -1;
            deleter_ = nullptr;
            cloner_ = nullptr;

            if (other.ptr_) 
            {
                ptr_ = other.cloner_(other.ptr_);
                if (ptr_) 
                {
                    any_type_id_ = other.any_type_id_;
                    deleter_ = other.deleter_;
                    cloner_ = other.cloner_;
                }
            }
        }
        return *this;
    }

    void_any(void_any&& other) noexcept
        : ptr_(other.ptr_)
        , any_type_id_(other.any_type_id_)
        , deleter_(other.deleter_)
        , cloner_(other.cloner_)  
    {
        other.ptr_ = nullptr;
        other.any_type_id_ = -1;
        other.deleter_ = nullptr;
        other.cloner_ = nullptr;
    }
    
    void_any& operator=(void_any&& other) noexcept
    {
        if (this != &other) 
        {
            if (deleter_ && ptr_) 
            {
                deleter_(ptr_);
            } 
            
            ptr_ = other.ptr_;
            any_type_id_ = other.any_type_id_;
            deleter_ = other.deleter_;
            cloner_ = other.cloner_;  
            
            other.ptr_ = nullptr;
            other.any_type_id_ = -1;
            other.deleter_ = nullptr;
            other.cloner_ = nullptr;
        }
        return *this;
    }

    bool operator==(const void_any& other) const
    {
        return any_type_id_ == other.any_type_id_;
    }
};

inline void_any& null_any() 
{
    static void_any instance;
    return instance;
}
