#pragma once
#include <type_traits>
#include <utility>
#include <new>
#include <cstdint>
#include <cstring>
#include "type_id.hpp"
#include "void_any_config.hpp"
#include "force_inline.hpp"

#if defined(VOID_ANY_USE_LAYERED_ALLOCATOR)
#include "layered_allocator.hpp"
#elif defined(VOID_ANY_ENABLE_MEMORY_POOL)
#include "memory_pool.hpp"
#endif

#if defined(VOID_ANY_USE_LAYERED_ALLOCATOR)
inline layered_allocator void_any_pool_{};
#elif defined(VOID_ANY_ENABLE_MEMORY_POOL)
inline memory_pool void_any_pool_{};
#endif

class void_any
{
private:
    struct vtable
    {
        int type_id;
        int pad_;
        size_t element_size;
        void (*destroy)(void* data) noexcept;
        void (*copy_to)(void* dst, const void* src) noexcept;
        void (*move_to)(void* dst, void* src) noexcept;
        void* (*clone)(const void* src) noexcept;
    };

#if defined(VOID_ANY_ENABLE_SSO)
    static constexpr size_t SSO_BUFFER_SIZE = VOID_ANY_SSO_BUFFER_SIZE;
    static constexpr size_t SSO_ALIGNMENT = VOID_ANY_SSO_ALIGNMENT;

    union storage
    {
        alignas(SSO_ALIGNMENT) uint8_t sso_data_[SSO_BUFFER_SIZE];
        void* ptr_;
    };

    storage storage_;
#else
    void* ptr_{nullptr};
#endif

    // 位编码: [63:48]=type_id(16位), [47:1]=vtable指针, [0]=SSO标志
    // x86-64 用户空间指针高16位为0,可安全存储type_id
    static constexpr uintptr_t TYPE_ID_SHIFT = 48;
    static constexpr uintptr_t TYPE_ID_MASK = 0xFFFFULL << TYPE_ID_SHIFT;
    static constexpr uintptr_t PTR_MASK = ~TYPE_ID_MASK;
    static constexpr uintptr_t SSO_BIT = 1ULL;

    uintptr_t vtable_sso_type_{0};

    [[nodiscard]] FORCE_INLINE const vtable* get_vtable() const noexcept
    {
        return reinterpret_cast<const vtable*>(
            vtable_sso_type_ & PTR_MASK & ~SSO_BIT);
    }

    [[nodiscard]] FORCE_INLINE bool is_sso() const noexcept
    {
        return (vtable_sso_type_ & SSO_BIT) != 0;
    }

    [[nodiscard]] FORCE_INLINE int get_cached_type_id() const noexcept
    {
        return static_cast<int>((vtable_sso_type_ & TYPE_ID_MASK) >> TYPE_ID_SHIFT);
    }

    FORCE_INLINE void set_vtable_sso_type(const vtable* vt, bool sso, int tid) noexcept
    {
        vtable_sso_type_ = reinterpret_cast<uintptr_t>(vt)
                         | (sso ? SSO_BIT : 0)
                         | (static_cast<uintptr_t>(static_cast<uint16_t>(tid)) << TYPE_ID_SHIFT);
    }

