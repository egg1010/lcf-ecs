// hash.hpp - 字段哈希查询接口 (#9)
// 命名空间: reflect
// 复用 meta.hpp 的单份字段遍历骨架, 逐字段/逐元素组合 FNV-1a 哈希
#pragma once

#include "reflection/core/query.hpp"
#include "reflection/base/field_ops.hpp"
#include "part/fnv1a.hpp"

namespace reflect {

class hash_view
{
    const type_meta* meta_{nullptr};

public:
    hash_view() noexcept = default;
    explicit hash_view(const type_meta* m) noexcept : meta_(m) {}

    // 逐字段哈希并组合 (FNV-1a 组合)
    [[nodiscard]] uint64_t hash(const void* obj) const noexcept
    {
        if (!meta_ || !obj)
        {
            return 0;
        }
        uint64_t h = FNV_OFFSET_BASIS_64;
        detail::visit_field_slots(obj, nullptr, *meta_,
            [&h](const detail::field_slot& s, const field_meta&) noexcept {
                for (uint32_t e = 0; e < s.count; ++e)
                {
                    const char* p = s.base + e * s.stride;
                    if (s.ops && s.ops->hash_fn)
                    {
                        h = fnv1a_mix(h, s.ops->hash_fn(p));
                    }
                    else
                    {
                        h = fnv1a_mix(h, p, s.stride);
                    }
                }
                return true;
            });
        return h;
    }
};

template<typename T>
[[nodiscard]] hash_view get_hash() noexcept
{
    return hash_view(global().get_type(type_id::get_type_id<T>()));
}

[[nodiscard]] inline hash_view get_hash(const query_view& q) noexcept
{
    return hash_view(q.meta());
}

} // namespace reflect
