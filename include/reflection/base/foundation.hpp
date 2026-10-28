#pragma once

#include <array>
#include <atomic>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <memory>
#include <new>
#include <optional>
#include <source_location>
#include <string_view>
#include <type_traits>
#include <utility>

#include "config/reflect_config.hpp"
#include "part/dense.hpp"
#include "part/fnv1a.hpp"
#include "part/force_inline.hpp"
#include "part/operating_message.hpp"
#include "part/type/type_id.hpp"
#include "part/type/void_any.hpp"

// foundation.hpp - 类型名 / 错误码 / 名字索引 / 偏移 / 授权
// 命名空间: reflect

// ============================================================================

// type_name.hpp - 编译期类型名提取
// 命名空间: reflect::detail
// 标识符名字只能由调用方给出, 此处仅解决类型名


namespace reflect {
namespace detail {

template<typename T>
constexpr std::string_view raw_type_name() noexcept
{
#if defined(_MSC_VER) && !defined(__clang__)
    return __FUNCSIG__;
#else
    return __PRETTY_FUNCTION__;
#endif
}

constexpr std::string_view strip_elaborated(std::string_view s) noexcept
{
    constexpr std::string_view tags[4] = {"struct ", "class ", "enum ", "union "};
    for (size_t i = 0; i < 4; ++i)
    {
        const std::string_view tag = tags[i];
        if (s.size() > tag.size() && s.substr(0, tag.size()) == tag)
        {
            return s.substr(tag.size());
        }
    }
    return s;
}

template<typename T>
constexpr std::string_view type_name_of() noexcept
{
    constexpr std::string_view raw = raw_type_name<T>();
#if defined(_MSC_VER) && !defined(__clang__)
    constexpr std::string_view key = "raw_type_name<";
#else
    constexpr std::string_view key = "T = ";
#endif
    constexpr size_t begin = raw.find(key);
    if (begin == std::string_view::npos)
    {
        return {};
    }
    constexpr size_t from = begin + key.size();
#if defined(_MSC_VER) && !defined(__clang__)
    size_t end = raw.rfind(">(");
    if (end == std::string_view::npos)
    {
        end = raw.rfind('>');
    }
#else
    size_t end = raw.find_first_of(";]", from);
#endif
    if (end == std::string_view::npos || end <= from)
    {
        return {};
    }
    return strip_elaborated(raw.substr(from, end - from));
}

// 以空字符结尾的静态副本, 供接受 const char* 的注册接口使用
template<typename T>
[[nodiscard]] const char* type_name_cstr() noexcept
{
    static const auto buffer = []() constexpr
    {
        constexpr size_t cap = 256;
        std::array<char, cap> out{};
        constexpr std::string_view n = type_name_of<T>();
        constexpr size_t limit = n.size() < (cap - 1) ? n.size() : (cap - 1);
        for (size_t i = 0; i < limit; ++i)
        {
            out[i] = n[i];
        }
        return out;
    }();
    return buffer.data();
}

} // namespace detail
} // namespace reflect

// ============================================================================
// ============================================================================

// error.hpp - 反射模块错误码
// 命名全局, 与 part/operating_message.hpp 的 om_err_* 同风格


inline constexpr msg::code_tag om_err_reflect_type_not_found =
    msg::code_tag{om_error_code("reflect_type_not_found")};

inline constexpr msg::code_tag om_err_reflect_field_not_found =
    msg::code_tag{om_error_code("reflect_field_not_found")};

inline constexpr msg::code_tag om_err_reflect_method_not_found =
    msg::code_tag{om_error_code("reflect_method_not_found")};

inline constexpr msg::code_tag om_err_reflect_enum_not_found =
    msg::code_tag{om_error_code("reflect_enum_not_found")};

inline constexpr msg::code_tag om_err_reflect_duplicate =
    msg::code_tag{om_error_code("reflect_duplicate")};

inline constexpr msg::code_tag om_err_reflect_frozen =
    msg::code_tag{om_error_code("reflect_frozen")};

