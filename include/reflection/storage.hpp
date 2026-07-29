#pragma once

// storage.hpp - 反射元数据存储类与全局存储对象
// 命名空间: reflect

#include <atomic>
#include <cstring>
#include <cstdlib>
#include <array>
#include <initializer_list>
#include "../part/type_id.hpp"
#include "../part/aggregate_reflect.hpp"
#include "../part/member_offset.hpp"
#include "../part/type_erasure.hpp"
#include "meta.hpp"

namespace reflect {

namespace detail {

struct spinlock_guard
{
    std::atomic_flag& flag_;
    explicit spinlock_guard(std::atomic_flag& f) noexcept : flag_(f)
    {
        while (flag_.test_and_set(std::memory_order_acquire)) {}
    }
    ~spinlock_guard() noexcept { flag_.clear(std::memory_order_release); }
    spinlock_guard(const spinlock_guard&) = delete;
    spinlock_guard& operator=(const spinlock_guard&) = delete;
};

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
        if (tid < 0 || tid >= static_cast<int>(MAX_TYPE_ID)) { return; }

        type_meta* existing = type_entries_[tid].load(std::memory_order_acquire);
        if (existing != nullptr) { return; }

        type_meta* m = new (std::nothrow) type_meta{};
        if (m == nullptr) { std::abort(); }
        m->name = name;
        m->size = static_cast<uint16_t>(sizeof(T));
        m->align = static_cast<uint16_t>(alignof(T));
        m->type_id = tid;

