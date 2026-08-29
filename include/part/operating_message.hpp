#pragma once

// operating_message.hpp - 操作消息 (SSO + 堆扩容 + 错误码 + 级别 + 位置)
// 单一 write 接口, 标签混入参数包任意组合

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

// 全局消息记录开关 (运行时)
inline bool& message_recording_enabled() noexcept
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

// 标签
namespace msg
{
    struct level_tag { msg_level lv; };
    struct code_tag
    {
        uint32_t code;
        [[nodiscard]] constexpr operator uint32_t() const noexcept { return code; }
    };
    struct loc_tag { std::source_location loc; };

    inline constexpr level_tag debug{msg_level::debug};
    inline constexpr level_tag info {msg_level::info};
    inline constexpr level_tag warn {msg_level::warn};
    inline constexpr level_tag error{msg_level::error};

    // 动态错误码
    [[nodiscard]] constexpr code_tag errc(uint32_t c) noexcept { return {c}; }

    // 位置 (须在调用点求值)
    [[nodiscard]] inline loc_tag here(
        std::source_location l = std::source_location::current()) noexcept
    {
        return {l};
    }

    // 编译期格式化
    template<typename... Args>
    struct fmt_tag
    {
        static constexpr int kind = 1;
        std::format_string<Args...> fs;
        std::tuple<Args...> args;
    };

    // 运行时格式串
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

    // 惰性求值
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

// 标签类型识别
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

// 错误码常量 (fnv1a)
inline constexpr msg::code_tag om_err_none              = msg::code_tag{0};
inline constexpr msg::code_tag om_err_type_mismatch     = msg::code_tag{static_cast<uint32_t>(fnv1a_consteval("type_mismatch"))};
inline constexpr msg::code_tag om_err_invalid_entity    = msg::code_tag{static_cast<uint32_t>(fnv1a_consteval("invalid_entity"))};
inline constexpr msg::code_tag om_err_version_mismatch  = msg::code_tag{static_cast<uint32_t>(fnv1a_consteval("version_mismatch"))};
inline constexpr msg::code_tag om_err_out_of_range      = msg::code_tag{static_cast<uint32_t>(fnv1a_consteval("out_of_range"))};
inline constexpr msg::code_tag om_err_null_pointer      = msg::code_tag{static_cast<uint32_t>(fnv1a_consteval("null_pointer"))};
inline constexpr msg::code_tag om_err_capacity_exceeded = msg::code_tag{static_cast<uint32_t>(fnv1a_consteval("capacity_exceeded"))};
inline constexpr msg::code_tag om_err_not_found         = msg::code_tag{static_cast<uint32_t>(fnv1a_consteval("not_found"))};
inline constexpr msg::code_tag om_err_already_exists    = msg::code_tag{static_cast<uint32_t>(fnv1a_consteval("already_exists"))};
inline constexpr msg::code_tag om_err_out_of_memory     = msg::code_tag{static_cast<uint32_t>(fnv1a_consteval("out_of_memory"))};

// 运行时格式串校验 (占位符数量 + 语法, 不校验类型)
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
private:
    static constexpr size_t SSO_SIZE = 48;

    // 构造不清零缓冲 (utf8pp 同策略)
    union msg_storage
    {
        char sso_buf[SSO_SIZE];
        char* heap_ptr;
    };

    msg_storage storage_;
    uint32_t size_{0};
    uint32_t capacity_{SSO_SIZE};
    bool switch_{true};
    uint8_t min_level_{static_cast<uint8_t>(msg_level::info)};
    uint32_t code_{0};

    static constexpr std::string_view k_level_prefix[] = {
        "[DEBUG] ",
        "[INFO]  ",
        "[WARN]  ",
        "[ERROR] "
    };

    [[nodiscard]] constexpr bool is_sso() const noexcept
    {
        return capacity_ <= SSO_SIZE;
    }

    [[nodiscard]] FORCE_INLINE char* data_ptr() noexcept
    {
        return is_sso() ? storage_.sso_buf : storage_.heap_ptr;
    }

    [[nodiscard]] FORCE_INLINE const char* data_ptr() const noexcept
    {
        return is_sso() ? storage_.sso_buf : storage_.heap_ptr;
    }

    void free_heap_buffer() noexcept
    {
        if (!is_sso())
        {
            ::operator delete(storage_.heap_ptr);
            storage_.heap_ptr = nullptr;
            capacity_ = SSO_SIZE;
        }
    }

