#pragma once

// operating_message.hpp - 操作消息 (SSO 缓冲 + 堆扩容 + 错误码 + 位置追踪)
// 无命名空间, 通用工具

#include <string_view>
#include <format>
#include <ostream>
#include <cstdint>
#include <charconv>
#include <type_traits>
#include <cstring>
#include <cstdlib>
#include <source_location>
#include <iterator>
#include "fnv1a.hpp"
#include "force_inline.hpp"

// 消息写入开关: Release 构建 (NDEBUG) 默认高性能零开销, Debug 构建默认写消息
// OM_MSG 宏在编译期根据 LCF_RELEASE_MESSAGES 选择展开, 运行时函数无法影响宏展开
#ifdef NDEBUG
#define LCF_RELEASE_MESSAGES 1
#else
#define LCF_RELEASE_MESSAGES 0
#endif

// 全局消息记录开关 (运行时控制)
// Release 构建 (NDEBUG) 默认 false (高性能); Debug 构建默认 true, 运行时可调
// 仅影响 write_message* 系列函数, 不影响 OM_MSG 宏的编译期展开
inline bool& message_recording_enabled() noexcept
{
    static bool enabled = !static_cast<bool>(LCF_RELEASE_MESSAGES);
    return enabled;
}

// 兼容别名 (已废弃, 使用 message_recording_enabled())
[[deprecated("use message_recording_enabled()")]]
inline bool& debug_messages_enabled() noexcept
{
    return message_recording_enabled();
}

// 兼容别名 (已废弃, 使用 message_recording_enabled())
[[deprecated("use message_recording_enabled()")]]
inline bool& ecs_debug_messages() noexcept
{
    return message_recording_enabled();
}

enum class msg_level : uint8_t
{
    debug = 0,
    info  = 1,
    warn  = 2,
    error = 3
};

// 错误码常量 (fnv1a 低 16 位)
inline constexpr uint16_t om_err_none              = 0;
inline constexpr uint16_t om_err_type_mismatch     = static_cast<uint16_t>(fnv1a_consteval("type_mismatch"));
inline constexpr uint16_t om_err_invalid_entity    = static_cast<uint16_t>(fnv1a_consteval("invalid_entity"));
inline constexpr uint16_t om_err_version_mismatch  = static_cast<uint16_t>(fnv1a_consteval("version_mismatch"));
inline constexpr uint16_t om_err_out_of_range      = static_cast<uint16_t>(fnv1a_consteval("out_of_range"));
inline constexpr uint16_t om_err_null_pointer      = static_cast<uint16_t>(fnv1a_consteval("null_pointer"));
inline constexpr uint16_t om_err_capacity_exceeded = static_cast<uint16_t>(fnv1a_consteval("capacity_exceeded"));
inline constexpr uint16_t om_err_not_found         = static_cast<uint16_t>(fnv1a_consteval("not_found"));
inline constexpr uint16_t om_err_already_exists    = static_cast<uint16_t>(fnv1a_consteval("already_exists"));
inline constexpr uint16_t om_err_out_of_memory     = static_cast<uint16_t>(fnv1a_consteval("out_of_memory"));

