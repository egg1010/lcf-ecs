#pragma once
#include <tuple>
#include <cstddef>
#include <type_traits>
#include <utility>
#include <ostream>
#include <functional>

// t_fun - 编译期推导函数类型的延迟调用器
// 支持函数指针 / lambda 仿函数 / 成员函数指针
// 用法: t_fun v{f, args...}; v(); v(a,b); v.result_ptr(); v.result_reset();

template<typename F>
class t_fun;

// ====================================================================
// 主模板: 仿函数 / lambda (通过 &F::operator() 推导签名)
// ====================================================================
template<typename R, typename... Args>
class t_fun<R(Args...)>
{
    using pointer = R(*)(Args...);
    pointer f_;
    std::tuple<std::decay_t<Args>...> bound_args_;
    R result_;
public:
    static constexpr size_t arity = sizeof...(Args);
    using return_type = R;
    using args_tuple  = std::tuple<Args...>;

    template<typename... BoundArgs>
    requires (sizeof...(BoundArgs) == sizeof...(Args))
    t_fun(pointer f, BoundArgs&&... args) noexcept
        : f_(f)
        , bound_args_(std::forward<BoundArgs>(args)...)
        , result_{}
    {
    }

    t_fun(t_fun&&) noexcept = default;
    t_fun& operator=(t_fun&&) noexcept = default;
    t_fun(const t_fun&) = default;
    t_fun& operator=(const t_fun&) = default;

    R operator()() noexcept
    {
        result_ = std::apply(f_, bound_args_);
        return result_;
    }

    template<typename... CallArgs>
    requires (sizeof...(CallArgs) == sizeof...(Args) && sizeof...(Args) > 0)
    R operator()(CallArgs&&... args) noexcept
    {
        result_ = f_(std::forward<CallArgs>(args)...);
        return result_;
    }

    R fun() noexcept
    {
        return operator()();
    }

    template<typename... CallArgs>
    requires (sizeof...(CallArgs) == sizeof...(Args) && sizeof...(Args) > 0)
    R fun(CallArgs&&... args) noexcept
    {
        return operator()(std::forward<CallArgs>(args)...);
    }

    pointer target() const noexcept
    {
        return f_;
    }

    template<size_t I>
    auto& bound_arg() noexcept
    {
        return std::get<I>(bound_args_);
    }

    template<size_t I>
    const auto& bound_arg() const noexcept
    {
        return std::get<I>(bound_args_);
    }

    template<size_t I, typename V>
    void set_arg(V&& v) noexcept
    {
        std::get<I>(bound_args_) = std::forward<V>(v);
    }

    void result_reset() noexcept
    {
        result_ = R{};
    }

    R* result_ptr() noexcept
    {
        return &result_;
    }

    // === 高级功能 ===

    // then_call 链式调用
    template<typename G>
    auto then_call(G&& g) noexcept
    {
        return std::forward<G>(g)(operator()());
    }

    // compose 函数组合
    template<typename G, typename... More>
    auto compose(G&& g, More&&... more) noexcept
    {
        return compose_impl(operator()(), std::forward<G>(g), std::forward<More>(more)...);
    }

    // bind_front 部分应用
    template<typename... FrontArgs>
    requires (sizeof...(FrontArgs) <= sizeof...(Args))
    void bind_front(FrontArgs&&... args) noexcept
    {
        bind_front_impl(std::make_index_sequence<sizeof...(FrontArgs)>{}, std::forward<FrontArgs>(args)...);
    }

    // apply_n 批量调用
    R apply_n(size_t count) noexcept
    {
        for (size_t i = 0; i < count; ++i)
        {
            operator()();
        }
        return result_;
    }

    // apply_range 范围应用 (仅 arity==1)
    template<typename T>
    requires (sizeof...(Args) == 1)
    R apply_range(const T* data, size_t count) noexcept
    {
        for (size_t i = 0; i < count; ++i)
        {
            operator()(data[i]);
        }
        return result_;
    }

    // swap O(1) 交换
    void swap(t_fun& other) noexcept
    {
        std::swap(f_, other.f_);
        std::swap(bound_args_, other.bound_args_);
        std::swap(result_, other.result_);
    }

    // 比较
    bool operator==(const t_fun& other) const noexcept
    {
        return f_ == other.f_ && bound_args_ == other.bound_args_;
    }
    bool operator!=(const t_fun& other) const noexcept
    {
        return !(*this == other);
    }

