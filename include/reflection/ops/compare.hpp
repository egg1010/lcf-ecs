// compare.hpp - 字段比较/克隆查询接口 (#8)
// 命名空间: reflect
// 复用 meta.hpp 的单份字段遍历骨架, 逐字段/逐元素比较与拷贝
#pragma once

#include "reflection/core/query.hpp"
#include "reflection/base/field_ops.hpp"

namespace reflect {

class compare_view
{
    const type_meta* meta_{nullptr};

public:
    compare_view() noexcept = default;
    explicit compare_view(const type_meta* m) noexcept : meta_(m) {}

    // 逐字段相等比较 (数组字段逐元素比较)
    [[nodiscard]] bool equal(const void* a, const void* b) const noexcept
    {
        if (!meta_)
        {
            return false;
        }
        if (a == b)
        {
            return true;
        }
        if (!a || !b)
        {
            return false;
        }
        return detail::visit_field_slots(a, b, *meta_,
            [](const detail::field_slot& s, const field_meta&) noexcept {
                if (s.ops && s.ops->equal_fn)
                {
                    for (uint32_t e = 0; e < s.count; ++e)
                    {
                        if (!s.ops->equal_fn(s.base + e * s.stride, s.other + e * s.stride))
                        {
                            return false;
                        }
                    }
                    return true;
                }
                if (!s.trivially_copyable)
                {
                    detail::abort_with_location("equal: field type ops missing for non-trivial field");
                }
                return std::memcmp(s.base, s.other,
                                   static_cast<size_t>(s.count) * s.stride) == 0;
            });
    }

    // 逐字段克隆 (copy_construct, dst 必须是未初始化内存)
    void clone(const void* src, void* dst) const noexcept
    {
        if (!meta_ || !src || !dst)
        {
            return;
        }
        detail::visit_field_slots(dst, src, *meta_,
            [](const detail::field_slot& s, const field_meta&) noexcept {
                char* pd = const_cast<char*>(s.base);
                const char* ps = s.other;
                if (s.ops && s.ops->copy_construct_fn)
                {
                    for (uint32_t e = 0; e < s.count; ++e)
                    {
                        s.ops->copy_construct_fn(pd + e * s.stride, ps + e * s.stride);
                    }
                    return true;
                }
                if (!s.trivially_copyable)
                {
                    detail::abort_with_location("clone: field type ops missing for non-trivial field");
                }
                std::memcpy(pd, ps, static_cast<size_t>(s.count) * s.stride);
                return true;
            });
    }

    // 逐字段 copy_assign (dst 已构造)
    void copy_assign(const void* src, void* dst) const noexcept
    {
        if (!meta_ || !src || !dst)
        {
            return;
        }
        detail::visit_field_slots(dst, src, *meta_,
            [](const detail::field_slot& s, const field_meta&) noexcept {
                char* pd = const_cast<char*>(s.base);
                const char* ps = s.other;
                if (s.ops && s.ops->copy_assign_fn)
                {
                    for (uint32_t e = 0; e < s.count; ++e)
                    {
                        s.ops->copy_assign_fn(pd + e * s.stride, ps + e * s.stride);
                    }
                    return true;
                }
                if (!s.trivially_copyable)
                {
                    detail::abort_with_location("copy_assign: field type ops missing for non-trivial field");
                }
                std::memcpy(pd, ps, static_cast<size_t>(s.count) * s.stride);
                return true;
            });
    }

    // 逐字段析构 (对象销毁前调用)
    void destroy_fields(void* obj) const noexcept
    {
        if (!meta_ || !obj)
        {
            return;
        }
        detail::visit_field_slots(obj, nullptr, *meta_,
            [](const detail::field_slot& s, const field_meta&) noexcept {
                if (!s.ops || !s.ops->destroy_fn)
                {
                    return true;
                }
                char* p = const_cast<char*>(s.base);
                for (uint32_t e = 0; e < s.count; ++e)
                {
                    s.ops->destroy_fn(p + e * s.stride);
                }
                return true;
            });
    }
};

template<typename T>
[[nodiscard]] compare_view get_compare() noexcept
{
    return compare_view(global().get_type(type_id::get_type_id<T>()));
}

[[nodiscard]] inline compare_view get_compare(const query_view& q) noexcept
{
    return compare_view(q.meta());
}

} // namespace reflect