// 运行时格式串校验: 占位符数量匹配 + 基本语法合法
// 不校验类型匹配 (如 {:f} 配 int), 类型错误仍可能触发 format_error
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

    // SSO 缓冲 + 堆指针 union
    union msg_storage
    {
        char sso_buf[SSO_SIZE];
        char* heap_ptr;
    };

    msg_storage storage_{};
    uint32_t size_{0};
    uint32_t capacity_{SSO_SIZE};
    bool switch_{true};
    uint8_t min_level_{static_cast<uint8_t>(msg_level::info)};
    uint16_t code_{0};

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

    // 释放堆缓冲, 回到 SSO 模式
    void free_heap_buffer() noexcept
    {
        if (!is_sso())
        {
            ::operator delete(storage_.heap_ptr);
            storage_ = {};
            capacity_ = SSO_SIZE;
        }
    }

    // 扩容到 need 字节 (SSO → 堆)
    void grow_to(size_t need) noexcept
    {
        if (need <= capacity_)
        {
            return;
        }

        size_t new_cap = need;
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

    // 追加原始字节
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
        asm volatile("" : "+r"(dst));  // 切断对象大小追踪, 避免 SSO union 触发 -Wstringop-overflow 误报
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

    // std::format_to 输出迭代器
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

    template<typename T>
    void append_arg(T&& v) noexcept
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

    // 检测 fmt 是否只含简单 {} 占位符
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
                if (i + 1 < sz && fmt[i + 1] == '{')
                {
                    return false;
                }
                if (i + 1 >= sz || fmt[i + 1] != '}')
                {
                    return false;
                }
                ++count;
                i += 2;
            }
            else if (c == '}')
            {
                if (i + 1 < sz && fmt[i + 1] == '}')
                {
                    return false;
                }
                return false;
            }
            else
            {
                ++i;
            }
        }
        return count == expected;
    }

    // 解析简单 {} 占位符 fmt
    template<typename... Args>
    void write_fmt_simple(std::string_view fmt, Args&&... args) noexcept
    {
        size_t pos = 0;
        const size_t sz = fmt.size();
        auto emit_literal = [&](size_t end) {
            if (end > pos)
            {
                append(fmt.data() + pos, end - pos);
            }
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

    // fast path 分派
    template<typename... Args>
    void dispatch_fmt(std::string_view fmt_sv, std::format_string<Args...> fmt, Args&&... args)
    {
        if constexpr (sizeof...(Args) > 0)
        {
            if (is_simple_fmt(fmt_sv, sizeof...(Args)))
            {
                size_t estimated = static_cast<size_t>(size_) + fmt_sv.size() + sizeof...(Args) * 32;
                if (estimated > capacity_)
                {
                    grow_to(estimated);
                }
                write_fmt_simple(fmt_sv, std::forward<Args>(args)...);
            }
            else
            {
                std::format_to(msg_appender{this}, fmt, std::forward<Args>(args)...);
            }
        }
        else
        {
            std::format_to(msg_appender{this}, fmt);
        }
    }

    // 运行时分派: fmt 为运行时 string_view, 不经编译期校验
    // fast path 复用 is_simple_fmt + write_fmt_simple; slow path 用 validate_format + vformat_to
    template<typename... Args>
    void dispatch_fmt_runtime(std::string_view fmt, Args&&... args)
    {
        if constexpr (sizeof...(Args) > 0)
        {
            if (is_simple_fmt(fmt, sizeof...(Args)))
            {
                size_t estimated = static_cast<size_t>(size_) + fmt.size() + sizeof...(Args) * 32;
                if (estimated > capacity_)
                {
                    grow_to(estimated);
                }
                write_fmt_simple(fmt, std::forward<Args>(args)...);
            }
            else
            {
                if (validate_format(fmt, sizeof...(Args)))
                {
                    std::vformat_to(msg_appender{this}, fmt,
                        std::make_format_args(args...));
                }
                else
                {
                    std::abort();
                }
            }
        }
        else
        {
            append(fmt.data(), fmt.size());
        }
    }

    // 文件名 basename
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

    operating_message() noexcept = default;

    operating_message(operating_message&& other) noexcept
        : storage_(other.storage_)
        , size_(other.size_)
        , capacity_(other.capacity_)
        , switch_(other.switch_)
        , min_level_(other.min_level_)
        , code_(other.code_)
    {
        other.storage_ = {};
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
            other.storage_ = {};
            other.size_ = 0;
            other.capacity_ = SSO_SIZE;
        }
        return *this;
    }

    operating_message(const operating_message& other) noexcept
        : storage_{}
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

    // 只更新 switch_ (release 模式 OM_MSG 宏专用, 保留操作成功/失败语义)
    constexpr void update_switch(bool sw) noexcept
    {
        switch_ = switch_ && sw;
    }

    constexpr void set_min_level(msg_level lv) noexcept { min_level_ = static_cast<uint8_t>(lv); }
    [[nodiscard]] constexpr msg_level get_min_level() const noexcept { return static_cast<msg_level>(min_level_); }

    // 错误码接口
    constexpr void set_code(uint16_t c) noexcept { code_ = c; }
    [[nodiscard]] constexpr uint16_t code() const noexcept { return code_; }
    [[nodiscard]] constexpr bool is_code(uint16_t c) const noexcept { return code_ == c; }

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

    // === 基础写入 ===

    template<typename... Args>
    void write_message(bool sw, Args&&... args)
    {
        switch_ = switch_ && sw;
        if (!message_recording_enabled())
        {
            return;
        }
        if (static_cast<uint8_t>(msg_level::info) < min_level_)
        {
            return;
        }
        (append_arg(std::forward<Args>(args)), ...);
        append_char('\n');
    }

    template<typename... Args>
    void write_message_level(msg_level lv, bool sw, Args&&... args)
    {
        switch_ = switch_ && sw;
        if (!message_recording_enabled())
        {
            return;
        }
        const uint8_t idx = static_cast<uint8_t>(lv);
        if (idx < min_level_)
        {
            return;
        }
        append(k_level_prefix[idx].data(), k_level_prefix[idx].size());
        (append_arg(std::forward<Args>(args)), ...);
        append_char('\n');
    }

    template<typename... Args>
    void write_message_fmt(bool sw, std::format_string<Args...> fmt, Args&&... args)
    {
        switch_ = switch_ && sw;
        if (!message_recording_enabled())
        {
            return;
        }
        if (static_cast<uint8_t>(msg_level::info) < min_level_)
        {
            return;
        }
        dispatch_fmt(fmt.get(), fmt, std::forward<Args>(args)...);
        append_char('\n');
    }

    template<typename... Args>
    void write_message_fmt_level(msg_level lv, bool sw, std::format_string<Args...> fmt, Args&&... args)
    {
        switch_ = switch_ && sw;
        if (!message_recording_enabled())
        {
            return;
        }
        const uint8_t idx = static_cast<uint8_t>(lv);
        if (idx < min_level_)
        {
            return;
        }
        append(k_level_prefix[idx].data(), k_level_prefix[idx].size());
        dispatch_fmt(fmt.get(), fmt, std::forward<Args>(args)...);
        append_char('\n');
    }

    // === 错误码写入 ===

    template<typename... Args>
    void write_message_code(uint16_t err_code, bool sw, Args&&... args)
    {
        if (!sw)
        {
            code_ = err_code;
        }
        write_message(sw, std::forward<Args>(args)...);
    }

    template<typename... Args>
    void write_message_code_level(uint16_t err_code, msg_level lv, bool sw, Args&&... args)
    {
        if (!sw)
        {
            code_ = err_code;
        }
        write_message_level(lv, sw, std::forward<Args>(args)...);
    }

    // === source_location 写入 ===

    // === source_location 写入 ===
    // loc 需显式传入 std::source_location::current()

    template<typename... Args>
    void write_message_loc(bool sw, std::source_location loc, Args&&... args)
    {
        switch_ = switch_ && sw;
        if (!message_recording_enabled())
        {
            return;
        }
        if (static_cast<uint8_t>(msg_level::info) < min_level_)
        {
            return;
        }
        append_char('[');
        std::string_view file = path_basename(loc.file_name());
        append(file.data(), file.size());
        append_char(':');
        char buf[16];
        auto r = std::to_chars(buf, buf + sizeof(buf), loc.line());
        append(buf, static_cast<size_t>(r.ptr - buf));
        append("] ", 2);
        (append_arg(std::forward<Args>(args)), ...);
        append_char('\n');
    }

    template<typename... Args>
    void write_message_code_loc(uint16_t err_code, bool sw, std::source_location loc, Args&&... args)
    {
        if (!sw)
        {
            code_ = err_code;
        }
        switch_ = switch_ && sw;
        if (!message_recording_enabled())
        {
            return;
        }
        if (static_cast<uint8_t>(msg_level::info) < min_level_)
        {
            return;
        }
        append_char('[');
        std::string_view file = path_basename(loc.file_name());
        append(file.data(), file.size());
        append_char(':');
        char buf[16];
        auto r = std::to_chars(buf, buf + sizeof(buf), loc.line());
        append(buf, static_cast<size_t>(r.ptr - buf));
        append("] ", 2);
        (append_arg(std::forward<Args>(args)), ...);
        append_char('\n');
    }

    // === 运行时格式化 (fmt 为运行时 string_view, 经 validate_format 校验) ===

    template<typename... Args>
    void write_message_fmt_runtime(bool sw, std::string_view fmt, Args&&... args)
    {
        switch_ = switch_ && sw;
        if (!message_recording_enabled())
        {
            return;
        }
        if (static_cast<uint8_t>(msg_level::info) < min_level_)
        {
            return;
        }
        dispatch_fmt_runtime(fmt, std::forward<Args>(args)...);
        append_char('\n');
    }

    template<typename... Args>
    void write_message_fmt_runtime_level(msg_level lv, bool sw,
                                         std::string_view fmt, Args&&... args)
    {
        switch_ = switch_ && sw;
        if (!message_recording_enabled())
        {
            return;
        }
        const uint8_t idx = static_cast<uint8_t>(lv);
        if (idx < min_level_)
        {
            return;
        }
        append(k_level_prefix[idx].data(), k_level_prefix[idx].size());
        dispatch_fmt_runtime(fmt, std::forward<Args>(args)...);
        append_char('\n');
    }

    template<typename... Args>
    void write_message_fmt_runtime_code(uint16_t err_code, bool sw,
                                        std::string_view fmt, Args&&... args)
    {
        if (!sw)
        {
            code_ = err_code;
        }
        write_message_fmt_runtime(sw, fmt, std::forward<Args>(args)...);
    }

    template<typename... Args>
    void write_message_fmt_runtime_code_level(uint16_t err_code, msg_level lv, bool sw,
                                              std::string_view fmt, Args&&... args)
    {
        if (!sw)
        {
            code_ = err_code;
        }
        write_message_fmt_runtime_level(lv, sw, fmt, std::forward<Args>(args)...);
    }

    template<typename... Args>
    void write_message_fmt_runtime_loc(bool sw, std::source_location loc,
                                       std::string_view fmt, Args&&... args)
    {
        switch_ = switch_ && sw;
        if (!message_recording_enabled())
        {
            return;
        }
        if (static_cast<uint8_t>(msg_level::info) < min_level_)
        {
            return;
        }
        append_char('[');
        std::string_view file = path_basename(loc.file_name());
        append(file.data(), file.size());
        append_char(':');
        char buf[16];
        auto r = std::to_chars(buf, buf + sizeof(buf), loc.line());
        append(buf, static_cast<size_t>(r.ptr - buf));
        append("] ", 2);
        dispatch_fmt_runtime(fmt, std::forward<Args>(args)...);
        append_char('\n');
    }

    template<typename... Args>
    void write_message_fmt_runtime_code_loc(uint16_t err_code, bool sw,
                                            std::source_location loc,
                                            std::string_view fmt, Args&&... args)
    {
        if (!sw)
        {
            code_ = err_code;
        }
        write_message_fmt_runtime_loc(sw, loc, fmt, std::forward<Args>(args)...);
    }
};

