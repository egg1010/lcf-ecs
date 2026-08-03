// container.hpp - 容器反射查询接口 (#5)
// 命名空间: reflect
// 基于 part/container_traits.hpp 的 container_ops_table 实现容器操作
#pragma once

#include "query.hpp"
#include "../part/container_traits.hpp"

namespace reflect {

class container_view
{
    void* cont_{nullptr};
    const container_ops_table* ops_{nullptr};

public:
    container_view() noexcept = default;
    container_view(void* cont, const container_ops_table* ops) noexcept
        : cont_(cont), ops_(ops) {}

    [[nodiscard]] bool valid() const noexcept { return cont_ && ops_; }
    [[nodiscard]] bool is_container() const noexcept { return ops_ != nullptr; }

    [[nodiscard]] container_category category() const noexcept
    {
        return ops_ ? ops_->category : container_category::none;
    }

    [[nodiscard]] int element_type_id() const noexcept
    {
        return ops_ ? ops_->element_type_id : -1;
    }

    [[nodiscard]] size_t size() const noexcept
    {
        if (!valid() || !ops_->size_fn)
        {
            return 0;
        }
        return ops_->size_fn(cont_);
    }

    [[nodiscard]] void* at(size_t index) const noexcept
    {
        if (!valid() || !ops_->at_index_fn)
        {
            return nullptr;
        }
        return ops_->at_index_fn(cont_, index);
    }

    template<typename T>
    [[nodiscard]] T* at_as(size_t index) const noexcept
    {
        void* p = at(index);
        return p ? static_cast<T*>(p) : nullptr;
    }

    void push_back(const void* element) const noexcept
    {
        if (!valid() || !ops_->push_back_fn)
        {
            return;
        }
        ops_->push_back_fn(cont_, element);
    }

    template<typename T>
    void push_back(const T& value) const noexcept
    {
        push_back(static_cast<const void*>(&value));
    }

    void clear() const noexcept
    {
        if (!valid() || !ops_->clear_fn)
        {
            return;
        }
        ops_->clear_fn(cont_);
    }

    void reserve(size_t n) const noexcept
    {
        if (!valid() || !ops_->reserve_fn)
        {
            return;
        }
        ops_->reserve_fn(cont_, n);
    }

    // 遍历元素 (回调签名: void(void* element, size_t index))
    template<typename F>
    void for_each(F&& f) const noexcept
    {
        if (!valid() || !ops_->at_index_fn)
        {
            return;
        }
        size_t n = size();
        for (size_t i = 0; i < n; ++i)
        {
            void* elem = ops_->at_index_fn(cont_, i);
            if (elem) f(elem, i);
        }
    }
};

// 从对象指针构造容器视图
[[nodiscard]] inline container_view as_container(void* obj, int type_id) noexcept
{
    const container_ops_table* ops = global_container_ops().get(type_id);
    return container_view(obj, ops);
}

// 从 query_view 构造容器视图 (用于字段类型为容器)
[[nodiscard]] inline container_view as_container(const query_view& q, void* obj) noexcept
{
    // 此处 q 是字段类型的 query_view, obj 是字段值指针
    // 但容器的 ops 存在 type_meta::container_ops 中
    const type_meta* m = q.meta();
    if (!m)
    {
        return container_view{};
    }
    return container_view(obj, m->container_ops);
}

} // namespace reflect
