#pragma once

#include <array>
#include <atomic>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <memory>
#include <new>
#include <optional>
#include <source_location>
#include <string_view>
#include <type_traits>
#include <utility>

#include "config/reflect_config.hpp"
#include "part/dense.hpp"
#include "part/fnv1a.hpp"
#include "part/force_inline.hpp"
#include "part/stable_array.hpp"
#include "part/operating_message.hpp"
#include "part/type/type_id.hpp"
#include "part/type/void_any.hpp"
#include "reflection/base/foundation.hpp"
#include "reflection/base/type_ops.hpp"
#include "reflection/base/invoke.hpp"
#include "reflection/base/meta.hpp"
#include "reflection/base/aggregate.hpp"

// storage.hpp - 槽位 / 名字索引 / 注册 / 查询入口
// 命名空间: reflect

// ============================================================================

// storage.hpp - 反射元数据存储类与全局存储对象
// 命名空间: reflect


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

template<typename M> struct array_element_of { using type = std::remove_all_extents_t<M>; };
template<typename T, std::size_t N> struct array_element_of<std::array<T, N>> { using type = T; };
template<typename M> using array_element_of_t = typename array_element_of<M>::type;

// 数组字段类型约束: C 数组或 std::array
template<typename M>
concept array_field_type =
    std::is_bounded_array_v<std::remove_cvref_t<M>> ||
    is_std_array_v<std::remove_cvref_t<M>>;

// 成员对象指针约束
template<typename ptr_t>
concept member_object_pointer = std::is_member_object_pointer_v<ptr_t>;

template<typename T>
inline void ensure_field_ops() noexcept
{
    if constexpr (std::is_trivially_copyable_v<T> || std::is_copy_constructible_v<T>)
    {
        global_ops().register_type_ops<std::remove_cv_t<T>>();
    }
}