    // hash
    size_t hash() const noexcept
    {
        return std::hash<pointer>{}(f_);
    }

    // empty / release
    bool empty() const noexcept
    {
        return f_ == nullptr;
    }
    void release() noexcept
    {
        f_ = nullptr;
        bound_args_ = {};
        result_ = {};
    }

    // reset 重置绑定参数
    template<typename... ResetArgs>
    requires (sizeof...(ResetArgs) == sizeof...(Args))
    void reset(ResetArgs&&... args) noexcept
    {
        bound_args_ = std::tuple<std::decay_t<Args>...>(std::forward<ResetArgs>(args)...);
    }

    // 流输出
    friend std::ostream& operator<<(std::ostream& os, const t_fun& v) noexcept
    {
        os << "[t_fun arity=" << arity << " target=" << (v.f_ ? "set" : "null") << "]";
        return os;
    }

private:
    template<typename T, typename G, typename... More>
    static auto compose_impl(T&& v, G&& g, More&&... more) noexcept
    {
        if constexpr (sizeof...(More) == 0)
        {
            return std::forward<G>(g)(std::forward<T>(v));
        }
        else
        {
            return compose_impl(std::forward<G>(g)(std::forward<T>(v)), std::forward<More>(more)...);
        }
    }

    template<size_t... Is, typename... FA>
    void bind_front_impl(std::index_sequence<Is...>, FA&&... args) noexcept
    {
        ((std::get<Is>(bound_args_) = std::forward<FA>(args)), ...);
    }
};

// ====================================================================
// void 特化版本
// ====================================================================
template<typename... Args>
class t_fun<void(Args...)>
{
    using pointer = void(*)(Args...);
    pointer f_;
    std::tuple<std::decay_t<Args>...> bound_args_;
public:
    static constexpr size_t arity = sizeof...(Args);
    using return_type = void;
    using args_tuple  = std::tuple<Args...>;

    template<typename... BoundArgs>
    requires (sizeof...(BoundArgs) == sizeof...(Args))
    t_fun(pointer f, BoundArgs&&... args) noexcept
        : f_(f)
        , bound_args_(std::forward<BoundArgs>(args)...)
    {
    }

    t_fun(t_fun&&) noexcept = default;
    t_fun& operator=(t_fun&&) noexcept = default;
    t_fun(const t_fun&) = default;
    t_fun& operator=(const t_fun&) = default;

    void operator()() noexcept
    {
        std::apply(f_, bound_args_);
    }

    template<typename... CallArgs>
    requires (sizeof...(CallArgs) == sizeof...(Args) && sizeof...(Args) > 0)
    void operator()(CallArgs&&... args) noexcept
    {
        f_(std::forward<CallArgs>(args)...);
    }

    void fun() noexcept
    {
        operator()();
    }

    template<typename... CallArgs>
    requires (sizeof...(CallArgs) == sizeof...(Args) && sizeof...(Args) > 0)
    void fun(CallArgs&&... args) noexcept
    {
        operator()(std::forward<CallArgs>(args)...);
    }

    pointer target() const noexcept
    {
        return f_;
    }

    template<size_t I>
    auto& bound_arg() noexcept
    {
        return std::get<I>(bound_args_);
    }

    template<size_t I>
    const auto& bound_arg() const noexcept
    {
        return std::get<I>(bound_args_);
    }

    template<size_t I, typename V>
    void set_arg(V&& v) noexcept
    {
        std::get<I>(bound_args_) = std::forward<V>(v);
    }

    void result_reset() noexcept {}

    void* result_ptr() noexcept
    {
        return nullptr;
    }

    // === 高级功能 ===

    // then_call 链式调用 (void 版本)
    template<typename G>
    void then_call(G&& g) noexcept
    {
        operator()();
        std::forward<G>(g)();
    }

    // compose 函数组合 (void 版本)
    template<typename G, typename... More>
    void compose(G&& g, More&&... more) noexcept
    {
        operator()();
        std::forward<G>(g)();
        (std::forward<More>(more)(), ...);
    }

    // bind_front 部分应用
    template<typename... FrontArgs>
    requires (sizeof...(FrontArgs) <= sizeof...(Args))
    void bind_front(FrontArgs&&... args) noexcept
    {
        bind_front_impl(std::make_index_sequence<sizeof...(FrontArgs)>{}, std::forward<FrontArgs>(args)...);
    }