// === 热路径专用宏: Release 构建零开销 (不求值消息参数, 只更新 switch_) ===
// 用法: OM_MSG(result, false, "错误: ", idx);   // 直接传整型, append_arg 用 to_chars 零分配写入
// Release (NDEBUG): 展开为 (result).update_switch(false)  — 参数不求值, 零开销
// Debug          : 展开为 (result).write_message(false, "错误: ", idx)  — 完整写入
// 注意: 整型直接传入, 勿用 std::to_string 包装 (debug 模式会额外 std::string 分配)
#if LCF_RELEASE_MESSAGES
    #define OM_MSG(om, sw, ...) (om).update_switch(sw)
#else
    #define OM_MSG(om, sw, ...) (om).write_message(sw, __VA_ARGS__)
#endif

// =============================================================================
// 高级功能与调试辅助 (基于 operating_message 现有接口组合, 零侵入)
// P0 段 (守卫/快照/便捷方法/宏) 留在此处; P1/P2 段依赖容器与时间模块,
// 已分离至 om_extensions.hpp (本模块保持零容器依赖)
// =============================================================================

// === P0: RAII 守卫 ===

// 消息记录守卫: 作用域内临时开启/关闭 message_recording_enabled, 析构自动恢复
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

// 级别守卫: 作用域内临时调整 om 的 min_level_, 析构自动恢复
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

