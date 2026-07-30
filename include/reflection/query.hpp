#pragma once

// query.hpp - 反射查询器
// 命名空间: reflect

#include <cstring>
#include <cstdlib>
#include <utility>
#include <type_traits>
#include <optional>
#include <source_location>
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
        if (!meta_) { return nullptr; }
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
        if (!meta_) { return nullptr; }
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
        if (!meta_) { return nullptr; }
        const field_meta* f = field_by_name(name);
        return f ? static_cast<char*>(obj) + f->offset : nullptr;
    }

    [[nodiscard]] const void* get_ptr(const void* obj, const char* name) const noexcept
    {
        if (!meta_) { return nullptr; }
        const field_meta* f = field_by_name(name);
        return f ? static_cast<const char*>(obj) + f->offset : nullptr;
    }

    // === 数组字段查询 ===
    [[nodiscard]] bool is_array(size_t i) const noexcept
    {
        if (!meta_) { return false; }
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
        if (!meta_ || dim >= 4) { return 0; }
        return meta_->fields[field_idx].extents[dim];
    }

    // 取数组元素指针
    [[nodiscard]] void* array_element_ptr(void* obj, size_t field_idx, uint32_t element_idx) const noexcept
    {
        if (!meta_) { return nullptr; }
        const field_meta& fm = meta_->fields[field_idx];
        if (element_idx >= fm.total_elements) { return nullptr; }
        return static_cast<char*>(obj) + fm.offset + element_idx * fm.element_stride;
    }

    [[nodiscard]] const void* array_element_ptr(const void* obj, size_t field_idx, uint32_t element_idx) const noexcept
    {
        if (!meta_) { return nullptr; }
        const field_meta& fm = meta_->fields[field_idx];
        if (element_idx >= fm.total_elements) { return nullptr; }
        return static_cast<const char*>(obj) + fm.offset + element_idx * fm.element_stride;
    }

    // 按名取数组元素指针
    [[nodiscard]] void* array_element_ptr_by_name(void* obj, const char* name, uint32_t element_idx) const noexcept
    {
        const field_meta* f = field_by_name(name);
        if (!f || element_idx >= f->total_elements) { return nullptr; }
        return static_cast<char*>(obj) + f->offset + element_idx * f->element_stride;
    }

    // === 数组便捷接口 ===

    // 聚合查询
    [[nodiscard]] const field_meta* array_info(size_t i) const noexcept
    {
        if (!meta_) { return nullptr; }
        if (i >= field_count()) { return nullptr; }
        const field_meta& fm = meta_->fields[i];
        return fm.array_rank > 0 ? &fm : nullptr;
    }

    // 聚合查询 (按名)
    [[nodiscard]] const field_meta* array_info_by_name(const char* name) const noexcept
    {
        const field_meta* f = field_by_name(name);
        if (!f || f->array_rank == 0) { return nullptr; }
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
        if (p) { *p = value; }
    }

    // 类型安全写入 (按名)
    template<typename T>
    void array_set_by_name(void* obj, const char* name, uint32_t element_idx, const T& value) const noexcept
    {
        void* p = array_element_ptr_by_name(obj, name, element_idx);
        if (p) { *static_cast<T*>(p) = value; }
    }

    // 遍历数组元素
    template<typename F>
    void for_each_array_element(void* obj, size_t field_idx, F&& f) const noexcept
    {
        if (!meta_) { return; }
        const field_meta& fm = meta_->fields[field_idx];
        if (fm.array_rank == 0) { return; }
        char* base = static_cast<char*>(obj) + fm.offset;
        for (uint32_t i = 0; i < fm.total_elements; ++i)
        {
            f(base + i * fm.element_stride, i, fm.type_id);
        }
    }

    template<typename F>
    void for_each_array_element(const void* obj, size_t field_idx, F&& f) const noexcept
    {
        if (!meta_) { return; }
        const field_meta& fm = meta_->fields[field_idx];
        if (fm.array_rank == 0) { return; }
        const char* base = static_cast<const char*>(obj) + fm.offset;
        for (uint32_t i = 0; i < fm.total_elements; ++i)
        {
            f(base + i * fm.element_stride, i, fm.type_id);
        }
    }

    // 方法调用 (失败 abort)
    template<typename R = void, typename... Args>
    R invoke(void* obj, const char* name, Args&&... args) const noexcept
    {
        const method_meta* m = method_by_name(name);
        if (m == nullptr) { detail::abort_with_location("invoke: method not found"); }
        if (m->arg_count != sizeof...(Args)) { detail::abort_with_location("invoke: arg count mismatch"); }

        const void* arg_ptrs[] = { static_cast<const void*>(&args)... };

        if constexpr (std::is_void_v<R>)
        {
            m->invoker(obj, arg_ptrs, nullptr);
        }
        else
        {
            alignas(alignof(R)) char result_buf[sizeof(R)];
            m->invoker(obj, arg_ptrs, result_buf);
            R* result_ptr = reinterpret_cast<R*>(result_buf);
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
            if (!meta_) { return false; }
            const method_meta* m = method_by_name(name);
            if (m == nullptr || m->arg_count != sizeof...(Args)) { return false; }
            const void* arg_ptrs[] = { static_cast<const void*>(&args)... };
            m->invoker(obj, arg_ptrs, nullptr);
            return true;
        }
        else
        {
            if (!meta_) { return std::optional<R>{}; }
            const method_meta* m = method_by_name(name);
            if (m == nullptr || m->arg_count != sizeof...(Args)) { return std::optional<R>{}; }
            const void* arg_ptrs[] = { static_cast<const void*>(&args)... };
            alignas(alignof(R)) char result_buf[sizeof(R)];
            m->invoker(obj, arg_ptrs, result_buf);
            R* result_ptr = reinterpret_cast<R*>(result_buf);
            std::optional<R> ret(std::move(*result_ptr));
            result_ptr->~R();
            return ret;
        }
    }

    template<typename F>
    void for_each_field(void* obj, F&& f) const noexcept
    {
        if (!meta_) { return; }
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
        if (!meta_) { return; }
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
        if (!meta_) { return; }
        size_t n = meta_->field_count.load(std::memory_order_acquire);
        for (size_t i = 0; i < n; ++i)
        {
            f(meta_->fields[i]);
        }
    }

    template<typename F>
    void for_each_method(F&& f) const noexcept
    {
        if (!meta_) { return; }
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
    if (m == nullptr) { std::abort(); }
    return query_view(m);
}

// 按类型名查询 (失败 abort)
[[nodiscard]] inline query_view get_by_name(const char* name) noexcept
{
    const type_meta* m = global().find_type(name);
    if (m == nullptr) { std::abort(); }
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
