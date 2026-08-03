#pragma once

// storage.hpp - 反射元数据存储类与全局存储对象
// 命名空间: reflect

#include <atomic>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <array>
#include <initializer_list>
#include <concepts>
#include <source_location>
#include <utility>
#include "../part/type_id.hpp"
#include "../part/aggregate_reflect.hpp"
#include "../part/member_offset.hpp"
#include "../part/type_erasure.hpp"
#include "../part/fnv1a.hpp"
#include "../part/type_ops.hpp"
#include "../part/container_traits.hpp"
#include "meta.hpp"

namespace reflect {

namespace detail {

struct spinlock_guard
{
    std::atomic_flag& flag_;
    explicit spinlock_guard(std::atomic_flag& f) noexcept : flag_(f)
    {
        while (flag_.test_and_set(std::memory_order_acquire))
        {
        }
    }
    ~spinlock_guard() noexcept { flag_.clear(std::memory_order_release); }
    spinlock_guard(const spinlock_guard&) = delete;
    spinlock_guard& operator=(const spinlock_guard&) = delete;
};

// #12 生成方法参数类型 id 静态数组, 返回指针 (生命周期: 程序结束)
template<typename MFnType>
struct static_arg_ids_holder
{
    static const int* get() noexcept
    {
        static const auto ids = arg_ids_maker<MFnType>::make();
        return ids.data();
    }
};

template<typename MFnType>
inline const int* make_arg_type_ids() noexcept
{
    return static_arg_ids_holder<MFnType>::get();
}

// === C++20 concepts 与类型萃取 ===

// std::array 探测 (用于数组字段约束)
template<typename T> struct is_std_array : std::false_type {};
template<typename T, std::size_t N> struct is_std_array<std::array<T, N>> : std::true_type {};
template<typename T> inline constexpr bool is_std_array_v = is_std_array<T>::value;

// std::array 元素类型与大小提取
template<typename T> struct std_array_traits;
template<typename T, std::size_t N>
struct std_array_traits<std::array<T, N>> {
    using element_type = T;
    static constexpr std::size_t size = N;
};
template<typename T>
using std_array_element_t = typename std_array_traits<T>::element_type;
template<typename T>
inline constexpr std::size_t std_array_size_v = std_array_traits<T>::size;

// 数组字段类型约束: C 数组或 std::array
template<typename M>
concept array_field_type =
    std::is_bounded_array_v<std::remove_cvref_t<M>> ||
    is_std_array_v<std::remove_cvref_t<M>>;

// 成员对象指针约束
template<typename ptr_t>
concept member_object_pointer = std::is_member_object_pointer_v<ptr_t>;

// 成员函数指针约束
template<typename ptr_t>
concept member_function_pointer = std::is_member_function_pointer_v<ptr_t>;

// === 错误诊断 (替代裸 std::abort, 输出位置信息) ===
[[noreturn]] inline void abort_with_location(
    const char* msg,
    const std::source_location& loc = std::source_location::current()) noexcept
{
    std::fprintf(stderr, "[reflect] %s:%d `%s`: %s\n",
                 loc.file_name(), loc.line(), loc.function_name(), msg);
    std::abort();
}

} // namespace detail

class storage
{
public:
    std::array<std::atomic<type_meta*>, MAX_TYPE_ID> type_entries_{};

    storage() noexcept
    {
        for (size_t i = 0; i < MAX_TYPE_ID; ++i)
        {
            type_entries_[i].store(nullptr, std::memory_order_relaxed);
        }
    }

    storage(const storage&) = delete;
    storage& operator=(const storage&) = delete;
    storage(storage&&) = delete;
    storage& operator=(storage&&) = delete;

