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
#include "part/operating_message.hpp"
#include "part/type/type_id.hpp"
#include "part/type/void_any.hpp"
#include "reflection/core/storage.hpp"

// query.hpp - 类型视图与枚举视图
// 命名空间: reflect

// ============================================================================

// query.hpp - 反射查询器
// 命名空间: reflect


namespace reflect {

namespace detail {

// 单份重载解析: 按方法名与参数类型 id 精确匹配
[[nodiscard]] inline const method_meta* find_overload(
    const type_meta* meta, const char* name, const int* given_ids, size_t n_args) noexcept
{
    if (!meta)
    {
        return nullptr;
    }
    size_t n = meta->method_count.load(std::memory_order_acquire);
    for (size_t i = 0; i < n; ++i)
    {
        const method_meta& mm = meta->methods[i];
        if (std::strcmp(mm.name, name) != 0)
        {
            continue;
        }
        if (mm.arg_count != n_args)
        {
            continue;
        }
        if (mm.arg_count == 0)
        {
            return &mm;
        }
        if (mm.arg_type_ids == nullptr)
        {
            continue;
        }
        bool exact = true;
        for (uint8_t j = 0; j < mm.arg_count; ++j)
        {
            if (mm.arg_type_ids[j] != given_ids[j])
            {
                exact = false;
                break;
            }
        }
        if (exact)
        {
            return &mm;
        }
    }
    return nullptr;
}

// 单份调用骨架: 解析重载并校验返回类型, 失败返回 nullptr
template<typename R, typename... Args>
[[nodiscard]] inline const method_meta* resolve_call(
    const type_meta* meta, const char* name, Args&&... args) noexcept
{
    if (!meta)
    {
        return nullptr;
    }
    ((void)args, ...);
    // 首元素 0 占位: 零参数时避免零长度数组 (MSVC 拒绝, GCC 扩展容忍)
    int given_ids[] = { 0, type_id::get_type_id<std::decay_t<Args>>()... };
    const method_meta* m = find_overload(meta, name, given_ids + 1, sizeof...(Args));
    if (m == nullptr || m->return_type_id != return_type_id<R>())
    {
        return nullptr;
    }
    return m;
}

// 单份调用骨架: 参数转发 + 结果缓冲
template<typename R, typename... Args>
inline R call_method(const method_meta* m, void* obj, Args&&... args) noexcept
{
    std::array<const void*, sizeof...(Args)> arg_ptrs = { static_cast<const void*>(&args)... };
    if constexpr (std::is_void_v<R>)
    {
        m->invoker(obj, arg_ptrs.data(), nullptr, m->fn_storage.data());
    }
    else
    {
        alignas(alignof(R)) std::array<char, sizeof(R)> result_buf{};
        m->invoker(obj, arg_ptrs.data(), result_buf.data(), m->fn_storage.data());
        R* result_ptr = reinterpret_cast<R*>(result_buf.data());
        R ret = std::move(*result_ptr);
        result_ptr->~R();
        return ret;
    }
}

} // namespace detail

class query_view
{
    const type_meta* meta_{nullptr};
    storage* store_{nullptr};

public:
    query_view() noexcept = default;
    explicit query_view(const type_meta* m, storage* s = nullptr) noexcept
        : meta_(m)
        , store_(s)
    {
    }

    [[nodiscard]] bool valid() const noexcept { return meta_ != nullptr; }

    [[nodiscard]] const type_meta* meta() const noexcept { return meta_; }

    [[nodiscard]] const char* name() const noexcept { return meta_ ? meta_->name : nullptr; }
    [[nodiscard]] size_t size() const noexcept { return meta_ ? meta_->size : 0; }
    [[nodiscard]] size_t align() const noexcept { return meta_ ? meta_->align : 0; }
    [[nodiscard]] size_t field_count() const noexcept
    {
        return meta_ ? meta_->field_count.load(std::memory_order_acquire) : 0;
    }
    [[nodiscard]] size_t method_count() const noexcept
    {
        return meta_ ? meta_->method_count.load(std::memory_order_acquire) : 0;
    }
    [[nodiscard]] int type_id_value() const noexcept { return meta_ ? meta_->type_id : -1; }

    [[nodiscard]] const field_meta& field(size_t i) const noexcept
    {
        if (!meta_ || i >= field_count())
        {
            detail::abort_with_location("query_view: field index out of range");
        }
        return meta_->fields[i];
    }

    [[nodiscard]] const field_meta* field_by_name(const char* name) const noexcept
    {
        if (!meta_)
        {
            return nullptr;
        }
        size_t n = meta_->field_count.load(std::memory_order_acquire);
        for (size_t i = 0; i < n; ++i)
        {
            const field_meta& fm = meta_->fields[i];
            if (std::strcmp(fm.name, name) == 0)
            {
                return &fm;
            }
        }
        return nullptr;
    }

