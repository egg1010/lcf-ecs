#pragma once
#include <string>
#include <string_view>
#include <format>
#include <iterator>
#include <ostream>
#include <cstdint>
#include <charconv>
#include <type_traits>

inline bool& ecs_debug_messages() noexcept
{
    static bool enabled = true;
    return enabled;
}

enum class msg_level : uint8_t
{
    debug = 0,
    info  = 1,
    warn  = 2,
    error = 3
};

class operating_message
{
private:
    bool switch_{true};
    uint8_t min_level_{static_cast<uint8_t>(msg_level::info)};
    std::string message_;

    static constexpr std::string_view k_level_prefix[] = {
        "[DEBUG] ",
        "[INFO]  ",
        "[WARN]  ",
        "[ERROR] "
    };

    template<typename T>
    void append_arg(T&& v) noexcept
    {
        using U = std::remove_cvref_t<T>;
        if constexpr (std::is_convertible_v<U, std::string_view>)
        {
            message_ += v;
        }
        else if constexpr (std::is_integral_v<U>)
        {
            char buf[32];
            auto r = std::to_chars(buf, buf + sizeof(buf), v);
            message_.append(buf, static_cast<size_t>(r.ptr - buf));
        }
        else if constexpr (std::is_floating_point_v<U>)
        {
            char buf[64];
            auto r = std::to_chars(buf, buf + sizeof(buf), v);
            message_.append(buf, static_cast<size_t>(r.ptr - buf));
        }
        else
        {
            std::format_to(std::back_inserter(message_), "{}", std::forward<T>(v));
        }
    }

    // 检测 fmt 是否只含简单 {} 占位符 (无 {{ }} 转义, 无 {:spec} 复杂格式)
    static constexpr bool is_simple_fmt(std::string_view fmt, size_t expected) noexcept
    {
        size_t count = 0;
        size_t i = 0;
        const size_t sz = fmt.size();
        while (i < sz)
        {
            char c = fmt[i];
            if (c == '{')
            {
                if (i + 1 < sz && fmt[i + 1] == '{') return false;
                if (i + 1 >= sz || fmt[i + 1] != '}') return false;
                ++count;
                i += 2;
            }
            else if (c == '}')
            {
                if (i + 1 < sz && fmt[i + 1] == '}') return false;
                return false;
            }
            else ++i;
        }
        return count == expected;
    }

    // 解析简单 {} 占位符 fmt, 用 append_arg 输出 (绕过 format_to + back_inserter)
    template<typename... Args>
    void write_fmt_simple(std::string_view fmt, Args&&... args) noexcept
    {
        size_t pos = 0;
        const size_t sz = fmt.size();
        auto emit_literal = [&](size_t end) {
            if (end > pos) message_.append(fmt.data() + pos, end - pos);
        };
        auto emit_arg = [&](auto&& a) {
            size_t i = pos;
            while (i < sz)
            {
                if (fmt[i] == '{')
                {
                    emit_literal(i);
                    i += 2;
                    pos = i;
                    append_arg(std::forward<decltype(a)>(a));
                    return;
                }
                ++i;
            }
        };

        (emit_arg(args), ...);
        emit_literal(sz);
    }

    // 公共 fast path 分派: 简单 {} 走 append_arg, 否则走 format_to
    template<typename... Args>
    void dispatch_fmt(std::string_view fmt_sv, std::format_string<Args...> fmt, Args&&... args)
    {
        if constexpr (sizeof...(Args) > 0)
        {
            if (is_simple_fmt(fmt_sv, sizeof...(Args)))
            {
                size_t estimated = message_.size() + fmt_sv.size() + sizeof...(Args) * 32;
                if (message_.capacity() < estimated) message_.reserve(estimated);
                write_fmt_simple(fmt_sv, std::forward<Args>(args)...);
            }
            else
            {
                std::format_to(std::back_inserter(message_), fmt, std::forward<Args>(args)...);
            }
        }
        else
        {
            std::format_to(std::back_inserter(message_), fmt);
        }
    }

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

    constexpr void set_min_level(msg_level lv) noexcept { min_level_ = static_cast<uint8_t>(lv); }
    [[nodiscard]] constexpr msg_level get_min_level() const noexcept { return static_cast<msg_level>(min_level_); }

    void reserve(size_t cap) noexcept { message_.reserve(cap); }
    [[nodiscard]] size_t capacity() const noexcept { return message_.capacity(); }
    [[nodiscard]] size_t message_size() const noexcept { return message_.size(); }

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
        if (!ecs_debug_messages()) return;
        if (static_cast<uint8_t>(msg_level::info) < min_level_) return;
        (append_arg(std::forward<Args>(args)), ...);
        message_ += '\n';
    }

    template<typename... Args>
    void write_message_level(msg_level lv, bool sw, Args&&... args)
    {
        switch_ = switch_ && sw;
        if (!ecs_debug_messages()) return;
        const uint8_t idx = static_cast<uint8_t>(lv);
        if (idx < min_level_) return;
        message_ += k_level_prefix[idx];
        (append_arg(std::forward<Args>(args)), ...);
        message_ += '\n';
    }

    template<typename... Args>
    void write_message_fmt(bool sw, std::format_string<Args...> fmt, Args&&... args)
    {
        switch_ = switch_ && sw;
        if (!ecs_debug_messages()) return;
        if (static_cast<uint8_t>(msg_level::info) < min_level_) return;
        dispatch_fmt(fmt.get(), fmt, std::forward<Args>(args)...);
        message_ += '\n';
    }

    template<typename... Args>
    void write_message_fmt_level(msg_level lv, bool sw, std::format_string<Args...> fmt, Args&&... args)
    {
        switch_ = switch_ && sw;
        if (!ecs_debug_messages()) return;
        const uint8_t idx = static_cast<uint8_t>(lv);
        if (idx < min_level_) return;
        message_ += k_level_prefix[idx];
        dispatch_fmt(fmt.get(), fmt, std::forward<Args>(args)...);
        message_ += '\n';
    }

    operating_message(operating_message&& other) noexcept = default;
    operating_message& operator=(operating_message&& other) noexcept = default;
    operating_message(const operating_message& other) noexcept = default;
    operating_message& operator=(const operating_message& other) noexcept = default;
};