    template<typename T>
    static const vtable& get_sso_vtable()
    {
        static const vtable vt = []{
            vtable v{};
            v.type_id = type_id::get_type_id<T>();
            v.pad_ = 0;
            v.element_size = sizeof(T);

            if constexpr (std::is_trivially_copyable_v<T>)
            {
                // trivially copyable: copy_to/move_to 设为 nullptr
                // 拷贝/移动时直接 memcpy,跳过间接调用
                v.copy_to = nullptr;
                v.move_to = nullptr;
                // trivially copyable 蕴含 trivially destructible
                v.destroy = nullptr;
            }
            else
            {
                v.destroy = [](void* p) noexcept { static_cast<T*>(p)->~T(); };

                if constexpr (std::is_copy_constructible_v<T>)
                {
                    v.copy_to = [](void* dst, const void* src) noexcept
                    {
                        new (dst) T(*static_cast<const T*>(src));
                    };
                }
                else
                {
                    v.copy_to = nullptr;
                }

                if constexpr (std::is_move_constructible_v<T>)
                {
                    v.move_to = [](void* dst, void* src) noexcept
                    {
                        new (dst) T(std::move(*static_cast<T*>(src)));
                        static_cast<T*>(src)->~T();
                    };
                }
                else
                {
                    v.move_to = nullptr;
                }
            }

            v.clone = nullptr;

            return v;
        }();
        return vt;
    }

#if defined(VOID_ANY_ENABLE_MEMORY_POOL)
    template<typename T>
    static const vtable& get_heap_vtable()
    {
        static const vtable vt = []{
            vtable v{};
            v.type_id = type_id::get_type_id<T>();
            v.pad_ = 0;
            v.element_size = sizeof(T);

            if constexpr (std::is_trivially_copyable_v<T>)
            {
                // trivially destructible: 只释放内存
                v.destroy = [](void* p) noexcept
                {
                    void_any_pool_.deallocate(p);
                };
                v.copy_to = nullptr;
                v.move_to = nullptr;
                if constexpr (std::is_copy_constructible_v<T>)
                {
                    v.clone = [](const void* src) noexcept -> void*
                    {
                        void* new_ptr = void_any_pool_.allocate(sizeof(T));
                        if (!new_ptr) [[unlikely]] return nullptr;
                        std::memcpy(new_ptr, src, sizeof(T));
                        return new_ptr;
                    };
                }
                else
                {
                    v.clone = nullptr;
                }
            }
            else
            {
                v.destroy = [](void* p) noexcept
                {
                    static_cast<T*>(p)->~T();
                    void_any_pool_.deallocate(p);
                };
                v.copy_to = nullptr;
                v.move_to = nullptr;
                if constexpr (std::is_copy_constructible_v<T>)
                {
                    v.clone = [](const void* src) noexcept -> void*
                    {
                        void* new_ptr = void_any_pool_.allocate(sizeof(T));
                        if (!new_ptr) [[unlikely]] return nullptr;
                        new (new_ptr) T(*static_cast<const T*>(src));
                        return new_ptr;
                    };
                }
                else
                {
                    v.clone = nullptr;
                }
            }
            return v;
        }();
        return vt;
    }
#else
    template<typename T>
    static const vtable& get_heap_vtable()
    {
        static const vtable vt = []{
            vtable v{};
            v.type_id = type_id::get_type_id<T>();
            v.pad_ = 0;
            v.element_size = sizeof(T);

            if constexpr (std::is_trivially_copyable_v<T>)
            {
                v.destroy = [](void* p) noexcept
                {
                    ::operator delete(p);
                };
                v.copy_to = nullptr;
                v.move_to = nullptr;
                if constexpr (std::is_copy_constructible_v<T>)
                {
                    v.clone = [](const void* src) noexcept -> void*
                    {
                        void* new_ptr = ::operator new(sizeof(T), std::nothrow);
                        if (!new_ptr) [[unlikely]] return nullptr;
                        std::memcpy(new_ptr, src, sizeof(T));
                        return new_ptr;
                    };
                }
                else
                {
                    v.clone = nullptr;
                }
            }
            else
            {
                v.destroy = [](void* p) noexcept
                {
                    static_cast<T*>(p)->~T();
                    ::operator delete(p);
                };
                v.copy_to = nullptr;
                v.move_to = nullptr;
                if constexpr (std::is_copy_constructible_v<T>)
                {
                    v.clone = [](const void* src) noexcept -> void*
                    {
                        void* new_ptr = ::operator new(sizeof(T), std::nothrow);
                        if (!new_ptr) [[unlikely]] return nullptr;
                        new (new_ptr) T(*static_cast<const T*>(src));
                        return new_ptr;
                    };
                }
                else
                {
                    v.clone = nullptr;
                }
            }
            return v;
        }();
        return vt;
    }
#endif

    FORCE_INLINE void destroy_data() noexcept
    {
        if (!has_value()) return;
        const vtable* vt = get_vtable();
        if (!vt->destroy) return;
#if defined(VOID_ANY_ENABLE_SSO)
        if (is_sso()) [[likely]]
            vt->destroy(storage_.sso_data_);
        else
            vt->destroy(storage_.ptr_);
#else
        vt->destroy(ptr_);
#endif
    }

