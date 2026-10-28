#pragma once


#include <cstddef>
#include <string_view>
#include <format>
#include <ostream>
#include <cstdint>
#include <charconv>
#include <type_traits>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <tuple>
#include <utility>
#include <source_location>
#include "fnv1a.hpp"
#include "force_inline.hpp"

inline constexpr size_t om_text_capacity = 64;

enum class msg_level : uint8_t
{
    debug = 0,
    info  = 1,
    warn  = 2,
    error = 3
};

enum class om_text_mode : uint8_t
{
    off       = 0,
    plain     = 1,
    formatted = 2
};

namespace msg
{
    struct level_tag { msg_level lv; };
    struct code_tag
    {
        uint32_t code;
        [[nodiscard]] constexpr operator uint32_t() const noexcept { return code; }
    };
    struct loc_tag
    {
        std::source_location loc;
        [[nodiscard]] constexpr operator std::source_location() const noexcept { return loc; }
    };

    inline constexpr level_tag debug{msg_level::debug};
    inline constexpr level_tag info {msg_level::info};
    inline constexpr level_tag warn {msg_level::warn};
    inline constexpr level_tag error{msg_level::error};

    [[nodiscard]] constexpr code_tag errc(uint32_t c) noexcept { return {c}; }

    [[nodiscard]] inline loc_tag here(
        std::source_location l = std::source_location::current()) noexcept
    {
        return {l};
    }

    template<typename... Args>
    struct fmt_tag
    {
        static constexpr int kind = 1;
        std::format_string<Args...> fs;
        std::tuple<Args...> args;
    };

    template<typename... Args>
    struct fmt_rt_tag
    {
        static constexpr int kind = 2;
        std::string_view sv;
        std::tuple<Args...> args;
    };

    template<typename... Args>
    [[nodiscard]] constexpr auto fmt(
        std::format_string<std::decay_t<Args>...> f, Args&&... args) noexcept
    {
        return fmt_tag<std::decay_t<Args>...>{
            f, std::tuple<std::decay_t<Args>...>(
                   std::forward<Args>(args)...)};
    }

    template<typename... Args>
    [[nodiscard]] constexpr auto fmt_rt(std::string_view sv, Args&&... args) noexcept
    {
        return fmt_rt_tag<std::decay_t<Args>...>{
            sv, std::tuple<std::decay_t<Args>...>(
                    std::forward<Args>(args)...)};
    }

    template<typename F>
    requires std::invocable<F>
    struct lazy_tag
    {
        static constexpr int kind = 3;
        F fn;
    };

    template<typename F>
    [[nodiscard]] constexpr lazy_tag<std::remove_cvref_t<F>> defer(F&& f) noexcept
    {
        return {std::forward<F>(f)};
    }
}

template<typename T> struct is_msg_tag : std::false_type {};
template<> struct is_msg_tag<msg::level_tag> : std::true_type {};
template<> struct is_msg_tag<msg::code_tag>  : std::true_type {};
template<> struct is_msg_tag<msg::loc_tag>   : std::true_type {};
template<typename T> inline constexpr bool is_msg_tag_v = is_msg_tag<T>::value;

template<typename T> struct is_msg_lazy : std::false_type {};
template<typename F> struct is_msg_lazy<msg::lazy_tag<F>> : std::true_type {};
template<typename T> inline constexpr bool is_msg_lazy_v = is_msg_lazy<T>::value;

template<typename T> struct is_msg_fmt : std::false_type {};
template<typename... A> struct is_msg_fmt<msg::fmt_tag<A...>> : std::true_type {};
template<typename... A> struct is_msg_fmt<msg::fmt_rt_tag<A...>> : std::true_type {};
template<typename T> inline constexpr bool is_msg_fmt_v = is_msg_fmt<T>::value;

[[nodiscard]] consteval uint32_t om_error_code(const char* name) noexcept
{
    const std::uint32_t folded = static_cast<std::uint32_t>(fnv1a_consteval(name)) & 0x3FFFu;
    return folded == 0u ? 1u : folded;
}