// 字段标志合成
constexpr uint8_t field_flags_of(bool is_const, bool is_private) noexcept
{
    return static_cast<uint8_t>((is_const ? field_flag_const : 0u)
                                | (is_private ? field_flag_private : 0u));
}

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
    // 槽位表按需增长: 分块分配, 元素地址稳定
    stable_array<std::atomic<type_meta*>> type_entries_;

    storage() noexcept = default;

    ~storage() noexcept
    {
        for (size_t i = 0; i < type_entries_.size(); ++i)
        {
            delete type_entries_[i].load(std::memory_order_relaxed);
        }
        for (size_t i = 0; i < enum_entries_.size(); ++i)
        {
            delete enum_entries_[i].load(std::memory_order_relaxed);
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
        const int tid = type_id::get_type_id<T>();
        if (!ensure_type_slot_index(tid))
        {
            return;
        }
        type_meta* existing = type_entries_[tid].load(std::memory_order_acquire);
        if (existing != nullptr)
        {
            if (existing->field_count.load(std::memory_order_acquire) == 0)
            {
                traverse_aggregate_fields<T>(existing);
            }
            return;
        }
        (void)publish_meta<T, true>(tid, name);
    }

    // 只注册类型元信息 (不自动遍历字段)
    template<typename T>
    void register_type_only(const char* name) noexcept
    {
        const int tid = type_id::get_type_id<T>();
        if (!ensure_type_slot_index(tid))
        {
            return;
        }
        type_meta* existing = type_entries_[tid].load(std::memory_order_acquire);
        if (existing != nullptr)
        {
            if (existing->name == nullptr && name != nullptr)
            {
                bind_name(existing, name);
            }
            return;
        }
        (void)publish_meta<T, false>(tid, name);
    }

    // 单份元数据发布: 分配 + 填充 + 发布, 冲突时丢弃新对象
    template<typename T, bool Traverse>
    type_meta* publish_meta(int tid, const char* name) noexcept
    {
        type_meta* m = new (std::nothrow) type_meta{};
        if (m == nullptr)
        {
            std::abort();
        }
        m->type_id = tid;
        fill_type_identity<T>(m, name);
        if constexpr (Traverse && std::is_aggregate_v<T> && detail::field_count_v<T> > 0)
        {
            traverse_aggregate_fields<T>(m);
        }
        m->state.store(static_cast<uint8_t>(type_state::live), std::memory_order_release);

        type_meta* expected = nullptr;
        if (!type_entries_[tid].compare_exchange_strong(
                expected, m, std::memory_order_release, std::memory_order_acquire))
        {
            delete m;
            return expected;
        }
        type_count_.fetch_add(1, std::memory_order_relaxed);
        return m;
    }

    // 成员指针注册字段 (成员指针作为运行期参数)
    template<typename T, typename M>
        requires detail::member_object_pointer<M T::*>
    void register_field_runtime(const char* name, M T::*ptr,
        const std::source_location& loc = std::source_location::current()) noexcept
    {
        const int tid = type_id::get_type_id<T>();
        if (!ensure_type_slot_index(tid))
        {
            return;
        }
        type_meta* m = ensure_type_slot<T>(tid);
        detail::spinlock_guard lock(reg_lock_);
        const size_t offset = detail::offset_of(ptr);
        if (!try_append_field(m, name, offset, type_id::get_type_id<M>(),
                              detail::field_flags_of(std::is_const_v<M>, true), 0, 0, {0, 0, 0, 0},
                              static_cast<uint32_t>(sizeof(M)),
                              std::is_trivially_copyable_v<M>))
        {
            detail::abort_with_location("field storage allocation failed", loc);
        }
        detail::ensure_field_ops<std::remove_cv_t<M>>();
    }

    // 手填偏移注册字段: 用于无法取成员指针的第三方类型
    template<typename T>
    operating_message add_field_raw(const char* name, size_t offset, int field_type_id,
                                    uint32_t size, bool trivially_copyable) noexcept
    {
        operating_message gate = check_writable();
        if (!gate)
        {
            return gate;
        }
        if (name == nullptr || name[0] == '\0')
        {
            return detail::failure(om_err_reflect_invalid_name);
        }
        type_meta* m = live_meta<T>();
        if (m == nullptr)
        {
            return detail::failure(om_err_reflect_type_not_found);
        }
        detail::spinlock_guard lock(reg_lock_);
        if (!try_append_field(m, name, offset, field_type_id, field_flag_private,
                              0, 0, {0, 0, 0, 0}, size, trivially_copyable))
        {
            return detail::failure(om_err_out_of_memory);
        }
        return operating_message{};
    }

    // 批量手填偏移注册: descs 中每项一个字段
    template<typename T>
    operating_message add_private_offsets(const offset_desc* descs, size_t count) noexcept
    {
        if (descs == nullptr || count == 0)
        {
            return detail::failure(om_err_invalid_argument);
        }
        operating_message result;
        for (size_t i = 0; i < count; ++i)
        {
            result += add_field_raw<T>(descs[i].name, descs[i].offset, descs[i].type_id,
                                       descs[i].size, descs[i].trivially_copyable);
            if (!result)
            {
                break;
            }
        }
        return result;
    }

    // 注册数组字段 (C 数组 / std::array), 成员指针作为运行期参数
    template<typename T, typename M>
        requires (detail::member_object_pointer<M T::*> &&
                  detail::array_field_type<M>)
    void register_array_field_runtime(const char* name, M T::*ptr,
        const std::source_location& loc = std::source_location::current()) noexcept
    {
        const int tid = type_id::get_type_id<T>();
        if (!ensure_type_slot_index(tid))
        {
            return;
        }
        type_meta* m = ensure_type_slot<T>(tid);
        const size_t offset = detail::offset_of(ptr);

        using raw_m = std::remove_cvref_t<M>;
        uint8_t rank = 0;
        std::array<uint32_t, 4> ext{0, 0, 0, 0};
        int elem_tid = 0;

        if constexpr (std::is_bounded_array_v<raw_m>)
        {
            constexpr uint8_t c_rank = std::rank_v<raw_m>;
            rank = c_rank;
            if constexpr (c_rank >= 1)
            {
                ext[0] = static_cast<uint32_t>(std::extent_v<raw_m, 0>);
            }
            if constexpr (c_rank >= 2)
            {
                ext[1] = static_cast<uint32_t>(std::extent_v<raw_m, 1>);
            }
            if constexpr (c_rank >= 3)
            {
                ext[2] = static_cast<uint32_t>(std::extent_v<raw_m, 2>);
            }
            if constexpr (c_rank >= 4)
            {
                ext[3] = static_cast<uint32_t>(std::extent_v<raw_m, 3>);
            }
            using elem = std::remove_all_extents_t<raw_m>;
            elem_tid = type_id::get_type_id<elem>();
        }
        else if constexpr (detail::is_std_array_v<raw_m>)
        {
            rank = 1;
            ext[0] = static_cast<uint32_t>(detail::std_array_size_v<raw_m>);
            using elem = detail::std_array_element_t<raw_m>;
            elem_tid = type_id::get_type_id<elem>();
        }

        detail::spinlock_guard lock(reg_lock_);
        uint64_t total64 = 1;
        for (uint8_t i = 0; i < rank; ++i)
        {
            total64 *= static_cast<uint64_t>(ext[i]);
        }
        using element_type = detail::array_element_of_t<raw_m>;
        const uint32_t stride = static_cast<uint32_t>(sizeof(element_type));
        if (offset > sizeof(T) || stride == 0 || total64 > 0xFFFFFFFFull
            || total64 * stride > sizeof(T) - offset)
        {
            detail::abort_with_location("array field size mismatch", loc);
        }
        if (!try_append_field(m, name, offset, elem_tid,
                              detail::field_flags_of(std::is_const_v<M>, true),
                              rank, static_cast<uint32_t>(total64), ext, stride,
                              std::is_trivially_copyable_v<element_type>))
        {
            detail::abort_with_location("field storage allocation failed", loc);
        }
        detail::ensure_field_ops<std::remove_cv_t<element_type>>();
    }

    // 注册成员 (标量/数组统一入口, 成员指针作为运行期参数)
    template<typename T, typename M>
        requires detail::member_object_pointer<M T::*>
    void register_member_runtime(const char* type_name, const char* field_name, M T::*ptr,
        const std::source_location& loc = std::source_location::current()) noexcept
    {
        const int tid = type_id::get_type_id<T>();
        if (!ensure_type_slot_index(tid))
        {
            return;
        }
        if (type_entries_[tid].load(std::memory_order_acquire) == nullptr)
        {
            register_type_only<T>(type_name);
        }

        if constexpr (detail::array_field_type<M>)
        {
            register_array_field_runtime<T, M>(field_name, ptr, loc);
        }
        else
        {
            register_field_runtime<T, M>(field_name, ptr, loc);
        }
    }

    // 注册成员方法
    //   vt_off >= 0 表示虚函数并给出 vtable 偏移 (跨平台一致); 默认 -1 非虚
    template<auto Fn>
    void register_method(const char* name, int vt_off = -1,
        const std::source_location& loc = std::source_location::current()) noexcept
    {
        using mfn_type = decltype(Fn);
        using traits = detail::mfn_traits<mfn_type>;
        using C = typename traits::class_type;
        using R = typename traits::return_type;

        const int tid = type_id::get_type_id<C>();
        if (!ensure_type_slot_index(tid))
        {
            return;
        }

        type_meta* m = ensure_type_slot<C>(tid);

        detail::spinlock_guard lock(reg_lock_);
        const size_t midx = m->method_count.load(std::memory_order_relaxed);
        if (!m->methods.ensure(midx + 1))
        {
            detail::abort_with_location("method storage allocation failed", loc);
        }

        method_meta mm;
        mm.name = name;
        mm.arg_count = static_cast<uint8_t>(traits::arg_count);
        mm.return_type_id = detail::return_type_id<R>();
        mm.invoker = &detail::mfn_invoker_t<Fn, mfn_type>::invoke;
        mm.is_const = traits::is_const;
        mm.is_static = false;
        mm.arg_type_ids = detail::make_arg_type_ids<mfn_type>();
        mm.vtable_offset = vt_off;
        auto fn_copy = Fn;
        static_assert(sizeof(fn_copy) <= fn_storage_size,
                      "member function pointer exceeds fn_storage_size");
        std::memcpy(mm.fn_storage.data(), &fn_copy, sizeof(fn_copy));
        mm.fn_size = static_cast<uint8_t>(sizeof(fn_copy));
        m->methods[midx] = mm;
        m->method_count.store(midx + 1, std::memory_order_release);
    }

    // 注册静态方法
    template<typename C, auto Fn>
    void register_static_method(const char* name,
        const std::source_location& loc = std::source_location::current()) noexcept
    {
        using mfn_type = decltype(Fn);
        using traits = detail::mfn_traits<mfn_type>;
        using R = typename traits::return_type;
        static_assert(traits::is_static, "Fn must be a free/static function pointer");

        const int tid = type_id::get_type_id<C>();
        if (!ensure_type_slot_index(tid))
        {
            return;
        }

        type_meta* m = ensure_type_slot<C>(tid);

        detail::spinlock_guard lock(reg_lock_);
        const size_t midx = m->method_count.load(std::memory_order_relaxed);
        if (!m->methods.ensure(midx + 1))
        {
            detail::abort_with_location("method storage allocation failed", loc);
        }

        method_meta mm;
        mm.name = name;
        mm.arg_count = static_cast<uint8_t>(traits::arg_count);
        mm.return_type_id = detail::return_type_id<R>();
        mm.invoker = &detail::sfn_invoker_t<Fn, mfn_type>::invoke;
        mm.is_const = false;
        mm.is_static = true;
        mm.arg_type_ids = detail::make_arg_type_ids<mfn_type>();
        mm.vtable_offset = -1;
        auto fn_copy = Fn;
        static_assert(sizeof(fn_copy) <= fn_storage_size,
                      "function pointer exceeds fn_storage_size");
        std::memcpy(mm.fn_storage.data(), &fn_copy, sizeof(fn_copy));
        mm.fn_size = static_cast<uint8_t>(sizeof(fn_copy));
        m->methods[midx] = mm;
        m->method_count.store(midx + 1, std::memory_order_release);
    }

    // 注册成员方法 (运行期指针): 私有函数在类内取地址后交给此处
    template<typename Fn>
        requires std::is_member_function_pointer_v<Fn>
    operating_message register_method_ptr(const char* name, Fn ptr, int vt_off = -1) noexcept
    {
        operating_message gate = check_writable();
        if (!gate)
        {
            return gate;
        }
        if (name == nullptr || name[0] == '\0')
        {
            return detail::failure(om_err_reflect_invalid_name);
        }
        if (ptr == nullptr)
        {
            return detail::failure(om_err_invalid_argument);
        }
        if (sizeof(Fn) > fn_storage_size)
        {
            return detail::failure(om_err_unsupported_operation);
        }

        using traits = detail::mfn_traits<Fn>;
        using C = typename traits::class_type;
        using R = typename traits::return_type;

        const int tid = type_id::get_type_id<C>();
        if (!ensure_type_slot_index(tid))
        {
            return detail::failure(om_err_out_of_memory);
        }
        type_meta* m = live_meta<C>();
        if (m == nullptr)
        {
            return detail::failure(om_err_reflect_type_not_found);
        }

        detail::spinlock_guard lock(reg_lock_);
        const size_t midx = m->method_count.load(std::memory_order_relaxed);
        if (!m->methods.ensure(midx + 1))
        {
            return detail::failure(om_err_out_of_memory);
        }

        method_meta mm;
        mm.name = name;
        mm.arg_count = static_cast<uint8_t>(traits::arg_count);
        mm.return_type_id = detail::return_type_id<R>();
        mm.invoker = &detail::mfn_ptr_invoker<Fn>::invoke;
        mm.is_const = traits::is_const;
        mm.is_static = false;
        mm.arg_type_ids = detail::make_arg_type_ids<Fn>();
        mm.vtable_offset = vt_off;
        std::memcpy(mm.fn_storage.data(), &ptr, sizeof(ptr));
        mm.fn_size = static_cast<uint8_t>(sizeof(ptr));
        m->methods[midx] = mm;
        m->method_count.store(midx + 1, std::memory_order_release);
        return operating_message{};
    }

    // 注册静态方法 (运行期指针)
    template<typename C, typename Fn>
        requires std::is_function_v<std::remove_pointer_t<Fn>>
    operating_message register_static_ptr(const char* name, Fn ptr) noexcept
    {
        operating_message gate = check_writable();
        if (!gate)
        {
            return gate;
        }
        if (name == nullptr || name[0] == '\0')
        {
            return detail::failure(om_err_reflect_invalid_name);
        }
        if (ptr == nullptr)
        {
            return detail::failure(om_err_invalid_argument);
        }
        if (sizeof(Fn) > fn_storage_size)
        {
            return detail::failure(om_err_unsupported_operation);
        }

        using traits = detail::mfn_traits<Fn>;
        using R = typename traits::return_type;

        const int tid = type_id::get_type_id<C>();
        if (!ensure_type_slot_index(tid))
        {
            return detail::failure(om_err_out_of_memory);
        }
        type_meta* m = live_meta<C>();
        if (m == nullptr)
        {
            return detail::failure(om_err_reflect_type_not_found);
        }

        detail::spinlock_guard lock(reg_lock_);
        const size_t midx = m->method_count.load(std::memory_order_relaxed);
        if (!m->methods.ensure(midx + 1))
        {
            return detail::failure(om_err_out_of_memory);
        }

        method_meta mm;
        mm.name = name;
        mm.arg_count = static_cast<uint8_t>(traits::arg_count);
        mm.return_type_id = detail::return_type_id<R>();
        mm.invoker = &detail::sfn_ptr_invoker<Fn>::invoke;
        mm.is_const = false;
        mm.is_static = true;
        mm.arg_type_ids = detail::make_arg_type_ids<Fn>();
        mm.vtable_offset = -1;
        std::memcpy(mm.fn_storage.data(), &ptr, sizeof(ptr));
        mm.fn_size = static_cast<uint8_t>(sizeof(ptr));
        m->methods[midx] = mm;
        m->method_count.store(midx + 1, std::memory_order_release);
        return operating_message{};
    }

    // 按类型 id 查询 (无锁, acquire)
    [[nodiscard]] const type_meta* get_type(int tid) const noexcept
    {
        if (!has_type_slot(tid))
        {
            return nullptr;
        }
        type_meta* m = type_entries_[tid].load(std::memory_order_acquire);
        if (m == nullptr)
        {
            return nullptr;
        }
        if (m->state.load(std::memory_order_acquire) != static_cast<uint8_t>(type_state::live))
        {
            return nullptr;
        }
        return m;
    }

    // 按名查找类型 (遍历, 适合初始化阶段)
    [[nodiscard]] const type_meta* find_type(const char* name) const noexcept
    {
        if (name == nullptr)
        {
            return nullptr;
        }
        const int id = names_.resolve(std::string_view{name});
        return id > 0 ? get_type(id) : nullptr;
    }

    // #3 枚举注册
    template<typename E>
    void register_enum(const char* name,
        std::initializer_list<std::pair<E, const char*>> values) noexcept
    {
        static_assert(std::is_enum_v<E>, "register_enum requires enum type");
        const int tid = type_id::get_type_id<E>();
        if (!ensure_enum_slot_index(tid))
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
        if (name != nullptr && tid > 0)
        {
            (void)type_id::bind_def_name(name, tid);
        }
        using UT = std::underlying_type_t<E>;
        em->underlying_type_id = type_id::get_type_id<UT>();
        em->values.increase_capacity(values.size());
        for (const auto& kv : values)
        {
            em->values.emplace_back(enum_value_entry{
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

    // 只注册枚举的底层类型 (无枚举值表, 供序列化等只需要整型宽度的场景)
    template<typename E>
    void register_enum_type() noexcept
    {
        static_assert(std::is_enum_v<E>, "register_enum_type requires enum type");
        const int tid = type_id::get_type_id<E>();
        if (!ensure_enum_slot_index(tid))
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
        em->name = nullptr;
        em->type_id = tid;
        using UT = std::underlying_type_t<E>;
        em->underlying_type_id = type_id::get_type_id<UT>();
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
        if (!has_enum_slot(tid))
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
        if (!ensure_type_slot_index(did) || !ensure_type_slot_index(bid))
        {
            return;
        }
        type_meta* dm = ensure_type_slot<Derived>(did);
        type_meta* bm = ensure_type_slot<Base>(bid);

        detail::spinlock_guard lock(reg_lock_);
        dm->base_offsets.emplace_back(base_offset_entry{bid, offset, nullptr});
        bm->derived_type_ids.emplace_back(did);
    }

    // 类型化继承注册 (指针调整由编译期转换承担, 不依赖手填偏移)
    template<typename Derived, typename Base>
    void register_base_typed() noexcept
    {
        static_assert(std::is_base_of_v<Base, Derived>, "Base must be a base of Derived");
        int did = type_id::get_type_id<Derived>();
        int bid = type_id::get_type_id<Base>();
        if (!ensure_type_slot_index(did) || !ensure_type_slot_index(bid))
        {
            return;
        }
        type_meta* dm = ensure_type_slot<Derived>(did);
        type_meta* bm = ensure_type_slot<Base>(bid);

        base_offset_entry e;
        e.base_type_id = bid;
        e.offset = 0;
        e.adjust_fn = +[](void* p) noexcept -> void*
        {
            return static_cast<Base*>(static_cast<Derived*>(p));
        };

        detail::spinlock_guard lock(reg_lock_);
        dm->base_offsets.emplace_back(e);
        bm->derived_type_ids.emplace_back(did);
    }

    template<typename T, typename U>
    void register_convert() noexcept
    {
        static_assert(std::is_convertible_v<T, U>, "T must be convertible to U");
        const int tid = type_id::get_type_id<T>();
        if (!ensure_type_slot_index(tid))
        {
            return;
        }
        type_meta* m = ensure_type_slot<T>(tid);

        convert_entry e;
        e.target_type_id = type_id::get_type_id<U>();
        e.convert_fn = +[](const void* src, void* dst) noexcept
        {
            *static_cast<U*>(dst) = static_cast<U>(*static_cast<const T*>(src));
        };
        detail::spinlock_guard lock(reg_lock_);
        m->converters.emplace_back(e);
    }

    // === 冻结与写入门禁 ===
    void freeze() noexcept { frozen_.store(true, std::memory_order_release); }
    void unfreeze() noexcept { frozen_.store(false, std::memory_order_release); }
    [[nodiscard]] bool frozen() const noexcept
    {
        return frozen_.load(std::memory_order_acquire);
    }

    [[nodiscard]] size_t type_count() const noexcept
    {
        return type_count_.load(std::memory_order_acquire);
    }

    [[nodiscard]] bool type_name_taken(const char* name) const noexcept
    {
        return name != nullptr && names_.resolve(std::string_view{name}) >= 0;
    }

    // === 退休 (标记失效, 内存保留) ===
    template<typename T>
    operating_message retire_type() noexcept
    {
        operating_message gate = check_writable();
        if (!gate)
        {
            return gate;
        }
        type_meta* m = live_meta<T>();
        if (m == nullptr)
        {
            return detail::failure(om_err_reflect_type_not_found);
        }
        detail::spinlock_guard lock(reg_lock_);
        if (m->state.load(std::memory_order_relaxed) != static_cast<uint8_t>(type_state::live))
        {
            return detail::failure(om_err_reflect_duplicate);
        }
        if (m->name != nullptr)
        {
            (void)names_.unbind(std::string_view{m->name});
        }
        m->state.store(static_cast<uint8_t>(type_state::retired), std::memory_order_release);
        return operating_message{};
    }

    // === 清除 (释放元数据, 要求已退休且无并发读者) ===
    template<typename T>
    operating_message purge_type() noexcept
    {
        const int tid = type_id::get_type_id<T>();
        if (!has_type_slot(tid))
        {
            return detail::failure(om_err_reflect_type_not_found);
        }
        type_meta* m = type_entries_[tid].load(std::memory_order_acquire);
        if (m == nullptr)
        {
            return detail::failure(om_err_reflect_type_not_found);
        }
        if (m->state.load(std::memory_order_acquire) != static_cast<uint8_t>(type_state::retired))
        {
            return detail::failure(om_err_reflect_busy);
        }
        type_meta* expected = m;
        if (!type_entries_[tid].compare_exchange_strong(
                expected, nullptr, std::memory_order_release, std::memory_order_acquire))
        {
            return detail::failure(om_err_reflect_busy);
        }
        delete m;
        type_count_.fetch_sub(1, std::memory_order_relaxed);
        return operating_message{};
    }

    // === 删除字段 ===
    template<typename T>
    operating_message remove_field(const char* name) noexcept
    {
        operating_message gate = check_writable();
        if (!gate)
        {
            return gate;
        }
        type_meta* m = live_meta<T>();
        if (m == nullptr)
        {
            return detail::failure(om_err_reflect_type_not_found);
        }
        if (name == nullptr || name[0] == '\0')
        {
            return detail::failure(om_err_reflect_invalid_name);
        }
        detail::spinlock_guard lock(reg_lock_);
        const size_t n = m->field_count.load(std::memory_order_relaxed);
        for (size_t i = 0; i < n; ++i)
        {
            if (m->fields[i].name != nullptr && std::strcmp(m->fields[i].name, name) == 0)
            {
                for (size_t j = i; j + 1 < n; ++j)
                {
                    m->fields[j] = std::move(m->fields[j + 1]);
                }
                m->fields[n - 1] = field_meta{};
                m->field_count.store(n - 1, std::memory_order_release);
                return operating_message{};
            }
        }
        return detail::failure(om_err_reflect_field_not_found);
    }

    // === 删除方法 ===
    template<auto Fn>
    operating_message remove_method(const char* name) noexcept
    {
        using mfn_type = decltype(Fn);
        using traits = detail::mfn_traits<mfn_type>;
        using C = typename traits::class_type;
        auto fn_copy = Fn;
        return remove_method_by_invoker<C>(name, &detail::mfn_invoker_t<Fn, mfn_type>::invoke,
                                           &fn_copy, sizeof(fn_copy));
    }

    template<typename C, auto Fn>
    operating_message remove_static_method(const char* name) noexcept
    {
        using mfn_type = decltype(Fn);
        auto fn_copy = Fn;
        return remove_method_by_invoker<C>(name, &detail::sfn_invoker_t<Fn, mfn_type>::invoke,
                                           &fn_copy, sizeof(fn_copy));
    }

    // 按运行期指针删除方法
    template<typename Fn>
        requires std::is_member_function_pointer_v<Fn>
    operating_message remove_method_ptr(const char* name, Fn ptr) noexcept
    {
        using traits = detail::mfn_traits<Fn>;
        using C = typename traits::class_type;
        return remove_method_by_invoker<C>(name, &detail::mfn_ptr_invoker<Fn>::invoke,
                                           &ptr, sizeof(ptr));
    }

    // === 删除枚举 ===
    template<typename E>
    operating_message remove_enum() noexcept
    {
        operating_message gate = check_writable();
        if (!gate)
        {
            return gate;
        }
        const int tid = type_id::get_type_id<E>();
        if (!has_enum_slot(tid))
        {
            return detail::failure(om_err_reflect_enum_not_found);
        }
        enum_meta* em = enum_entries_[tid].load(std::memory_order_acquire);
        if (em == nullptr)
        {
            return detail::failure(om_err_reflect_enum_not_found);
        }
        detail::spinlock_guard lock(reg_lock_);
        enum_meta* expected = em;
        if (!enum_entries_[tid].compare_exchange_strong(
                expected, nullptr, std::memory_order_release, std::memory_order_acquire))
        {
            return detail::failure(om_err_reflect_busy);
        }
        delete em;
        return operating_message{};
    }

    // === 删除继承关系 ===
    template<typename D, typename B>
    operating_message remove_base() noexcept
    {
        operating_message gate = check_writable();
        if (!gate)
        {
            return gate;
        }
        type_meta* dm = live_meta<D>();
        type_meta* bm = live_meta<B>();
        if (dm == nullptr || bm == nullptr)
        {
            return detail::failure(om_err_reflect_type_not_found);
        }
        const int bid = type_id::get_type_id<B>();
        const int did = type_id::get_type_id<D>();

        detail::spinlock_guard lock(reg_lock_);
        bool removed = false;
        for (size_t i = 0; i < dm->base_offsets.size(); ++i)
        {
            if (dm->base_offsets[i].base_type_id == bid)
            {
                (void)dm->base_offsets.erase_unordered(i);
                removed = true;
                break;
            }
        }
        if (!removed)
        {
            return detail::failure(om_err_reflect_type_not_found);
        }
        for (size_t i = 0; i < bm->derived_type_ids.size(); ++i)
        {
            if (bm->derived_type_ids[i] == did)
            {
                (void)bm->derived_type_ids.erase_unordered(i);
                break;
            }
        }
        return operating_message{};
    }

    // === 改类型名 ===
    template<typename T>
    operating_message rename_type(const char* new_name) noexcept
    {
        operating_message gate = check_writable();
        if (!gate)
        {
            return gate;
        }
        if (new_name == nullptr || new_name[0] == '\0')
        {
            return detail::failure(om_err_reflect_invalid_name);
        }
        type_meta* m = live_meta<T>();
        if (m == nullptr)
        {
            return detail::failure(om_err_reflect_type_not_found);
        }
        detail::spinlock_guard lock(reg_lock_);
        const int owner = names_.resolve(std::string_view{new_name});
        if (owner >= 0 && owner != m->type_id)
        {
            return detail::failure(om_err_reflect_duplicate);
        }
        if (m->name != nullptr)
        {
            (void)names_.unbind(std::string_view{m->name});
        }
        (void)names_.bind(std::string_view{new_name}, m->type_id);
        m->name = new_name;
        return operating_message{};
    }

    // === 改字段名 ===
    template<typename T>
    operating_message rename_field(const char* old_name, const char* new_name) noexcept
    {
        operating_message gate = check_writable();
        if (!gate)
        {
            return gate;
        }
        if (new_name == nullptr || new_name[0] == '\0' || old_name == nullptr)
        {
            return detail::failure(om_err_reflect_invalid_name);
        }
        type_meta* m = live_meta<T>();
        if (m == nullptr)
        {
            return detail::failure(om_err_reflect_type_not_found);
        }
        detail::spinlock_guard lock(reg_lock_);
        field_meta* f = find_field(m, old_name);
        if (f == nullptr)
        {
            return detail::failure(om_err_reflect_field_not_found);
        }
        if (field_name_taken(m, new_name))
        {
            return detail::failure(om_err_reflect_duplicate);
        }
        f->name = new_name;
        return operating_message{};
    }

    // === 重绑字段偏移与类型 (布局变化后修正) ===
    template<typename T, typename M, M T::*Ptr>
    operating_message rebind_field(const char* name) noexcept
    {
        if constexpr (detail::array_field_type<M>)
        {
            operating_message gate = check_writable();
            if (!gate)
            {
                return gate;
            }
            return detail::failure(om_err_unsupported_operation);
        }
        else
        {
            return rebind_field_at<T, M>(name, detail::offset_of(Ptr));
        }
    }

    // 运行期偏移版: 成员指针作为函数参数
    template<typename T, typename M>
    operating_message rebind_field_at(const char* name, size_t offset) noexcept
    {
        operating_message gate = check_writable();
        if (!gate)
        {
            return gate;
        }
        type_meta* m = live_meta<T>();
        if (m == nullptr)
        {
            return detail::failure(om_err_reflect_type_not_found);
        }
        detail::spinlock_guard lock(reg_lock_);
        field_meta* f = find_field(m, name);
        if (f == nullptr)
        {
            return detail::failure(om_err_reflect_field_not_found);
        }
        f->offset = static_cast<uint32_t>(offset);
        f->type_id = type_id::get_type_id<M>();
        f->trivially_copyable = std::is_trivially_copyable_v<M>;
        f->flags = detail::field_flags_of(std::is_const_v<M>, f->is_private());
        f->array_rank = 0;
        f->total_elements = 0;
        f->element_stride = static_cast<uint32_t>(sizeof(M));
        return operating_message{};
    }

    // === 改方法名 ===
    template<auto Fn>
    operating_message rename_method(const char* old_name, const char* new_name) noexcept
    {
        using mfn_type = decltype(Fn);
        using traits = detail::mfn_traits<mfn_type>;
        using C = typename traits::class_type;
        auto fn_copy = Fn;
        return rename_method_by_invoker<C>(old_name, new_name,
            &detail::mfn_invoker_t<Fn, mfn_type>::invoke, &fn_copy, sizeof(fn_copy));
    }

    // 按运行期指针改方法名
    template<typename Fn>
        requires std::is_member_function_pointer_v<Fn>
    operating_message rename_method_ptr(const char* old_name, const char* new_name, Fn ptr) noexcept
    {
        using traits = detail::mfn_traits<Fn>;
        using C = typename traits::class_type;
        return rename_method_by_invoker<C>(old_name, new_name,
            &detail::mfn_ptr_invoker<Fn>::invoke, &ptr, sizeof(ptr));
    }

    // === 改字段标志 ===
    template<typename T>
    operating_message set_field_flags(const char* name, uint8_t flags) noexcept
    {
        operating_message gate = check_writable();
        if (!gate)
        {
            return gate;
        }
        type_meta* m = live_meta<T>();
        if (m == nullptr)
        {
            return detail::failure(om_err_reflect_type_not_found);
        }
        detail::spinlock_guard lock(reg_lock_);
        field_meta* f = find_field(m, name);
        if (f == nullptr)
        {
            return detail::failure(om_err_reflect_field_not_found);
        }
        f->flags = flags;
        return operating_message{};
    }

    // === 改字段属性 (键存在则覆盖) ===
    template<typename T, typename V>
    operating_message set_field_attr(const char* field, const char* key, V&& value) noexcept
    {
        operating_message gate = check_writable();
        if (!gate)
        {
            return gate;
        }
        if (key == nullptr || key[0] == '\0')
        {
            return detail::failure(om_err_reflect_invalid_name);
        }
        type_meta* m = live_meta<T>();
        if (m == nullptr)
        {
            return detail::failure(om_err_reflect_type_not_found);
        }
        detail::spinlock_guard lock(reg_lock_);
        field_meta* f = find_field(m, field);
        if (f == nullptr)
        {
            return detail::failure(om_err_reflect_field_not_found);
        }
        const uint64_t kh = fnv1a_runtime(key);
        for (size_t i = 0; i < f->attrs.size(); ++i)
        {
            if (f->attrs[i].key_hash == kh)
            {
                f->attrs[i].value = void_any(std::forward<V>(value));
                return operating_message{};
            }
        }
        attr_entry e;
        e.key_hash = kh;
        e.value = void_any(std::forward<V>(value));
        f->attrs.emplace_back(std::move(e));
        return operating_message{};
    }

    // === 自定义构造/析构: 供私有构造、私有析构类型使用 ===
    // create 收到 nullptr 时分配堆对象; destroy 负责析构并释放
    template<typename T>
    operating_message set_construct_destruct(construct_func create, destruct_func destroy) noexcept
    {
        operating_message gate = check_writable();
        if (!gate)
        {
            return gate;
        }
        if (create == nullptr || destroy == nullptr)
        {
            return detail::failure(om_err_invalid_argument);
        }
        type_meta* m = live_meta<T>();
        if (m == nullptr)
        {
            return detail::failure(om_err_reflect_type_not_found);
        }
        detail::spinlock_guard lock(reg_lock_);
        m->default_construct_ = create;
        m->free_heap_ = destroy;
        m->has_default_construct = true;
        return operating_message{};
    }

    // === 继承调整函数: 供私有/虚拟继承使用 (类型化转换在类外不可访问) ===
    template<typename D, typename B>
    operating_message add_base_with_adjuster(void* (*adjust)(void*) noexcept) noexcept
    {
        operating_message gate = check_writable();
        if (!gate)
        {
            return gate;
        }
        if (adjust == nullptr)
        {
            return detail::failure(om_err_invalid_argument);
        }
        type_meta* dm = live_meta<D>();
        type_meta* bm = live_meta<B>();
        if (dm == nullptr || bm == nullptr)
        {
            return detail::failure(om_err_reflect_type_not_found);
        }
        const int did = type_id::get_type_id<D>();
        const int bid = type_id::get_type_id<B>();

        detail::spinlock_guard lock(reg_lock_);
        dm->base_offsets.emplace_back(base_offset_entry{bid, 0, adjust});
        bm->derived_type_ids.emplace_back(did);
        return operating_message{};
    }


private:
    // 槽位表按需增长; 读侧只查已保证范围, 不触发分配
    bool ensure_type_slot_index(int tid) noexcept
    {
        if (tid <= 0)
        {
            return false;
        }
        return type_entries_.ensure(static_cast<size_t>(tid) + 1);
    }

    bool ensure_enum_slot_index(int tid) noexcept
    {
        if (tid <= 0)
        {
            return false;
        }
        return enum_entries_.ensure(static_cast<size_t>(tid) + 1);
    }

    bool has_type_slot(int tid) const noexcept
    {
        return tid > 0 && static_cast<size_t>(tid) < type_entries_.size();
    }

    bool has_enum_slot(int tid) const noexcept
    {
        return tid > 0 && static_cast<size_t>(tid) < enum_entries_.size();
    }

    operating_message check_writable() noexcept
    {
        if (frozen())
        {
            return detail::failure(om_err_reflect_frozen);
        }
        return operating_message{};
    }

    template<typename T>
    type_meta* live_meta() noexcept
    {
        const int tid = type_id::get_type_id<T>();
        if (!has_type_slot(tid))
        {
            return nullptr;
        }
        type_meta* m = type_entries_[tid].load(std::memory_order_acquire);
        if (m == nullptr
            || m->state.load(std::memory_order_acquire) != static_cast<uint8_t>(type_state::live))
        {
            return nullptr;
        }
        return m;
    }

    static field_meta* find_field(type_meta* m, const char* name) noexcept
    {
        if (m == nullptr || name == nullptr)
        {
            return nullptr;
        }
        const size_t n = m->field_count.load(std::memory_order_relaxed);
        for (size_t i = 0; i < n; ++i)
        {
            if (m->fields[i].name != nullptr && std::strcmp(m->fields[i].name, name) == 0)
            {
                return &m->fields[i];
            }
        }
        return nullptr;
    }

    static bool field_name_taken(const type_meta* m, const char* name) noexcept
    {
        if (m == nullptr || name == nullptr)
        {
            return false;
        }
        const size_t n = m->field_count.load(std::memory_order_relaxed);
        for (size_t i = 0; i < n; ++i)
        {
            if (m->fields[i].name != nullptr && std::strcmp(m->fields[i].name, name) == 0)
            {
                return true;
            }
        }
        return false;
    }

    // 方法匹配: 名字 + 指针副本, 无副本时退回 invoker 比较
    static bool method_matches(const method_meta& mm, const char* name, invoker_func fn,
                               const void* fn_ptr, size_t fn_size) noexcept
    {
        if (name != nullptr && (mm.name == nullptr || std::strcmp(mm.name, name) != 0))
        {
            return false;
        }
        if (fn_ptr != nullptr)
        {
            return mm.fn_size == fn_size
                && std::memcmp(mm.fn_storage.data(), fn_ptr, fn_size) == 0;
        }
        return mm.invoker == fn;
    }

    template<typename C>
    operating_message remove_method_by_invoker(const char* name, invoker_func fn,
                                               const void* fn_ptr, size_t fn_size) noexcept
    {
        operating_message gate = check_writable();
        if (!gate)
        {
            return gate;
        }
        type_meta* m = live_meta<C>();
        if (m == nullptr)
        {
            return detail::failure(om_err_reflect_type_not_found);
        }
        detail::spinlock_guard lock(reg_lock_);
        const size_t n = m->method_count.load(std::memory_order_relaxed);
        for (size_t i = 0; i < n; ++i)
        {
            const method_meta& mm = m->methods[i];
            if (method_matches(mm, name, fn, fn_ptr, fn_size))
            {
                for (size_t j = i; j + 1 < n; ++j)
                {
                    m->methods[j] = m->methods[j + 1];
                }
                m->methods[n - 1] = method_meta{};
                m->method_count.store(n - 1, std::memory_order_release);
                return operating_message{};
            }
        }
        return detail::failure(om_err_reflect_method_not_found);
    }

    template<typename C>
    operating_message rename_method_by_invoker(const char* old_name, const char* new_name,
                                               invoker_func fn,
                                               const void* fn_ptr, size_t fn_size) noexcept
    {
        operating_message gate = check_writable();
        if (!gate)
        {
            return gate;
        }
        if (new_name == nullptr || new_name[0] == '\0')
        {
            return detail::failure(om_err_reflect_invalid_name);
        }
        type_meta* m = live_meta<C>();
        if (m == nullptr)
        {
            return detail::failure(om_err_reflect_type_not_found);
        }
        detail::spinlock_guard lock(reg_lock_);
        const size_t n = m->method_count.load(std::memory_order_relaxed);
        for (size_t i = 0; i < n; ++i)
        {
            method_meta& mm = m->methods[i];
            if (!method_matches(mm, old_name, fn, fn_ptr, fn_size))
            {
                continue;
            }
            mm.name = new_name;
            return operating_message{};
        }
        return detail::failure(om_err_reflect_method_not_found);
    }

    // 单份字段追加 (4 条注册路径共用): 存储分配失败返回 false
    bool try_append_field(type_meta* m, const char* name, size_t offset, int field_type_id,
                          uint8_t flags, uint8_t rank, uint32_t total_elements,
                          std::array<uint32_t, 4> extents, uint32_t stride, bool trivial) noexcept
    {
        const size_t fidx = m->field_count.load(std::memory_order_relaxed);
        if (!m->fields.ensure(fidx + 1))
        {
            return false;
        }
        m->fields[fidx] = field_meta{
            name,
            static_cast<uint32_t>(offset),
            field_type_id,
            flags,
            rank, 0, total_elements, extents, stride, trivial, {}
        };
        m->field_count.store(fidx + 1, std::memory_order_release);
        return true;
    }

    template<typename T>
    void fill_type_identity(type_meta* m, const char* name) noexcept
    {
        if (name != nullptr)
        {
            bind_name(m, name);
        }
        m->size = sizeof(T);
        m->align = alignof(T);
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
            m->free_heap_ = +[](void* obj) noexcept
            {
                delete static_cast<T*>(obj);
            };
        }
        if constexpr (!std::is_trivially_destructible_v<T>)
        {
            m->destruct_ = +[](void* obj) noexcept
            {
                static_cast<T*>(obj)->~T();
            };
        }
        detail::global_ops().register_type_ops<T>();
        if constexpr (detail::sequential_container<T>)
        {
            detail::global_ops().register_sequential<T>();
            m->container_ops = detail::global_ops().get_container(m->type_id);
        }
    }

    template<typename T>
    void traverse_aggregate_fields(type_meta* m) noexcept
    {
        T obj{};
        detail::for_each_member(obj, [&](auto& member, size_t idx) {
            using member_type = std::remove_reference_t<decltype(member)>;
            if (!try_append_field(m, make_field_name(idx),
                                  reinterpret_cast<size_t>(&member) -
                                      reinterpret_cast<size_t>(&obj),
                                  type_id::get_type_id<member_type>(),
                                  detail::field_flags_of(std::is_const_v<member_type>, false),
                                  0, 0, {0, 0, 0, 0},
                                  static_cast<uint32_t>(sizeof(member_type)),
                                  std::is_trivially_copyable_v<member_type>))
            {
                return;
            }
            detail::ensure_field_ops<std::remove_cv_t<member_type>>();
        });
    }

    template<typename T>
    [[nodiscard]] type_meta* ensure_type_slot(int tid) noexcept
    {
        if (!ensure_type_slot_index(tid))
        {
            std::abort();
        }
        type_meta* m = type_entries_[tid].load(std::memory_order_acquire);
        if (m != nullptr
            && m->state.load(std::memory_order_acquire) == static_cast<uint8_t>(type_state::live))
        {
            return m;
        }
        type_meta* fresh = new (std::nothrow) type_meta{};
        if (fresh == nullptr)
        {
            std::abort();
        }
        fresh->type_id = tid;
        fill_type_identity<T>(fresh, nullptr);
        fresh->state.store(static_cast<uint8_t>(type_state::live), std::memory_order_release);
        type_meta* expected = nullptr;
        if (!type_entries_[tid].compare_exchange_strong(
                expected, fresh, std::memory_order_release, std::memory_order_acquire))
        {
            delete fresh;
            return expected;
        }
        type_count_.fetch_add(1, std::memory_order_relaxed);
        return fresh;
    }

    std::atomic_flag reg_lock_{};
    stable_array<std::atomic<enum_meta*>> enum_entries_;
    detail::name_index names_;
    std::atomic<bool> frozen_{false};
    std::atomic<size_t> type_count_{0};

    // 名字绑定: 本地索引 + 进程级 type_id 表
    void bind_name(type_meta* m, const char* name) noexcept
    {
        m->name = name;
        if (m->type_id <= 0 || name == nullptr)
        {
            return;
        }
        (void)names_.bind(std::string_view{name}, m->type_id);
        (void)type_id::bind_def_name(name, m->type_id);
    }

    static const char* make_field_name(size_t idx) noexcept
    {
        static std::array<std::array<char, 16>, REFLECT_MAX_FIELDS> names{};
        if (idx >= REFLECT_MAX_FIELDS)
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
                tmp[len++] = static_cast<char>('0' + static_cast<int>(val % 10));
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

// ============================================================================