    // 倍增扩容
    void grow_to(size_t need) noexcept
    {
        if (need <= capacity_)
        {
            return;
        }
        size_t new_cap = capacity_ * 2;
        while (new_cap < need)
        {
            new_cap *= 2;
        }
        char* new_buf = static_cast<char*>(::operator new(new_cap, std::nothrow));
        if (!new_buf)
        {
            std::abort();
        }
        if (size_ > 0)
        {
            std::memcpy(new_buf, data_ptr(), size_);
        }
        free_heap_buffer();
        storage_.heap_ptr = new_buf;
        capacity_ = static_cast<uint32_t>(new_cap);
    }

    FORCE_INLINE void append(const char* s, size_t len) noexcept
    {
        if (len == 0)
        {
            return;
        }
        size_t need = size_ + len;
        if (need > capacity_)
        {
            grow_to(need);
        }
        char* dst = data_ptr();
#if defined(__GNUC__) || defined(__clang__)
        asm volatile("" : "+r"(dst));  // 切断对象大小追踪, 抑制误报
#endif
        std::memcpy(dst + size_, s, len);
        size_ = static_cast<uint32_t>(need);
    }

    FORCE_INLINE void append_char(char c) noexcept
    {
        if (size_ + 1 > capacity_)
        {
            grow_to(static_cast<size_t>(size_) + 1);
        }
        data_ptr()[size_] = c;
        ++size_;
    }

    // format_to 输出迭代器
    struct msg_appender
    {
        operating_message* self;
        using iterator_category = std::output_iterator_tag;
        using value_type = void;
        using difference_type = std::ptrdiff_t;
        using pointer = void;
        using reference = void;

        msg_appender& operator=(char c)
        {
            self->append_char(c);
            return *this;
        }
        msg_appender& operator*() { return *this; }
        msg_appender& operator++() { return *this; }
        msg_appender operator++(int) { return *this; }
    };

    // 单参数类型分派
    template<typename T>
    FORCE_INLINE void append_arg(T&& v) noexcept
    {
        using U = std::remove_cvref_t<T>;
        if constexpr (std::is_convertible_v<U, std::string_view>)
        {
            std::string_view sv = v;
            append(sv.data(), sv.size());
        }
        else if constexpr (std::is_integral_v<U>)
        {
            char buf[32];
            auto r = std::to_chars(buf, buf + sizeof(buf), v);
            append(buf, static_cast<size_t>(r.ptr - buf));
        }
        else if constexpr (std::is_floating_point_v<U>)
        {
            char buf[64];
            auto r = std::to_chars(buf, buf + sizeof(buf), v);
            append(buf, static_cast<size_t>(r.ptr - buf));
        }
        else
        {
            std::format_to(msg_appender{this}, "{}", std::forward<T>(v));
        }
    }

    // 标签扫描结果
    struct tag_scan_ctx
    {
        msg_level lv = msg_level::info;
        uint32_t code = 0;
        bool has_code = false;
        const msg::loc_tag* loc = nullptr;
    };

    // Pass 1: 结构标签提取
    template<typename T>
    FORCE_INLINE static void scan_tag_(T& v, tag_scan_ctx& ctx) noexcept
    {
        using U = std::remove_cvref_t<T>;
        if constexpr (std::same_as<U, msg::level_tag>)
        {
            ctx.lv = v.lv;
        }
        else if constexpr (std::same_as<U, msg::code_tag>)
        {
            ctx.code = v.code;
            ctx.has_code = true;
        }
        else if constexpr (std::same_as<U, msg::loc_tag>)
        {
            ctx.loc = &v;
        }
    }

    // Pass 2: 单参数写入
    template<typename T>
    FORCE_INLINE void emit_one_(T&& v)
    {
        using U = std::remove_cvref_t<T>;
        if constexpr (is_msg_tag_v<U>)
        {
        }
        else if constexpr (is_msg_fmt_v<U>)
        {
            std::apply([&](auto&&... as) {
                if constexpr (U::kind == 1)
                {
                    std::format_to(msg_appender{this}, v.fs, std::move(as)...);
                }
                else
                {
                    if (validate_format(v.sv, sizeof...(as)))
                    {
                        std::vformat_to(msg_appender{this}, v.sv,
                                        std::make_format_args(as...));
                    }
                    else
                    {
                        append(v.sv.data(), v.sv.size());
                    }
                }
            }, v.args);
        }
        else if constexpr (is_msg_lazy_v<U>)
        {
            append_arg(v.fn());
        }
        else
        {
            append_arg(std::forward<T>(v));
        }
    }