    // apply_n 批量调用 (无返回)
    void apply_n(size_t count) noexcept
    {
        for (size_t i = 0; i < count; ++i)
        {
            operator()();
        }
    }

    // apply_range 范围应用 (仅 arity==1, 无返回)
    template<typename T>
    requires (sizeof...(Args) == 1)
    void apply_range(const T* data, size_t count) noexcept
    {
        for (size_t i = 0; i < count; ++i)
        {
            operator()(data[i]);
        }
    }

    // swap O(1) 交换
    void swap(t_fun& other) noexcept
    {
        std::swap(f_, other.f_);
        std::swap(bound_args_, other.bound_args_);
    }

    // 比较
    bool operator==(const t_fun& other) const noexcept
    {
        return f_ == other.f_ && bound_args_ == other.bound_args_;
    }
    bool operator!=(const t_fun& other) const noexcept
    {
        return !(*this == other);
    }

    // hash
    size_t hash() const noexcept
    {
        return std::hash<pointer>{}(f_);
    }

    // empty / release
    bool empty() const noexcept
    {
        return f_ == nullptr;
    }
    void release() noexcept
    {
        f_ = nullptr;
        bound_args_ = {};
    }

    // reset 重置绑定参数
    template<typename... ResetArgs>
    requires (sizeof...(ResetArgs) == sizeof...(Args))
    void reset(ResetArgs&&... args) noexcept
    {
        bound_args_ = std::tuple<std::decay_t<Args>...>(std::forward<ResetArgs>(args)...);
    }

    // 流输出
    friend std::ostream& operator<<(std::ostream& os, const t_fun& v) noexcept
    {
        os << "[t_fun arity=" << arity << " target=" << (v.f_ ? "set" : "null") << "]";
        return os;
    }

private:
    template<size_t... Is, typename... FA>
    void bind_front_impl(std::index_sequence<Is...>, FA&&... args) noexcept
    {
        ((std::get<Is>(bound_args_) = std::forward<FA>(args)), ...);
    }
};

// ====================================================================
// 成员函数指针特化 (非 void)
// ====================================================================
template<typename C, typename R, typename... Args>
class t_fun<R(C::*)(Args...)>
{
    using pointer = R(C::*)(Args...);
    pointer f_;
    C* obj_;
    std::tuple<std::decay_t<Args>...> bound_args_;
    R result_;
public:
    static constexpr size_t arity = sizeof...(Args);
    using return_type = R;
    using args_tuple  = std::tuple<Args...>;

    template<typename... BoundArgs>
    requires (sizeof...(BoundArgs) == sizeof...(Args))
    t_fun(pointer f, C* obj, BoundArgs&&... args) noexcept
        : f_(f)
        , obj_(obj)
        , bound_args_(std::forward<BoundArgs>(args)...)
        , result_{}
    {
    }

    t_fun(t_fun&&) noexcept = default;
    t_fun& operator=(t_fun&&) noexcept = default;
    t_fun(const t_fun&) = default;
    t_fun& operator=(const t_fun&) = default;

    R operator()() noexcept
    {
        auto fn = [this](Args... a) { return (obj_->*f_)(a...); };
        result_ = std::apply(fn, bound_args_);
        return result_;
    }

    template<typename... CallArgs>
    requires (sizeof...(CallArgs) == sizeof...(Args) && sizeof...(Args) > 0)
    R operator()(CallArgs&&... args) noexcept
    {
        result_ = (obj_->*f_)(std::forward<CallArgs>(args)...);
        return result_;
    }

    R fun() noexcept
    {
        return operator()();
    }

    template<typename... CallArgs>
    requires (sizeof...(CallArgs) == sizeof...(Args) && sizeof...(Args) > 0)
    R fun(CallArgs&&... args) noexcept
    {
        return operator()(std::forward<CallArgs>(args)...);
    }

    pointer target() const noexcept
    {
        return f_;
    }

    C* object() const noexcept
    {
        return obj_;
    }

    template<size_t I>
    auto& bound_arg() noexcept
    {
        return std::get<I>(bound_args_);
    }

    template<size_t I>
    const auto& bound_arg() const noexcept
    {
        return std::get<I>(bound_args_);
    }

    template<size_t I, typename V>
    void set_arg(V&& v) noexcept
    {
        std::get<I>(bound_args_) = std::forward<V>(v);
    }

    void result_reset() noexcept
    {
        result_ = R{};
    }