    // 注册类型 (聚合类型自动遍历公有字段, 字段名自动生成 field_N)
    template<typename T>
    void register_type(const char* name) noexcept
    {
        int tid = type_id::get_type_id<T>();
        if (tid < 0 || tid >= static_cast<int>(MAX_TYPE_ID))
        {
            return;
        }

        type_meta* existing = type_entries_[tid].load(std::memory_order_acquire);
        if (existing != nullptr)
        {
            return;
        }

        type_meta* m = new (std::nothrow) type_meta{};
        if (m == nullptr)
        {
            std::abort();
        }
        m->name = name;
        m->name_hash = fnv1a_runtime(name);
        m->size = static_cast<uint16_t>(sizeof(T));
        m->align = static_cast<uint16_t>(alignof(T));
        m->type_id = tid;

        // #1 构造/销毁注册
        if constexpr (std::is_default_constructible_v<T>)
        {
            m->default_construct_ = +[](void* buf) noexcept -> void*
            {
                if (buf)
                {
                    return new(buf) T{};
                }
                return new (std::nothrow) T{};
            };
            m->has_default_construct = true;
        }
        if constexpr (!std::is_trivially_destructible_v<T>)
        {
            m->destruct_ = +[](void* obj) noexcept
            {
                static_cast<T*>(obj)->~T();
            };
        }

        // #8/#9 type_ops 注册
        global_type_ops().register_type_ops<T>();

        // #5 容器特征注册
        if constexpr (sequential_container<T>)
        {
            global_container_ops().register_sequential<T>();
            m->container_ops = global_container_ops().get(tid);
        }

        if constexpr (std::is_aggregate_v<T> && aggregate_field_count_v<T> > 0)
        {
            T obj{};
            for_each_aggregate_member(obj, [&](auto& member, size_t idx) {
                using member_type = std::remove_reference_t<decltype(member)>;
                uint16_t fidx = m->field_count.load(std::memory_order_relaxed);
                if (fidx >= MAX_FIELDS_PER_TYPE)
                {
                    return;
                }
                m->fields[fidx] = field_meta{
                    make_field_name(idx),
                    static_cast<uint32_t>(
                        reinterpret_cast<size_t>(&member) -
                        reinterpret_cast<size_t>(&obj)),
                    type_id::get_type_id<member_type>(),
                    std::is_const_v<member_type>,
                    false,
                    0, 0, 0, {0, 0, 0, 0},
                    static_cast<uint32_t>(sizeof(member_type)),
                    {}
                };
                m->field_count.store(static_cast<uint16_t>(fidx + 1), std::memory_order_release);
            });
        }
        m->registered.store(true, std::memory_order_release);

        type_meta* expected = nullptr;
        if (!type_entries_[tid].compare_exchange_strong(
                expected, m, std::memory_order_release, std::memory_order_acquire))
        {
            delete m;
        }
    }

    // 只注册类型元信息 (不自动遍历字段, 用于含 C 数组的类型需手动 REGISTER_ARRAY_FIELD)
    template<typename T>
    void register_type_only(const char* name) noexcept
    {
        int tid = type_id::get_type_id<T>();
        if (tid < 0 || tid >= static_cast<int>(MAX_TYPE_ID))
        {
            return;
        }

        type_meta* existing = type_entries_[tid].load(std::memory_order_acquire);
        if (existing != nullptr)
        {
            return;
        }

        type_meta* m = new (std::nothrow) type_meta{};
        if (m == nullptr)
        {
            std::abort();
        }
        m->name = name;
        m->name_hash = fnv1a_runtime(name);
        m->size = static_cast<uint16_t>(sizeof(T));
        m->align = static_cast<uint16_t>(alignof(T));
        m->type_id = tid;

        // #1 构造/销毁注册
        if constexpr (std::is_default_constructible_v<T>)
        {
            m->default_construct_ = +[](void* buf) noexcept -> void*
            {
                if (buf)
                {
                    return new(buf) T{};
                }
                return new (std::nothrow) T{};
            };
            m->has_default_construct = true;
        }
        if constexpr (!std::is_trivially_destructible_v<T>)
        {
            m->destruct_ = +[](void* obj) noexcept
            {
                static_cast<T*>(obj)->~T();
            };
        }

        // #8/#9 type_ops 注册
        global_type_ops().register_type_ops<T>();

        // #5 容器特征注册
        if constexpr (sequential_container<T>)
        {
            global_container_ops().register_sequential<T>();
            m->container_ops = global_container_ops().get(tid);
        }

        m->registered.store(true, std::memory_order_release);

        type_meta* expected = nullptr;
        if (!type_entries_[tid].compare_exchange_strong(
                expected, m, std::memory_order_release, std::memory_order_acquire))
        {
            delete m;
        }
    }