inline constexpr msg::code_tag om_err_none              = msg::code_tag{0};
inline constexpr msg::code_tag om_err_type_mismatch     = msg::code_tag{om_error_code("type_mismatch")};
inline constexpr msg::code_tag om_err_invalid_entity    = msg::code_tag{om_error_code("invalid_entity")};
inline constexpr msg::code_tag om_err_version_mismatch  = msg::code_tag{om_error_code("version_mismatch")};
inline constexpr msg::code_tag om_err_out_of_range      = msg::code_tag{om_error_code("out_of_range")};
inline constexpr msg::code_tag om_err_null_pointer      = msg::code_tag{om_error_code("null_pointer")};
inline constexpr msg::code_tag om_err_capacity_exceeded = msg::code_tag{om_error_code("capacity_exceeded")};
inline constexpr msg::code_tag om_err_not_found         = msg::code_tag{om_error_code("not_found")};
inline constexpr msg::code_tag om_err_already_exists    = msg::code_tag{om_error_code("already_exists")};
inline constexpr msg::code_tag om_err_out_of_memory     = msg::code_tag{om_error_code("out_of_memory")};

inline constexpr msg::code_tag om_err_invalid_argument      = msg::code_tag{om_error_code("invalid_argument")};
inline constexpr msg::code_tag om_err_unsupported_operation = msg::code_tag{om_error_code("unsupported_operation")};
inline constexpr msg::code_tag om_err_cancelled             = msg::code_tag{om_error_code("cancelled")};
inline constexpr msg::code_tag om_err_timeout               = msg::code_tag{om_error_code("timeout")};
inline constexpr msg::code_tag om_err_not_running           = msg::code_tag{om_error_code("not_running")};
inline constexpr msg::code_tag om_err_shutting_down         = msg::code_tag{om_error_code("shutting_down")};
inline constexpr msg::code_tag om_err_queue_full            = msg::code_tag{om_error_code("queue_full")};
inline constexpr msg::code_tag om_err_decode_failed         = msg::code_tag{om_error_code("decode_failed")};
inline constexpr msg::code_tag om_err_io_failed             = msg::code_tag{om_error_code("io_failed")};
inline constexpr msg::code_tag om_err_checksum_failed       = msg::code_tag{om_error_code("checksum_failed")};
inline constexpr msg::code_tag om_err_spawn_failed          = msg::code_tag{om_error_code("spawn_failed")};
inline constexpr msg::code_tag om_err_internal_invariant    = msg::code_tag{om_error_code("internal_invariant")};
inline constexpr msg::code_tag om_err_task_body_faulted     = msg::code_tag{om_error_code("thread_task_body_faulted")};


static_assert(om_err_type_mismatch.code != 0 && om_err_type_mismatch.code <= 0x7FFFFFFFu);
static_assert(om_err_invalid_entity.code != 0 && om_err_invalid_entity.code <= 0x7FFFFFFFu);
static_assert(om_err_version_mismatch.code != 0 && om_err_version_mismatch.code <= 0x7FFFFFFFu);
static_assert(om_err_out_of_range.code != 0 && om_err_out_of_range.code <= 0x7FFFFFFFu);
static_assert(om_err_null_pointer.code != 0 && om_err_null_pointer.code <= 0x7FFFFFFFu);
static_assert(om_err_capacity_exceeded.code != 0 && om_err_capacity_exceeded.code <= 0x7FFFFFFFu);
static_assert(om_err_not_found.code != 0 && om_err_not_found.code <= 0x7FFFFFFFu);
static_assert(om_err_already_exists.code != 0 && om_err_already_exists.code <= 0x7FFFFFFFu);
static_assert(om_err_out_of_memory.code != 0 && om_err_out_of_memory.code <= 0x7FFFFFFFu);
static_assert(om_err_invalid_argument.code != 0 && om_err_invalid_argument.code <= 0x7FFFFFFFu);
static_assert(om_err_unsupported_operation.code != 0 && om_err_unsupported_operation.code <= 0x7FFFFFFFu);
static_assert(om_err_cancelled.code != 0 && om_err_cancelled.code <= 0x7FFFFFFFu);
static_assert(om_err_timeout.code != 0 && om_err_timeout.code <= 0x7FFFFFFFu);
static_assert(om_err_not_running.code != 0 && om_err_not_running.code <= 0x7FFFFFFFu);
static_assert(om_err_shutting_down.code != 0 && om_err_shutting_down.code <= 0x7FFFFFFFu);
static_assert(om_err_queue_full.code != 0 && om_err_queue_full.code <= 0x7FFFFFFFu);
static_assert(om_err_decode_failed.code != 0 && om_err_decode_failed.code <= 0x7FFFFFFFu);
static_assert(om_err_io_failed.code != 0 && om_err_io_failed.code <= 0x7FFFFFFFu);
static_assert(om_err_checksum_failed.code != 0 && om_err_checksum_failed.code <= 0x7FFFFFFFu);
static_assert(om_err_spawn_failed.code != 0 && om_err_spawn_failed.code <= 0x7FFFFFFFu);