        if constexpr (std::is_aggregate_v<T> && aggregate_field_count_v<T> > 0)
        {
            T obj{};
            for_each_aggregate_member(obj, [&](auto& member, size_t idx) {
                using member_type = std::remove_reference_t<decltype(member)>;
                uint16_t fidx = m->field_count.load(std::memory_order_relaxed);
                if (fidx >= MAX_FIELDS_PER_TYPE) { return; }
                m->fields[fidx] = field_meta{
                    make_field_name(idx),
                    static_cast<uint32_t>(
                        reinterpret_cast<size_t>(&member) -
                        reinterpret_cast<size_t>(&obj)),
                    type_id::get_type_id<member_type>(),
                    std::is_const_v<member_type>,
                    false
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

    // 注册类型 (聚合类型, 自定义字段名)
    template<typename T>
    void register_type_named(const char* name,
                             std::initializer_list<const char*> field_names) noexcept
    {
        int tid = type_id::get_type_id<T>();
        if (tid < 0 || tid >= static_cast<int>(MAX_TYPE_ID)) { return; }

        type_meta* existing = type_entries_[tid].load(std::memory_order_acquire);
        if (existing != nullptr) { return; }

        type_meta* m = new (std::nothrow) type_meta{};
        if (m == nullptr) { std::abort(); }
        m->name = name;
        m->size = static_cast<uint16_t>(sizeof(T));
        m->align = static_cast<uint16_t>(alignof(T));
        m->type_id = tid;

        if constexpr (std::is_aggregate_v<T> && aggregate_field_count_v<T> > 0)
        {
            T obj{};
            size_t name_idx = 0;
            for_each_aggregate_member(obj, [&](auto& member, size_t idx) {
                using member_type = std::remove_reference_t<decltype(member)>;
                uint16_t fidx = m->field_count.load(std::memory_order_relaxed);
                if (fidx >= MAX_FIELDS_PER_TYPE) { return; }
                const char* fname = (name_idx < field_names.size())
                    ? field_names.begin()[name_idx]
                    : make_field_name(idx);
                ++name_idx;
                m->fields[fidx] = field_meta{
                    fname,
                    static_cast<uint32_t>(
                        reinterpret_cast<size_t>(&member) -
                        reinterpret_cast<size_t>(&obj)),
                    type_id::get_type_id<member_type>(),
                    std::is_const_v<member_type>,
                    false
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

    // 手填偏移量注册私有成员
    template<typename T>
    void register_private_offsets(const offset_desc* descs, size_t count) noexcept
    {
        int tid = type_id::get_type_id<T>();
        if (tid < 0 || tid >= static_cast<int>(MAX_TYPE_ID)) { return; }

        type_meta* m = type_entries_[tid].load(std::memory_order_acquire);
        if (m == nullptr || !m->registered.load(std::memory_order_acquire)) { return; }

        detail::spinlock_guard lock(reg_lock_);
        for (size_t i = 0; i < count; ++i)
        {
            uint16_t fidx = m->field_count.load(std::memory_order_relaxed);
            if (fidx >= MAX_FIELDS_PER_TYPE) { break; }
            m->fields[fidx] = field_meta{
                descs[i].name,
                static_cast<uint32_t>(descs[i].offset),
                descs[i].type_id,
                false,
                true
            };
            m->field_count.store(static_cast<uint16_t>(fidx + 1), std::memory_order_release);
        }
    }

    // 成员指针注册字段 (偏移量和类型自动推导)
    template<typename T, typename M, M T::*Ptr>
    void register_field(const char* name) noexcept
    {
        int tid = type_id::get_type_id<T>();
        if (tid < 0 || tid >= static_cast<int>(MAX_TYPE_ID)) { return; }

        type_meta* m = type_entries_[tid].load(std::memory_order_acquire);
        if (m == nullptr || !m->registered.load(std::memory_order_acquire)) { return; }

        detail::spinlock_guard lock(reg_lock_);
        uint16_t fidx = m->field_count.load(std::memory_order_relaxed);
        if (fidx >= MAX_FIELDS_PER_TYPE) { std::abort(); }

        T* null_obj = nullptr;
        size_t offset = reinterpret_cast<size_t>(&(null_obj->*Ptr));
        m->fields[fidx] = field_meta{
            name,
            static_cast<uint32_t>(offset),
            type_id::get_type_id<M>(),
            std::is_const_v<M>,
            true
        };
        m->field_count.store(static_cast<uint16_t>(fidx + 1), std::memory_order_release);
    }

    // 注册成员方法
    template<auto Fn>
    void register_method(const char* name) noexcept
    {
        using MFnType = decltype(Fn);
        using traits = mfn_traits<MFnType>;
        using C = typename traits::class_type;
        using R = typename traits::return_type;

        int tid = type_id::get_type_id<C>();
        if (tid < 0 || tid >= static_cast<int>(MAX_TYPE_ID)) { return; }

        type_meta* m = type_entries_[tid].load(std::memory_order_acquire);
        if (m == nullptr || !m->registered.load(std::memory_order_acquire)) { return; }

        detail::spinlock_guard lock(reg_lock_);
        uint16_t midx = m->method_count.load(std::memory_order_relaxed);
        if (midx >= MAX_METHODS_PER_TYPE) { std::abort(); }

        method_meta mm;
        mm.name = name;
        mm.arg_count = static_cast<uint8_t>(traits::arg_count);
        mm.return_type_id = return_type_id<R>();
        mm.invoker = &mfn_invoker_t<Fn, MFnType>::invoke;
        mm.is_const = traits::is_const;
        mm.is_static = false;
        m->methods[midx] = mm;
        m->method_count.store(static_cast<uint16_t>(midx + 1), std::memory_order_release);
    }

    // 注册静态方法
    template<typename C, auto Fn>
    void register_static_method(const char* name) noexcept
    {
        using MFnType = decltype(Fn);
        using traits = mfn_traits<MFnType>;
        using R = typename traits::return_type;
        static_assert(traits::is_static, "Fn must be a free/static function pointer");

        int tid = type_id::get_type_id<C>();
        if (tid < 0 || tid >= static_cast<int>(MAX_TYPE_ID)) { return; }

        type_meta* m = type_entries_[tid].load(std::memory_order_acquire);
        if (m == nullptr || !m->registered.load(std::memory_order_acquire)) { return; }

        detail::spinlock_guard lock(reg_lock_);
        uint16_t midx = m->method_count.load(std::memory_order_relaxed);
        if (midx >= MAX_METHODS_PER_TYPE) { std::abort(); }

        method_meta mm;
        mm.name = name;
        mm.arg_count = static_cast<uint8_t>(traits::arg_count);
        mm.return_type_id = return_type_id<R>();
        mm.invoker = &sfn_invoker_t<Fn, MFnType>::invoke;
        mm.is_const = false;
        mm.is_static = true;
        m->methods[midx] = mm;
        m->method_count.store(static_cast<uint16_t>(midx + 1), std::memory_order_release);
    }

    // 按类型 id 查询 (无锁, acquire)
    [[nodiscard]] const type_meta* get_type(int tid) const noexcept
    {
        if (tid < 0 || tid >= static_cast<int>(MAX_TYPE_ID)) { return nullptr; }
        type_meta* m = type_entries_[tid].load(std::memory_order_acquire);
        if (m == nullptr) { return nullptr; }
        if (!m->registered.load(std::memory_order_acquire)) { return nullptr; }
        return m;
    }

    // 按名查找类型 (遍历, 适合初始化阶段)
    [[nodiscard]] const type_meta* find_type(const char* name) const noexcept
    {
        for (size_t i = 0; i < MAX_TYPE_ID; ++i)
        {
            type_meta* m = type_entries_[i].load(std::memory_order_acquire);
            if (m == nullptr) { continue; }
            if (!m->registered.load(std::memory_order_acquire)) { continue; }
            if (m->name != nullptr && std::strcmp(m->name, name) == 0)
            {
                return m;
            }
        }
        return nullptr;
    }

private:
    std::atomic_flag reg_lock_{};

    static const char* make_field_name(size_t idx) noexcept
    {
        static char names[MAX_FIELDS_PER_TYPE][16];
        if (idx >= MAX_FIELDS_PER_TYPE) { return "field_?"; }

        static std::atomic_flag name_lock{};
        while (name_lock.test_and_set(std::memory_order_acquire)) {}
        char* p = names[idx];
        if (p[0] == '\0')
        {
            const char prefix[] = "field_";
            size_t i = 0;
            while (prefix[i]) { p[i] = prefix[i]; ++i; }
            size_t val = idx;
            char tmp[8];
            size_t len = 0;
            if (val == 0) { tmp[len++] = '0'; }
            while (val > 0) { tmp[len++] = '0' + (val % 10); val /= 10; }
            while (len > 0) { p[i++] = tmp[--len]; }
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
