#pragma once

// query.hpp - 反射查询器
// 命名空间: reflect

#include <cstring>
#include <cstdlib>
#include <utility>
#include <type_traits>
#include <optional>
#include <source_location>
#include <array>
#include "../part/type_id.hpp"
#include "storage.hpp"

namespace reflect {

class query_view
{
    const type_meta* meta_{nullptr};

public:
    query_view() noexcept = default;
    explicit query_view(const type_meta* m) noexcept : meta_(m) {}

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
        return *reinterpret_cast<const R*>(
            static_cast<const char*>(obj) + field(i).offset);
    }

    template<typename R>
    [[nodiscard]] R& get_by_name(void* obj, const char* name) const noexcept
    {
        return *reinterpret_cast<R*>(
            static_cast<char*>(obj) + field_by_name(name)->offset);
    }

    template<typename R>
    [[nodiscard]] const R& get_by_name(const void* obj, const char* name) const noexcept
    {
        return *reinterpret_cast<const R*>(
            static_cast<const char*>(obj) + field_by_name(name)->offset);
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
        if (!meta_)
        {
            return nullptr;
        }
        const field_meta* f = field_by_name(name);
        return f ? static_cast<const char*>(obj) + f->offset : nullptr;
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

    [[nodiscard]] uint16_t array_extent(size_t field_idx, uint8_t dim) const noexcept
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
        if (!meta_)
        {
            return nullptr;
        }
        const field_meta& fm = meta_->fields[field_idx];
        if (element_idx >= fm.total_elements)
        {
            return nullptr;
        }
        return static_cast<char*>(obj) + fm.offset + element_idx * fm.element_stride;
    }

