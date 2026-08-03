// hash.hpp - 字段哈希查询接口 (#9)
// 命名空间: reflect
// 基于 part/type_ops.hpp 的 hash_fn 实现逐字段 FNV-1a 哈希组合
#pragma once

#include "query.hpp"
#include "../part/type_ops.hpp"
#include "../part/fnv1a.hpp"

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
        size_t n = meta_->field_count.load(std::memory_order_acquire);
        for (size_t i = 0; i < n; ++i)
        {
            const field_meta& fm = meta_->fields[i];
            const char* p = static_cast<const char*>(obj) + fm.offset;

            if (fm.array_rank > 0)
            {
                // 数组字段: 逐元素哈希再组合
                const type_ops_table* ops = global_type_ops().get(fm.type_id);
                if (ops && ops->hash_fn)
                {
                    for (uint32_t e = 0; e < fm.total_elements; ++e)
                    {
                        uint64_t fh = ops->hash_fn(p + e * fm.element_stride);
                        h ^= fh;
                        h *= FNV_PRIME_64;
                    }
                }
                else
                {
                    // 无 hash_fn: 字节级 FNV-1a
                    for (uint32_t e = 0; e < fm.total_elements; ++e)
                    {
                        const uint8_t* bytes = reinterpret_cast<const uint8_t*>(p + e * fm.element_stride);
                        for (size_t b = 0; b < fm.element_stride; ++b)
                        {
                            h ^= bytes[b];
                            h *= FNV_PRIME_64;
                        }
                    }
                }
            }
            else
            {
                const type_ops_table* ops = global_type_ops().get(fm.type_id);
                if (ops && ops->hash_fn)
                {
                    uint64_t fh = ops->hash_fn(p);
                    h ^= fh;
                    h *= FNV_PRIME_64;
                }
                else
                {
                    // 无 hash_fn: 字节级 FNV-1a
                    const uint8_t* bytes = reinterpret_cast<const uint8_t*>(p);
                    for (size_t b = 0; b < fm.element_stride; ++b)
                    {
                        h ^= bytes[b];
                        h *= FNV_PRIME_64;
                    }
                }
            }
        }
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