    template<typename T>
    FORCE_INLINE void construct_from(T&& object) noexcept
    {
        using DecayedT = std::decay_t<T>;
        int tid = type_id::get_type_id<DecayedT>();

#if defined(VOID_ANY_ENABLE_SSO)
        constexpr bool USE_SSO = sizeof(DecayedT) <= SSO_BUFFER_SIZE
                              && alignof(DecayedT) <= SSO_ALIGNMENT;

        if constexpr (USE_SSO)
        {
            set_vtable_sso_type(&get_sso_vtable<DecayedT>(), true, tid);
            new (storage_.sso_data_) DecayedT(std::forward<T>(object));
            return;
        }
#endif

        set_vtable_sso_type(&get_heap_vtable<DecayedT>(), false, tid);

#if defined(VOID_ANY_ENABLE_MEMORY_POOL)
        void* new_ptr = void_any_pool_.allocate(sizeof(DecayedT));
#else
        void* new_ptr = ::operator new(sizeof(DecayedT), std::nothrow);
#endif

        if (!new_ptr) [[unlikely]]
        {
            vtable_sso_type_ = 0;
            return;
        }
        new (new_ptr) DecayedT(std::forward<T>(object));

#if defined(VOID_ANY_ENABLE_SSO)
        storage_.ptr_ = new_ptr;
#else
        ptr_ = new_ptr;
#endif
    }

    template<typename T>
    [[nodiscard]] FORCE_INLINE T* get_ptr_internal() noexcept
    {
#if defined(VOID_ANY_ENABLE_SSO)
        if (is_sso()) [[likely]]
            return reinterpret_cast<T*>(storage_.sso_data_);
        return static_cast<T*>(storage_.ptr_);
#else
        return static_cast<T*>(ptr_);
#endif
    }

    template<typename T>
    [[nodiscard]] FORCE_INLINE const T* get_ptr_internal() const noexcept
    {
#if defined(VOID_ANY_ENABLE_SSO)
        if (is_sso()) [[likely]]
            return reinterpret_cast<const T*>(storage_.sso_data_);
        return static_cast<const T*>(storage_.ptr_);
#else
        return static_cast<const T*>(ptr_);
#endif
    }

public:
    void_any() noexcept = default;

    template<typename T>
        requires (!std::is_same_v<std::decay_t<T>, void_any>)
    FORCE_INLINE void_any(T&& object) noexcept
    {
        construct_from(std::forward<T>(object));
    }

    ~void_any() noexcept
    {
        destroy_data();
    }

    template<typename T>
        requires (!std::is_same_v<std::decay_t<T>, void_any>)
    FORCE_INLINE void set(T&& object) noexcept
    {
        destroy_data();
        vtable_sso_type_ = 0;
        construct_from(std::forward<T>(object));
    }

    FORCE_INLINE void_any(const void_any& other) noexcept
    {
        vtable_sso_type_ = other.vtable_sso_type_;
        if (!other.has_value()) return;

#if defined(VOID_ANY_ENABLE_SSO)
        if (other.is_sso()) [[likely]]
        {
            const vtable* vt = other.get_vtable();
            if (vt->copy_to)
                vt->copy_to(storage_.sso_data_, other.storage_.sso_data_);
            else
                std::memcpy(storage_.sso_data_, other.storage_.sso_data_, vt->element_size);
        }
        else
        {
            const vtable* vt = other.get_vtable();
            storage_.ptr_ = vt->clone ? vt->clone(other.storage_.ptr_) : nullptr;
        }
#else
        const vtable* vt = other.get_vtable();
        ptr_ = (vt->clone && other.ptr_) ? vt->clone(other.ptr_) : nullptr;
#endif
    }

    FORCE_INLINE void_any(void_any&& other) noexcept
        : vtable_sso_type_(other.vtable_sso_type_)
    {
        if (!other.has_value()) return;

#if defined(VOID_ANY_ENABLE_SSO)
        if (other.is_sso()) [[likely]]
        {
            const vtable* vt = other.get_vtable();
            if (vt->move_to)
                vt->move_to(storage_.sso_data_, other.storage_.sso_data_);
            else
                std::memcpy(storage_.sso_data_, other.storage_.sso_data_, vt->element_size);
        }
        else
        {
            storage_.ptr_ = other.storage_.ptr_;
        }
#else
        ptr_ = other.ptr_;
#endif

        other.vtable_sso_type_ = 0;
    }