    [[nodiscard]] const void* array_element_ptr(const void* obj, size_t field_idx, uint32_t element_idx) const noexcept
    {
        if (!meta_)
        {
            return nullptr;
        }
        const field_meta& fm = meta_->fields[field_idx];
        if (element_idx >= fm.total_elements)
        {
            return nullptr;
        }
        return static_cast<const char*>(obj) + fm.offset + element_idx * fm.element_stride;
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

    // 聚合查询
    [[nodiscard]] const field_meta* array_info(size_t i) const noexcept
    {
        if (!meta_)
        {
            return nullptr;
        }
        if (i >= field_count())
        {
            return nullptr;
        }
        const field_meta& fm = meta_->fields[i];
        return fm.array_rank > 0 ? &fm : nullptr;
    }

    // 聚合查询 (按名)
    [[nodiscard]] const field_meta* array_info_by_name(const char* name) const noexcept
    {
        const field_meta* f = field_by_name(name);
        if (!f || f->array_rank == 0)
        {
            return nullptr;
        }
        return f;
    }

    // 类型安全访问
    template<typename T>
    [[nodiscard]] T& array_get(void* obj, size_t field_idx, uint32_t element_idx) const noexcept
    {
        return *static_cast<T*>(array_element_ptr(obj, field_idx, element_idx));
    }

    template<typename T>
    [[nodiscard]] const T& array_get(const void* obj, size_t field_idx, uint32_t element_idx) const noexcept
    {
        return *static_cast<const T*>(array_element_ptr(obj, field_idx, element_idx));
    }

    // 类型安全访问 (按名)
    template<typename T>
    [[nodiscard]] T& array_get_by_name(void* obj, const char* name, uint32_t element_idx) const noexcept
    {
        return *static_cast<T*>(array_element_ptr_by_name(obj, name, element_idx));
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
        if (!meta_)
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
        if (!meta_)
        {
            return;
        }
        const field_meta& fm = meta_->fields[field_idx];
        if (fm.array_rank == 0)
        {
            return;
        }
        const char* base = static_cast<const char*>(obj) + fm.offset;
        for (uint32_t i = 0; i < fm.total_elements; ++i)
        {
            f(base + i * fm.element_stride, i, fm.type_id);
        }
    }

    // #12 按参数类型 id 精确匹配的重载查找
    [[nodiscard]] const method_meta* find_overload(
        const char* name, const int* given_ids, size_t n_args) const noexcept
    {
        if (!meta_)
        {
            return nullptr;
        }
        size_t n = meta_->method_count.load(std::memory_order_acquire);
        const method_meta* fallback = nullptr;
        for (size_t i = 0; i < n; ++i)
        {
            const method_meta& mm = meta_->methods[i];
            if (std::strcmp(mm.name, name) != 0)
            {
                continue;
            }
            if (mm.arg_count != n_args)
            {
                continue;
            }
            // 精确匹配 arg_type_ids
            if (mm.arg_type_ids)
            {
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
            if (!fallback)
            {
                fallback = &mm;  // 无 arg_type_ids 或不精确, 作为回退
            }
        }
        return fallback;
    }

    // 方法调用 (失败 abort, #12 支持重载按类型匹配)
    template<typename R = void, typename... Args>
    R invoke(void* obj, const char* name, Args&&... args) const noexcept
    {
        if (!meta_)
        {
            detail::abort_with_location("invoke: query_view invalid");
        }
        // 首元素 0 占位: 零参数时避免零长度数组 (MSVC 拒绝, GCC 扩展容忍)
        int given_ids[] = { 0, type_id::get_type_id<std::decay_t<Args>>()... };
        const method_meta* m = find_overload(name, given_ids + 1, sizeof...(Args));
        if (m == nullptr)
        {
            detail::abort_with_location("invoke: method not found");
        }

        std::array<const void*, sizeof...(Args)> arg_ptrs = { static_cast<const void*>(&args)... };

        if constexpr (std::is_void_v<R>)
        {
            m->invoker(obj, arg_ptrs.data(), nullptr);
        }
        else
        {
            alignas(alignof(R)) std::array<char, sizeof(R)> result_buf{};
            m->invoker(obj, arg_ptrs.data(), result_buf.data());
            R* result_ptr = reinterpret_cast<R*>(result_buf.data());
            R ret = std::move(*result_ptr);
            result_ptr->~R();
            return ret;
        }
    }

    // 软失败方法调用 (void 返回 bool, 非 void 返回 optional<R>)
    template<typename R = void, typename... Args>
    [[nodiscard]] auto try_invoke(void* obj, const char* name, Args&&... args) const noexcept
    {
        if constexpr (std::is_void_v<R>)
        {
            if (!meta_)
            {
                return false;
            }
            // 首元素 0 占位: 零参数时避免零长度数组 (MSVC 拒绝, GCC 扩展容忍)
            int given_ids[] = { 0, type_id::get_type_id<std::decay_t<Args>>()... };
            const method_meta* m = find_overload(name, given_ids + 1, sizeof...(Args));
            if (!m)
            {
                return false;
            }
            std::array<const void*, sizeof...(Args)> arg_ptrs = { static_cast<const void*>(&args)... };
            m->invoker(obj, arg_ptrs.data(), nullptr);
            return true;
        }
        else
        {
            if (!meta_)
            {
                return std::optional<R>{};
            }
            // 首元素 0 占位: 零参数时避免零长度数组 (MSVC 拒绝, GCC 扩展容忍)
            int given_ids[] = { 0, type_id::get_type_id<std::decay_t<Args>>()... };
            const method_meta* m = find_overload(name, given_ids + 1, sizeof...(Args));
            if (!m)
            {
                return std::optional<R>{};
            }
            std::array<const void*, sizeof...(Args)> arg_ptrs = { static_cast<const void*>(&args)... };
            alignas(alignof(R)) std::array<char, sizeof(R)> result_buf{};
            m->invoker(obj, arg_ptrs.data(), result_buf.data());
            R* result_ptr = reinterpret_cast<R*>(result_buf.data());
            std::optional<R> ret(std::move(*result_ptr));
            result_ptr->~R();
            return ret;
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
        // 递归: 根据 found->type_id 查子类型
        const type_meta* sub = global().get_type(found->type_id);
        if (!sub)
        {
            return nullptr;
        }
        return query_view(sub).get_by_path(next, dot + 1);
    }

    [[nodiscard]] const void* get_by_path(const void* obj, const char* path) const noexcept
    {
        if (!meta_ || !path)
        {
            return nullptr;
        }
        const char* dot = std::strchr(path, '.');
        size_t len = dot ? static_cast<size_t>(dot - path) : std::strlen(path);
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
        const void* next = static_cast<const char*>(obj) + found->offset;
        if (!dot)
        {
            return next;
        }
        const type_meta* sub = global().get_type(found->type_id);
        if (!sub)
        {
            return nullptr;
        }
        return query_view(sub).get_by_path(next, dot + 1);
    }

    template<typename T>
    [[nodiscard]] T* get_by_path_as(void* obj, const char* path) const noexcept
    {
        return static_cast<T*>(get_by_path(obj, path));
    }

    template<typename T>
    [[nodiscard]] const T* get_by_path_as(const void* obj, const char* path) const noexcept
    {
        return static_cast<const T*>(get_by_path(obj, path));
    }

    template<typename F>
    void for_each_field(void* obj, F&& f) const noexcept
    {
        if (!meta_)
        {
            return;
        }
        size_t n = meta_->field_count.load(std::memory_order_acquire);
        for (size_t i = 0; i < n; ++i)
        {
            const field_meta& fm = meta_->fields[i];
            f(fm.name,
              static_cast<char*>(obj) + fm.offset,
              fm.type_id);
        }
    }

    template<typename F>
    void for_each_field(const void* obj, F&& f) const noexcept
    {
        if (!meta_)
        {
            return;
        }
        size_t n = meta_->field_count.load(std::memory_order_acquire);
        for (size_t i = 0; i < n; ++i)
        {
            const field_meta& fm = meta_->fields[i];
            f(fm.name,
              static_cast<const char*>(obj) + fm.offset,
              fm.type_id);
        }
    }

    template<typename F>
    void for_each_field_meta(F&& f) const noexcept
    {
        if (!meta_)
        {
            return;
        }
        size_t n = meta_->field_count.load(std::memory_order_acquire);
        for (size_t i = 0; i < n; ++i)
        {
            f(meta_->fields[i]);
        }
    }

    template<typename F>
    void for_each_method(F&& f) const noexcept
    {
        if (!meta_)
        {
            return;
        }
        size_t n = meta_->method_count.load(std::memory_order_acquire);
        for (size_t i = 0; i < n; ++i)
        {
            f(meta_->methods[i]);
        }
    }
};

// 按类型查询 (失败 abort)
template<typename T>
[[nodiscard]] query_view get() noexcept
{
    const type_meta* m = global().get_type(type_id::get_type_id<T>());
    if (m == nullptr)
    {
        std::abort();
    }
    return query_view(m);
}

// 按类型名查询 (失败 abort)
[[nodiscard]] inline query_view get_by_name(const char* name) noexcept
{
    const type_meta* m = global().find_type(name);
    if (m == nullptr)
    {
        std::abort();
    }
    return query_view(m);
}

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
