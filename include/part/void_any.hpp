#pragma once
#include <type_traits>
#include <utility>
#include <new>
#include <cstdint>
#include <cstring>
#include "type_id.hpp"
#include "../config/void_any_config.hpp"
#include "force_inline.hpp"

#if defined(VOID_ANY_USE_LAYERED_ALLOCATOR)
#include "layered_allocator.hpp"
#endif

#if defined(VOID_ANY_USE_LAYERED_ALLOCATOR)
inline layered_allocator void_any_pool_{};

[[nodiscard]] inline void* void_any_allocate(size_t n) noexcept
{
    return void_any_pool_.allocate(n);
}

inline void void_any_deallocate(void* p) noexcept
{
    void_any_pool_.deallocate(p);
}
#else
[[nodiscard]] inline void* void_any_allocate(size_t n) noexcept
{
    return ::operator new(n, std::nothrow);
}

inline void void_any_deallocate(void* p) noexcept
{
    ::operator delete(p);
}
#endif

// 编译期类型标签: 每个类型 T 拥有唯一地址, 链接期常量, 无守卫检查
// static inline char tag = 0 是常量初始化, 程序启动前完成, 无运行时 guard
// 地址本身作为类型标识符, 用于 inline 路径的快速比较
namespace void_any_detail {
    template<typename T>
    struct type_tag_holder {
        static inline char tag = 0;
    };
}

// 模板化 void_any: SsoSize 控制 SSO 缓冲区大小, SsoAlign 控制对齐
// 对外仅暴露 void_any 别名, 用户无需选择类型
template<size_t SsoSize = 56, size_t SsoAlign = 8>
class define_void_any
{
    static_assert(SsoSize % 8 == 0, "SsoSize must be a multiple of 8");
    static_assert(SsoAlign >= 8, "SsoAlign must be at least 8");

private:
    struct vtable
    {
        int type_id;
        size_t element_size;
        void (*destroy)(void* data) noexcept;
        void (*copy_to)(void* dst, const void* src) noexcept;
        void (*move_to)(void* dst, void* src) noexcept;
        void* (*clone)(const void* src) noexcept;
    };

#if defined(VOID_ANY_ENABLE_SSO)
    static constexpr size_t sso_buffer_size = SsoSize;
    static constexpr size_t sso_alignment = SsoAlign;

    union storage
    {
        alignas(SsoAlign) uint8_t sso_data_[SsoSize];
        void* ptr_;
    };

    storage storage_;
#else
    void* ptr_{nullptr};
#endif

    // 位编码 (64 位平台):
    // [63:57] element_size (7 位, SSO 模式存储 sizeof(T), heap 模式为 0)
    // [56]    inline_type_id 标志 (1 = 无 vtable, type_id 内联在 [53:1])
    // [55]    trivially_destructible
    // [54]    trivially_copyable
    // [53:1]  vtable 指针 (53 位) 或 type_id (当 [56]=1 时)
    // [0]     sso 标志
    static constexpr uintptr_t sso_bit = 1ULL;
    static constexpr uintptr_t size_shift = 57;
    static constexpr uintptr_t size_mask = 0x7FULL << size_shift;
    static constexpr uintptr_t inline_type_id_bit = 1ULL << 56;
    static constexpr uintptr_t trivial_dtor_bit = 1ULL << 55;
    static constexpr uintptr_t trivial_copy_bit = 1ULL << 54;
    static constexpr uintptr_t ptr_mask = ~(size_mask | inline_type_id_bit
                                            | trivial_dtor_bit | trivial_copy_bit);
    static constexpr uintptr_t vtable_ptr_mask = ptr_mask & ~sso_bit;

#if INTPTR_MAX == INT64_MAX
    uintptr_t vtable_sso_type_{0};

    [[nodiscard]] FORCE_INLINE const vtable* get_vtable() const noexcept
    {
        return reinterpret_cast<const vtable*>(vtable_sso_type_ & vtable_ptr_mask);
    }

    [[nodiscard]] FORCE_INLINE bool is_sso() const noexcept
    {
        return (vtable_sso_type_ & sso_bit) != 0;
    }

    [[nodiscard]] FORCE_INLINE bool is_inline_type_id() const noexcept
    {
        return (vtable_sso_type_ & inline_type_id_bit) != 0;
    }

    [[nodiscard]] FORCE_INLINE bool is_trivially_destructible() const noexcept
    {
        return (vtable_sso_type_ & trivial_dtor_bit) != 0;
    }