    [[nodiscard]] const method_meta& method(size_t i) const noexcept
    {
        if (!meta_ || i >= method_count())
        {
            detail::abort_with_location("query_view: method index out of range");
        }
        return meta_->methods[i];
    }

    [[nodiscard]] const method_meta* method_by_name(const char* name) const noexcept
    {
        if (!meta_)
        {
            return nullptr;
        }
        size_t n = meta_->method_count.load(std::memory_order_acquire);
        for (size_t i = 0; i < n; ++i)
        {
            const method_meta& mm = meta_->methods[i];
            if (std::strcmp(mm.name, name) == 0)
            {
                return &mm;
            }
        }
        return nullptr;
    }

    template<typename R>
    [[nodiscard]] R& get(void* obj, size_t i) const noexcept
    {
        return *reinterpret_cast<R*>(
            static_cast<char*>(obj) + field(i).offset);
    }

    template<typename R>
    [[nodiscard]] const R& get(const void* obj, size_t i) const noexcept
    {
        return get<R>(const_cast<void*>(obj), i);
    }

    template<typename R>
    [[nodiscard]] R& get_by_name(void* obj, const char* name) const noexcept
    {
        R* p = try_get_by_name<R>(obj, name);
        if (p == nullptr)
        {
            detail::abort_with_location("query_view: field not found");
        }
        return *p;
    }

    template<typename R>
    [[nodiscard]] const R& get_by_name(const void* obj, const char* name) const noexcept
    {
        return get_by_name<R>(const_cast<void*>(obj), name);
    }

    template<typename R>
    [[nodiscard]] R* try_get_by_name(void* obj, const char* name) const noexcept
    {
        if (!meta_ || !obj)
        {
            return nullptr;
        }
        const field_meta* f = field_by_name(name);
        return f ? reinterpret_cast<R*>(static_cast<char*>(obj) + f->offset) : nullptr;
    }

    template<typename R>
    [[nodiscard]] const R* try_get_by_name(const void* obj, const char* name) const noexcept
    {
        return try_get_by_name<R>(const_cast<void*>(obj), name);
    }

    [[nodiscard]] void* get_ptr(void* obj, const char* name) const noexcept
    {
        if (!meta_)
        {
            return nullptr;
        }
        const field_meta* f = field_by_name(name);
        return f ? static_cast<char*>(obj) + f->offset : nullptr;
    }

    [[nodiscard]] const void* get_ptr(const void* obj, const char* name) const noexcept
    {
        return get_ptr(const_cast<void*>(obj), name);
    }

    // === 数组字段查询 ===
    [[nodiscard]] bool is_array(size_t i) const noexcept
    {
        if (!meta_)
        {
            return false;
        }
        return meta_->fields[i].array_rank > 0;
    }

    [[nodiscard]] bool is_array_by_name(const char* name) const noexcept
    {
        const field_meta* f = field_by_name(name);
        return f ? f->array_rank > 0 : false;
    }

    [[nodiscard]] uint8_t array_rank(size_t i) const noexcept
    {
        return meta_ ? meta_->fields[i].array_rank : 0;
    }

    [[nodiscard]] uint32_t array_total_elements(size_t i) const noexcept
    {
        return meta_ ? meta_->fields[i].total_elements : 0;
    }

    [[nodiscard]] uint32_t array_element_stride(size_t i) const noexcept
    {
        return meta_ ? meta_->fields[i].element_stride : 0;
    }

    [[nodiscard]] uint32_t array_extent(size_t field_idx, uint8_t dim) const noexcept
    {
        if (!meta_ || dim >= 4)
        {
            return 0;
        }
        return meta_->fields[field_idx].extents[dim];
    }

    // 取数组元素指针
    [[nodiscard]] void* array_element_ptr(void* obj, size_t field_idx, uint32_t element_idx) const noexcept
    {
        if (!meta_ || !obj || field_idx >= field_count())
        {
            return nullptr;
        }
        const field_meta& fm = meta_->fields[field_idx];
        if (fm.array_rank == 0 || element_idx >= fm.total_elements)
        {
            return nullptr;
        }
        return static_cast<char*>(obj) + fm.offset + element_idx * fm.element_stride;
    }

    [[nodiscard]] const void* array_element_ptr(const void* obj, size_t field_idx, uint32_t element_idx) const noexcept
    {
        return array_element_ptr(const_cast<void*>(obj), field_idx, element_idx);
    }

    // 按名取数组元素指针
    [[nodiscard]] void* array_element_ptr_by_name(void* obj, const char* name, uint32_t element_idx) const noexcept
    {
        const field_meta* f = field_by_name(name);
        if (!f || element_idx >= f->total_elements)
        {
            return nullptr;
        }
        return static_cast<char*>(obj) + f->offset + element_idx * f->element_stride;
    }