[[nodiscard]] inline bool validate_format(std::string_view fmt, size_t expected_count) noexcept
{
    size_t count = 0;
    size_t i = 0;
    const size_t sz = fmt.size();
    while (i < sz)
    {
        char c = fmt[i];
        if (c == '{')
        {
            if (i + 1 < sz && fmt[i + 1] == '{')
            {
                i += 2;
                continue;
            }
            ++count;
            while (i < sz && fmt[i] != '}')
            {
                ++i;
            }
            if (i >= sz)
            {
                return false;
            }
            ++i;
        }
        else if (c == '}')
        {
            if (i + 1 < sz && fmt[i + 1] == '}')
            {
                i += 2;
                continue;
            }
            return false;
        }
        else
        {
            ++i;
        }
    }
    return count == expected_count;
}

class operating_message
{
public:
    operating_message() noexcept = default;

    [[nodiscard]] constexpr operator bool() const noexcept { return ok(); }
    [[nodiscard]] constexpr bool ok() const noexcept { return bits_ == 0u; }

    [[nodiscard]] constexpr uint32_t code() const noexcept { return bits_ & code_mask; }
    [[nodiscard]] constexpr bool is_code(uint32_t c) const noexcept
    {
        return code() == (c & code_mask);
    }

    [[nodiscard]] constexpr msg_level level() const noexcept
    {
        return static_cast<msg_level>(bits_ >> level_shift);
    }

    // 站点信息已移出对象：排障依赖错误码名称与调用栈
    [[nodiscard]] constexpr std::string_view text() const noexcept { return {}; }
    [[nodiscard]] constexpr bool truncated() const noexcept { return false; }
    [[nodiscard]] constexpr size_t message_size() const noexcept { return 0; }

    constexpr void set_text_mode(om_text_mode) noexcept {}
    [[nodiscard]] constexpr om_text_mode text_mode() const noexcept { return om_text_mode::off; }
    constexpr void set_min_level(msg_level) noexcept {}
    [[nodiscard]] constexpr msg_level get_min_level() const noexcept { return msg_level::debug; }

    void reset() noexcept { bits_ = 0u; }
    void clear_message() noexcept {}

    operating_message& write_failure(uint32_t code,
                                     msg_level level,
                                     std::source_location = std::source_location::current()) noexcept
    {
        if (!ok())
        {
            return *this;
        }
        bits_ = static_cast<uint16_t>((code & code_mask) |
                                      (static_cast<uint32_t>(level) << level_shift));
        return *this;
    }

    operating_message& write_failure_text(uint32_t code,
                                          msg_level level,
                                          std::source_location site,
                                          std::string_view) noexcept
    {
        return write_failure(code, level, site);
    }

    template <typename... Args>
    FORCE_INLINE operating_message& write(bool success, Args&&... args) noexcept
    {
        if (success || !ok())
        {
            return *this;
        }
        write(std::forward<Args>(args)...);
        if (ok())
        {
            bits_ = static_cast<std::uint16_t>(
                (om_err_internal_invariant.code & code_mask) |
                (static_cast<std::uint32_t>(msg_level::error) << level_shift));
        }
        return *this;
    }

    template <typename... Args>
    FORCE_INLINE operating_message& write(Args&&... args) noexcept
    {
        uint32_t parsed_code = 0u;
        msg_level parsed_level = msg_level::error;
        (scan_one_(args, parsed_code, parsed_level), ...);
        if (ok() && parsed_code != 0u)
        {
            bits_ = static_cast<uint16_t>((parsed_code & code_mask) |
                                          (static_cast<uint32_t>(parsed_level) << level_shift));
        }
        return *this;
    }

