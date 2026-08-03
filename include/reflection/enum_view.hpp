// enum_view.hpp - 枚举反射查询接口 (#3)
// 命名空间: reflect
// 基于 enum_meta 的 values 列表实现枚举值↔名称转换
#pragma once

#include "storage.hpp"

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

template<typename E>
[[nodiscard]] enum_view get_enum() noexcept
{
    return enum_view(global().find_enum<E>());
}

[[nodiscard]] inline enum_view get_enum(int type_id) noexcept
{
    return enum_view(global().find_enum(type_id));
}

} // namespace reflect