    R* result_ptr() noexcept
    {
        return &result_;
    }

    // === 高级功能 ===

    // then_call 链式调用
    template<typename G>
    auto then_call(G&& g) noexcept
    {
        return std::forward<G>(g)(operator()());
    }

    // compose 函数组合
    template<typename G, typename... More>
    auto compose(G&& g, More&&... more) noexcept
    {
        return compose_impl(operator()(), std::forward<G>(g), std::forward<More>(more)...);
    }

    // bind_front 部分应用
    template<typename... FrontArgs>
    requires (sizeof...(FrontArgs) <= sizeof...(Args))
    void bind_front(FrontArgs&&... args) noexcept
    {
        bind_front_impl(std::make_index_sequence<sizeof...(FrontArgs)>{}, std::forward<FrontArgs>(args)...);
    }

    // apply_n 批量调用
    R apply_n(size_t count) noexcept
    {
        for (size_t i = 0; i < count; ++i)
        {
            operator()();
        }
        return result_;
    }

    // apply_range 范围应用 (仅 arity==1)
    template<typename T>
    requires (sizeof...(Args) == 1)
    R apply_range(const T* data, size_t count) noexcept
    {
        for (size_t i = 0; i < count; ++i)
        {
            operator()(data[i]);
        }
        return result_;
    }

    // swap O(1) 交换
    void swap(t_fun& other) noexcept
    {
        std::swap(f_, other.f_);
        std::swap(obj_, other.obj_);
        std::swap(bound_args_, other.bound_args_);
        std::swap(result_, other.result_);
    }

    // 比较
    bool operator==(const t_fun& other) const noexcept
    {
        return f_ == other.f_ && obj_ == other.obj_ && bound_args_ == other.bound_args_;
    }
    bool operator!=(const t_fun& other) const noexcept
    {
        return !(*this == other);
    }

    // hash
    size_t hash() const noexcept
    {
        // 成员函数指针字节哈希
        size_t h = 0;
        const auto* bytes = reinterpret_cast<const unsigned char*>(&f_);
        for (size_t i = 0; i < sizeof(f_); ++i)
        {
            h = h * 131u + bytes[i];
        }
        h ^= std::hash<C*>{}(obj_) + 0x9e3779b9 + (h << 6) + (h >> 2);
        return h;
    }

    // empty / release
    bool empty() const noexcept
    {
        return f_ == nullptr;
    }
    void release() noexcept
    {
        f_ = nullptr;
        obj_ = nullptr;
        bound_args_ = {};
        result_ = {};
    }

    // reset 重置绑定参数
    template<typename... ResetArgs>
    requires (sizeof...(ResetArgs) == sizeof...(Args))
    void reset(ResetArgs&&... args) noexcept
    {
        bound_args_ = std::tuple<std::decay_t<Args>...>(std::forward<ResetArgs>(args)...);
    }

    // 流输出
    friend std::ostream& operator<<(std::ostream& os, const t_fun& v) noexcept
    {
        os << "[t_fun arity=" << arity << " target=" << (v.f_ ? "set" : "null") << "]";
        return os;
    }

private:
    template<typename T, typename G, typename... More>
    static auto compose_impl(T&& v, G&& g, More&&... more) noexcept
    {
        if constexpr (sizeof...(More) == 0)
        {
            return std::forward<G>(g)(std::forward<T>(v));
        }
        else
        {
            return compose_impl(std::forward<G>(g)(std::forward<T>(v)), std::forward<More>(more)...);
        }
    }

    template<size_t... Is, typename... FA>
    void bind_front_impl(std::index_sequence<Is...>, FA&&... args) noexcept
    {
        ((std::get<Is>(bound_args_) = std::forward<FA>(args)), ...);
    }
};

// ====================================================================
// 成员函数指针特化 (void)
// ====================================================================
template<typename C, typename... Args>
class t_fun<void(C::*)(Args...)>
{
    using pointer = void(C::*)(Args...);
    pointer f_;
    C* obj_;
    std::tuple<std::decay_t<Args>...> bound_args_;
public:
    static constexpr size_t arity = sizeof...(Args);
    using return_type = void;
    using args_tuple  = std::tuple<Args...>;

    template<typename... BoundArgs>
    requires (sizeof...(BoundArgs) == sizeof...(Args))
    t_fun(pointer f, C* obj, BoundArgs&&... args) noexcept
        : f_(f)
        , obj_(obj)
        , bound_args_(std::forward<BoundArgs>(args)...)
    {
    }