// === P0: 消息快照与差异提取 ===

// 消息快照: 记录某时刻的 size/switch/code, 用于定位后续追加内容
struct message_snapshot
{
    size_t size_;
    bool switch_;
    uint16_t code_;
};

[[nodiscard]] inline message_snapshot snapshot_of(const operating_message& om) noexcept
{
    return message_snapshot{om.message_size(), (bool)om, om.code()};
}

// 提取快照之后追加的消息内容 (零拷贝 string_view)
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

// === P0: 调试断点辅助宏 ===

#if defined(_MSC_VER)
    #define LCF_DEBUG_BREAK() __debugbreak()
#else
    #define LCF_DEBUG_BREAK() __builtin_trap()
#endif

// 失败时追加诊断消息并触发调试器中断
#define LCF_DEBUG_MSG(om, ...) \
    do { \
        if (!(om)) \
        { \
            (om).write_message_fmt_runtime(true, __VA_ARGS__); \
            LCF_DEBUG_BREAK(); \
        } \
    } while (0)

// === P0: 级别便捷方法 (薄封装, 提升调用方可读性) ===

template<typename... Args>
void om_debug(operating_message& om, bool sw, Args&&... args) noexcept
{
    om.write_message_level(msg_level::debug, sw, std::forward<Args>(args)...);
}

