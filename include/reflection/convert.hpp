// convert.hpp - 类型转换查询接口 (#7)
// 命名空间: reflect
// 基于 type_meta 的 converters 列表实现类型转换
#pragma once

#include "query.hpp"
#include "../part/type_ops.hpp"

namespace reflect {

class convert_view
{
    const type_meta* meta_{nullptr};

public:
    convert_view() noexcept = default;
    explicit convert_view(const type_meta* m) noexcept : meta_(m) {}

    // 检查是否可转换到目标类型
    [[nodiscard]] bool can_convert_to(int target_type_id) const noexcept
    {
        if (!meta_)
        {
            return false;
        }
        size_t n = meta_->converters.size();
        for (size_t i = 0; i < n; ++i)
        {
            if (meta_->converters[i].target_type_id == target_type_id)
            {
                return true;
            }
        }
        return false;
    }

    template<typename U>
    [[nodiscard]] bool can_convert_to() const noexcept
    {
        return can_convert_to(type_id::get_type_id<U>());
    }

    // 转换到目标类型 (失败返回 false)
    [[nodiscard]] bool convert_to(const void* src, int target_type_id, void* dst) const noexcept
    {
        if (!meta_ || !src || !dst)
        {
            return false;
        }
        size_t n = meta_->converters.size();
        for (size_t i = 0; i < n; ++i)
        {
            if (meta_->converters[i].target_type_id == target_type_id)
            {
                meta_->converters[i].convert_fn(src, dst);
                return true;
            }
        }
        return false;
    }

    // 类型安全的转换 (返回 optional)
    template<typename U>
    [[nodiscard]] std::optional<U> convert_to(const void* src) const noexcept
    {
        if (!meta_ || !src)
        {
            return std::nullopt;
        }
        int tid = type_id::get_type_id<U>();
        size_t n = meta_->converters.size();
        for (size_t i = 0; i < n; ++i)
        {
            if (meta_->converters[i].target_type_id == tid)
            {
                U result{};
                meta_->converters[i].convert_fn(src, &result);
                return result;
            }
        }
        return std::nullopt;
    }

    // 遍历所有可转换目标
    template<typename F>
    void for_each_convertible(F&& f) const noexcept
    {
        if (!meta_)
        {
            return;
        }
        size_t n = meta_->converters.size();
        for (size_t i = 0; i < n; ++i)
        {
            f(meta_->converters[i].target_type_id);
        }
    }
};

template<typename T>
[[nodiscard]] convert_view get_convert() noexcept
{
    return convert_view(global().get_type(type_id::get_type_id<T>()));
}

[[nodiscard]] inline convert_view get_convert(const query_view& q) noexcept
{
    return convert_view(q.meta());
}

} // namespace reflect
