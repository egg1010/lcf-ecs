// virtual_dispatch.hpp - 动态派发查询接口 (#6)
// 命名空间: reflect
// 基于 method_meta::vtable_offset 实现虚函数调用
// 注意: vtable 布局依赖 ABI (Itanium/MSVC), 单继承下指针无调整
#pragma once

#include "query.hpp"
#include "../part/type_id.hpp"

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

    // 通过 vtable 调用虚函数 (Itanium ABI: 单继承下 vptr 在对象首 8 字节)
    // 注意: 此接口为高级用途, 需要用户确保 ABI 正确性
    // 当前实现: 委托给普通 invoke (因为 C++ 成员函数指针已包含 vtable 信息)
    template<typename R = void, typename... Args>
    R invoke_virtual(void* obj, const char* name, Args&&... args) const noexcept
    {
        // 对于通过 register_method 注册的虚函数, invoker 已正确处理 vtable
        // 此处直接调用普通 invoke 路径, 复用重载解析
        if (!meta_)
        {
            detail::abort_with_location("invoke_virtual: query_view invalid");
        }
        // 首元素 0 占位: 零参数时避免零长度数组 (MSVC 拒绝, GCC 扩展容忍)
        int given_ids[] = { 0, type_id::get_type_id<std::decay_t<Args>>()... };
        const method_meta* m = nullptr;
        size_t n = meta_->method_count.load(std::memory_order_acquire);
        for (size_t i = 0; i < n; ++i)
        {
            const method_meta& mm = meta_->methods[i];
            if (std::strcmp(mm.name, name) != 0)
            {
                continue;
            }
            if (mm.arg_count != sizeof...(Args))
            {
                continue;
            }
            if (mm.arg_type_ids)
            {
                bool exact = true;
                for (uint8_t j = 0; j < mm.arg_count; ++j)
                {
                    if (mm.arg_type_ids[j] != given_ids[j + 1])
                    {
                        exact = false;
                        break;
                    }
                }
                if (exact)
                {
                    m = &mm;
                    break;
                }
            }
            if (!m)
            {
                m = &mm;
            }
        }
        if (!m)
        {
            detail::abort_with_location("invoke_virtual: method not found");
        }

        std::array<const void*, sizeof...(Args)> arg_ptrs = { static_cast<const void*>(&args)... };
        if constexpr (std::is_void_v<R>)
        {
            m->invoker(obj, arg_ptrs.data(), nullptr);
        }
        else
        {
            alignas(alignof(R)) std::array<char, sizeof(R)> result_buf{};
            m->invoker(obj, arg_ptrs.data(), result_buf.data());
            R* result_ptr = reinterpret_cast<R*>(result_buf.data());
            R ret = std::move(*result_ptr);
            result_ptr->~R();
            return ret;
        }
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