template<typename... Args>
void om_info(operating_message& om, bool sw, Args&&... args) noexcept
{
    om.write_message_level(msg_level::info, sw, std::forward<Args>(args)...);
}

template<typename... Args>
void om_warn(operating_message& om, bool sw, Args&&... args) noexcept
{
    om.write_message_level(msg_level::warn, sw, std::forward<Args>(args)...);
}

template<typename... Args>
void om_error(operating_message& om, bool sw, Args&&... args) noexcept
{
    om.write_message_level(msg_level::error, sw, std::forward<Args>(args)...);
}

// === P0: KV 风格日志 ===

// 写入 key=value 形式消息 (复用 write_message 多参数拼接)
template<typename V>
void om_kv(operating_message& om, std::string_view key, V&& val) noexcept
{
    om.write_message(true, key, "=", std::forward<V>(val));
}

// === P0: 进度消息 ===

inline void om_progress(operating_message& om, size_t cur, size_t total,
                        std::string_view task) noexcept
{
    om.write_message_fmt_runtime(true, "[{}/{}] {}", cur, total, task);
}

// 消息前缀栈 (om_prefix_stack/om_prefix/om_write_prefixed) 依赖 dense,
// 已分离至 om_extensions.hpp

// === P0: 消息分组缩进 ===

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

// 带缩进写入: 追加 2*level 个空格前缀后调用 write_message
template<typename... Args>
void om_write_indented(operating_message& om, bool sw, Args&&... args) noexcept
{
    for (uint8_t i = 0; i < om_indent_level(); ++i)
    {
        om += "  ";
    }
    om.write_message(sw, std::forward<Args>(args)...);
}

// === P0: 体积监控 (超阈值自动清空, 防止无限增长) ===

inline void om_clear_if_over(operating_message& om, size_t threshold) noexcept
{
    if (om.message_size() > threshold)
    {
        om.clear_message();
    }
}

// === P0: 断言式写入宏 (前置条件检查) ===

#define LCF_CHECK(om, cond, err_code, ...) \
    do { \
        if (!(cond)) \
        { \
            (om).write_message_fmt_runtime_code(err_code, false, __VA_ARGS__); \
            return om; \
        } \
    } while (0)

// === P0: 测试断言宏 ===

#define LCF_EXPECT_OK(om) \
    do { \
        if (!(om)) \
        { \
            std::printf("FAIL: %s:%d 期望成功, code=%u msg=%.*s\n", \
                __FILE__, __LINE__, static_cast<unsigned>((om).code()), \
                static_cast<int>((om).message_size()), (om).read_message().data()); \
            std::abort(); \
        } \
    } while (0)

#define LCF_EXPECT_CODE(om, expected) \
    do { \
        if (!(om).is_code(expected)) \
        { \
            std::printf("FAIL: %s:%d 期望 code=%u, 实际 code=%u\n", \
                __FILE__, __LINE__, static_cast<unsigned>(expected), \
                static_cast<unsigned>((om).code())); \
            std::abort(); \
        } \
    } while (0)

#define LCF_EXPECT_FAIL(om) \
    do { \
        if ((om)) \
        { \
            std::printf("FAIL: %s:%d 期望失败但成功了\n", __FILE__, __LINE__); \
            std::abort(); \
        } \
    } while (0)
