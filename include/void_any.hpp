#pragma once
#include <type_traits>
#include <utility>
#include <new>
#include <cstring>
#include "type_id.hpp"
#include "void_any_config.hpp"

#define FORCE_INLINE inline

#if defined(VOID_ANY_ENABLE_MEMORY_POOL)
#include "memory_pool.hpp"

static memory_pool void_any_memory_pool_;

static struct void_any_memory_pool_initializer
{
    void_any_memory_pool_initializer()
    {
        void_any_memory_pool_.resize(10 * 1000 * 1000);
    }
} void_any_memory_pool_initializer_;
#endif

class void_any
{
private:
#if defined(VOID_ANY_ENABLE_SSO)
    static constexpr size_t SSO_BUFFER_SIZE = VOID_ANY_SSO_BUFFER_SIZE;
    
    struct sso_buffer
    {
        alignas(8) uint8_t data[SSO_BUFFER_SIZE];
    };
    
    union storage
    {
        sso_buffer sso_;
        void* ptr_;
    };
    
    storage storage_;
    int any_type_id_{-1};
    bool use_sso_{false};
#else
    void* ptr_{nullptr};
    int any_type_id_{-1};
#endif
    
    void (*deleter_)(void*){nullptr};
    void* (*cloner_)(const void*){nullptr};
    void (*copier_)(void*, const void*){nullptr};

    template<typename T>
    FORCE_INLINE
    void construct_from(T&& object) noexcept
    {
        using DecayedT = std::decay_t<T>;
        
        any_type_id_ = type_id::get_type_id<DecayedT>();
        
#if defined(VOID_ANY_ENABLE_SSO)
        constexpr bool USE_SSO = sizeof(DecayedT) <= SSO_BUFFER_SIZE;
        
        if constexpr (USE_SSO)
        {
            use_sso_ = true;
            new (storage_.sso_.data) DecayedT(std::forward<T>(object));
            
            deleter_ = [](void* p) noexcept
            {
                static_cast<DecayedT*>(p)->~DecayedT();
            };
            
            cloner_ = [](const void* /*p*/) -> void*
            {
                return nullptr;
            };
            
            copier_ = [](void* dst, const void* src) noexcept
            {
                new (dst) DecayedT(*static_cast<const DecayedT*>(src));
            };
            return;
        }
        else
        {
            use_sso_ = false;
        }
#endif
        
#if defined(VOID_ANY_ENABLE_MEMORY_POOL)
        void* new_ptr = void_any_memory_pool_.allocate(sizeof(DecayedT));
#else
        void* new_ptr = ::operator new(sizeof(DecayedT), std::nothrow);
#endif
        
        if (!new_ptr) [[unlikely]]
        {
            return;
        }
        new (new_ptr) DecayedT(std::forward<T>(object));
        
#if defined(VOID_ANY_ENABLE_SSO)
        storage_.ptr_ = new_ptr;
#else
        ptr_ = new_ptr;
#endif
        
#if defined(VOID_ANY_ENABLE_MEMORY_POOL)
        deleter_ = [](void* p) noexcept 
        {
            static_cast<DecayedT*>(p)->~DecayedT();
            void_any_memory_pool_.deallocate(p);
        };
        cloner_ = [](const void* p) -> void*  
        {
            void* new_ptr = void_any_memory_pool_.allocate(sizeof(DecayedT));
            if (!new_ptr) [[unlikely]] return nullptr;
            new (new_ptr) DecayedT(*static_cast<const DecayedT*>(p));
            return new_ptr;
        };
#else
        deleter_ = [](void* p) noexcept 
        {
            static_cast<DecayedT*>(p)->~DecayedT();
            ::operator delete(p);
        };
        cloner_ = [](const void* p) -> void*  
        {
            void* new_ptr = ::operator new(sizeof(DecayedT), std::nothrow);
            if (!new_ptr) [[unlikely]] return nullptr;
            new (new_ptr) DecayedT(*static_cast<const DecayedT*>(p));
            return new_ptr;
        };
#endif
        
        copier_ = nullptr;
    }
    
    template<typename T>
    [[nodiscard]] FORCE_INLINE
    T* get_ptr_internal() noexcept
    {
#if defined(VOID_ANY_ENABLE_SSO)
        if (use_sso_) [[likely]]
        {
            return reinterpret_cast<T*>(storage_.sso_.data);
        }
        return static_cast<T*>(storage_.ptr_);
#else
        return static_cast<T*>(ptr_);
#endif
    }
    