inline constexpr msg::code_tag om_err_reflect_busy =
    msg::code_tag{om_error_code("reflect_busy")};

inline constexpr msg::code_tag om_err_reflect_limit =
    msg::code_tag{om_error_code("reflect_limit")};

inline constexpr msg::code_tag om_err_reflect_invalid_name =
    msg::code_tag{om_error_code("reflect_invalid_name")};

namespace reflect {
namespace detail {

// 失败对象: 只写错误码与级别; 成功返回默认构造
inline operating_message failure(msg::code_tag code) noexcept
{
    operating_message message;
    (void)message.write(false, code, msg::warn, msg::here());
    return message;
}

} // namespace detail
} // namespace reflect

// ============================================================================
// ============================================================================

// member_offset.hpp - 成员偏移量访问工具
// 命名空间: reflect


namespace reflect {

// 私有成员偏移量描述
struct offset_desc
{
    const char* name;
    size_t offset;
    int type_id;
    uint32_t size;
    bool trivially_copyable;
};

namespace detail {

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
    size_t raw = offset;
    std::memcpy(&mptr, &raw, sizeof(mptr));
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

} // namespace detail

} // namespace reflect

// ============================================================================
// ============================================================================

// name_index.hpp - 名字到类型 id 的开放寻址索引
// 命名空间: reflect::detail
// 名字指针由调用方持有, 索引不拷贝字符串


namespace reflect {
namespace detail {

struct name_entry
{
    uint64_t hash{0};
    const char* name{nullptr};
    uint32_t length{0};
    int32_t type_id{-1};
};

class name_index
{
public:
    name_index() noexcept = default;
    name_index(const name_index&) = delete;
    name_index& operator=(const name_index&) = delete;
    name_index(name_index&&) noexcept = default;
    name_index& operator=(name_index&&) noexcept = default;

    // 绑定名字; 同名同 id 幂等返回 true, 同名异 id 返回 false
    bool bind(std::string_view name, int type_id) noexcept
    {
        if (name.empty() || type_id <= 0) [[unlikely]]
        {
            return false;
        }
        if (buckets_.size() == 0)
        {
            if (!rehash(256))
            {
                return false;
            }
        }
        if ((size_ + tombstones_ + 1) * 10 >= buckets_.size() * 7)
        {
            if (!rehash(buckets_.size() * 2))
            {
                return false;
            }
        }

        const uint64_t h = fnv1a_runtime(name.data(), name.size());
        const uint32_t len = static_cast<uint32_t>(name.size());
        size_t slot = static_cast<size_t>(h) & (buckets_.size() - 1);
        size_t tomb = buckets_.size();
        bool empty_found = false;
        for (size_t probe = 0; probe < buckets_.size(); ++probe)
        {
            const name_entry& e = buckets_[slot];
            if (e.length == 0)
            {
                empty_found = true;
                break;
            }
            if (e.type_id < 0)
            {
                if (tomb == buckets_.size())
                {
                    tomb = slot;
                }
            }
            else if (e.hash == h && e.length == len
                     && std::memcmp(e.name, name.data(), len) == 0)
            {
                return e.type_id == type_id;
            }
            slot = (slot + 1) & (buckets_.size() - 1);
        }
        if (!empty_found && tomb == buckets_.size())
        {
            return false;
        }

        const size_t target = (tomb != buckets_.size()) ? tomb : slot;
        name_entry& dst = buckets_[target];
        if (dst.length != 0 && dst.type_id < 0)
        {
            --tombstones_;
        }
        dst.hash = h;
        dst.name = name.data();
        dst.length = len;
        dst.type_id = type_id;
        ++size_;
        return true;
    }

    // 改名; 新名被占用返回 false, 失败时保留原名
    bool rebind(std::string_view old_name, std::string_view new_name, int type_id) noexcept
    {
        if (old_name.empty() || new_name.empty() || type_id <= 0)
        {
            return false;
        }
        const int occupied = resolve(new_name);
        if (occupied >= 0 && occupied != type_id)
        {
            return false;
        }
        if (!unbind(old_name))
        {
            return false;
        }
        if (!bind(new_name, type_id))
        {
            (void)bind(old_name, type_id);
            return false;
        }
        return true;
    }