    t_fun(t_fun&&) noexcept = default;
    t_fun& operator=(t_fun&&) noexcept = default;
    t_fun(const t_fun&) = default;
    t_fun& operator=(const t_fun&) = default;

    void operator()() noexcept
    {
        auto fn = [this](Args... a) { (obj_->*f_)(a...); };
        std::apply(fn, bound_args_);
    }

    template<typename... CallArgs>
    requires (sizeof...(CallArgs) == sizeof...(Args) && sizeof...(Args) > 0)
    void operator()(CallArgs&&... args) noexcept
    {
        (obj_->*f_)(std::forward<CallArgs>(args)...);
    }

    void fun() noexcept
    {
        operator()();
    }

    template<typename... CallArgs>
    requires (sizeof...(CallArgs) == sizeof...(Args) && sizeof...(Args) > 0)
    void fun(CallArgs&&... args) noexcept
    {
        operator()(std::forward<CallArgs>(args)...);
    }

    pointer target() const noexcept
    {
        return f_;
    }

    C* object() const noexcept
    {
        return obj_;
    }

    template<size_t I>
    auto& bound_arg() noexcept
    {
        return std::get<I>(bound_args_);
    }

    template<size_t I>
    const auto& bound_arg() const noexcept
    {
        return std::get<I>(bound_args_);
    }

    template<size_t I, typename V>
    void set_arg(V&& v) noexcept
    {
        std::get<I>(bound_args_) = std::forward<V>(v);
    }

    void result_reset() noexcept {}

    void* result_ptr() noexcept
    {
        return nullptr;
    }

    // === 高级功能 ===

    // then_call 链式调用 (void 版本)
    template<typename G>
    void then_call(G&& g) noexcept
    {
        operator()();
        std::forward<G>(g)();
    }

    // compose 函数组合 (void 版本)
    template<typename G, typename... More>
    void compose(G&& g, More&&... more) noexcept
    {
        operator()();
        std::forward<G>(g)();
        (std::forward<More>(more)(), ...);
    }

    // bind_front 部分应用
    template<typename... FrontArgs>
    requires (sizeof...(FrontArgs) <= sizeof...(Args))
    void bind_front(FrontArgs&&... args) noexcept
    {
        bind_front_impl(std::make_index_sequence<sizeof...(FrontArgs)>{}, std::forward<FrontArgs>(args)...);
    }

    // apply_n 批量调用 (无返回)
    void apply_n(size_t count) noexcept
    {
        for (size_t i = 0; i < count; ++i)
        {
            operator()();
        }
    }

    // apply_range 范围应用 (仅 arity==1, 无返回)
    template<typename T>
    requires (sizeof...(Args) == 1)
    void apply_range(const T* data, size_t count) noexcept
    {
        for (size_t i = 0; i < count; ++i)
        {
            operator()(data[i]);
        }
    }

    // swap O(1) 交换
    void swap(t_fun& other) noexcept
    {
        std::swap(f_, other.f_);
        std::swap(obj_, other.obj_);
        std::swap(bound_args_, other.bound_args_);
    }

    // 比较
    bool operator==(const t_fun& other) const noexcept
    {
        return f_ == other.f_ && obj_ == other.obj_ && bound_args_ == other.bound_args_;
    }
    bool operator!=(const t_fun& other) const noexcept
    {
        return !(*this == other);
    }

    // hash
    size_t hash() const noexcept
    {
        size_t h = 0;
        const auto* bytes = reinterpret_cast<const unsigned char*>(&f_);
        for (size_t i = 0; i < sizeof(f_); ++i)
        {
            h = h * 131u + bytes[i];
        }
        h ^= std::hash<C*>{}(obj_) + 0x9e3779b9 + (h << 6) + (h >> 2);
        return h;
    }

    // empty / release
    bool empty() const noexcept
    {
        return f_ == nullptr;
    }
    void release() noexcept
    {
        f_ = nullptr;
        obj_ = nullptr;
        bound_args_ = {};
    }

    // reset 重置绑定参数
    template<typename... ResetArgs>
    requires (sizeof...(ResetArgs) == sizeof...(Args))
    void reset(ResetArgs&&... args) noexcept
    {
        bound_args_ = std::tuple<std::decay_t<Args>...>(std::forward<ResetArgs>(args)...);
    }