    // 手填偏移量注册私有成员
    template<typename T>
    void register_private_offsets(const offset_desc* descs, size_t count) noexcept
    {
        int tid = type_id::get_type_id<T>();
        if (tid < 0 || tid >= static_cast<int>(MAX_TYPE_ID))
        {
            return;
        }

        type_meta* m = type_entries_[tid].load(std::memory_order_acquire);
        if (m == nullptr || !m->registered.load(std::memory_order_acquire))
        {
            return;
        }

        detail::spinlock_guard lock(reg_lock_);
        for (size_t i = 0; i < count; ++i)
        {
            uint16_t fidx = m->field_count.load(std::memory_order_relaxed);
            if (fidx >= MAX_FIELDS_PER_TYPE)
            {
                break;
            }
            m->fields[fidx] = field_meta{
                descs[i].name,
                static_cast<uint32_t>(descs[i].offset),
                descs[i].type_id,
                false,
                true,
                0, 0, 0, {0, 0, 0, 0},
                descs[i].size,
                {}
            };
            m->field_count.store(static_cast<uint16_t>(fidx + 1), std::memory_order_release);
        }
    }

    // 成员指针注册字段 (偏移量和类型自动推导)
    template<typename T, typename M, M T::*Ptr>
        requires detail::member_object_pointer<decltype(Ptr)>
    void register_field(const char* name,
        const std::source_location& loc = std::source_location::current()) noexcept
    {
        int tid = type_id::get_type_id<T>();
        if (tid < 0 || tid >= static_cast<int>(MAX_TYPE_ID))
        {
            return;
        }

        type_meta* m = type_entries_[tid].load(std::memory_order_acquire);
        if (m == nullptr || !m->registered.load(std::memory_order_acquire))
        {
            return;
        }

        detail::spinlock_guard lock(reg_lock_);
        uint16_t fidx = m->field_count.load(std::memory_order_relaxed);
        if (fidx >= MAX_FIELDS_PER_TYPE)
        {
            detail::abort_with_location("field limit exceeded", loc);
        }

        T* null_obj = nullptr;
        size_t offset = reinterpret_cast<size_t>(&(null_obj->*Ptr));
        m->fields[fidx] = field_meta{
            name,
            static_cast<uint32_t>(offset),
            type_id::get_type_id<M>(),
            std::is_const_v<M>,
            true,
            0, 0, 0, {0, 0, 0, 0},
            static_cast<uint32_t>(sizeof(M)),
            {}
        };
        m->field_count.store(static_cast<uint16_t>(fidx + 1), std::memory_order_release);
    }

    // 注册数组字段 (C 数组 / std::array)
    // rank: 维度数 (1~4); extents: 每维元素数; element_type_id: 元素类型 id
    template<typename T, typename M, M T::*Ptr>
        requires (detail::member_object_pointer<decltype(Ptr)> &&
                  detail::array_field_type<M>)
    void register_array_field(const char* name,
                              uint8_t rank,
                              const uint16_t* extents,
                              int element_type_id,
        const std::source_location& loc = std::source_location::current()) noexcept
    {
        int tid = type_id::get_type_id<T>();
        if (tid < 0 || tid >= static_cast<int>(MAX_TYPE_ID))
        {
            return;
        }

        type_meta* m = type_entries_[tid].load(std::memory_order_acquire);
        if (m == nullptr || !m->registered.load(std::memory_order_acquire))
        {
            return;
        }
        if (rank == 0 || rank > 4)
        {
            return;
        }

        detail::spinlock_guard lock(reg_lock_);
        uint16_t fidx = m->field_count.load(std::memory_order_relaxed);
        if (fidx >= MAX_FIELDS_PER_TYPE)
        {
            detail::abort_with_location("field limit exceeded", loc);
        }

        T* null_obj = nullptr;
        size_t offset = reinterpret_cast<size_t>(&(null_obj->*Ptr));

        uint32_t total = 1;
        for (uint8_t i = 0; i < rank; ++i)
        {
            total *= static_cast<uint32_t>(extents[i]);
        }

        using element_type = std::remove_all_extents_t<std::remove_cvref_t<M>>;
        uint32_t stride = static_cast<uint32_t>(sizeof(element_type));

        std::array<uint16_t, 4> ext{0, 0, 0, 0};
        for (uint8_t i = 0; i < rank; ++i)
        {
            ext[i] = extents[i];
        }

        m->fields[fidx] = field_meta{
            name,
            static_cast<uint32_t>(offset),
            element_type_id,
            std::is_const_v<M>,
            true,
            rank, 0, total,
            {ext[0], ext[1], ext[2], ext[3]},
            stride,
            {}
        };
        m->field_count.store(static_cast<uint16_t>(fidx + 1), std::memory_order_release);
    }

