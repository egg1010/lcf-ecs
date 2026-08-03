// construct.hpp - 对象构造/销毁查询接口 (#1)
// 命名空间: reflect
// 通过 type_meta 的 default_construct_/destruct_ 函数指针实现反射构造
#pragma once

#include "query.hpp"

namespace reflect {

class construct_view
{
    const type_meta* meta_{nullptr};

public:
    construct_view() noexcept = default;
    explicit construct_view(const type_meta* m) noexcept : meta_(m) {}

    [[nodiscard]] bool has_default_construct() const noexcept
    {
        return meta_ && meta_->has_default_construct;
    }

    // 堆分配构造 (失败返回 nullptr)
    [[nodiscard]] void* create() const noexcept
    {
        if (!meta_ || !meta_->default_construct_)
        {
            return nullptr;
        }
        return meta_->default_construct_(nullptr);
    }

    // 就地构造 (buf 必须足够大且对齐)
    [[nodiscard]] void* create_inplace(void* buf) const noexcept
    {
        if (!meta_ || !meta_->default_construct_)
        {
            return nullptr;
        }
        return meta_->default_construct_(buf);
    }

    // 析构 (不释放内存, 用于就地对象的析构)
    void destroy(void* obj) const noexcept
    {
        if (!meta_ || !meta_->destruct_)
        {
            return;
        }
        meta_->destruct_(obj);
    }

    // 堆分配 + 析构 + 释放 (便利接口)
    void destroy_heap(void* obj) const noexcept
    {
        if (!obj)
        {
            return;
        }
        if (meta_ && meta_->destruct_) meta_->destruct_(obj);
        ::operator delete(obj);
    }
};

// 便捷工厂函数
template<typename T>
[[nodiscard]] construct_view get_construct() noexcept
{
    return construct_view(global().get_type(type_id::get_type_id<T>()));
}

[[nodiscard]] inline construct_view get_construct(const type_meta* m) noexcept
{
    return construct_view(m);
}

[[nodiscard]] inline construct_view get_construct(const query_view& q) noexcept
{
    return construct_view(q.meta());
}

} // namespace reflect
