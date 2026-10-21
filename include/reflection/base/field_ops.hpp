#pragma once

// field_ops.hpp - 字段操作表与统一操作表注册器
// 命名空间: reflect

#include <cstdint>
#include <cstring>
#include <type_traits>
#include <new>
#include <array>
#include "../part/type/type_id.hpp"
#include "../part/fnv1a.hpp"
#include "../config/reflect_config.hpp"
#include "container_ops.hpp"

namespace reflect {
namespace detail {

// 类型操作函数表
struct type_ops_table
{
    bool (*equal_fn)(const void*, const void*) noexcept{nullptr};
    void (*copy_construct_fn)(void*, const void*) noexcept{nullptr};
    void (*copy_assign_fn)(void*, const void*) noexcept{nullptr};
    void (*destroy_fn)(void*) noexcept{nullptr};
    uint64_t (*hash_fn)(const void*) noexcept{nullptr};
};

// 为类型 T 生成操作函数
template<typename T>
struct type_ops
{
    static bool equal(const void* a, const void* b) noexcept
    {
        if constexpr (std::has_unique_object_representations_v<T> && std::is_trivially_copyable_v<T>)
        {
            return std::memcmp(a, b, sizeof(T)) == 0;
        }
        else if constexpr (std::equality_comparable<T>)
        {
            return *static_cast<const T*>(a) == *static_cast<const T*>(b);
        }
        else
        {
            (void)a; (void)b;
            return std::memcmp(a, b, sizeof(T)) == 0;
        }
    }

    static void copy_construct(void* dst, const void* src) noexcept
    {
        if constexpr (std::is_trivially_copyable_v<T>)
        {
            std::memcpy(dst, src, sizeof(T));
        }
        else
        {
            new(dst) T(*static_cast<const T*>(src));
        }
    }

    static void copy_assign(void* dst, const void* src) noexcept
    {
        if constexpr (std::is_trivially_copyable_v<T>)
        {
            std::memcpy(dst, src, sizeof(T));
        }
        else if constexpr (std::is_copy_assignable_v<T>)
        {
            *static_cast<T*>(dst) = *static_cast<const T*>(src);
        }
        else
        {
            static_cast<T*>(dst)->~T();
            new(dst) T(*static_cast<const T*>(src));
        }
    }

    static void destroy(void* obj) noexcept
    {
        if constexpr (!std::is_trivially_destructible_v<T>)
        {
            static_cast<T*>(obj)->~T();
        }
    }

    static uint64_t hash(const void* obj) noexcept
    {
        if constexpr (std::is_same_v<T, float> || std::is_same_v<T, double>)
        {
            // float/double 归零 +0/-0, 再按字节 FNV-1a
            T v = *static_cast<const T*>(obj);
            if (v == 0)
            {
                v = 0;
            }
            const uint8_t* p = reinterpret_cast<const uint8_t*>(&v);
            uint64_t h = FNV_OFFSET_BASIS_64;
            for (size_t i = 0; i < sizeof(T); ++i)
            {
                h ^= p[i];
                h *= FNV_PRIME_64;
            }
            return h;
        }
        else if constexpr (std::is_trivially_copyable_v<T>)
        {
            // trivially copyable 类型用字节级 FNV-1a
            const uint8_t* p = static_cast<const uint8_t*>(obj);
            uint64_t h = FNV_OFFSET_BASIS_64;
            for (size_t i = 0; i < sizeof(T); ++i)
            {
                h ^= p[i];
                h *= FNV_PRIME_64;
            }
            return h;
        }
        else
        {
            // 非 trivial 类型回退到 0, 用户可特化
            (void)obj;
            return 0;
        }
    }

    static constexpr type_ops_table make_table() noexcept
    {
        return type_ops_table{
            &equal, &copy_construct, &copy_assign, &destroy, &hash
        };
    }
};

// 统一操作表注册器: 字段操作表与容器操作表按同一 type_id 索引共存
class ops_registry
{
public:
    std::array<type_ops_table, MAX_TYPE_ID> field_entries_{};
    std::array<container_ops_table, MAX_TYPE_ID> container_entries_{};

    ops_registry() noexcept
    {
        // 内建类型预注册
        register_type_ops<int8_t>();
        register_type_ops<int16_t>();
        register_type_ops<int32_t>();
        register_type_ops<int64_t>();
        register_type_ops<uint8_t>();
        register_type_ops<uint16_t>();
        register_type_ops<uint32_t>();
        register_type_ops<uint64_t>();
        register_type_ops<float>();
        register_type_ops<double>();
        register_type_ops<bool>();
    }

    template<typename T>
    void register_type_ops() noexcept
    {
        int tid = type_id::get_type_id<T>();
        if (tid >= 0 && tid < static_cast<int>(MAX_TYPE_ID))
        {
            field_entries_[tid] = type_ops<T>::make_table();
        }
    }

    template<typename T>
        requires sequential_container<T>
    void register_sequential() noexcept
    {
        int tid = type_id::get_type_id<T>();
        if (tid >= 0 && tid < static_cast<int>(MAX_TYPE_ID))
        {
            container_entries_[tid] = container_traits_impl<T>::make_table();
        }
    }

    [[nodiscard]] const type_ops_table* get(int tid) const noexcept
    {
        if (tid < 0 || tid >= static_cast<int>(MAX_TYPE_ID)) return nullptr;
        const auto& ops = field_entries_[tid];
        return ops.equal_fn ? &ops : nullptr;
    }

    [[nodiscard]] const container_ops_table* get_container(int tid) const noexcept
    {
        if (tid < 0 || tid >= static_cast<int>(MAX_TYPE_ID)) return nullptr;
        return container_entries_[tid].category != container_category::none
            ? &container_entries_[tid] : nullptr;
    }
};

// 全局实例
inline ops_registry& global_ops() noexcept
{
    static ops_registry inst;
    return inst;
}

} // namespace detail
} // namespace reflect
