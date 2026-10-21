#pragma once
#include <type_traits>

namespace ecs
{

template <typename... Types>
struct without_t {};

template <typename... Types>
struct with_t {};

template <typename... Types>
inline constexpr without_t<Types...> without{};

template <typename... Types>
inline constexpr with_t<Types...> with{};

template <typename... Types>
using exclude = without_t<Types...>;

template <typename... Types>
using get = with_t<Types...>;

template <typename... Types>
struct owned_t {};

template <typename... Types>
inline constexpr owned_t<Types...> owned{};

template <typename... Types>
struct ordered {};

template <typename T>
concept Component = std::is_copy_constructible_v<std::decay_t<T>>
                  || std::is_move_constructible_v<std::decay_t<T>>;

}