    [[nodiscard]] FORCE_INLINE bool is_trivially_copyable() const noexcept
    {
        return (vtable_sso_type_ & trivial_copy_bit) != 0;
    }

    [[nodiscard]] FORCE_INLINE const char* get_inline_type_tag_ptr() const noexcept
    {
        return reinterpret_cast<const char*>((vtable_sso_type_ & vtable_ptr_mask) >> 1);
    }

    [[nodiscard]] FORCE_INLINE uint8_t get_encoded_size() const noexcept
    {
        return static_cast<uint8_t>((vtable_sso_type_ & size_mask) >> size_shift);
    }

    [[nodiscard]] FORCE_INLINE bool has_value_fast() const noexcept
    {
        return vtable_sso_type_ != 0;
    }

    FORCE_INLINE void set_encoded(const vtable* vt, bool sso, bool trivial_dtor,
                                   bool trivial_copy, uint8_t size) noexcept
    {
        vtable_sso_type_ = reinterpret_cast<uintptr_t>(vt)
                         | (sso ? sso_bit : 0)
                         | (trivial_dtor ? trivial_dtor_bit : 0)
                         | (trivial_copy ? trivial_copy_bit : 0)
                         | (static_cast<uintptr_t>(size) << size_shift);
    }
#else
    const vtable* vtable_{nullptr};
    bool is_sso_{false};

    [[nodiscard]] FORCE_INLINE const vtable* get_vtable() const noexcept { return vtable_; }
    [[nodiscard]] FORCE_INLINE bool is_sso() const noexcept { return is_sso_; }
    [[nodiscard]] FORCE_INLINE bool is_trivially_destructible() const noexcept
    {
        return vtable_ && vtable_->destroy == nullptr && is_sso_;
    }
    [[nodiscard]] FORCE_INLINE bool is_trivially_copyable() const noexcept
    {
        return vtable_ && vtable_->copy_to == nullptr && is_sso_;
    }
    [[nodiscard]] FORCE_INLINE uint8_t get_encoded_size() const noexcept
    {
        return vtable_ ? static_cast<uint8_t>(vtable_->element_size) : 0;
    }
    [[nodiscard]] FORCE_INLINE bool has_value_fast() const noexcept { return vtable_ != nullptr; }
    FORCE_INLINE void set_encoded(const vtable* vt, bool sso, bool, bool, uint8_t) noexcept
    {
        vtable_ = vt;
        is_sso_ = sso;
    }
#endif

    [[nodiscard]] FORCE_INLINE void* get_void_ptr() noexcept
    {
#if defined(VOID_ANY_ENABLE_SSO)
        if (is_sso()) [[likely]]
        {
            return storage_.sso_data_;
        }
        return storage_.ptr_;
#else
        return ptr_;
#endif
    }

    [[nodiscard]] FORCE_INLINE const void* get_void_ptr() const noexcept
    {
#if defined(VOID_ANY_ENABLE_SSO)
        if (is_sso()) [[likely]]
        {
            return storage_.sso_data_;
        }
        return storage_.ptr_;
#else
        return ptr_;
#endif
    }

    // 全量复制 SSO buffer (struct copy, GCC 内联为 mov 指令序列)
    static FORCE_INLINE void sso_buffer_copy(void* dst, const void* src) noexcept
    {
        struct sso_copy_t { uint64_t q[SsoSize / 8]; };
        *static_cast<sso_copy_t*>(dst) = *static_cast<const sso_copy_t*>(src);
    }

    // 按 element_size 分级复制 SSO buffer, 减少小对象的内存访问次数
    // SSO buffer 为 56 字节, 按对齐宽度向上取整复制是安全的
    static FORCE_INLINE void sso_buffer_copy_sized(void* dst, const void* src, uint8_t sz) noexcept
    {
        if (sz <= 8) [[likely]]
        {
            *static_cast<uint64_t*>(dst) = *static_cast<const uint64_t*>(src);
        }
        else if (sz <= 16)
        {
            struct sso_copy_16 { uint64_t q[2]; };
            *static_cast<sso_copy_16*>(dst) = *static_cast<const sso_copy_16*>(src);
        }
        else if (sz <= 32)
        {
            struct sso_copy_32 { uint64_t q[4]; };
            *static_cast<sso_copy_32*>(dst) = *static_cast<const sso_copy_32*>(src);
        }
        else
        {
            sso_buffer_copy(dst, src);
        }
    }

