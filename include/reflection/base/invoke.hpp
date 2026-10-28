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

// invoke.hpp - 类型擦除的方法调用器
// 命名空间: reflect

// ============================================================================

// invoke.hpp - 类型擦除的方法调用器
// 命名空间: reflect
// invoker 第四参数 fn 指向 method_meta 内的指针副本: 模板注册忽略它, 运行期注册从它取指针


namespace reflect {

// 成员函数指针存储容量 (GCC/Clang 16 字节, MSVC 多继承 16 字节)
inline constexpr size_t fn_storage_size = 16;

// invoker 函数指针类型 (统一签名)
using invoker_func = void(*)(void* obj, const void* const* args, void* result, const void* fn);

namespace detail {

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
    static constexpr bool is_noexcept = false;
};

template<typename C, typename R, typename... Args>
struct mfn_traits<R(C::*)(Args...) const> {
    using class_type = C;
    using return_type = R;
    static constexpr size_t arg_count = sizeof...(Args);
    static constexpr bool is_const = true;
    static constexpr bool is_static = false;
    static constexpr bool is_noexcept = false;
};

template<typename C, typename R, typename... Args>
struct mfn_traits<R(C::*)(Args...) noexcept> {
    using class_type = C;
    using return_type = R;
    static constexpr size_t arg_count = sizeof...(Args);
    static constexpr bool is_const = false;
    static constexpr bool is_static = false;
    static constexpr bool is_noexcept = true;
};

template<typename C, typename R, typename... Args>
struct mfn_traits<R(C::*)(Args...) const noexcept> {
    using class_type = C;
    using return_type = R;
    static constexpr size_t arg_count = sizeof...(Args);
    static constexpr bool is_const = true;
    static constexpr bool is_static = false;
    static constexpr bool is_noexcept = true;
};

template<typename R, typename... Args>
struct mfn_traits<R(*)(Args...)> {
    using return_type = R;
    static constexpr size_t arg_count = sizeof...(Args);
    static constexpr bool is_const = false;
    static constexpr bool is_static = true;
    static constexpr bool is_noexcept = false;
};

template<typename R, typename... Args>
struct mfn_traits<R(*)(Args...) noexcept> {
    using return_type = R;
    static constexpr size_t arg_count = sizeof...(Args);
    static constexpr bool is_const = false;
    static constexpr bool is_static = true;
    static constexpr bool is_noexcept = true;
};

// 普通成员方法 invoker
template<auto Fn, typename MFnType>
struct mfn_invoker_t;

template<auto Fn, typename C, typename R, typename... Args>
struct mfn_invoker_t<Fn, R(C::*)(Args...)> {
    static void invoke(void* obj, const void* const* args, void* result, const void*) noexcept {
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
    static void invoke(void* obj, const void* const* args, void* result, const void*) noexcept {
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
    static void invoke(void* obj, const void* const* args, void* result, const void*) noexcept {
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

// 运行期成员函数指针 invoker: 指针从 fn 参数读取
template<typename MFnType>
struct mfn_ptr_invoker;

template<typename C, typename R, typename... Args>
struct mfn_ptr_invoker<R(C::*)(Args...)> {
    using fn_type = R(C::*)(Args...);
    static void invoke(void* obj, const void* const* args, void* result, const void* fn) noexcept {
        fn_type fp = nullptr;
        std::memcpy(&fp, fn, sizeof(fp));
        C* self = static_cast<C*>(obj);
        auto call = [self, args, fp]<size_t... Is>(std::index_sequence<Is...>) -> R {
            return (self->*fp)(*static_cast<std::remove_reference_t<Args>*>(
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

template<typename C, typename R, typename... Args>
struct mfn_ptr_invoker<R(C::*)(Args...) const> {
    using fn_type = R(C::*)(Args...) const;
    static void invoke(void* obj, const void* const* args, void* result, const void* fn) noexcept {
        fn_type fp = nullptr;
        std::memcpy(&fp, fn, sizeof(fp));
        const C* self = static_cast<const C*>(obj);
        auto call = [self, args, fp]<size_t... Is>(std::index_sequence<Is...>) -> R {
            return (self->*fp)(*static_cast<std::remove_reference_t<Args>*>(
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

template<typename C, typename R, typename... Args>
struct mfn_ptr_invoker<R(C::*)(Args...) noexcept> {
    using fn_type = R(C::*)(Args...) noexcept;
    static void invoke(void* obj, const void* const* args, void* result, const void* fn) noexcept {
        fn_type fp = nullptr;
        std::memcpy(&fp, fn, sizeof(fp));
        C* self = static_cast<C*>(obj);
        auto call = [self, args, fp]<size_t... Is>(std::index_sequence<Is...>) -> R {
            return (self->*fp)(*static_cast<std::remove_reference_t<Args>*>(
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

template<typename C, typename R, typename... Args>
struct mfn_ptr_invoker<R(C::*)(Args...) const noexcept> {
    using fn_type = R(C::*)(Args...) const noexcept;
    static void invoke(void* obj, const void* const* args, void* result, const void* fn) noexcept {
        fn_type fp = nullptr;
        std::memcpy(&fp, fn, sizeof(fp));
        const C* self = static_cast<const C*>(obj);
        auto call = [self, args, fp]<size_t... Is>(std::index_sequence<Is...>) -> R {
            return (self->*fp)(*static_cast<std::remove_reference_t<Args>*>(
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

// 运行期静态函数指针 invoker
template<typename FnType>
struct sfn_ptr_invoker;

template<typename R, typename... Args>
struct sfn_ptr_invoker<R(*)(Args...)> {
    using fn_type = R(*)(Args...);
    static void invoke(void* obj, const void* const* args, void* result, const void* fn) noexcept {
        (void)obj;
        fn_type fp = nullptr;
        std::memcpy(&fp, fn, sizeof(fp));
        auto call = [args, fp]<size_t... Is>(std::index_sequence<Is...>) -> R {
            return fp(*static_cast<std::remove_reference_t<Args>*>(
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

template<typename R, typename... Args>
struct sfn_ptr_invoker<R(*)(Args...) noexcept> {
    using fn_type = R(*)(Args...) noexcept;
    static void invoke(void* obj, const void* const* args, void* result, const void* fn) noexcept {
        (void)obj;
        fn_type fp = nullptr;
        std::memcpy(&fp, fn, sizeof(fp));
        auto call = [args, fp]<size_t... Is>(std::index_sequence<Is...>) -> R {
            return fp(*static_cast<std::remove_reference_t<Args>*>(
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
template<typename... Args>
dense<int> make_arg_ids() noexcept
{
    dense<int> ids;
    ids.increase_capacity(sizeof...(Args));
    (ids.emplace_back(type_id::get_type_id<std::decay_t<Args>>()), ...);
    return ids;
}

template<typename MFnType>
struct arg_ids_maker;

template<typename C, typename R, typename... Args>
struct arg_ids_maker<R(C::*)(Args...)> {
    static dense<int> make() noexcept { return make_arg_ids<Args...>(); }
};

template<typename C, typename R, typename... Args>
struct arg_ids_maker<R(C::*)(Args...) const> {
    static dense<int> make() noexcept { return make_arg_ids<Args...>(); }
};

template<typename C, typename R, typename... Args>
struct arg_ids_maker<R(C::*)(Args...) noexcept> {
    static dense<int> make() noexcept { return make_arg_ids<Args...>(); }
};

template<typename C, typename R, typename... Args>
struct arg_ids_maker<R(C::*)(Args...) const noexcept> {
    static dense<int> make() noexcept { return make_arg_ids<Args...>(); }
};

template<typename R, typename... Args>
struct arg_ids_maker<R(*)(Args...)> {
    static dense<int> make() noexcept { return make_arg_ids<Args...>(); }
};

template<typename R, typename... Args>
struct arg_ids_maker<R(*)(Args...) noexcept> {
    static dense<int> make() noexcept { return make_arg_ids<Args...>(); }
};

} // namespace detail

} // namespace reflect

// ============================================================================