    // 注册数组字段 (自动推导 rank/extents/element_type)
    template<typename T, typename M, M T::*Ptr>
        requires (detail::member_object_pointer<decltype(Ptr)> &&
                  detail::array_field_type<M>)
    void register_array_field_auto(const char* name,
        const std::source_location& loc = std::source_location::current()) noexcept
    {
        using raw_m = std::remove_cvref_t<M>;

        uint8_t rank = 0;
        std::array<uint16_t, 4> ext{0, 0, 0, 0};
        int elem_tid = 0;

        if constexpr (std::is_bounded_array_v<raw_m>)
        {
            constexpr uint8_t c_rank = std::rank_v<raw_m>;
            rank = c_rank;
            if constexpr (c_rank >= 1)
            {
                ext[0] = static_cast<uint16_t>(std::extent_v<raw_m, 0>);
            }
            if constexpr (c_rank >= 2)
            {
                ext[1] = static_cast<uint16_t>(std::extent_v<raw_m, 1>);
            }
            if constexpr (c_rank >= 3)
            {
                ext[2] = static_cast<uint16_t>(std::extent_v<raw_m, 2>);
            }
            if constexpr (c_rank >= 4)
            {
                ext[3] = static_cast<uint16_t>(std::extent_v<raw_m, 3>);
            }
            using elem = std::remove_all_extents_t<raw_m>;
            elem_tid = type_id::get_type_id<elem>();
        }
        else if constexpr (detail::is_std_array_v<raw_m>)
        {
            rank = 1;
            ext[0] = static_cast<uint16_t>(detail::std_array_size_v<raw_m>);
            using elem = detail::std_array_element_t<raw_m>;
            elem_tid = type_id::get_type_id<elem>();
        }

        register_array_field<T, M, Ptr>(name, rank, ext.data(), elem_tid, loc);
    }

    // 注册成员 (标量/数组统一入口)
    template<typename T, typename M, M T::*Ptr>
        requires detail::member_object_pointer<decltype(Ptr)>
    void register_member_auto(const char* type_name, const char* field_name,
        const std::source_location& loc = std::source_location::current()) noexcept
    {
        int tid = type_id::get_type_id<T>();
        if (tid < 0 || tid >= static_cast<int>(MAX_TYPE_ID))
        {
            return;
        }
        if (type_entries_[tid].load(std::memory_order_acquire) == nullptr)
        {
            register_type_only<T>(type_name);
        }

        if constexpr (detail::array_field_type<M>)
        {
            register_array_field_auto<T, M, Ptr>(field_name, loc);
        }
        else
        {
            register_field<T, M, Ptr>(field_name, loc);
        }
    }

    // 注册成员方法
    template<auto Fn>
    void register_method(const char* name,
        const std::source_location& loc = std::source_location::current()) noexcept
    {
        using mfn_type = decltype(Fn);
        using traits = mfn_traits<mfn_type>;
        using C = typename traits::class_type;
        using R = typename traits::return_type;

        int tid = type_id::get_type_id<C>();
        if (tid < 0 || tid >= static_cast<int>(MAX_TYPE_ID))
        {
            return;
        }

        type_meta* m = type_entries_[tid].load(std::memory_order_acquire);
        if (m == nullptr || !m->registered.load(std::memory_order_acquire))
        {
            return;
        }

        detail::spinlock_guard lock(reg_lock_);
        uint16_t midx = m->method_count.load(std::memory_order_relaxed);
        if (midx >= MAX_METHODS_PER_TYPE)
        {
            detail::abort_with_location("method limit exceeded", loc);
        }

        method_meta mm;
        mm.name = name;
        mm.arg_count = static_cast<uint8_t>(traits::arg_count);
        mm.return_type_id = return_type_id<R>();
        mm.invoker = &mfn_invoker_t<Fn, mfn_type>::invoke;
        mm.is_const = traits::is_const;
        mm.is_static = false;
        mm.arg_type_ids = detail::make_arg_type_ids<mfn_type>();
        mm.vtable_offset = -1;
        m->methods[midx] = mm;
        m->method_count.store(static_cast<uint16_t>(midx + 1), std::memory_order_release);
    }

