#pragma once
#include <typeinfo>
#include "type_id.hpp"
#include "void_any_config.hpp"
#include <new>
#include <type_traits>
#include <cstddef>
#include <cstring>

#ifdef _WIN32
    #include <windows.h>
#else
    #include <pthread.h>
    #include <unistd.h>
#endif



class stack_monitor 
{
public:
    static bool is_stack_safe(size_t required_size) 
    {
        char dummy;
        const char* stack_addr = reinterpret_cast<const char*>(&dummy);
        
#ifdef _WIN32

        ULONG_PTR stack_low, stack_high;
        GetCurrentThreadStackLimits(&stack_low, &stack_high);
        size_t remaining = static_cast<size_t>(stack_high - reinterpret_cast<ULONG_PTR>(stack_addr));
#else

        pthread_attr_t attr;
        void* stack_base;
        size_t stack_size;
        
        pthread_getattr_np(pthread_self(), &attr);
        pthread_attr_getstack(&attr, &stack_base, &stack_size);
        pthread_attr_destroy(&attr);
        
        const char* stack_top = static_cast<const char*>(stack_base) + stack_size;
        size_t remaining = static_cast<size_t>(stack_top - stack_addr);
#endif
        
        return remaining > (required_size + 1024);
    }
};


enum class void_any_option
{
    Enable_stack_memory=1,
    Absolute_heap_memory=2
};



using vao=void_any_option;

class void_any
{
private:

    inline static constexpr size_t index_max{SINGLE_OBJECT_STACK_SIZE};
    alignas(std::max_align_t) std::byte buff_[index_max];
    bool is_in_buff_{false};
    size_t type_size_{0};

    void* ptr_={nullptr};
    int any_type_id_{-1};
    void (*deleter_)(void*){nullptr};
    void (*inplace_destructor_)(std::byte*){nullptr};
    void_any_option option_=void_any_option::Absolute_heap_memory;
public:

    void_any() 
    : 
        ptr_(nullptr), 
        deleter_(nullptr),
        inplace_destructor_(nullptr),
        option_(void_any_option::Absolute_heap_memory),
        any_type_id_(-1)
    {}
    void_any(void_any_option options) 
    : 
        ptr_(nullptr), 
        deleter_(nullptr),
        inplace_destructor_(nullptr),
        any_type_id_(-1)
    {   
        option_=options;
    }
    void set_memory_mode(void_any_option options)
    {
        option_=options;
    }
    void_any_option get_memory_mode()
    {
        return option_;
    }
    

    template<typename T>
    void_any(T&& object,void_any_option options=vao::Absolute_heap_memory)
    {
        option_=options;
        using DecayedT = std::decay_t<T>;
        type_size_=sizeof(DecayedT);
        any_type_id_=type_id::get_type_id<DecayedT>();

        if constexpr (sizeof(DecayedT) <= index_max && std::is_trivially_copyable_v<DecayedT>)
        {
            if(option_==vao::Enable_stack_memory)
            {
                if(!stack_monitor::is_stack_safe(type_size_)) return;

                new (buff_) DecayedT(std::forward<T>(object));
                is_in_buff_ = true;
                if constexpr (!std::is_trivially_destructible_v<DecayedT>) 
                {
                    inplace_destructor_ = [](std::byte* p) 
                    {
                        static_cast<DecayedT*>(static_cast<void*>(p))->~DecayedT();
                    };
                } 
                else 
                {
                    inplace_destructor_ = nullptr;
                }


            }
            else
            {
                is_in_buff_ = false;
                ptr_ = ::operator new(sizeof(DecayedT));        
                ptr_ = new (ptr_) DecayedT(std::forward<T>(object));
                deleter_ = [](void* p) 
                {
                    static_cast<DecayedT*>(p)->~DecayedT();
                    ::operator delete(p);
                };
                inplace_destructor_ = nullptr;
            }
        }
        else
        {
            is_in_buff_ = false;
            ptr_ = ::operator new(sizeof(DecayedT));        
            ptr_ = new (ptr_) DecayedT(std::forward<T>(object));
            deleter_ = [](void* p) 
            {
                static_cast<DecayedT*>(p)->~DecayedT();
                ::operator delete(p);
            };
            inplace_destructor_ = nullptr;
        }
    }