    void write_loc_prefix_(const msg::loc_tag& tag) noexcept
    {
        std::string_view file = path_basename(tag.loc.file_name());
        append_char('[');
        append(file.data(), file.size());
        append_char(':');
        char buf[16];
        auto r = std::to_chars(buf, buf + sizeof(buf), tag.loc.line());
        append(buf, static_cast<size_t>(r.ptr - buf));
        append("] ", 2);
    }

    [[nodiscard]] static constexpr std::string_view path_basename(const char* path) noexcept
    {
        if (!path)
        {
            return {};
        }
        std::string_view sv(path);
        size_t pos = sv.find_last_of("/\\");
        return (pos == std::string_view::npos) ? sv : sv.substr(pos + 1);
    }

public:
    ~operating_message() noexcept
    {
        free_heap_buffer();
    }

    operating_message() noexcept
    {
        storage_.heap_ptr = nullptr;
    }

    operating_message(operating_message&& other) noexcept
        : storage_(other.storage_)
        , size_(other.size_)
        , capacity_(other.capacity_)
        , switch_(other.switch_)
        , min_level_(other.min_level_)
        , code_(other.code_)
    {
        other.storage_.heap_ptr = nullptr;
        other.size_ = 0;
        other.capacity_ = SSO_SIZE;
    }

    operating_message& operator=(operating_message&& other) noexcept
    {
        if (this != &other)
        {
            free_heap_buffer();
            storage_ = other.storage_;
            size_ = other.size_;
            capacity_ = other.capacity_;
            switch_ = other.switch_;
            min_level_ = other.min_level_;
            code_ = other.code_;
            other.storage_.heap_ptr = nullptr;
            other.size_ = 0;
            other.capacity_ = SSO_SIZE;
        }
        return *this;
    }

    operating_message(const operating_message& other) noexcept
        : storage_()
        , size_(0)
        , capacity_(SSO_SIZE)
        , switch_(other.switch_)
        , min_level_(other.min_level_)
        , code_(other.code_)
    {
        if (other.size_ > 0)
        {
            grow_to(other.size_);
            std::memcpy(data_ptr(), other.data_ptr(), other.size_);
            size_ = other.size_;
        }
    }

    operating_message& operator=(const operating_message& other) noexcept
    {
        if (this != &other)
        {
            free_heap_buffer();
            size_ = 0;
            capacity_ = SSO_SIZE;
            switch_ = other.switch_;
            min_level_ = other.min_level_;
            code_ = other.code_;
            if (other.size_ > 0)
            {
                grow_to(other.size_);
                std::memcpy(data_ptr(), other.data_ptr(), other.size_);
                size_ = other.size_;
            }
        }
        return *this;
    }

    [[nodiscard]] constexpr operator bool() const noexcept
    {
        return switch_;
    }

    void reset() noexcept
    {
        free_heap_buffer();
        size_ = 0;
        switch_ = true;
        code_ = 0;
    }

    void clear_message() noexcept
    {
        size_ = 0;
    }

    constexpr void set_switch_bool(bool sw) noexcept { switch_ = sw; }
    [[nodiscard]] constexpr bool& get_switch_bool() noexcept { return switch_; }
    [[nodiscard]] constexpr const bool& get_switch_bool() const noexcept { return switch_; }

    constexpr void update_switch(bool sw) noexcept
    {
        switch_ = switch_ && sw;
    }

    constexpr void set_min_level(msg_level lv) noexcept { min_level_ = static_cast<uint8_t>(lv); }
    [[nodiscard]] constexpr msg_level get_min_level() const noexcept { return static_cast<msg_level>(min_level_); }

    constexpr void set_code(uint32_t c) noexcept { code_ = c; }
    [[nodiscard]] constexpr uint32_t code() const noexcept { return code_; }
    [[nodiscard]] constexpr bool is_code(uint32_t c) const noexcept { return code_ == c; }

    void reserve(size_t cap) noexcept
    {
        if (cap > capacity_)
        {
            grow_to(cap);
        }
    }

    [[nodiscard]] constexpr size_t capacity() const noexcept { return capacity_; }
    [[nodiscard]] constexpr size_t message_size() const noexcept { return size_; }

