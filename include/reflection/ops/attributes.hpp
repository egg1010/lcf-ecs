// attributes.hpp - 字段属性/注解查询接口 (#4)
// 命名空间: reflect
// 基于 field_meta::attrs 的 dense<attr_entry> 实现
#pragma once

#include "query.hpp"
#include "../part/fnv1a.hpp"

namespace reflect {

class attribute_view
{
    const type_meta* meta_{nullptr};

public:
    attribute_view() noexcept = default;
    explicit attribute_view(const type_meta* m) noexcept : meta_(m) {}

    // 检查字段是否有指定属性
    [[nodiscard]] bool has_attr(size_t field_idx, const char* key) const noexcept
    {
        if (!meta_ || field_idx >= meta_->field_count.load(std::memory_order_acquire))
        {
            return false;
        }
        uint64_t kh = fnv1a_runtime(key);
        const auto& attrs = meta_->fields[field_idx].attrs;
        size_t n = attrs.size();
        for (size_t i = 0; i < n; ++i)
        {
            if (attrs[i].key_hash == kh)
            {
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] bool has_attr(const char* field_name, const char* key) const noexcept
    {
        const field_meta* f = meta_ ? field_by_name_impl(field_name) : nullptr;
        if (!f)
        {
            return false;
        }
        uint64_t kh = fnv1a_runtime(key);
        size_t n = f->attrs.size();
        for (size_t i = 0; i < n; ++i)
        {
            if (f->attrs[i].key_hash == kh)
            {
                return true;
            }
        }
        return false;
    }

    // 获取属性值 (void_any 指针, 失败返回 nullptr)
    [[nodiscard]] const void_any* get_attr(size_t field_idx, const char* key) const noexcept
    {
        if (!meta_ || field_idx >= meta_->field_count.load(std::memory_order_acquire))
        {
            return nullptr;
        }
        uint64_t kh = fnv1a_runtime(key);
        const auto& attrs = meta_->fields[field_idx].attrs;
        size_t n = attrs.size();
        for (size_t i = 0; i < n; ++i)
        {
            if (attrs[i].key_hash == kh)
            {
                return &attrs[i].value;
            }
        }
        return nullptr;
    }

    [[nodiscard]] const void_any* get_attr(const char* field_name, const char* key) const noexcept
    {
        const field_meta* f = meta_ ? field_by_name_impl(field_name) : nullptr;
        if (!f)
        {
            return nullptr;
        }
        uint64_t kh = fnv1a_runtime(key);
        size_t n = f->attrs.size();
        for (size_t i = 0; i < n; ++i)
        {
            if (f->attrs[i].key_hash == kh)
            {
                return &f->attrs[i].value;
            }
        }
        return nullptr;
    }

    // 类型安全获取
    template<typename V>
    [[nodiscard]] const V* get_attr_as(size_t field_idx, const char* key) const noexcept
    {
        const void_any* any = get_attr(field_idx, key);
        return any ? any->get_ptr<V>() : nullptr;
    }

    template<typename V>
    [[nodiscard]] const V* get_attr_as(const char* field_name, const char* key) const noexcept
    {
        const void_any* any = get_attr(field_name, key);
        return any ? any->get_ptr<V>() : nullptr;
    }

    // 遍历字段所有属性
    template<typename F>
    void for_each_attr(size_t field_idx, F&& f) const noexcept
    {
        if (!meta_ || field_idx >= meta_->field_count.load(std::memory_order_acquire))
        {
            return;
        }
        const auto& attrs = meta_->fields[field_idx].attrs;
        size_t n = attrs.size();
        for (size_t i = 0; i < n; ++i)
        {
            f(attrs[i].key_hash, attrs[i].value);
        }
    }

private:
    [[nodiscard]] const field_meta* field_by_name_impl(const char* name) const noexcept
    {
        size_t n = meta_->field_count.load(std::memory_order_acquire);
        for (size_t i = 0; i < n; ++i)
        {
            if (std::strcmp(meta_->fields[i].name, name) == 0)
            {
                return &meta_->fields[i];
            }
        }
        return nullptr;
    }
};

template<typename T>
[[nodiscard]] attribute_view get_attributes() noexcept
{
    return attribute_view(global().get_type(type_id::get_type_id<T>()));
}

[[nodiscard]] inline attribute_view get_attributes(const query_view& q) noexcept
{
    return attribute_view(q.meta());
}

} // namespace reflect