    // === 数组便捷接口 ===

    // 类型安全访问
    template<typename T>
    [[nodiscard]] T* try_array_get(void* obj, size_t field_idx, uint32_t element_idx) const noexcept
    {
        return static_cast<T*>(array_element_ptr(obj, field_idx, element_idx));
    }

    template<typename T>
    [[nodiscard]] const T* try_array_get(const void* obj, size_t field_idx, uint32_t element_idx) const noexcept
    {
        return try_array_get<T>(const_cast<void*>(obj), field_idx, element_idx);
    }

    template<typename T>
    [[nodiscard]] T& array_get(void* obj, size_t field_idx, uint32_t element_idx) const noexcept
    {
        T* p = try_array_get<T>(obj, field_idx, element_idx);
        if (p == nullptr)
        {
            detail::abort_with_location("query_view: array element out of range");
        }
        return *p;
    }

    template<typename T>
    [[nodiscard]] const T& array_get(const void* obj, size_t field_idx, uint32_t element_idx) const noexcept
    {
        return array_get<T>(const_cast<void*>(obj), field_idx, element_idx);
    }

    // 类型安全访问 (按名)
    template<typename T>
    [[nodiscard]] T& array_get_by_name(void* obj, const char* name, uint32_t element_idx) const noexcept
    {
        T* p = static_cast<T*>(array_element_ptr_by_name(obj, name, element_idx));
        if (p == nullptr)
        {
            detail::abort_with_location("query_view: array element out of range");
        }
        return *p;
    }

    // 类型安全写入
    template<typename T>
    void array_set(void* obj, size_t field_idx, uint32_t element_idx, const T& value) const noexcept
    {
        T* p = static_cast<T*>(array_element_ptr(obj, field_idx, element_idx));
        if (p)
        {
            *p = value;
        }
    }

    // 类型安全写入 (按名)
    template<typename T>
    void array_set_by_name(void* obj, const char* name, uint32_t element_idx, const T& value) const noexcept
    {
        void* p = array_element_ptr_by_name(obj, name, element_idx);
        if (p)
        {
            *static_cast<T*>(p) = value;
        }
    }

    // 遍历数组元素
    template<typename F>
    void for_each_array_element(void* obj, size_t field_idx, F&& f) const noexcept
    {
        if (!meta_ || field_idx >= field_count())
        {
            return;
        }
        const field_meta& fm = meta_->fields[field_idx];
        if (fm.array_rank == 0)
        {
            return;
        }
        char* base = static_cast<char*>(obj) + fm.offset;
        for (uint32_t i = 0; i < fm.total_elements; ++i)
        {
            f(base + i * fm.element_stride, i, fm.type_id);
        }
    }

    template<typename F>
    void for_each_array_element(const void* obj, size_t field_idx, F&& f) const noexcept
    {
        for_each_array_element(const_cast<void*>(obj), field_idx, std::forward<F>(f));
    }

    // #12 按参数类型 id 精确匹配的重载查找
    [[nodiscard]] const method_meta* find_overload(
        const char* name, const int* given_ids, size_t n_args) const noexcept
    {
        return detail::find_overload(meta_, name, given_ids, n_args);
    }

    // 方法调用 (失败 abort, #12 支持重载按类型匹配)
    template<typename R = void, typename... Args>
    R invoke(void* obj, const char* name, Args&&... args) const noexcept
    {
        const method_meta* m = detail::resolve_call<R>(meta_, name, args...);
        if (m == nullptr)
        {
            detail::abort_with_location("invoke: method not found or return type mismatch");
        }
        return detail::call_method<R>(m, obj, std::forward<Args>(args)...);
    }

    // 软失败方法调用 (void 返回 bool, 非 void 返回 optional<R>)
    template<typename R = void, typename... Args>
    [[nodiscard]] auto try_invoke(void* obj, const char* name, Args&&... args) const noexcept
    {
        const method_meta* m = detail::resolve_call<R>(meta_, name, args...);
        if constexpr (std::is_void_v<R>)
        {
            if (m == nullptr)
            {
                return false;
            }
            detail::call_method<void>(m, obj, std::forward<Args>(args)...);
            return true;
        }
        else
        {
            if (m == nullptr)
            {
                return std::optional<R>{};
            }
            return std::optional<R>(detail::call_method<R>(m, obj, std::forward<Args>(args)...));
        }
    }