    operating_message& operator+=(std::string_view sv) noexcept
    {
        if (message_recording_enabled())
        {
            append(sv.data(), sv.size());
        }
        return *this;
    }

    operating_message& operator+=(operating_message&& other) noexcept
    {
        if (message_recording_enabled())
        {
            append(other.data_ptr(), other.size_);
        }
        switch_ = switch_ && other.switch_;
        return *this;
    }

    operating_message& operator+=(const operating_message& other) noexcept
    {
        if (message_recording_enabled())
        {
            append(other.data_ptr(), other.size_);
        }
        switch_ = switch_ && other.switch_;
        return *this;
    }

    friend std::ostream& operator<<(std::ostream& os, const operating_message& msg)
    {
        os.write(msg.data_ptr(), msg.size_);
        return os;
    }

    [[nodiscard]] std::string_view read_message() const noexcept
    {
        return std::string_view(data_ptr(), size_);
    }

    // write: 唯一写入入口
    //   write(sw, args...)  sw 调整成败 (粘性 false)
    //   write(args...)      省略 = 成功
    // 标签: msg::debug/info/warn/error | om_err_* / msg::errc | msg::here
    //       msg::fmt / msg::fmt_rt / msg::defer
    template<typename... Args>
    FORCE_INLINE operating_message& write(bool sw, Args&&... args)
    {
        switch_ = switch_ && sw;

        tag_scan_ctx ctx;
        (scan_tag_(args, ctx), ...);

        // 失败设码, 成功清零 (不受记录开关与级别过滤影响)
        if (ctx.has_code)
        {
            code_ = sw ? 0 : ctx.code;
        }

        if (message_recording_enabled() && static_cast<uint8_t>(ctx.lv) >= min_level_)
        {
            if (ctx.loc)
            {
                write_loc_prefix_(*ctx.loc);
            }
            if (ctx.lv != msg_level::info)
            {
                const uint8_t idx = static_cast<uint8_t>(ctx.lv);
                append(k_level_prefix[idx].data(), k_level_prefix[idx].size());
            }
            (emit_one_(std::forward<Args>(args)), ...);
            append_char('\n');
        }
        return *this;
    }

    template<typename... Args>
    FORCE_INLINE operating_message& write(Args&&... args)
    {
        return write(true, std::forward<Args>(args)...);
    }

    // 前置条件检查: 失败写入并返回 false
    template<typename... Args>
    [[nodiscard]] FORCE_INLINE bool check(bool cond, Args&&... args)
    {
        if (cond)
        {
            return true;
        }
        write(false, std::forward<Args>(args)...);
        return false;
    }
};

// 测试断言与调试辅助
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
        __builtin_trap();
#endif
    }
}

// RAII 守卫
struct message_recording_guard
{
    bool old_;
    explicit message_recording_guard(bool enable) noexcept
        : old_(message_recording_enabled())
    {
        message_recording_enabled() = enable;
    }
    ~message_recording_guard() noexcept
    {
        message_recording_enabled() = old_;
    }
    message_recording_guard(const message_recording_guard&) = delete;
    message_recording_guard& operator=(const message_recording_guard&) = delete;
};

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

// 消息快照
struct message_snapshot
{
    size_t size_;
    bool switch_;
    uint32_t code_;
};

[[nodiscard]] inline message_snapshot snapshot_of(const operating_message& om) noexcept
{
    return message_snapshot{om.message_size(), (bool)om, om.code()};
}

// 提取快照后追加的内容
[[nodiscard]] inline std::string_view appended_since(
    const operating_message& om, const message_snapshot& snap) noexcept
{
    if (om.message_size() <= snap.size_)
    {
        return {};
    }
    std::string_view full = om.read_message();
    return full.substr(snap.size_);
}

// 缩进级别 (全局, 单线程)
[[nodiscard]] inline uint8_t& om_indent_level() noexcept
{
    static uint8_t level = 0;
    return level;
}

struct om_indent
{
    om_indent() noexcept
    {
        ++om_indent_level();
    }
    ~om_indent() noexcept
    {
        --om_indent_level();
    }
    om_indent(const om_indent&) = delete;
    om_indent& operator=(const om_indent&) = delete;
};

// 超阈值清空
inline void om_clear_if_over(operating_message& om, size_t threshold) noexcept
{
    if (om.message_size() > threshold)
    {
        om.clear_message();
    }
}