    ~void_any() 
    {
        if (!is_in_buff_ && deleter_ && ptr_) 
        {
            deleter_(ptr_);
            ptr_ = nullptr;
            deleter_ = nullptr;
        } 
        else if (is_in_buff_ && inplace_destructor_) 
        {
            inplace_destructor_(buff_);
            inplace_destructor_ = nullptr;
        }

    }

    
    template<typename T>
    void set(T&& object,void_any_option options = vao::Absolute_heap_memory)
    {
        option_=options;
        if (!is_in_buff_ && deleter_ && ptr_) 
        {
            deleter_(ptr_);
            ptr_ = nullptr;
            deleter_ = nullptr;
        } 
        else if (is_in_buff_ && inplace_destructor_) 
        {
            inplace_destructor_(buff_);
            inplace_destructor_ = nullptr;
        }

        using DecayedT = std::decay_t<T>;

        type_size_=sizeof(DecayedT);
        any_type_id_=type_id::get_type_id<DecayedT>();

        if constexpr (sizeof(DecayedT) <= index_max && std::is_trivially_copyable_v<DecayedT>)
        {
            if(option_==vao::Enable_stack_memory)
            {
                if(!stack_monitor::is_stack_safe(type_size_)) return;

                new (buff_) DecayedT(std::forward<T>(object));
                is_in_buff_ = true;
                if constexpr (!std::is_trivially_destructible_v<DecayedT>) 
                {
                    inplace_destructor_ = [](std::byte* p) 
                    {
                        static_cast<DecayedT*>(static_cast<void*>(p))->~DecayedT();
                    };
                } 
                else 
                {
                    inplace_destructor_ = nullptr;
                }


            }
            else
            {
                is_in_buff_ = false;
                ptr_ = ::operator new(sizeof(DecayedT));        
                ptr_ = new (ptr_) DecayedT(std::forward<T>(object));
                deleter_ = [](void* p) 
                {
                    static_cast<DecayedT*>(p)->~DecayedT();
                    ::operator delete(p);
                };
                inplace_destructor_ = nullptr;
            }
        }
        else
        {
            is_in_buff_ = false;
            ptr_ = ::operator new(sizeof(DecayedT));        
            ptr_ = new (ptr_) DecayedT(std::forward<T>(object));
            deleter_ = [](void* p) 
            {
                static_cast<DecayedT*>(p)->~DecayedT();
                ::operator delete(p);
            };
            inplace_destructor_ = nullptr;
        }
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
        if(type_id::get_type_id<DT>()!= any_type_id_|| any_type_id_==-1)
        {
            return nullptr;
        }

        if(is_in_buff_)
        {
            return reinterpret_cast<T*>(buff_);
        }
        else
        {
            return static_cast<T*>(ptr_);
        }
        
    }

    int get_type_id() const 
    {
        return any_type_id_;
    }
    void_any(const void_any& other)= delete;
    
    void_any& operator=(const void_any&) = delete;
    

    void_any(void_any&& other) noexcept
        : is_in_buff_(other.is_in_buff_)
        , type_size_(other.type_size_)
        , ptr_(other.ptr_)
        , any_type_id_(other.any_type_id_)
        , deleter_(other.deleter_)
        , inplace_destructor_(other.inplace_destructor_)
        , option_(other.option_)
    {
        if (other.is_in_buff_) 
        {
            std::copy(other.buff_, other.buff_ + type_size_, buff_);
        }

        other.ptr_ = nullptr;
        other.deleter_ = nullptr;
        other.inplace_destructor_ = nullptr;
        other.any_type_id_ = -1;
    }
    
    void_any& operator=(void_any&& other) noexcept
    {
        if (this != &other) 
        {
            if (!is_in_buff_ && deleter_ && ptr_) 
            {
                deleter_(ptr_);
            } 
            else if (is_in_buff_ && inplace_destructor_) 
            {
                inplace_destructor_(buff_);
            }
            
            is_in_buff_ = other.is_in_buff_;
            type_size_ = other.type_size_;
            ptr_ = other.ptr_;
            any_type_id_ = other.any_type_id_;
            deleter_ = other.deleter_;
            inplace_destructor_ = other.inplace_destructor_;
            option_ = other.option_;
            
            if (other.is_in_buff_) 
            {
                std::copy(other.buff_, other.buff_ + type_size_, buff_);
            }
            
            other.ptr_ = nullptr;
            other.deleter_ = nullptr;
            other.inplace_destructor_ = nullptr;
            other.any_type_id_ = -1;
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