    template<typename T>
    static const vtable& get_sso_vtable()
    {
        static const vtable vt = []{
            vtable v{};
            v.type_id = type_id::get_type_id<T>();
            v.element_size = sizeof(T);

            if constexpr (std::is_trivially_copyable_v<T>)
            {
                v.copy_to = nullptr;
                v.move_to = nullptr;
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

    // 内联模式: SSO + trivially_copyable + trivially_destructible, 无需 vtable
    // type_tag 地址是链接期常量, 编码到 [53:1], 构造时无 guard 检查
    template<typename T>
    static FORCE_INLINE uintptr_t get_inline_encoded() noexcept
    {
        const char* tag = &void_any_detail::type_tag_holder<T>::tag;
        uintptr_t ptr_val = reinterpret_cast<uintptr_t>(tag);
        return inline_type_id_bit | sso_bit
             | trivial_dtor_bit | trivial_copy_bit
             | (static_cast<uintptr_t>(sizeof(T)) << size_shift)
             | ((ptr_val << 1) & vtable_ptr_mask);
    }

    // SSO vtable 模式: 合并 vtable 初始化与编码值计算, 单次 guard 检查
    template<typename T>
    static uintptr_t get_sso_vtable_encoded()
    {
        static const uintptr_t encoded = []{
            const vtable& vt = get_sso_vtable<T>();
            uintptr_t v = reinterpret_cast<uintptr_t>(&vt) | sso_bit
                        | (static_cast<uintptr_t>(sizeof(T)) << size_shift);
            if constexpr (std::is_trivially_destructible_v<T>) v |= trivial_dtor_bit;
            if constexpr (std::is_trivially_copyable_v<T>) v |= trivial_copy_bit;
            return v;
        }();
        return encoded;
    }

    // heap vtable 模式: 合并 vtable 初始化与编码值计算, 单次 guard 检查
    template<typename T>
    static uintptr_t get_heap_vtable_encoded()
    {
        static const uintptr_t encoded = []{
            const vtable& vt = get_heap_vtable<T>();
            uintptr_t v = reinterpret_cast<uintptr_t>(&vt);
            if constexpr (std::is_trivially_destructible_v<T>) v |= trivial_dtor_bit;
            if constexpr (std::is_trivially_copyable_v<T>) v |= trivial_copy_bit;
            return v;
        }();
        return encoded;
    }

    // 编译期判断是否可用内联模式
    template<typename T>
    static constexpr bool use_inline_encoded_v =
        std::is_trivially_destructible_v<T> && std::is_trivially_copyable_v<T>;

    template<typename T>
    static const vtable& get_heap_vtable()
    {
        static const vtable vt = []{
            vtable v{};
            v.type_id = type_id::get_type_id<T>();
            v.element_size = sizeof(T);

            if constexpr (std::is_trivially_copyable_v<T>)
            {
                v.destroy = [](void* p) noexcept
                {
                    void_any_deallocate(p);
                };
                v.copy_to = nullptr;
                v.move_to = nullptr;
                if constexpr (std::is_copy_constructible_v<T>)
                {
                    v.clone = [](const void* src) noexcept -> void*
                    {
                        void* new_ptr = void_any_allocate(sizeof(T));
                        if (!new_ptr) [[unlikely]]
                        {
                            return nullptr;
                        }
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
                    void_any_deallocate(p);
                };
                v.copy_to = nullptr;
                v.move_to = nullptr;
                if constexpr (std::is_copy_constructible_v<T>)
                {
                    v.clone = [](const void* src) noexcept -> void*
                    {
                        void* new_ptr = void_any_allocate(sizeof(T));
                        if (!new_ptr) [[unlikely]]
                        {
                            return nullptr;
                        }
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

    FORCE_INLINE void destroy_data() noexcept
    {
        if (!has_value_fast()) { return; }

#if INTPTR_MAX == INT64_MAX
        if (is_inline_type_id()) [[likely]] { return; }
#endif

        if (is_trivially_destructible()) [[likely]]
        {
            if (is_sso()) [[likely]]
            {
                return;
            }
            const vtable* vt = get_vtable();
            if (vt->destroy) [[likely]]
            {
#if defined(VOID_ANY_ENABLE_SSO)
                vt->destroy(storage_.ptr_);
#else
                vt->destroy(ptr_);
#endif
            }
            return;
        }

        const vtable* vt = get_vtable();
        if (!vt->destroy) { return; }
#if defined(VOID_ANY_ENABLE_SSO)
        if (is_sso()) [[likely]]
        {
            vt->destroy(storage_.sso_data_);
        }
        else
        {
            vt->destroy(storage_.ptr_);
        }
#else
        vt->destroy(ptr_);
#endif
    }

    template<typename T>
    FORCE_INLINE void construct_from(T&& object) noexcept
    {
        using DecayedT = std::decay_t<T>;
        [[maybe_unused]] constexpr bool trivial_dtor = std::is_trivially_destructible_v<DecayedT>;
        [[maybe_unused]] constexpr bool trivial_copy = std::is_trivially_copyable_v<DecayedT>;

#if defined(VOID_ANY_ENABLE_SSO)
        constexpr bool use_sso = sizeof(DecayedT) <= sso_buffer_size
                              && alignof(DecayedT) <= sso_alignment;

        if constexpr (use_sso)
        {
#if INTPTR_MAX == INT64_MAX
            if constexpr (use_inline_encoded_v<DecayedT>)
            {
                vtable_sso_type_ = get_inline_encoded<DecayedT>();
            }
            else
            {
                vtable_sso_type_ = get_sso_vtable_encoded<DecayedT>();
            }
#else
            set_encoded(&get_sso_vtable<DecayedT>(), true, trivial_dtor, trivial_copy,
                        static_cast<uint8_t>(sizeof(DecayedT)));
#endif
            new (storage_.sso_data_) DecayedT(std::forward<T>(object));
            return;
        }
#endif

#if INTPTR_MAX == INT64_MAX
        vtable_sso_type_ = get_heap_vtable_encoded<DecayedT>();
#else
        set_encoded(&get_heap_vtable<DecayedT>(), false, trivial_dtor, trivial_copy, 0);
#endif

        void* new_ptr = void_any_allocate(sizeof(DecayedT));

        if (!new_ptr) [[unlikely]]
        {
#if INTPTR_MAX == INT64_MAX
            vtable_sso_type_ = 0;
#else
            vtable_ = nullptr;
#endif
            return;
        }
        new (new_ptr) DecayedT(std::forward<T>(object));

#if defined(VOID_ANY_ENABLE_SSO)
        storage_.ptr_ = new_ptr;
#else
        ptr_ = new_ptr;
#endif
    }

public:
    define_void_any() noexcept = default;

    template<typename T>
        requires (!std::is_same_v<std::decay_t<T>, define_void_any>)
    FORCE_INLINE define_void_any(T&& object) noexcept
    {
        construct_from(std::forward<T>(object));
    }

    FORCE_INLINE ~define_void_any() noexcept
    {
        destroy_data();
    }

    template<typename T>
        requires (!std::is_same_v<std::decay_t<T>, define_void_any>)
    FORCE_INLINE void set(T&& object) noexcept
    {
        using DecayedT = std::decay_t<T>;

        // 同类型 fast path: 直接赋值, 跳过析构+构造
        if (has_value_fast()) [[likely]]
        {
            bool same_type = false;
            void* data_ptr = nullptr;
#if INTPTR_MAX == INT64_MAX
            if (is_inline_type_id()) [[unlikely]]
            {
                same_type = (get_inline_type_tag_ptr() == &void_any_detail::type_tag_holder<DecayedT>::tag);
            }
            else
            {
                same_type = (get_vtable()->type_id == type_id::get_type_id<DecayedT>());
            }
#else
            same_type = (vtable_ && vtable_->type_id == type_id::get_type_id<DecayedT>());
#endif
            if (same_type) [[likely]]
            {
                data_ptr = get_void_ptr();
                if constexpr (std::is_trivially_copyable_v<DecayedT>)
                {
                    std::memcpy(data_ptr, &object, sizeof(DecayedT));
                }
                else
                {
                    *static_cast<DecayedT*>(data_ptr) = std::forward<T>(object);
                }
                return;
            }
        }

        // 慢路径: 析构旧值 + 构造新值
        destroy_data();
#if INTPTR_MAX == INT64_MAX
        vtable_sso_type_ = 0;
#else
        vtable_ = nullptr;
#endif
        construct_from(std::forward<T>(object));
    }

    FORCE_INLINE define_void_any(const define_void_any& other) noexcept
    {
#if INTPTR_MAX == INT64_MAX
        uintptr_t vst = other.vtable_sso_type_;
        vtable_sso_type_ = vst;

        if (!vst) [[unlikely]] { return; }

        if (vst & inline_type_id_bit) [[likely]]
        {
            sso_buffer_copy_sized(storage_.sso_data_, other.storage_.sso_data_,
                                  static_cast<uint8_t>((vst & size_mask) >> size_shift));
            return;
        }

        if (vst & sso_bit) [[likely]]
        {
            if (vst & trivial_copy_bit) [[likely]]
            {
                sso_buffer_copy_sized(storage_.sso_data_, other.storage_.sso_data_,
                                      static_cast<uint8_t>((vst & size_mask) >> size_shift));
            }
            else
            {
                const vtable* vt = reinterpret_cast<const vtable*>(vst & vtable_ptr_mask);
                if (vt->copy_to)
                {
                    vt->copy_to(storage_.sso_data_, other.storage_.sso_data_);
                }
                else
                {
                    std::memcpy(storage_.sso_data_, other.storage_.sso_data_, vt->element_size);
                }
            }
        }
        else
        {
            const vtable* vt = reinterpret_cast<const vtable*>(vst & vtable_ptr_mask);
            storage_.ptr_ = vt->clone ? vt->clone(other.storage_.ptr_) : nullptr;
        }
#else
        vtable_ = other.vtable_;
        is_sso_ = other.is_sso_;
        if (!other.has_value_fast()) { return; }

#if defined(VOID_ANY_ENABLE_SSO)
        if (other.is_sso()) [[likely]]
        {
            if (other.is_trivially_copyable()) [[likely]]
            {
                sso_buffer_copy(storage_.sso_data_, other.storage_.sso_data_);
            }
            else
            {
                const vtable* vt = other.get_vtable();
                if (vt->copy_to)
                {
                    vt->copy_to(storage_.sso_data_, other.storage_.sso_data_);
                }
                else
                {
                    std::memcpy(storage_.sso_data_, other.storage_.sso_data_, vt->element_size);
                }
            }
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
#endif
    }

    FORCE_INLINE define_void_any(define_void_any&& other) noexcept
    {
#if INTPTR_MAX == INT64_MAX
        uintptr_t vst = other.vtable_sso_type_;
        vtable_sso_type_ = vst;

        if (!vst) [[unlikely]] { return; }

        if (vst & inline_type_id_bit) [[likely]]
        {
            sso_buffer_copy_sized(storage_.sso_data_, other.storage_.sso_data_,
                                  static_cast<uint8_t>((vst & size_mask) >> size_shift));
            other.vtable_sso_type_ = 0;
            return;
        }

        if (vst & sso_bit) [[likely]]
        {
            if (vst & trivial_copy_bit) [[likely]]
            {
                sso_buffer_copy_sized(storage_.sso_data_, other.storage_.sso_data_,
                                      static_cast<uint8_t>((vst & size_mask) >> size_shift));
            }
            else
            {
                const vtable* vt = reinterpret_cast<const vtable*>(vst & vtable_ptr_mask);
                if (vt->move_to)
                {
                    vt->move_to(storage_.sso_data_, other.storage_.sso_data_);
                }
                else
                {
                    std::memcpy(storage_.sso_data_, other.storage_.sso_data_, vt->element_size);
                }
            }
        }
        else
        {
            storage_.ptr_ = other.storage_.ptr_;
        }

        other.vtable_sso_type_ = 0;
#else
        vtable_ = other.vtable_;
        is_sso_ = other.is_sso_;

        if (!other.has_value_fast()) { return; }

#if defined(VOID_ANY_ENABLE_SSO)
        if (other.is_sso()) [[likely]]
        {
            if (other.is_trivially_copyable()) [[likely]]
            {
                sso_buffer_copy(storage_.sso_data_, other.storage_.sso_data_);
            }
            else
            {
                const vtable* vt = other.get_vtable();
                if (vt->move_to)
                {
                    vt->move_to(storage_.sso_data_, other.storage_.sso_data_);
                }
                else
                {
                    std::memcpy(storage_.sso_data_, other.storage_.sso_data_, vt->element_size);
                }
            }
        }
        else
        {
            storage_.ptr_ = other.storage_.ptr_;
        }
#else
        ptr_ = other.ptr_;
#endif

        other.vtable_ = nullptr;
#endif
    }

    FORCE_INLINE define_void_any& operator=(const define_void_any& other) noexcept
    {
        if (this != &other) [[likely]]
        {
            destroy_data();
#if INTPTR_MAX == INT64_MAX
            uintptr_t vst = other.vtable_sso_type_;
            vtable_sso_type_ = vst;

            if (!vst) [[unlikely]] { return *this; }

            if (vst & inline_type_id_bit) [[likely]]
            {
                sso_buffer_copy_sized(storage_.sso_data_, other.storage_.sso_data_,
                                      static_cast<uint8_t>((vst & size_mask) >> size_shift));
                return *this;
            }

            if (vst & sso_bit) [[likely]]
            {
                if (vst & trivial_copy_bit) [[likely]]
                {
                    sso_buffer_copy_sized(storage_.sso_data_, other.storage_.sso_data_,
                                          static_cast<uint8_t>((vst & size_mask) >> size_shift));
                }
                else
                {
                    const vtable* vt = reinterpret_cast<const vtable*>(vst & vtable_ptr_mask);
                    if (vt->copy_to)
                    {
                        vt->copy_to(storage_.sso_data_, other.storage_.sso_data_);
                    }
                    else
                    {
                        std::memcpy(storage_.sso_data_, other.storage_.sso_data_, vt->element_size);
                    }
                }
            }
            else
            {
                const vtable* vt = reinterpret_cast<const vtable*>(vst & vtable_ptr_mask);
                storage_.ptr_ = vt->clone ? vt->clone(other.storage_.ptr_) : nullptr;
            }
#else
            vtable_ = other.vtable_;
            is_sso_ = other.is_sso_;

            if (!other.has_value_fast()) { return *this; }

#if defined(VOID_ANY_ENABLE_SSO)
            if (other.is_sso()) [[likely]]
            {
                if (other.is_trivially_copyable()) [[likely]]
                {
                    sso_buffer_copy(storage_.sso_data_, other.storage_.sso_data_);
                }
                else
                {
                    const vtable* vt = other.get_vtable();
                    if (vt->copy_to)
                    {
                        vt->copy_to(storage_.sso_data_, other.storage_.sso_data_);
                    }
                    else
                    {
                        std::memcpy(storage_.sso_data_, other.storage_.sso_data_, vt->element_size);
                    }
                }
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
#endif
        }
        return *this;
    }

    FORCE_INLINE define_void_any& operator=(define_void_any&& other) noexcept
    {
        if (this != &other) [[likely]]
        {
            destroy_data();
#if INTPTR_MAX == INT64_MAX
            uintptr_t vst = other.vtable_sso_type_;
            vtable_sso_type_ = vst;

            if (!vst) [[unlikely]] { return *this; }

            if (vst & inline_type_id_bit) [[likely]]
            {
                sso_buffer_copy_sized(storage_.sso_data_, other.storage_.sso_data_,
                                      static_cast<uint8_t>((vst & size_mask) >> size_shift));
                other.vtable_sso_type_ = 0;
                return *this;
            }

            if (vst & sso_bit) [[likely]]
            {
                if (vst & trivial_copy_bit) [[likely]]
                {
                    sso_buffer_copy_sized(storage_.sso_data_, other.storage_.sso_data_,
                                          static_cast<uint8_t>((vst & size_mask) >> size_shift));
                }
                else
                {
                    const vtable* vt = reinterpret_cast<const vtable*>(vst & vtable_ptr_mask);
                    if (vt->move_to)
                    {
                        vt->move_to(storage_.sso_data_, other.storage_.sso_data_);
                    }
                    else
                    {
                        std::memcpy(storage_.sso_data_, other.storage_.sso_data_, vt->element_size);
                    }
                }
            }
            else
            {
                storage_.ptr_ = other.storage_.ptr_;
            }

            other.vtable_sso_type_ = 0;
#else
            vtable_ = other.vtable_;
            is_sso_ = other.is_sso_;

            if (!other.has_value_fast()) { return *this; }

#if defined(VOID_ANY_ENABLE_SSO)
            if (other.is_sso()) [[likely]]
            {
                if (other.is_trivially_copyable()) [[likely]]
                {
                    sso_buffer_copy(storage_.sso_data_, other.storage_.sso_data_);
                }
                else
                {
                    const vtable* vt = other.get_vtable();
                    if (vt->move_to)
                    {
                        vt->move_to(storage_.sso_data_, other.storage_.sso_data_);
                    }
                    else
                    {
                        std::memcpy(storage_.sso_data_, other.storage_.sso_data_, vt->element_size);
                    }
                }
            }
            else
            {
                storage_.ptr_ = other.storage_.ptr_;
            }
#else
            ptr_ = other.ptr_;
#endif

            other.vtable_ = nullptr;
#endif
        }
        return *this;
    }

    [[nodiscard]] FORCE_INLINE int type_id() const noexcept
    {
        if (!has_value_fast()) [[unlikely]] { return -1; }
#if INTPTR_MAX == INT64_MAX
        if (is_inline_type_id()) [[unlikely]]
        {
            // 返回 type_tag 指针低 32 位作为伪 type_id
            // 仅用于类型区分, 不与 vtable 路径的 int type_id 混用
            return static_cast<int>(reinterpret_cast<uintptr_t>(get_inline_type_tag_ptr()));
        }
#endif
        return get_vtable()->type_id;
    }

    template<typename T>
    [[nodiscard]] FORCE_INLINE T* get_ptr() noexcept
    {
        if (!has_value_fast()) [[unlikely]] { return nullptr; }
#if INTPTR_MAX == INT64_MAX
        if (is_inline_type_id()) [[unlikely]]
        {
            // 指针比较: 编译期常量, 无 guard 检查
            if (get_inline_type_tag_ptr() != &void_any_detail::type_tag_holder<T>::tag) [[unlikely]] { return nullptr; }
            return static_cast<T*>(get_void_ptr());
        }
#endif
        if (get_vtable()->type_id != type_id::get_type_id<T>()) [[unlikely]] { return nullptr; }
        return static_cast<T*>(get_void_ptr());
    }

    template<typename T>
    [[nodiscard]] FORCE_INLINE const T* get_ptr() const noexcept
    {
        if (!has_value_fast()) [[unlikely]] { return nullptr; }
#if INTPTR_MAX == INT64_MAX
        if (is_inline_type_id()) [[unlikely]]
        {
            if (get_inline_type_tag_ptr() != &void_any_detail::type_tag_holder<T>::tag) [[unlikely]] { return nullptr; }
            return static_cast<const T*>(get_void_ptr());
        }
#endif
        if (get_vtable()->type_id != type_id::get_type_id<T>()) [[unlikely]] { return nullptr; }
        return static_cast<const T*>(get_void_ptr());
    }

    template<typename T>
    [[nodiscard]] FORCE_INLINE T* fast_get_ptr() noexcept
    {
        return static_cast<T*>(get_void_ptr());
    }

    template<typename T>
    [[nodiscard]] FORCE_INLINE const T* fast_get_ptr() const noexcept
    {
        return static_cast<const T*>(get_void_ptr());
    }

    template<typename T>
    [[nodiscard]] FORCE_INLINE T* get_ptr_unchecked() noexcept
    {
#if defined(VOID_ANY_ENABLE_SSO)
        using DecayedT = std::decay_t<T>;
        constexpr bool use_sso = sizeof(DecayedT) <= sso_buffer_size
                              && alignof(DecayedT) <= sso_alignment;
        if constexpr (use_sso)
        {
            return reinterpret_cast<T*>(storage_.sso_data_);
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
    [[nodiscard]] FORCE_INLINE const T* get_ptr_unchecked() const noexcept
    {
#if defined(VOID_ANY_ENABLE_SSO)
        using DecayedT = std::decay_t<T>;
        constexpr bool use_sso = sizeof(DecayedT) <= sso_buffer_size
                              && alignof(DecayedT) <= sso_alignment;
        if constexpr (use_sso)
        {
            return reinterpret_cast<const T*>(storage_.sso_data_);
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
    [[nodiscard]] FORCE_INLINE T get() noexcept
    {
        T* p = get_ptr<T>();
        if (!p) [[unlikely]] { return T{}; }
        return *p;
    }

    [[nodiscard]] FORCE_INLINE bool has_value() const noexcept
    {
        return has_value_fast();
    }

    FORCE_INLINE void reset() noexcept
    {
        destroy_data();
#if INTPTR_MAX == INT64_MAX
        vtable_sso_type_ = 0;
#else
        vtable_ = nullptr;
#endif
    }

    template<typename T>
        requires (!std::is_same_v<std::decay_t<T>, define_void_any>)
    FORCE_INLINE void copy_from(const T& object) noexcept
    {
        using DecayedT = std::decay_t<T>;
        destroy_data();
        [[maybe_unused]] constexpr bool trivial_dtor = std::is_trivially_destructible_v<DecayedT>;
        [[maybe_unused]] constexpr bool trivial_copy = std::is_trivially_copyable_v<DecayedT>;

#if defined(VOID_ANY_ENABLE_SSO)
        constexpr bool use_sso = sizeof(DecayedT) <= sso_buffer_size
                              && alignof(DecayedT) <= sso_alignment;
        if constexpr (use_sso)
        {
#if INTPTR_MAX == INT64_MAX
            if constexpr (use_inline_encoded_v<DecayedT>)
            {
                vtable_sso_type_ = get_inline_encoded<DecayedT>();
            }
            else
            {
                vtable_sso_type_ = get_sso_vtable_encoded<DecayedT>();
            }
#else
            set_encoded(&get_sso_vtable<DecayedT>(), true, trivial_dtor, trivial_copy,
                        static_cast<uint8_t>(sizeof(DecayedT)));
#endif
            if constexpr (std::is_trivially_copyable_v<DecayedT>)
            {
                std::memcpy(storage_.sso_data_, &object, sizeof(DecayedT));
            }
            else
            {
                new (storage_.sso_data_) DecayedT(object);
            }
            return;
        }
#endif
#if INTPTR_MAX == INT64_MAX
        vtable_sso_type_ = get_heap_vtable_encoded<DecayedT>();
#else
        set_encoded(&get_heap_vtable<DecayedT>(), false, trivial_dtor, trivial_copy, 0);
#endif
        void* new_ptr = void_any_allocate(sizeof(DecayedT));
        if (!new_ptr) [[unlikely]]
        {
#if INTPTR_MAX == INT64_MAX
            vtable_sso_type_ = 0;
#else
            vtable_ = nullptr;
#endif
            return;
        }
        if constexpr (std::is_trivially_copyable_v<DecayedT>)
        {
            std::memcpy(new_ptr, &object, sizeof(DecayedT));
        }
        else
        {
            new (new_ptr) DecayedT(object);
        }
#if defined(VOID_ANY_ENABLE_SSO)
        storage_.ptr_ = new_ptr;
#else
        ptr_ = new_ptr;
#endif
    }

    template<typename T>
        requires (!std::is_same_v<std::decay_t<T>, define_void_any>)
    FORCE_INLINE void move_from(T&& object) noexcept
    {
        using DecayedT = std::decay_t<T>;
        destroy_data();
        [[maybe_unused]] constexpr bool trivial_dtor = std::is_trivially_destructible_v<DecayedT>;
        [[maybe_unused]] constexpr bool trivial_copy = std::is_trivially_copyable_v<DecayedT>;

#if defined(VOID_ANY_ENABLE_SSO)
        constexpr bool use_sso = sizeof(DecayedT) <= sso_buffer_size
                              && alignof(DecayedT) <= sso_alignment;
        if constexpr (use_sso)
        {
#if INTPTR_MAX == INT64_MAX
            if constexpr (use_inline_encoded_v<DecayedT>)
            {
                vtable_sso_type_ = get_inline_encoded<DecayedT>();
            }
            else
            {
                vtable_sso_type_ = get_sso_vtable_encoded<DecayedT>();
            }
#else
            set_encoded(&get_sso_vtable<DecayedT>(), true, trivial_dtor, trivial_copy,
                        static_cast<uint8_t>(sizeof(DecayedT)));
#endif
            if constexpr (std::is_trivially_copyable_v<DecayedT>)
            {
                std::memcpy(storage_.sso_data_, &object, sizeof(DecayedT));
            }
            else
            {
                new (storage_.sso_data_) DecayedT(std::forward<T>(object));
            }
            return;
        }
#endif
#if INTPTR_MAX == INT64_MAX
        vtable_sso_type_ = get_heap_vtable_encoded<DecayedT>();
#else
        set_encoded(&get_heap_vtable<DecayedT>(), false, trivial_dtor, trivial_copy, 0);
#endif
        void* new_ptr = void_any_allocate(sizeof(DecayedT));
        if (!new_ptr) [[unlikely]]
        {
#if INTPTR_MAX == INT64_MAX
            vtable_sso_type_ = 0;
#else
            vtable_ = nullptr;
#endif
            return;
        }
        new (new_ptr) DecayedT(std::forward<T>(object));
#if defined(VOID_ANY_ENABLE_SSO)
        storage_.ptr_ = new_ptr;
#else
        ptr_ = new_ptr;
#endif
    }

    [[nodiscard]] FORCE_INLINE void* get_void() noexcept
    {
        return has_value_fast() ? get_void_ptr() : nullptr;
    }

    [[nodiscard]] FORCE_INLINE const void* get_void() const noexcept
    {
        return has_value_fast() ? get_void_ptr() : nullptr;
    }
};

// void_any: 唯一对外别名, 通用类型擦除容器
using void_any = define_void_any<56, 8>;
