#pragma once
#include <string>
#include <string_view>
#include <format>
#include <iterator>
#include <ostream>

inline bool& ecs_debug_messages() noexcept
{
    static bool enabled = true;
    return enabled;
}

class operating_message
{
private:
    bool switch_{true};
    std::string message_;

public:
    ~operating_message() noexcept = default;
    constexpr operating_message() noexcept = default;

    [[nodiscard]] constexpr operator bool() const noexcept
    {
        return switch_;
    }

    constexpr void reset() noexcept
    {
        switch_ = true;
        message_.clear();
    }

    constexpr void clear_message() noexcept
    {
        message_.clear();
    }

    constexpr void set_switch_bool(bool sw) noexcept { switch_ = sw; }

    [[nodiscard]] constexpr bool& get_switch_bool() noexcept { return switch_; }
    [[nodiscard]] constexpr const bool& get_switch_bool() const noexcept { return switch_; }

    operating_message& operator+=(std::string_view sv) noexcept
    {
        if (ecs_debug_messages()) message_ += sv;
        return *this;
    }

    operating_message& operator+=(operating_message&& other) noexcept
    {
        if (ecs_debug_messages()) message_ += std::move(other.message_);
        switch_ = switch_ && other.switch_;
        return *this;
    }

    operating_message& operator+=(const operating_message& other) noexcept
    {
        if (ecs_debug_messages()) message_ += other.message_;
        switch_ = switch_ && other.switch_;
        return *this;
    }

    friend std::ostream& operator<<(std::ostream& os, const operating_message& str)
    {
        os << str.message_;
        return os;
    }

    [[nodiscard]] std::string_view read_message() const noexcept { return message_; }

    template<typename... Args>
    void write_message(bool sw, Args&&... args)
    {
        switch_ = switch_ && sw;
        if (ecs_debug_messages())
        {
            ((std::format_to(std::back_inserter(message_), "{}", std::forward<Args>(args))), ...);
            message_ += '\n';
        }
    }

    template<typename... Args>
    void write_message_fmt(bool sw, std::format_string<Args...> fmt, Args&&... args)
    {
        switch_ = switch_ && sw;
        if (ecs_debug_messages())
        {
            std::format_to(std::back_inserter(message_), fmt, std::forward<Args>(args)...);
            message_ += '\n';
        }
    }

    operating_message(operating_message&& other) noexcept = default;
    operating_message& operator=(operating_message&& other) noexcept = default;
    operating_message(const operating_message& other) noexcept = default;
    operating_message& operator=(const operating_message& other) noexcept = default;
};