    // 解绑; 槽位留墓碑以维持探测链
    bool unbind(std::string_view name) noexcept
    {
        name_entry* e = find_entry_mut(name);
        if (e == nullptr || e->type_id < 0)
        {
            return false;
        }
        e->type_id = -1;
        --size_;
        ++tombstones_;
        return true;
    }

    // 查询; 未绑定返回 -1
    [[nodiscard]] int resolve(std::string_view name) const noexcept
    {
        const name_entry* e = find_entry(name);
        return (e == nullptr || e->type_id < 0) ? -1 : e->type_id;
    }

    [[nodiscard]] size_t size() const noexcept { return size_; }

    [[nodiscard]] size_t capacity() const noexcept { return buckets_.size(); }

    [[nodiscard]] bool empty() const noexcept { return size_ == 0; }

    void clear() noexcept
    {
        buckets_.clear();
        size_ = 0;
        tombstones_ = 0;
    }

private:
    [[nodiscard]] const name_entry* find_entry(std::string_view name) const noexcept
    {
        if (name.empty() || buckets_.size() == 0)
        {
            return nullptr;
        }
        const uint64_t h = fnv1a_runtime(name.data(), name.size());
        const uint32_t len = static_cast<uint32_t>(name.size());
        size_t slot = static_cast<size_t>(h) & (buckets_.size() - 1);
        for (size_t probe = 0; probe < buckets_.size(); ++probe)
        {
            const name_entry& e = buckets_[slot];
            if (e.length == 0)
            {
                return nullptr;
            }
            if (e.type_id >= 0 && e.hash == h && e.length == len
                && std::memcmp(e.name, name.data(), len) == 0)
            {
                return &e;
            }
            slot = (slot + 1) & (buckets_.size() - 1);
        }
        return nullptr;
    }

    [[nodiscard]] name_entry* find_entry_mut(std::string_view name) noexcept
    {
        return const_cast<name_entry*>(find_entry(name));
    }

    static void insert_raw(dense<name_entry>& table, const name_entry& e) noexcept
    {
        size_t slot = static_cast<size_t>(e.hash) & (table.size() - 1);
        while (table[slot].length != 0)
        {
            slot = (slot + 1) & (table.size() - 1);
        }
        table[slot] = e;
    }

    // 重建表; 容量必须为 2 的幂且大于现有条目数
    bool rehash(size_t capacity) noexcept
    {
        if (capacity < 16 || (capacity & (capacity - 1)) != 0)
        {
            return false;
        }
        dense<name_entry> next;
        next.increase_capacity(capacity);
        for (size_t i = 0; i < capacity; ++i)
        {
            next.emplace_back(name_entry{});
        }
        for (size_t i = 0; i < buckets_.size(); ++i)
        {
            const name_entry& e = buckets_[i];
            if (e.length == 0 || e.type_id < 0)
            {
                continue;
            }
            insert_raw(next, e);
        }
        buckets_ = std::move(next);
        tombstones_ = 0;
        return true;
    }

    dense<name_entry> buckets_;
    size_t size_{0};
    size_t tombstones_{0};
};

} // namespace detail
} // namespace reflect

// ============================================================================
// ============================================================================

// access.hpp - 私有成员授权入口
// 命名空间: reflect
// 类内写 template<typename> friend struct ::reflect::reflect_access;
// 类外特化 reflect_access<T>, 提供 fields() / methods() / static_methods()


namespace reflect {

template<typename Cls>
struct reflect_access;

namespace detail {

template<typename T>
concept has_access_fields = requires { reflect_access<T>::fields(); };

template<typename T>
concept has_access_methods = requires { reflect_access<T>::methods(); };

template<typename T>
concept has_access_static_methods = requires { reflect_access<T>::static_methods(); };

} // namespace detail
} // namespace reflect

// ============================================================================
