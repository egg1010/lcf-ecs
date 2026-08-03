#pragma once

// member_offset.hpp - 成员偏移量访问工具

#include <cstddef>
#include <cstdint>
#include <type_traits>
#include "force_inline.hpp"

// 私有成员偏移量描述
struct offset_desc
{
    const char* name;
    size_t offset;
    int type_id;
    uint32_t size;  // 类型大小 (字节), 用于 compare/hash 回退
};

namespace detail_member_offset {

// 通过成员指针计算偏移量 (需访问权限)
template<typename T, typename M>
FORCE_INLINE size_t offset_of(M T::*member) noexcept
{
    return reinterpret_cast<size_t>(&(reinterpret_cast<T*>(0)->*member));
}

// UB 反向构造成员指针, 突破私有访问限制
template<typename T, typename M>
FORCE_INLINE M& ub_access(T& obj, size_t offset) noexcept
{
    M T::*mptr = nullptr;
    *reinterpret_cast<size_t*>(&mptr) = offset;
    return obj.*mptr;
}

// 直接指针运算访问
template<typename M>
FORCE_INLINE M& offset_access(void* obj, size_t offset) noexcept
{
    return *reinterpret_cast<M*>(static_cast<char*>(obj) + offset);
}

template<typename M>
FORCE_INLINE const M& offset_access(const void* obj, size_t offset) noexcept
{
    return *reinterpret_cast<const M*>(static_cast<const char*>(obj) + offset);
}

} // namespace detail_member_offset

// === 对外导出 (全局命名空间) ===
using detail_member_offset::offset_of;
using detail_member_offset::ub_access;
using detail_member_offset::offset_access;