    // 注册静态方法
    template<typename C, auto Fn>
    void register_static_method(const char* name,
        const std::source_location& loc = std::source_location::current()) noexcept
    {
        using mfn_type = decltype(Fn);
        using traits = mfn_traits<mfn_type>;
        using R = typename traits::return_type;
        static_assert(traits::is_static, "Fn must be a free/static function pointer");

        int tid = type_id::get_type_id<C>();
        if (tid < 0 || tid >= static_cast<int>(MAX_TYPE_ID))
        {
            return;
        }

        type_meta* m = type_entries_[tid].load(std::memory_order_acquire);
        if (m == nullptr || !m->registered.load(std::memory_order_acquire))
        {
            return;
        }

        detail::spinlock_guard lock(reg_lock_);
        uint16_t midx = m->method_count.load(std::memory_order_relaxed);
        if (midx >= MAX_METHODS_PER_TYPE)
        {
            detail::abort_with_location("method limit exceeded", loc);
        }

        method_meta mm;
        mm.name = name;
        mm.arg_count = static_cast<uint8_t>(traits::arg_count);
        mm.return_type_id = return_type_id<R>();
        mm.invoker = &sfn_invoker_t<Fn, mfn_type>::invoke;
        mm.is_const = false;
        mm.is_static = true;
        mm.arg_type_ids = detail::make_arg_type_ids<mfn_type>();
        mm.vtable_offset = -1;
        m->methods[midx] = mm;
        m->method_count.store(static_cast<uint16_t>(midx + 1), std::memory_order_release);
    }

    // 按类型 id 查询 (无锁, acquire)
    [[nodiscard]] const type_meta* get_type(int tid) const noexcept
    {
        if (tid < 0 || tid >= static_cast<int>(MAX_TYPE_ID))
        {
            return nullptr;
        }
        type_meta* m = type_entries_[tid].load(std::memory_order_acquire);
        if (m == nullptr)
        {
            return nullptr;
        }
        if (!m->registered.load(std::memory_order_acquire))
        {
            return nullptr;
        }
        return m;
    }

    // 按名查找类型 (遍历, 适合初始化阶段)
    [[nodiscard]] const type_meta* find_type(const char* name) const noexcept
    {
        for (size_t i = 0; i < MAX_TYPE_ID; ++i)
        {
            type_meta* m = type_entries_[i].load(std::memory_order_acquire);
            if (m == nullptr)
            {
                continue;
            }
            if (!m->registered.load(std::memory_order_acquire))
            {
                continue;
            }
            if (m->name != nullptr && std::strcmp(m->name, name) == 0)
            {
                return m;
            }
        }
        return nullptr;
    }

    // #10 按 name_hash 查找类型 (稳定标识, 跨 DLL/编译器)
    [[nodiscard]] const type_meta* find_type_by_hash(uint64_t hash) const noexcept
    {
        for (size_t i = 0; i < MAX_TYPE_ID; ++i)
        {
            type_meta* m = type_entries_[i].load(std::memory_order_acquire);
            if (m == nullptr)
            {
                continue;
            }
            if (!m->registered.load(std::memory_order_acquire))
            {
                continue;
            }
            if (m->name_hash == hash)
            {
                return m;
            }
        }
        return nullptr;
    }

    // #3 枚举注册
    template<typename E>
    void register_enum(const char* name,
        std::initializer_list<std::pair<E, const char*>> values) noexcept
    {
        static_assert(std::is_enum_v<E>, "register_enum requires enum type");
        int tid = type_id::get_type_id<E>();
        if (tid < 0 || tid >= static_cast<int>(MAX_TYPE_ID))
        {
            return;
        }

        enum_meta* existing = enum_entries_[tid].load(std::memory_order_acquire);
        if (existing != nullptr)
        {
            return;
        }

        enum_meta* em = new (std::nothrow) enum_meta{};
        if (em == nullptr)
        {
            std::abort();
        }
        em->name = name;
        em->type_id = tid;
        using UT = std::underlying_type_t<E>;
        em->underlying_type_id = type_id::get_type_id<UT>();
        em->values.reserve_exact(values.size());
        for (const auto& kv : values)
        {
            em->values.push_back(enum_value_entry{
                static_cast<uint64_t>(static_cast<UT>(kv.first)),
                kv.second,
                fnv1a_runtime(kv.second)
            });
        }
        em->registered = true;

        enum_meta* expected = nullptr;
        if (!enum_entries_[tid].compare_exchange_strong(
                expected, em, std::memory_order_release, std::memory_order_acquire))
        {
            delete em;
        }
    }

