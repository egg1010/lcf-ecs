#pragma once

// type_erasure.hpp - 类型擦除的方法调用器

#include <cstddef>
#include <utility>
#include <type_traits>
#include "force_inline.hpp"
#include "type_id.hpp"
#include "dense.hpp"

namespace detail_type_erasure {

// 方法签名 traits (提取 class_type / return_type / arg_count / is_const / is_static)
template<typename MFnType>
struct mfn_traits;

template<typename C, typename R, typename... Args>
struct mfn_traits<R(C::*)(Args...)> {
    using class_type = C;
    using return_type = R;
    static constexpr size_t arg_count = sizeof...(Args);
    static constexpr bool is_const = false;
    static constexpr bool is_static = false;
};

template<typename C, typename R, typename... Args>
struct mfn_traits<R(C::*)(Args...) const> {
    using class_type = C;
    using return_type = R;
    static constexpr size_t arg_count = sizeof...(Args);
    static constexpr bool is_const = true;
    static constexpr bool is_static = false;
};

template<typename R, typename... Args>
struct mfn_traits<R(*)(Args...)> {
    using return_type = R;
    static constexpr size_t arg_count = sizeof...(Args);
    static constexpr bool is_const = false;
    static constexpr bool is_static = true;
};

// 普通成员方法 invoker
template<auto Fn, typename MFnType>
struct mfn_invoker_t;

template<auto Fn, typename C, typename R, typename... Args>
struct mfn_invoker_t<Fn, R(C::*)(Args...)> {
    static void invoke(void* obj, const void* const* args, void* result) noexcept {
        C* self = static_cast<C*>(obj);
        auto call = [self, args]<size_t... Is>(std::index_sequence<Is...>) -> R {
            return (self->*Fn)(*static_cast<std::remove_reference_t<Args>*>(
                const_cast<void*>(args[Is]))...);
        };
        if constexpr (std::is_void_v<R>) {
            call(std::index_sequence_for<Args...>{});
            (void)result;
        } else {
            new (result) R(call(std::index_sequence_for<Args...>{}));
        }
    }
};

// const 成员方法 invoker
template<auto Fn, typename C, typename R, typename... Args>
struct mfn_invoker_t<Fn, R(C::*)(Args...) const> {
    static void invoke(void* obj, const void* const* args, void* result) noexcept {
        const C* self = static_cast<const C*>(obj);
        auto call = [self, args]<size_t... Is>(std::index_sequence<Is...>) -> R {
            return (self->*Fn)(*static_cast<std::remove_reference_t<Args>*>(
                const_cast<void*>(args[Is]))...);
        };
        if constexpr (std::is_void_v<R>) {
            call(std::index_sequence_for<Args...>{});
            (void)result;
        } else {
            new (result) R(call(std::index_sequence_for<Args...>{}));
        }
    }
};

// 静态方法 invoker
template<auto Fn, typename MFnType>
struct sfn_invoker_t;

template<auto Fn, typename R, typename... Args>
struct sfn_invoker_t<Fn, R(*)(Args...)> {
    static void invoke(void* obj, const void* const* args, void* result) noexcept {
        (void)obj;
        auto call = [args]<size_t... Is>(std::index_sequence<Is...>) -> R {
            return Fn(*static_cast<std::remove_reference_t<Args>*>(
                const_cast<void*>(args[Is]))...);
        };
        if constexpr (std::is_void_v<R>) {
            call(std::index_sequence_for<Args...>{});
            (void)result;
        } else {
            new (result) R(call(std::index_sequence_for<Args...>{}));
        }
    }
};

// 返回类型 id
template<typename R>
FORCE_INLINE int return_type_id() noexcept {
    if constexpr (std::is_void_v<R>) { return -1; }
    else { return type_id::get_type_id<std::decay_t<R>>(); }
}

// 参数类型 id 列表生成器
template<typename MFnType>
struct arg_ids_maker;

template<typename C, typename R, typename... Args>
struct arg_ids_maker<R(C::*)(Args...)> {
    static dense<int> make() noexcept {
        dense<int> ids;
        ids.reserve_exact(sizeof...(Args));
        (ids.push_back(type_id::get_type_id<Args>()), ...);
        return ids;
    }
};

template<typename C, typename R, typename... Args>
struct arg_ids_maker<R(C::*)(Args...) const> {
    static dense<int> make() noexcept {
        dense<int> ids;
        ids.reserve_exact(sizeof...(Args));
        (ids.push_back(type_id::get_type_id<Args>()), ...);
        return ids;
    }
};

template<typename R, typename... Args>
struct arg_ids_maker<R(*)(Args...)> {
    static dense<int> make() noexcept {
        dense<int> ids;
        ids.reserve_exact(sizeof...(Args));
        (ids.push_back(type_id::get_type_id<Args>()), ...);
        return ids;
    }
};

} // namespace detail_type_erasure

// === 对外导出 (全局命名空间) ===
using detail_type_erasure::mfn_traits;
using detail_type_erasure::mfn_invoker_t;
using detail_type_erasure::sfn_invoker_t;
using detail_type_erasure::return_type_id;
using detail_type_erasure::arg_ids_maker;

// invoker 函数指针类型 (统一签名)
using invoker_func = void(*)(void* obj, const void* const* args, void* result);
