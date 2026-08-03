// compare.hpp - 字段比较/克隆查询接口 (#8)
// 命名空间: reflect
// 基于 part/type_ops.hpp 的 type_ops_table 实现逐字段比较与克隆
#pragma once

#include "query.hpp"
#include "../part/type_ops.hpp"

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

        size_t n = meta_->field_count.load(std::memory_order_acquire);
        for (size_t i = 0; i < n; ++i)
        {
            const field_meta& fm = meta_->fields[i];
            const char* pa = static_cast<const char*>(a) + fm.offset;
            const char* pb = static_cast<const char*>(b) + fm.offset;

            if (fm.array_rank > 0)
            {
                // 数组字段: 逐元素比较
                const type_ops_table* ops = global_type_ops().get(fm.type_id);
                if (!ops || !ops->equal_fn)
                {
                    return false;
                }
                for (uint32_t e = 0; e < fm.total_elements; ++e)
                {
                    if (!ops->equal_fn(pa + e * fm.element_stride,
                                       pb + e * fm.element_stride))
                        return false;
                }
            }
            else
            {
                const type_ops_table* ops = global_type_ops().get(fm.type_id);
                if (!ops || !ops->equal_fn)
                {
                    // 无 ops 回退到 memcmp
                    if (std::memcmp(pa, pb, fm.element_stride) != 0)
                    {
                        return false;
                    }
                    continue;
                }
                if (!ops->equal_fn(pa, pb))
                {
                    return false;
                }
            }
        }
        return true;
    }

    // 逐字段克隆 (copy_construct, dst 必须是未初始化内存)
    void clone(const void* src, void* dst) const noexcept
    {
        if (!meta_ || !src || !dst)
        {
            return;
        }
        size_t n = meta_->field_count.load(std::memory_order_acquire);
        for (size_t i = 0; i < n; ++i)
        {
            const field_meta& fm = meta_->fields[i];
            const char* ps = static_cast<const char*>(src) + fm.offset;
            char* pd = static_cast<char*>(dst) + fm.offset;

            if (fm.array_rank > 0)
            {
                const type_ops_table* ops = global_type_ops().get(fm.type_id);
                if (!ops || !ops->copy_construct_fn)
                {
                    std::memcpy(pd, ps, fm.total_elements * fm.element_stride);
                    continue;
                }
                for (uint32_t e = 0; e < fm.total_elements; ++e)
                {
                    ops->copy_construct_fn(pd + e * fm.element_stride,
                                           ps + e * fm.element_stride);
                }
            }
            else
            {
                const type_ops_table* ops = global_type_ops().get(fm.type_id);
                if (!ops || !ops->copy_construct_fn)
                {
                    std::memcpy(pd, ps, fm.element_stride);
                    continue;
                }
                ops->copy_construct_fn(pd, ps);
            }
        }
    }

    // 逐字段 copy_assign (dst 已构造)
    void copy_assign(const void* src, void* dst) const noexcept
    {
        if (!meta_ || !src || !dst)
        {
            return;
        }
        size_t n = meta_->field_count.load(std::memory_order_acquire);
        for (size_t i = 0; i < n; ++i)
        {
            const field_meta& fm = meta_->fields[i];
            const char* ps = static_cast<const char*>(src) + fm.offset;
            char* pd = static_cast<char*>(dst) + fm.offset;

            if (fm.array_rank > 0)
            {
                const type_ops_table* ops = global_type_ops().get(fm.type_id);
                if (!ops || !ops->copy_assign_fn)
                {
                    std::memcpy(pd, ps, fm.total_elements * fm.element_stride);
                    continue;
                }
                for (uint32_t e = 0; e < fm.total_elements; ++e)
                {
                    ops->copy_assign_fn(pd + e * fm.element_stride,
                                        ps + e * fm.element_stride);
                }
            }
            else
            {
                const type_ops_table* ops = global_type_ops().get(fm.type_id);
                if (!ops || !ops->copy_assign_fn)
                {
                    std::memcpy(pd, ps, fm.element_stride);
                    continue;
                }
                ops->copy_assign_fn(pd, ps);
            }
        }
    }

    // 逐字段析构 (对象销毁前调用)
    void destroy_fields(void* obj) const noexcept
    {
        if (!meta_ || !obj)
        {
            return;
        }
        size_t n = meta_->field_count.load(std::memory_order_acquire);
        for (size_t i = 0; i < n; ++i)
        {
            const field_meta& fm = meta_->fields[i];
            char* p = static_cast<char*>(obj) + fm.offset;

            if (fm.array_rank > 0)
            {
                const type_ops_table* ops = global_type_ops().get(fm.type_id);
                if (!ops || !ops->destroy_fn)
                {
                    continue;
                }
                for (uint32_t e = 0; e < fm.total_elements; ++e)
                {
                    ops->destroy_fn(p + e * fm.element_stride);
                }
            }
            else
            {
                const type_ops_table* ops = global_type_ops().get(fm.type_id);
                if (!ops || !ops->destroy_fn)
                {
                    continue;
                }
                ops->destroy_fn(p);
            }
        }
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