    template<typename T>
    [[nodiscard]] FORCE_INLINE
    const T* get_ptr_internal() const noexcept
    {
#if defined(VOID_ANY_ENABLE_SSO)
        if (use_sso_) [[likely]]
        {
            return reinterpret_cast<const T*>(storage_.sso_.data);
        }
        return static_cast<const T*>(storage_.ptr_);
#else
        return static_cast<const T*>(ptr_);
#endif
    }

public:
    void_any() noexcept = default;
    
    template<typename T>
        requires (!std::is_same_v<std::decay_t<T>, void_any>)
    FORCE_INLINE
    void_any(T&& object) noexcept
    {
        construct_from(std::forward<T>(object));
    }

    ~void_any() noexcept
    {
        if (deleter_) [[likely]]
        {
#if defined(VOID_ANY_ENABLE_SSO)
            if (use_sso_) [[likely]]
            {
                deleter_(storage_.sso_.data);
            }
            else
            {
                deleter_(storage_.ptr_);
            }
#else
            deleter_(ptr_);
#endif
        }
    }

    template<typename T>
        requires (!std::is_same_v<std::decay_t<T>, void_any>)
    FORCE_INLINE
    void set(T&& object) noexcept
    {
        if (deleter_) [[likely]]
        {
#if defined(VOID_ANY_ENABLE_SSO)
            if (use_sso_) [[likely]]
            {
                deleter_(storage_.sso_.data);
            }
            else
            {
                deleter_(storage_.ptr_);
            }
#else
            deleter_(ptr_);
#endif
        }
        
#if defined(VOID_ANY_ENABLE_SSO)
        storage_.ptr_ = nullptr;
        use_sso_ = false;
#else
        ptr_ = nullptr;
#endif
        any_type_id_ = -1;
        
        construct_from(std::forward<T>(object));
    }

    FORCE_INLINE
    void_any(const void_any& other) noexcept
        : any_type_id_(other.any_type_id_)
        , deleter_(other.deleter_)
        , cloner_(other.cloner_)
        , copier_(other.copier_)
#if defined(VOID_ANY_ENABLE_SSO)
        , use_sso_(other.use_sso_)
#endif
    {
#if defined(VOID_ANY_ENABLE_SSO)
        if (other.use_sso_) [[likely]]
        {
            if (other.copier_) [[likely]]
            {
                other.copier_(storage_.sso_.data, other.storage_.sso_.data);
            }
        }
        else if (other.cloner_) [[likely]]
        {
            storage_.ptr_ = other.cloner_(other.storage_.ptr_);
        }
#else
        if (other.cloner_ && other.ptr_) [[likely]]
        {
            ptr_ = other.cloner_(other.ptr_);
        }
#endif
    }

    FORCE_INLINE
    void_any(void_any&& other) noexcept
#if defined(VOID_ANY_ENABLE_SSO)
        : storage_(other.storage_)
        , any_type_id_(other.any_type_id_)
        , use_sso_(other.use_sso_)
        , deleter_(other.deleter_)
        , cloner_(other.cloner_)
        , copier_(other.copier_)
#else
        : ptr_(other.ptr_)
        , any_type_id_(other.any_type_id_)
        , deleter_(other.deleter_)
        , cloner_(other.cloner_)
        , copier_(other.copier_)
#endif
    {
#if defined(VOID_ANY_ENABLE_SSO)
        other.storage_.ptr_ = nullptr;
        other.use_sso_ = false;
#else
        other.ptr_ = nullptr;
#endif
        other.any_type_id_ = -1;
        other.deleter_ = nullptr;
        other.cloner_ = nullptr;
        other.copier_ = nullptr;
    }

    FORCE_INLINE
    void_any& operator=(const void_any& other) noexcept
    {
        if (this != &other) [[likely]]
        {
            if (deleter_) [[likely]]
            {
#if defined(VOID_ANY_ENABLE_SSO)
                if (use_sso_) [[likely]]
                {
                    deleter_(storage_.sso_.data);
                }
                else
                {
                    deleter_(storage_.ptr_);
                }
#else
                deleter_(ptr_);
#endif
            }
            
            any_type_id_ = other.any_type_id_;
            deleter_ = other.deleter_;
            cloner_ = other.cloner_;
            copier_ = other.copier_;
#if defined(VOID_ANY_ENABLE_SSO)
            use_sso_ = other.use_sso_;
            
            if (other.use_sso_) [[likely]]
            {
                if (other.copier_) [[likely]]
                {
                    other.copier_(storage_.sso_.data, other.storage_.sso_.data);
                }
            }
            else if (other.cloner_) [[likely]]
            {
                storage_.ptr_ = other.cloner_(other.storage_.ptr_);
            }
#else
            if (other.cloner_ && other.ptr_) [[likely]]
            {
                ptr_ = other.cloner_(other.ptr_);
            }
#endif
        }
        return *this;
    }

