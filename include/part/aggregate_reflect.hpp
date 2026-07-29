#pragma once

// aggregate_reflect.hpp - 聚合类型编译期字段计数与遍历

#include <type_traits>
#include <cstddef>
#include <utility>
#include "force_inline.hpp"
#include "../config/reflect_config.hpp"

static_assert(REFLECT_MAX_FIELDS == 16 ||
              REFLECT_MAX_FIELDS == 32 ||
              REFLECT_MAX_FIELDS == 64 ||
              REFLECT_MAX_FIELDS == 128 ||
              REFLECT_MAX_FIELDS == 256,
              "REFLECT_MAX_FIELDS must be 16/32/64/128/256");

#include "aggregate_reflect_pp.hpp"

namespace detail_aggregate_reflect {

// 通用转换类型 (SFINAE 探测用)
struct any_type {
    std::size_t ignore;
    template<typename U>
    constexpr operator U() const noexcept;
};

// SFINAE 探测: T 能否用 N 个 any_type 聚合初始化
template<typename T, typename Seq, typename = void>
struct can_construct_n : std::false_type {};

template<typename T, size_t... Is>
struct can_construct_n<T, std::index_sequence<Is...>,
    std::void_t<decltype(T{any_type{Is}...})>> : std::true_type {};

template<typename T, size_t N>
inline constexpr bool can_construct_n_v =
    can_construct_n<T, std::make_index_sequence<N>>::value;

// 模板递归探测字段数
template<typename T, size_t N>
constexpr size_t detect_field_count() noexcept
{
    if constexpr (!std::is_aggregate_v<T>) { return 0; }
    else if constexpr (can_construct_n_v<T, N>) { return N; }
    else if constexpr (N > 1) { return detect_field_count<T, N - 1>(); }
    else { return 0; }
}

// 字段数超过上限检查
template<typename T>
inline constexpr bool exceeds_field_limit_v =
    can_construct_n_v<std::remove_cv_t<T>, REFLECT_MAX_FIELDS + 1>;

template<typename T>
inline constexpr size_t field_count_v =
    detect_field_count<std::remove_cv_t<T>, REFLECT_MAX_FIELDS>();

// === 宏生成器 ===
#define REFLECT_VAR(i, data) REFLECT_CAT(m, i)
#define REFLECT_CALL(i, f) f(REFLECT_CAT(m, i), i);
#define REFLECT_BRANCH(i, data) \
    else if constexpr (N == i) { \
        auto& [REFLECT_ENUM(i, REFLECT_VAR, ~)] = obj; \
        REFLECT_REPEAT(i, REFLECT_CALL, f) \
    }

template<typename T, typename F>
FORCE_INLINE void for_each_member(T& obj, F&& f)
{
    using type = std::remove_cv_t<T>;
    constexpr size_t N = field_count_v<type>;
    static_assert(!exceeds_field_limit_v<type>,
                  "Field count exceeds REFLECT_MAX_FIELDS, "
                  "increase REFLECT_MAX_FIELDS before including this header");
    if constexpr (N == 0) { return; }
    REFLECT_REPEAT1(REFLECT_MAX_FIELDS, REFLECT_BRANCH, ~)
}

#undef REFLECT_VAR
#undef REFLECT_CALL
#undef REFLECT_BRANCH

// 计算成员偏移量
template<typename T, typename M>
FORCE_INLINE size_t offset_of_member(M T::*member) noexcept
{
    return reinterpret_cast<size_t>(&(reinterpret_cast<T*>(0)->*member));
}

} // namespace detail_aggregate_reflect

// === 对外导出 (全局命名空间) ===
template<typename T>
inline constexpr size_t aggregate_field_count_v = detail_aggregate_reflect::field_count_v<T>;

template<typename T, typename F>
FORCE_INLINE void for_each_aggregate_member(T& obj, F&& f)
{
    detail_aggregate_reflect::for_each_member(obj, std::forward<F>(f));
}

template<typename T, typename M>
FORCE_INLINE size_t member_offset(M T::*member) noexcept
{
    return detail_aggregate_reflect::offset_of_member(member);
}
