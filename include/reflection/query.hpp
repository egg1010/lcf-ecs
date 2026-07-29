#pragma once

// query.hpp - 反射查询器
// 命名空间: reflect

#include <cstring>
#include <cstdlib>
#include <utility>
#include <type_traits>
#include <optional>
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

    // 方法调用 (失败 abort)
    template<typename R = void, typename... Args>
    R invoke(void* obj, const char* name, Args&&... args) const noexcept
    {
        const method_meta* m = method_by_name(name);
        if (m == nullptr) { std::abort(); }
        if (m->arg_count != sizeof...(Args)) { std::abort(); }

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