    // #3 获取枚举元数据
    [[nodiscard]] const enum_meta* find_enum(int tid) const noexcept
    {
        if (tid < 0 || tid >= static_cast<int>(MAX_TYPE_ID))
        {
            return nullptr;
        }
        return enum_entries_[tid].load(std::memory_order_acquire);
    }

    template<typename E>
    [[nodiscard]] const enum_meta* find_enum() const noexcept
    {
        return find_enum(type_id::get_type_id<E>());
    }

    // #2 继承关系注册
    template<typename Derived, typename Base>
    void register_base(ptrdiff_t offset = 0) noexcept
    {
        int did = type_id::get_type_id<Derived>();
        int bid = type_id::get_type_id<Base>();
        if (did < 0 || bid < 0)
        {
            return;
        }
        type_meta* dm = type_entries_[did].load(std::memory_order_acquire);
        type_meta* bm = type_entries_[bid].load(std::memory_order_acquire);
        if (!dm || !bm)
        {
            return;
        }

        detail::spinlock_guard lock(reg_lock_);
        dm->base_offsets.push_back(base_offset_entry{bid, offset});
        bm->derived_type_ids.push_back(did);
    }

    // #4 字段属性注册
    template<typename T, typename M, M T::*Ptr, typename V>
    void register_field_attr(const char* field_name,
        const char* attr_key, V&& value) noexcept
    {
        int tid = type_id::get_type_id<T>();
        if (tid < 0)
        {
            return;
        }
        type_meta* m = type_entries_[tid].load(std::memory_order_acquire);
        if (!m)
        {
            return;
        }

        for (uint16_t i = 0; i < m->field_count.load(std::memory_order_acquire); ++i)
        {
            if (m->fields[i].name && std::strcmp(m->fields[i].name, field_name) == 0)
            {
                attr_entry e;
                e.key_hash = fnv1a_runtime(attr_key);
                e.value = void_any(std::forward<V>(value));
                m->fields[i].attrs.push_back(std::move(e));
                break;
            }
        }
    }

    // #7 类型转换注册
    template<typename T, typename U>
    void register_convert() noexcept
    {
        static_assert(std::is_convertible_v<T, U>, "T must be convertible to U");
        int tid = type_id::get_type_id<T>();
        if (tid < 0)
        {
            return;
        }
        type_meta* m = type_entries_[tid].load(std::memory_order_acquire);
        if (!m)
        {
            return;
        }

        convert_entry e;
        e.target_type_id = type_id::get_type_id<U>();
        e.convert_fn = +[](const void* src, void* dst) noexcept
        {
            *static_cast<U*>(dst) = static_cast<U>(*static_cast<const T*>(src));
        };
        detail::spinlock_guard lock(reg_lock_);
        m->converters.push_back(e);
    }

private:
    std::atomic_flag reg_lock_{};
    std::array<std::atomic<enum_meta*>, MAX_TYPE_ID> enum_entries_{};

    static const char* make_field_name(size_t idx) noexcept
    {
        static std::array<std::array<char, 16>, MAX_FIELDS_PER_TYPE> names{};
        if (idx >= MAX_FIELDS_PER_TYPE)
        {
            return "field_?";
        }

        static std::atomic_flag name_lock{};
        while (name_lock.test_and_set(std::memory_order_acquire))
        {
        }
        char* p = names[idx].data();
        if (p[0] == '\0')
        {
            const char prefix[] = "field_";
            size_t i = 0;
            while (prefix[i])
            {
                p[i] = prefix[i];
                ++i;
            }
            size_t val = idx;
            std::array<char, 8> tmp{};
            size_t len = 0;
            if (val == 0)
            {
                tmp[len++] = '0';
            }
            while (val > 0)
            {
                tmp[len++] = '0' + (val % 10);
                val /= 10;
            }
            while (len > 0)
            {
                p[i++] = tmp[--len];
            }
            p[i] = '\0';
        }
        name_lock.clear(std::memory_order_release);
        return p;
    }
};

// 全局存储对象
inline storage& global() noexcept
{
    static storage instance;
    return instance;
}

} // namespace reflect
