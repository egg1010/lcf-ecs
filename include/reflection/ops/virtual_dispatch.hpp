// virtual_dispatch.hpp - 动态派发查询接口 (#6)
// 命名空间: reflect
// 基于 method_meta::vtable_offset 实现虚函数调用
// 注意: vtable 布局依赖 ABI (Itanium/MSVC), 单继承下指针无调整
#pragma once

#include "reflection/core/query.hpp"
#include "part/type/type_id.hpp"

namespace reflect {

class virtual_dispatch_view
{
    const type_meta* meta_{nullptr};

public:
    virtual_dispatch_view() noexcept = default;
    explicit virtual_dispatch_view(const type_meta* m) noexcept : meta_(m) {}

    // 检查方法是否为虚函数
    [[nodiscard]] bool is_virtual(const char* method_name) const noexcept
    {
        if (!meta_)
        {
            return false;
        }
        size_t n = meta_->method_count.load(std::memory_order_acquire);
        for (size_t i = 0; i < n; ++i)
        {
            const method_meta& mm = meta_->methods[i];
            if (std::strcmp(mm.name, method_name) == 0)
            {
                return mm.vtable_offset >= 0;
            }
        }
        return false;
    }

    // 获取虚函数 vtable 偏移 (非虚返回 -1)
    [[nodiscard]] int vtable_offset(const char* method_name) const noexcept
    {
        if (!meta_)
        {
            return -1;
        }
        size_t n = meta_->method_count.load(std::memory_order_acquire);
        for (size_t i = 0; i < n; ++i)
        {
            const method_meta& mm = meta_->methods[i];
            if (std::strcmp(mm.name, method_name) == 0)
            {
                return mm.vtable_offset;
            }
        }
        return -1;
    }

    // 通过虚函数派发调用 (成员函数指针已含 vtable 信息, 调用点自动派发)
    // 虚函数需在 register_method 时给出 vtable 偏移 (>=0) 以启用 is_virtual 查询
    template<typename R = void, typename... Args>
    R invoke_virtual(void* obj, const char* name, Args&&... args) const noexcept
    {
        // 复用 query 的单份重载解析与结果缓冲
        const method_meta* m = detail::resolve_call<R>(meta_, name, args...);
        if (m == nullptr)
        {
            detail::abort_with_location("invoke_virtual: method not found or return type mismatch");
        }
        return detail::call_method<R>(m, obj, std::forward<Args>(args)...);
    }
};

template<typename T>
[[nodiscard]] virtual_dispatch_view get_virtual_dispatch() noexcept
{
    return virtual_dispatch_view(global().get_type(type_id::get_type_id<T>()));
}

[[nodiscard]] inline virtual_dispatch_view get_virtual_dispatch(const query_view& q) noexcept
{
    return virtual_dispatch_view(q.meta());
}

} // namespace reflect