    FORCE_INLINE
    void_any& operator=(void_any&& other) noexcept
    {
        if (this != &other) [[likely]]
        {
            if (deleter_) [[likely]]
            {
#if defined(VOID_ANY_ENABLE_SSO)
                if (use_sso_) [[likely]]
                {
                    deleter_(storage_.sso_.data);
                }
                else
                {
                    deleter_(storage_.ptr_);
                }
#else
                deleter_(ptr_);
#endif
            }
            
#if defined(VOID_ANY_ENABLE_SSO)
            storage_ = other.storage_;
            other.storage_.ptr_ = nullptr;
            other.use_sso_ = false;
#else
            ptr_ = other.ptr_;
            other.ptr_ = nullptr;
#endif
            any_type_id_ = other.any_type_id_;
            deleter_ = other.deleter_;
            cloner_ = other.cloner_;
            copier_ = other.copier_;
            
            other.any_type_id_ = -1;
            other.deleter_ = nullptr;
            other.cloner_ = nullptr;
            other.copier_ = nullptr;
        }
        return *this;
    }

    [[nodiscard]] FORCE_INLINE
    int type_id() const noexcept
    {
        return any_type_id_;
    }

    template<typename T>
    [[nodiscard]] FORCE_INLINE
    T* get_ptr() noexcept
    {
        if (any_type_id_ != type_id::get_type_id<T>()) [[unlikely]]
        {
            return nullptr;
        }
        return get_ptr_internal<T>();
    }

    template<typename T>
    [[nodiscard]] FORCE_INLINE
    const T* get_ptr() const noexcept
    {
        if (any_type_id_ != type_id::get_type_id<T>()) [[unlikely]]
        {
            return nullptr;
        }
        return get_ptr_internal<T>();
    }

    template<typename T>
    [[nodiscard]] FORCE_INLINE
    T* fast_get_ptr() noexcept
    {
        return get_ptr_internal<T>();
    }

    template<typename T>
    [[nodiscard]] FORCE_INLINE
    const T* fast_get_ptr() const noexcept
    {
        return get_ptr_internal<T>();
    }

    template<typename T>
    [[nodiscard]] FORCE_INLINE
    T* get_ptr_unchecked() noexcept
    {
        using DecayedT = std::decay_t<T>;
#if defined(VOID_ANY_ENABLE_SSO)
        constexpr bool USE_SSO = sizeof(DecayedT) <= SSO_BUFFER_SIZE;
        if constexpr (USE_SSO)
        {
            return reinterpret_cast<T*>(storage_.sso_.data);
        }
        else
        {
            return static_cast<T*>(storage_.ptr_);
        }
#else
        return static_cast<T*>(ptr_);
#endif
    }

    template<typename T>
    [[nodiscard]] FORCE_INLINE
    const T* get_ptr_unchecked() const noexcept
    {
        using DecayedT = std::decay_t<T>;
#if defined(VOID_ANY_ENABLE_SSO)
        constexpr bool USE_SSO = sizeof(DecayedT) <= SSO_BUFFER_SIZE;
        if constexpr (USE_SSO)
        {
            return reinterpret_cast<const T*>(storage_.sso_.data);
        }
        else
        {
            return static_cast<const T*>(storage_.ptr_);
        }
#else
        return static_cast<const T*>(ptr_);
#endif
    }

    template<typename T>
    [[nodiscard]] FORCE_INLINE
    T get() noexcept
    { 
        T* p = get_ptr<T>();
        if (!p) [[unlikely]] return T{};
        else [[likely]] return *p;
    }

    [[nodiscard]] FORCE_INLINE
    bool has_value() const noexcept
    {
#if defined(VOID_ANY_ENABLE_SSO)
        return use_sso_ || (storage_.ptr_ != nullptr);
#else
        return ptr_ != nullptr;
#endif
    }

    FORCE_INLINE
    void reset() noexcept
    {
        if (deleter_) [[likely]]
        {
#if defined(VOID_ANY_ENABLE_SSO)
            if (use_sso_) [[likely]]
            {
                deleter_(storage_.sso_.data);
            }
            else
            {
                deleter_(storage_.ptr_);
            }
            storage_.ptr_ = nullptr;
            use_sso_ = false;
#else
            deleter_(ptr_);
            ptr_ = nullptr;
#endif
        }
        any_type_id_ = -1;
        deleter_ = nullptr;
        cloner_ = nullptr;
        copier_ = nullptr;
    }
};
