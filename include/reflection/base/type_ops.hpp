#pragma once

#include <array>
#include <atomic>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
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

// type_ops.hpp - 容器操作表与类型操作表
// 命名空间: reflect

// ============================================================================

// container_ops.hpp - 容器特征探测与类型擦除操作
// 命名空间: reflect


namespace reflect {

// 容器类别
enum class container_category : uint8_t
{
    none = 0,
    sequential = 1,   // vector, dense, array 等
    associative = 2,  // map (暂不实现)
    set_type = 3      // set (暂不实现)
};

// 容器操作函数表 (类型擦除)
struct container_ops_table
{
    container_category category{container_category::none};
    int element_type_id{-1};
    int key_type_id{-1};

    size_t (*size_fn)(const void*) noexcept{nullptr};
    void* (*get_index_fn)(void*, size_t) noexcept{nullptr};
    void* (*at_key_fn)(void*, const void* key) noexcept{nullptr};
    void (*push_back_fn)(void*, const void* element) noexcept{nullptr};
    void (*insert_kv_fn)(void*, const void* key, const void* val) noexcept{nullptr};
    void (*clear_fn)(void*) noexcept{nullptr};
    void (*reserve_fn)(void*, size_t) noexcept{nullptr};
};

namespace detail {

// === Concepts ===

template<typename T>
concept sequential_container = requires(const T& c)
{
    { c.size() } -> std::convertible_to<size_t>;
    { c.begin() };
    { c.end() };
} && requires(T& c)
{
    typename T::value_type;
    { c.emplace_back(std::declval<const typename T::value_type&>()) };
    { c.clear() } -> std::same_as<void>;
};

template<typename T>
concept resizable_container = requires(T& c, size_t n)
{
    c.reserve(n);
};

template<typename T>
concept indexable_container = requires(T& c, size_t i)
{
    { c[i] } -> std::same_as<typename T::value_type&>;
};

// 为 sequential 容器生成操作表的主模板
template<typename T, typename = void>
struct container_traits_impl
{
    static constexpr container_ops_table make_table() noexcept { return {}; }
};

// sequential 容器特化
template<typename T>
    requires sequential_container<T>
struct container_traits_impl<T>
{
    using value_type = typename T::value_type;

    static constexpr container_ops_table make_table() noexcept
    {
        container_ops_table t{};
        t.category = container_category::sequential;
        t.element_type_id = type_id::get_type_id<value_type>();
        t.key_type_id = -1;
        t.size_fn = [](const void* c) noexcept -> size_t
        {
            return static_cast<const T*>(c)->size();
        };
        t.push_back_fn = [](void* c, const void* v) noexcept
        {
            static_cast<T*>(c)->emplace_back(*static_cast<const value_type*>(v));
        };
        t.clear_fn = [](void* c) noexcept
        {
            static_cast<T*>(c)->clear();
        };
        if constexpr (indexable_container<T>)
        {
            t.get_index_fn = [](void* c, size_t i) noexcept -> void*
            {
                T* cont = static_cast<T*>(c);
                return i < cont->size() ? &(*cont)[i] : nullptr;
            };
        }
        if constexpr (resizable_container<T>)
        {
            t.reserve_fn = [](void* c, size_t n) noexcept
            {
                static_cast<T*>(c)->reserve(n);
            };
        }
        return t;
    }
};

} // namespace detail

} // namespace reflect

// ============================================================================
// ============================================================================

// field_ops.hpp - 字段操作表与统一操作表注册器
// 命名空间: reflect


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
        else if constexpr (requires(const T& value) {
            { std::hash<T>{}(value) } -> std::convertible_to<size_t>;
        })
        {
            return static_cast<uint64_t>(std::hash<T>{}(*static_cast<const T*>(obj)));
        }
        else if constexpr (std::has_unique_object_representations_v<T>)
        {
            // 无 padding，字节哈希与相等一致
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
            // 无哈希语义时保持 equal 相同则 hash 相同
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

// ============================================================================
