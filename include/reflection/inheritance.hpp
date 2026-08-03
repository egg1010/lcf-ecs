// inheritance.hpp - 继承关系查询接口 (#2)
// 命名空间: reflect
// 基于 type_meta 的 base_offsets/derived_type_ids 实现向上/向下转型
#pragma once

#include "query.hpp"

namespace reflect {

class inheritance_view
{
    const type_meta* meta_{nullptr};

public:
    inheritance_view() noexcept = default;
    explicit inheritance_view(const type_meta* m) noexcept : meta_(m) {}

    // 检查是否继承自 base_type_id (含间接继承)
    [[nodiscard]] bool is_derived_from(int base_type_id) const noexcept
    {
        if (!meta_)
        {
            return false;
        }
        if (meta_->type_id == base_type_id)
        {
            return true;
        }

        // 直接基类检查
        size_t n = meta_->base_offsets.size();
        for (size_t i = 0; i < n; ++i)
        {
            if (meta_->base_offsets[i].base_type_id == base_type_id)
            {
                return true;
            }
        }

        // 递归检查间接基类
        for (size_t i = 0; i < n; ++i)
        {
            const type_meta* base = global().get_type(meta_->base_offsets[i].base_type_id);
            if (base && inheritance_view(base).is_derived_from(base_type_id))
            {
                return true;
            }
        }
        return false;
    }

    template<typename Base>
    [[nodiscard]] bool is_derived_from() const noexcept
    {
        return is_derived_from(type_id::get_type_id<Base>());
    }

    // 检查是否是 base_type_id 的基类
    [[nodiscard]] bool is_base_of(int derived_type_id) const noexcept
    {
        if (!meta_)
        {
            return false;
        }
        const type_meta* derived = global().get_type(derived_type_id);
        if (!derived)
        {
            return false;
        }
        return inheritance_view(derived).is_derived_from(meta_->type_id);
    }

    template<typename Derived>
    [[nodiscard]] bool is_base_of() const noexcept
    {
        return is_base_of(type_id::get_type_id<Derived>());
    }

    // 向上转型 (派生类指针 → 基类指针)
    [[nodiscard]] void* upcast(void* derived_obj, int base_type_id) const noexcept
    {
        if (!meta_ || !derived_obj)
        {
            return nullptr;
        }
        if (meta_->type_id == base_type_id)
        {
            return derived_obj;
        }

        size_t n = meta_->base_offsets.size();
        for (size_t i = 0; i < n; ++i)
        {
            const auto& be = meta_->base_offsets[i];
            if (be.base_type_id == base_type_id)
            {
                return static_cast<char*>(derived_obj) + be.offset;
            }
            // 递归向上转型
            const type_meta* base = global().get_type(be.base_type_id);
            if (base)
            {
                void* intermediate = static_cast<char*>(derived_obj) + be.offset;
                void* result = inheritance_view(base).upcast(intermediate, base_type_id);
                if (result)
                {
                    return result;
                }
            }
        }
        return nullptr;
    }

    template<typename Base>
    [[nodiscard]] Base* upcast(void* derived_obj) const noexcept
    {
        return static_cast<Base*>(upcast(derived_obj, type_id::get_type_id<Base>()));
    }

    // 直接基类数量
    [[nodiscard]] size_t base_count() const noexcept
    {
        return meta_ ? meta_->base_offsets.size() : 0;
    }

    // 直接派生类数量
    [[nodiscard]] size_t derived_count() const noexcept
    {
        return meta_ ? meta_->derived_type_ids.size() : 0;
    }

    // 获取直接基类 type_id
    [[nodiscard]] int base_type_id_at(size_t idx) const noexcept
    {
        if (!meta_ || idx >= meta_->base_offsets.size())
        {
            return -1;
        }
        return meta_->base_offsets[idx].base_type_id;
    }

    // 获取直接派生类 type_id
    [[nodiscard]] int derived_type_id_at(size_t idx) const noexcept
    {
        if (!meta_ || idx >= meta_->derived_type_ids.size())
        {
            return -1;
        }
        return meta_->derived_type_ids[idx];
    }

    // 遍历所有直接基类
    template<typename F>
    void for_each_base(F&& f) const noexcept
    {
        if (!meta_)
        {
            return;
        }
        size_t n = meta_->base_offsets.size();
        for (size_t i = 0; i < n; ++i)
        {
            f(meta_->base_offsets[i].base_type_id, meta_->base_offsets[i].offset);
        }
    }

    // 遍历所有直接派生类
    template<typename F>
    void for_each_derived(F&& f) const noexcept
    {
        if (!meta_)
        {
            return;
        }
        size_t n = meta_->derived_type_ids.size();
        for (size_t i = 0; i < n; ++i)
        {
            f(meta_->derived_type_ids[i]);
        }
    }
};

template<typename T>
[[nodiscard]] inheritance_view get_inheritance() noexcept
{
    return inheritance_view(global().get_type(type_id::get_type_id<T>()));
}

[[nodiscard]] inline inheritance_view get_inheritance(const query_view& q) noexcept
{
    return inheritance_view(q.meta());
}

} // namespace reflect
