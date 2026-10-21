#pragma once

// container_ops.hpp - 容器特征探测与类型擦除操作
// 命名空间: reflect

#include <cstdint>
#include <cstddef>
#include <concepts>
#include <type_traits>
#include <array>
#include "part/type/type_id.hpp"

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
    { c.push_back(std::declval<const typename T::value_type&>()) };
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
            static_cast<T*>(c)->push_back(*static_cast<const value_type*>(v));
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