    FORCE_INLINE void_any& operator=(const void_any& other) noexcept
    {
        if (this != &other) [[likely]]
        {
            destroy_data();
            vtable_sso_type_ = other.vtable_sso_type_;

            if (!other.has_value()) return *this;

#if defined(VOID_ANY_ENABLE_SSO)
            if (other.is_sso()) [[likely]]
            {
                const vtable* vt = other.get_vtable();
                if (vt->copy_to)
                    vt->copy_to(storage_.sso_data_, other.storage_.sso_data_);
                else
                    std::memcpy(storage_.sso_data_, other.storage_.sso_data_, vt->element_size);
            }
            else
            {
                const vtable* vt = other.get_vtable();
                storage_.ptr_ = vt->clone ? vt->clone(other.storage_.ptr_) : nullptr;
            }
#else
            const vtable* vt = other.get_vtable();
            ptr_ = (vt->clone && other.ptr_) ? vt->clone(other.ptr_) : nullptr;
#endif
        }
        return *this;
    }

    FORCE_INLINE void_any& operator=(void_any&& other) noexcept
    {
        if (this != &other) [[likely]]
        {
            destroy_data();
            vtable_sso_type_ = other.vtable_sso_type_;

            if (!other.has_value()) return *this;

#if defined(VOID_ANY_ENABLE_SSO)
            if (other.is_sso()) [[likely]]
            {
                const vtable* vt = other.get_vtable();
                if (vt->move_to)
                    vt->move_to(storage_.sso_data_, other.storage_.sso_data_);
                else
                    std::memcpy(storage_.sso_data_, other.storage_.sso_data_, vt->element_size);
            }
            else
            {
                storage_.ptr_ = other.storage_.ptr_;
            }
#else
            ptr_ = other.ptr_;
#endif

            other.vtable_sso_type_ = 0;
        }
        return *this;
    }

    [[nodiscard]] FORCE_INLINE int type_id() const noexcept
    {
        if (!has_value()) [[unlikely]] return -1;
        return get_cached_type_id();
    }

    template<typename T>
    [[nodiscard]] FORCE_INLINE T* get_ptr() noexcept
    {
        if (!has_value() || get_cached_type_id() != type_id::get_type_id<T>()) [[unlikely]]
            return nullptr;
        return get_ptr_internal<T>();
    }

    template<typename T>
    [[nodiscard]] FORCE_INLINE const T* get_ptr() const noexcept
    {
        if (!has_value() || get_cached_type_id() != type_id::get_type_id<T>()) [[unlikely]]
            return nullptr;
        return get_ptr_internal<T>();
    }

    template<typename T>
    [[nodiscard]] FORCE_INLINE T* fast_get_ptr() noexcept
    {
        return get_ptr_internal<T>();
    }

    template<typename T>
    [[nodiscard]] FORCE_INLINE const T* fast_get_ptr() const noexcept
    {
        return get_ptr_internal<T>();
    }

    template<typename T>
    [[nodiscard]] FORCE_INLINE T* get_ptr_unchecked() noexcept
    {
#if defined(VOID_ANY_ENABLE_SSO)
        using DecayedT = std::decay_t<T>;
        constexpr bool USE_SSO = sizeof(DecayedT) <= SSO_BUFFER_SIZE
                              && alignof(DecayedT) <= SSO_ALIGNMENT;
        if constexpr (USE_SSO)
            return reinterpret_cast<T*>(storage_.sso_data_);
        else
            return static_cast<T*>(storage_.ptr_);
#else
        return static_cast<T*>(ptr_);
#endif
    }

    template<typename T>
    [[nodiscard]] FORCE_INLINE const T* get_ptr_unchecked() const noexcept
    {
#if defined(VOID_ANY_ENABLE_SSO)
        using DecayedT = std::decay_t<T>;
        constexpr bool USE_SSO = sizeof(DecayedT) <= SSO_BUFFER_SIZE
                              && alignof(DecayedT) <= SSO_ALIGNMENT;
        if constexpr (USE_SSO)
            return reinterpret_cast<const T*>(storage_.sso_data_);
        else
            return static_cast<const T*>(storage_.ptr_);
#else
        return static_cast<const T*>(ptr_);
#endif
    }

    template<typename T>
    [[nodiscard]] FORCE_INLINE T get() noexcept
    {
        T* p = get_ptr<T>();
        if (!p) [[unlikely]] return T{};
        return *p;
    }

    [[nodiscard]] FORCE_INLINE bool has_value() const noexcept
    {
        return vtable_sso_type_ != 0;
    }

    FORCE_INLINE void reset() noexcept
    {
        destroy_data();
        vtable_sso_type_ = 0;
    }
};