    // 流输出
    friend std::ostream& operator<<(std::ostream& os, const t_fun& v) noexcept
    {
        os << "[t_fun arity=" << arity << " target=" << (v.f_ ? "set" : "null") << "]";
        return os;
    }

private:
    template<size_t... Is, typename... FA>
    void bind_front_impl(std::index_sequence<Is...>, FA&&... args) noexcept
    {
        ((std::get<Is>(bound_args_) = std::forward<FA>(args)), ...);
    }
};

// ====================================================================
// 成员函数指针 const 版特化 (非 void)
// ====================================================================
template<typename C, typename R, typename... Args>
class t_fun<R(C::*)(Args...) const>
{
    using pointer = R(C::*)(Args...) const;
    pointer f_;
    const C* obj_;
    std::tuple<std::decay_t<Args>...> bound_args_;
    R result_;
public:
    static constexpr size_t arity = sizeof...(Args);
    using return_type = R;
    using args_tuple  = std::tuple<Args...>;

    template<typename... BoundArgs>
    requires (sizeof...(BoundArgs) == sizeof...(Args))
    t_fun(pointer f, const C* obj, BoundArgs&&... args) noexcept
        : f_(f)
        , obj_(obj)
        , bound_args_(std::forward<BoundArgs>(args)...)
        , result_{}
    {
    }

    t_fun(t_fun&&) noexcept = default;
    t_fun& operator=(t_fun&&) noexcept = default;
    t_fun(const t_fun&) = default;
    t_fun& operator=(const t_fun&) = default;

    R operator()() noexcept
    {
        auto fn = [this](Args... a) { return (obj_->*f_)(a...); };
        result_ = std::apply(fn, bound_args_);
        return result_;
    }

    template<typename... CallArgs>
    requires (sizeof...(CallArgs) == sizeof...(Args) && sizeof...(Args) > 0)
    R operator()(CallArgs&&... args) noexcept
    {
        result_ = (obj_->*f_)(std::forward<CallArgs>(args)...);
        return result_;
    }

    R fun() noexcept
    {
        return operator()();
    }

    template<typename... CallArgs>
    requires (sizeof...(CallArgs) == sizeof...(Args) && sizeof...(Args) > 0)
    R fun(CallArgs&&... args) noexcept
    {
        return operator()(std::forward<CallArgs>(args)...);
    }

    pointer target() const noexcept
    {
        return f_;
    }

    const C* object() const noexcept
    {
        return obj_;
    }

    template<size_t I>
    auto& bound_arg() noexcept
    {
        return std::get<I>(bound_args_);
    }

    template<size_t I>
    const auto& bound_arg() const noexcept
    {
        return std::get<I>(bound_args_);
    }

    template<size_t I, typename V>
    void set_arg(V&& v) noexcept
    {
        std::get<I>(bound_args_) = std::forward<V>(v);
    }

    void result_reset() noexcept
    {
        result_ = R{};
    }

    R* result_ptr() noexcept
    {
        return &result_;
    }

    // === 高级功能 ===

    // then_call 链式调用
    template<typename G>
    auto then_call(G&& g) noexcept
    {
        return std::forward<G>(g)(operator()());
    }

    // compose 函数组合
    template<typename G, typename... More>
    auto compose(G&& g, More&&... more) noexcept
    {
        return compose_impl(operator()(), std::forward<G>(g), std::forward<More>(more)...);
    }

    // bind_front 部分应用
    template<typename... FrontArgs>
    requires (sizeof...(FrontArgs) <= sizeof...(Args))
    void bind_front(FrontArgs&&... args) noexcept
    {
        bind_front_impl(std::make_index_sequence<sizeof...(FrontArgs)>{}, std::forward<FrontArgs>(args)...);
    }

    // apply_n 批量调用
    R apply_n(size_t count) noexcept
    {
        for (size_t i = 0; i < count; ++i)
        {
            operator()();
        }
        return result_;
    }

    // apply_range 范围应用 (仅 arity==1)
    template<typename T>
    requires (sizeof...(Args) == 1)
    R apply_range(const T* data, size_t count) noexcept
    {
        for (size_t i = 0; i < count; ++i)
        {
            operator()(data[i]);
        }
        return result_;
    }

    // swap O(1) 交换
    void swap(t_fun& other) noexcept
    {
        std::swap(f_, other.f_);
        std::swap(obj_, other.obj_);
        std::swap(bound_args_, other.bound_args_);
        std::swap(result_, other.result_);
    }

