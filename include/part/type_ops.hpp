// type_ops.hpp - 类型操作函数表 (equal/copy/destroy/hash)
// 无命名空间, 通用化, 不依赖反射概念
// 复用 type_id 体系, 为每个类型生成类型擦除的操作函数
#pragma once

#include <cstdint>
#include <cstring>
#include <type_traits>
#include <new>
#include <array>
#include "type_id.hpp"
#include "fnv1a.hpp"

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
        if constexpr (std::is_trivially_copyable_v<T>)
        {
            // trivially copyable 类型用字节级 FNV-1a
            // 注意: float/double 不满足 has_unique_object_representations (+0/-0 同值不同表示)
            // 但字节级哈希仍可用于区分不同值
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

// 全局操作表注册器
class type_ops_registry
{
public:
    std::array<type_ops_table, MAX_TYPE_ID> entries_{};

    type_ops_registry() noexcept
    {
        // 内建类型预注册
        register_builtin<int8_t>();
        register_builtin<int16_t>();
        register_builtin<int32_t>();
        register_builtin<int64_t>();
        register_builtin<uint8_t>();
        register_builtin<uint16_t>();
        register_builtin<uint32_t>();
        register_builtin<uint64_t>();
        register_builtin<float>();
        register_builtin<double>();
        register_builtin<bool>();
    }

    template<typename T>
    void register_type_ops() noexcept
    {
        int tid = type_id::get_type_id<T>();
        if (tid >= 0 && tid < static_cast<int>(MAX_TYPE_ID))
        {
            entries_[tid] = type_ops<T>::make_table();
        }
    }

    [[nodiscard]] const type_ops_table* get(int tid) const noexcept
    {
        if (tid < 0 || tid >= static_cast<int>(MAX_TYPE_ID)) return nullptr;
        const auto& ops = entries_[tid];
        return ops.equal_fn ? &ops : nullptr;
    }

private:
    template<typename T>
    void register_builtin() noexcept
    {
        register_type_ops<T>();
    }
};

// 全局实例
inline type_ops_registry& global_type_ops() noexcept
{
    static type_ops_registry inst;
    return inst;
}