    // #11 字段路径访问 (支持 "a.b.c" 嵌套路径)
    [[nodiscard]] void* get_by_path(void* obj, const char* path) const noexcept
    {
        if (!meta_ || !path)
        {
            return nullptr;
        }
        const char* dot = std::strchr(path, '.');
        size_t len = dot ? static_cast<size_t>(dot - path) : std::strlen(path);
        // 当前级字段查找
        size_t n = meta_->field_count.load(std::memory_order_acquire);
        const field_meta* found = nullptr;
        for (size_t i = 0; i < n; ++i)
        {
            const field_meta& fm = meta_->fields[i];
            if (fm.name && std::strlen(fm.name) == len && std::strncmp(fm.name, path, len) == 0)
            {
                found = &fm;
                break;
            }
        }
        if (!found)
        {
            return nullptr;
        }
        void* next = static_cast<char*>(obj) + found->offset;
        if (!dot)
        {
            return next;
        }
        // 优先当前 manager 存储
        const type_meta* sub = store_ != nullptr
            ? store_->get_type(found->type_id)
            : global().get_type(found->type_id);
        if (!sub)
        {
            return nullptr;
        }
        return query_view(sub, store_).get_by_path(next, dot + 1);
    }

    [[nodiscard]] const void* get_by_path(const void* obj, const char* path) const noexcept
    {
        return get_by_path(const_cast<void*>(obj), path);
    }

    template<typename T>
    [[nodiscard]] T* get_by_path_as(void* obj, const char* path) const noexcept
    {
        return static_cast<T*>(get_by_path(obj, path));
    }

    template<typename T>
    [[nodiscard]] const T* get_by_path_as(const void* obj, const char* path) const noexcept
    {
        return get_by_path_as<T>(const_cast<void*>(obj), path);
    }
};
// 软失败查询: 按类型
template<typename T>
[[nodiscard]] query_view try_get() noexcept
{
    return query_view(global().get_type(type_id::get_type_id<T>()));
}

// 软失败查询: 按类型名
[[nodiscard]] inline query_view try_get_by_name(const char* name) noexcept
{
    return query_view(global().find_type(name));
}

} // namespace reflect

// ============================================================================
// ============================================================================
// enum_view.hpp - 枚举反射查询接口 (#3)
// 命名空间: reflect
// 基于 enum_meta 的 values 列表实现枚举值↔名称转换


namespace reflect {

class enum_view
{
    const enum_meta* meta_{nullptr};

public:
    enum_view() noexcept = default;
    explicit enum_view(const enum_meta* m) noexcept : meta_(m) {}

    [[nodiscard]] bool valid() const noexcept { return meta_ != nullptr; }
    [[nodiscard]] const char* name() const noexcept { return meta_ ? meta_->name : nullptr; }
    [[nodiscard]] int type_id_value() const noexcept { return meta_ ? meta_->type_id : -1; }
    [[nodiscard]] int underlying_type_id() const noexcept { return meta_ ? meta_->underlying_type_id : -1; }
    [[nodiscard]] size_t value_count() const noexcept { return meta_ ? meta_->values.size() : 0; }

    // 值 → 名称 (失败返回 nullptr)
    [[nodiscard]] const char* value_to_name(uint64_t value) const noexcept
    {
        if (!meta_)
        {
            return nullptr;
        }
        size_t n = meta_->values.size();
        for (size_t i = 0; i < n; ++i)
        {
            if (meta_->values[i].value == value)
            {
                return meta_->values[i].name;
            }
        }
        return nullptr;
    }

    template<typename E>
    [[nodiscard]] const char* value_to_name(E value) const noexcept
    {
        static_assert(std::is_enum_v<E>);
        using UT = std::underlying_type_t<E>;
        return value_to_name(static_cast<uint64_t>(static_cast<UT>(value)));
    }

    // 名称 → 值 (失败返回 false)
    [[nodiscard]] bool name_to_value(const char* name, uint64_t& out) const noexcept
    {
        if (!meta_ || !name)
        {
            return false;
        }
        uint64_t nh = fnv1a_runtime(name);
        size_t n = meta_->values.size();
        for (size_t i = 0; i < n; ++i)
        {
            if (meta_->values[i].name_hash == nh)
            {
                out = meta_->values[i].value;
                return true;
            }
        }
        return false;
    }

    template<typename E>
    [[nodiscard]] bool name_to_value(const char* name, E& out) const noexcept
    {
        static_assert(std::is_enum_v<E>);
        using UT = std::underlying_type_t<E>;
        uint64_t v;
        if (!name_to_value(name, v))
        {
            return false;
        }
        out = static_cast<E>(static_cast<UT>(v));
        return true;
    }

    // 遍历所有枚举值
    template<typename F>
    void for_each_value(F&& f) const noexcept
    {
        if (!meta_)
        {
            return;
        }
        size_t n = meta_->values.size();
        for (size_t i = 0; i < n; ++i)
        {
            f(meta_->values[i].value, meta_->values[i].name);
        }
    }
};

} // namespace reflect

// ============================================================================