    // 比较
    bool operator==(const t_fun& other) const noexcept
    {
        return f_ == other.f_ && obj_ == other.obj_ && bound_args_ == other.bound_args_;
    }
    bool operator!=(const t_fun& other) const noexcept
    {
        return !(*this == other);
    }

    // hash
    size_t hash() const noexcept
    {
        size_t h = 0;
        const auto* bytes = reinterpret_cast<const unsigned char*>(&f_);
        for (size_t i = 0; i < sizeof(f_); ++i)
        {
            h = h * 131u + bytes[i];
        }
        h ^= std::hash<const C*>{}(obj_) + 0x9e3779b9 + (h << 6) + (h >> 2);
        return h;
    }

    // empty / release
    bool empty() const noexcept
    {
        return f_ == nullptr;
    }
    void release() noexcept
    {
        f_ = nullptr;
        obj_ = nullptr;
        bound_args_ = {};
        result_ = {};
    }

    // reset 重置绑定参数
    template<typename... ResetArgs>
    requires (sizeof...(ResetArgs) == sizeof...(Args))
    void reset(ResetArgs&&... args) noexcept
    {
        bound_args_ = std::tuple<std::decay_t<Args>...>(std::forward<ResetArgs>(args)...);
    }

    // 流输出
    friend std::ostream& operator<<(std::ostream& os, const t_fun& v) noexcept
    {
        os << "[t_fun arity=" << arity << " target=" << (v.f_ ? "set" : "null") << "]";
        return os;
    }

private:
    template<typename T, typename G, typename... More>
    static auto compose_impl(T&& v, G&& g, More&&... more) noexcept
    {
        if constexpr (sizeof...(More) == 0)
        {
            return std::forward<G>(g)(std::forward<T>(v));
        }
        else
        {
            return compose_impl(std::forward<G>(g)(std::forward<T>(v)), std::forward<More>(more)...);
        }
    }

    template<size_t... Is, typename... FA>
    void bind_front_impl(std::index_sequence<Is...>, FA&&... args) noexcept
    {
        ((std::get<Is>(bound_args_) = std::forward<FA>(args)), ...);
    }
};

// ====================================================================
// 成员函数指针 const 版特化 (void)
// ====================================================================
template<typename C, typename... Args>
class t_fun<void(C::*)(Args...) const>
{
    using pointer = void(C::*)(Args...) const;
    pointer f_;
    const C* obj_;
    std::tuple<std::decay_t<Args>...> bound_args_;
public:
    static constexpr size_t arity = sizeof...(Args);
    using return_type = void;
    using args_tuple  = std::tuple<Args...>;

    template<typename... BoundArgs>
    requires (sizeof...(BoundArgs) == sizeof...(Args))
    t_fun(pointer f, const C* obj, BoundArgs&&... args) noexcept
        : f_(f)
        , obj_(obj)
        , bound_args_(std::forward<BoundArgs>(args)...)
    {
    }

    t_fun(t_fun&&) noexcept = default;
    t_fun& operator=(t_fun&&) noexcept = default;
    t_fun(const t_fun&) = default;
    t_fun& operator=(const t_fun&) = default;

    void operator()() noexcept
    {
        auto fn = [this](Args... a) { (obj_->*f_)(a...); };
        std::apply(fn, bound_args_);
    }

    template<typename... CallArgs>
    requires (sizeof...(CallArgs) == sizeof...(Args) && sizeof...(Args) > 0)
    void operator()(CallArgs&&... args) noexcept
    {
        (obj_->*f_)(std::forward<CallArgs>(args)...);
    }

    void fun() noexcept
    {
        operator()();
    }

    template<typename... CallArgs>
    requires (sizeof...(CallArgs) == sizeof...(Args) && sizeof...(Args) > 0)
    void fun(CallArgs&&... args) noexcept
    {
        operator()(std::forward<CallArgs>(args)...);
    }

    pointer target() const noexcept
    {
        return f_;
    }

    const C* object() const noexcept
    {
        return obj_;
    }

    template<size_t I>
    auto& bound_arg() noexcept
    {
        return std::get<I>(bound_args_);
    }

    template<size_t I>
    const auto& bound_arg() const noexcept
    {
        return std::get<I>(bound_args_);
    }

    template<size_t I, typename V>
    void set_arg(V&& v) noexcept
    {
        std::get<I>(bound_args_) = std::forward<V>(v);
    }

