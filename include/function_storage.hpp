#pragma once

#include <tuple>
#include <utility>
#include <functional>
#include <type_traits>
#include <concepts>
#include <optional>

template<typename F, typename... Args>
concept InvocableWithDecayedArgs = requires(F&& f, Args&&... args) 
{
    std::invoke(std::forward<F>(f), std::forward<Args>(args)...);
};

template <typename fun, typename return_type, typename ...parameter_type> 
class function_storage
{
private:
    std::type_identity_t<fun> f_;
    std::type_identity_t<std::optional<return_type>> r_;
    std::type_identity_t<std::tuple<parameter_type...>> p_;

public:
    function_storage() = default;

    function_storage(fun f, parameter_type ...p): f_(std::move(f)), p_(std::make_tuple(std::move(p)...)) {}

    function_storage(const function_storage& other)
        : f_(other.f_), p_(other.p_), r_(other.r_) {}

    function_storage& operator=(const function_storage& other)
    {
        if (this != &other) 
        {
            f_ = other.f_;
            p_ = other.p_;
            r_ = other.r_;
        }
        return *this;
    }

    function_storage(function_storage&& other) noexcept
        : f_(std::move(other.f_)), p_(std::move(other.p_)), r_(std::move(other.r_)) {}

    function_storage& operator=(function_storage&& other) noexcept
    {
        if (this != &other) 
        {
            f_ = std::move(other.f_);
            p_ = std::move(other.p_);
            r_ = std::move(other.r_);
        }
        return *this;
    }

    void* get_return_void_ptr()
    { 
        if constexpr(std::is_same_v<return_type, void>) 
        {
            return nullptr;
        } 
        else 
        {
            return r_.has_value() ? &r_.value() : nullptr;
        }
    }

    void set_parameter(parameter_type ...p) { p_ = std::make_tuple(std::move(p)...); }

    void operator()() 
    { 
        if constexpr(std::is_same_v<return_type, void>) 
        {
            std::apply(f_, p_);
        } 
        else 
        {
            r_.emplace(std::apply(f_, p_));
        }
    }

    void call()
    {
        if constexpr(std::is_same_v<return_type, void>)
        {
            std::apply(f_, p_);
        }
        else
        {
            r_.emplace(std::apply(f_, p_));
        }
    }
};

template<typename F, typename... Args>
requires InvocableWithDecayedArgs<F, Args...>
function_storage(F&& f, Args&&... args) 
-> function_storage<
    std::decay_t<F>, 
    std::invoke_result_t<F, Args...>, 
    std::decay_t<Args>...
>;