    template <typename... Args>
    [[nodiscard]] FORCE_INLINE bool check(bool cond, Args&&... args) noexcept
    {
        if (!cond)
        {
            write(false, std::forward<Args>(args)...);
        }
        return cond;
    }

    operating_message& operator+=(std::string_view) noexcept { return *this; }

    operating_message& operator+=(const operating_message& other) noexcept
    {
        if (ok() && !other.ok())
        {
            bits_ = other.bits_;
        }
        return *this;
    }

    friend std::ostream& operator<<(std::ostream& os, const operating_message& msg)
    {
        if (msg.ok())
        {
            os << "ok";
            return os;
        }
        os << "code=" << msg.code() << " level=" << static_cast<int>(msg.level());
        return os;
    }

private:
    static constexpr uint32_t code_mask = 0x3FFFu;
    static constexpr uint32_t level_shift = 14u;

    FORCE_INLINE static void scan_one_(const msg::code_tag& tag, uint32_t& code,
                                       msg_level&) noexcept
    {
        code = tag.code;
    }

    FORCE_INLINE static void scan_one_(const msg::level_tag& tag, uint32_t&,
                                       msg_level& level) noexcept
    {
        level = tag.lv;
    }

    template <typename T>
    FORCE_INLINE static void scan_one_(const T&, uint32_t&, msg_level&) noexcept
    {
    }

    std::uint16_t bits_{0u};
};

static_assert(std::is_trivially_copyable_v<operating_message>,
              "operating_message must stay trivially copyable");
static_assert(sizeof(operating_message) == 2,
              "operating_message must stay 2 bytes");

namespace msg
{
    [[noreturn]] inline void fail_(std::string_view what,
                                   std::source_location loc) noexcept
    {
        std::fprintf(stderr, "FAIL: %s:%u %s\n",
                     loc.file_name(), loc.line(), what.data());
        std::abort();
    }

    inline void expect_ok(const operating_message& om,
                          std::source_location loc = std::source_location::current()) noexcept
    {
        if (!static_cast<bool>(om))
        {
            fail_("expected success", loc);
        }
    }

    inline void expect_code(const operating_message& om, uint32_t expected,
                            std::source_location loc = std::source_location::current()) noexcept
    {
        if (!om.is_code(expected))
        {
            fail_("expected code mismatch", loc);
        }
    }

    inline void expect_fail(const operating_message& om,
                            std::source_location loc = std::source_location::current()) noexcept
    {
        if (static_cast<bool>(om))
        {
            fail_("expected failure but succeeded", loc);
        }
    }

    [[noreturn]] FORCE_INLINE void debug_break() noexcept
    {
#if defined(_MSC_VER)
        __debugbreak();
#else
        LCF_TRAP();
#endif
    }
}

struct min_level_guard
{
    operating_message& om_;
    msg_level old_;
    min_level_guard(operating_message& om, msg_level lv) noexcept
        : om_(om), old_(om.get_min_level())
    {
        om_.set_min_level(lv);
    }
    ~min_level_guard() noexcept
    {
        om_.set_min_level(old_);
    }
    min_level_guard(const min_level_guard&) = delete;
    min_level_guard& operator=(const min_level_guard&) = delete;
};

struct message_snapshot
{
    size_t size_;
    bool switch_;
    uint32_t code_;
};

[[nodiscard]] inline message_snapshot snapshot_of(const operating_message& om) noexcept
{
    return message_snapshot{om.message_size(), static_cast<bool>(om), om.code()};
}

[[nodiscard]] inline std::string_view appended_since(
    const operating_message& om, const message_snapshot& snap) noexcept
{
    if (om.message_size() <= snap.size_)
    {
        return {};
    }
    std::string_view full = om.text();
    return full.substr(snap.size_);
}

inline void om_clear_if_over(operating_message& om, size_t threshold) noexcept
{
    if (om.message_size() > threshold)
    {
        om.clear_message();
    }
}

// 构造一次失败：返回带码、级别、站点的错误对象；成功路径不调用
[[nodiscard]] inline operating_message om_failure(
    msg::code_tag code,
    msg_level level,
    std::source_location site = std::source_location::current()) noexcept
{
    operating_message message;
    message.write_failure(code, level, site);
    return message;
}