    void result_reset() noexcept {}

    void* result_ptr() noexcept
    {
        return nullptr;
    }

    // === 高级功能 ===

    // then_call 链式调用 (void 版本)
    template<typename G>
    void then_call(G&& g) noexcept
    {
        operator()();
        std::forward<G>(g)();
    }

    // compose 函数组合 (void 版本)
    template<typename G, typename... More>
    void compose(G&& g, More&&... more) noexcept
    {
        operator()();
        std::forward<G>(g)();
        (std::forward<More>(more)(), ...);
    }

    // bind_front 部分应用
    template<typename... FrontArgs>
    requires (sizeof...(FrontArgs) <= sizeof...(Args))
    void bind_front(FrontArgs&&... args) noexcept
    {
        bind_front_impl(std::make_index_sequence<sizeof...(FrontArgs)>{}, std::forward<FrontArgs>(args)...);
    }

    // apply_n 批量调用 (无返回)
    void apply_n(size_t count) noexcept
    {
        for (size_t i = 0; i < count; ++i)
        {
            operator()();
        }
    }

    // apply_range 范围应用 (仅 arity==1, 无返回)
    template<typename T>
    requires (sizeof...(Args) == 1)
    void apply_range(const T* data, size_t count) noexcept
    {
        for (size_t i = 0; i < count; ++i)
        {
            operator()(data[i]);
        }
    }

    // swap O(1) 交换
    void swap(t_fun& other) noexcept
    {
        std::swap(f_, other.f_);
        std::swap(obj_, other.obj_);
        std::swap(bound_args_, other.bound_args_);
    }

    // 比较
    bool operator==(const t_fun& other) const noexcept
    {
        return f_ == other.f_ && obj_ == other.obj_ && bound_args_ == other.bound_args_;
    }
    bool operator!=(const t_fun& other) const noexcept
    {
        return !(*this == other);
    }

    // hash
    size_t hash() const noexcept
    {
        size_t h = 0;
        const auto* bytes = reinterpret_cast<const unsigned char*>(&f_);
        for (size_t i = 0; i < sizeof(f_); ++i)
        {
            h = h * 131u + bytes[i];
        }
        h ^= std::hash<const C*>{}(obj_) + 0x9e3779b9 + (h << 6) + (h >> 2);
        return h;
    }

    // empty / release
    bool empty() const noexcept
    {
        return f_ == nullptr;
    }
    void release() noexcept
    {
        f_ = nullptr;
        obj_ = nullptr;
        bound_args_ = {};
    }

    // reset 重置绑定参数
    template<typename... ResetArgs>
    requires (sizeof...(ResetArgs) == sizeof...(Args))
    void reset(ResetArgs&&... args) noexcept
    {
        bound_args_ = std::tuple<std::decay_t<Args>...>(std::forward<ResetArgs>(args)...);
    }

    // 流输出
    friend std::ostream& operator<<(std::ostream& os, const t_fun& v) noexcept
    {
        os << "[t_fun arity=" << arity << " target=" << (v.f_ ? "set" : "null") << "]";
        return os;
    }

private:
    template<size_t... Is, typename... FA>
    void bind_front_impl(std::index_sequence<Is...>, FA&&... args) noexcept
    {
        ((std::get<Is>(bound_args_) = std::forward<FA>(args)), ...);
    }
};

// ====================================================================
// CTAD 推导指引
// ====================================================================

// 函数指针
template<typename R, typename... Args>
t_fun(R(*)(Args...)) -> t_fun<R(Args...)>;

template<typename R, typename... Args, typename... BoundArgs>
t_fun(R(*)(Args...), BoundArgs...) -> t_fun<R(Args...)>;

// 成员函数指针 (非 const)
template<typename C, typename R, typename... Args>
t_fun(R(C::*)(Args...)) -> t_fun<R(C::*)(Args...)>;

template<typename C, typename R, typename... Args, typename... BoundArgs>
t_fun(R(C::*)(Args...), C*, BoundArgs...) -> t_fun<R(C::*)(Args...)>;

// 成员函数指针 (const)
template<typename C, typename R, typename... Args>
t_fun(R(C::*)(Args...) const) -> t_fun<R(C::*)(Args...) const>;

template<typename C, typename R, typename... Args, typename... BoundArgs>
t_fun(R(C::*)(Args...) const, const C*, BoundArgs...) -> t_fun<R(C::*)(Args...) const>;
