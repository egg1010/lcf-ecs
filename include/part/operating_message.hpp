#pragma once

// operating_message.hpp - 操作消息 (SSO 缓冲 + slab 分配 + 错误码 + 位置追踪)
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
#include "slab_allocator.hpp"
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

// slab 池 (256 字节块, 覆盖溢出消息)
inline slab_allocator& om_slab_pool() noexcept
{
    static slab_allocator pool(256, 8, 64);
    return pool;
}

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
    static constexpr size_t SLAB_BLOCK = 256;

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
    bool is_large_{false};
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
            if (is_large_)
            {
                ::operator delete(storage_.heap_ptr);
            }
            else
            {
                om_slab_pool().deallocate(storage_.heap_ptr);
            }
            storage_ = {};
            capacity_ = SSO_SIZE;
            is_large_ = false;
        }
    }

    // 扩容到 need 字节 (SSO → slab → large)
    void grow_to(size_t need) noexcept
    {
        if (need <= capacity_)
        {
            return;
        }

        size_t new_cap;
        bool new_large;
        char* new_buf;

        if (need > SLAB_BLOCK)
        {
            new_cap = need;
            new_large = true;
            new_buf = static_cast<char*>(::operator new(new_cap, std::nothrow));
        }
        else
        {
            new_cap = SLAB_BLOCK;
            new_large = false;
            new_buf = static_cast<char*>(om_slab_pool().allocate());
        }

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
        is_large_ = new_large;
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
        , is_large_(other.is_large_)
        , min_level_(other.min_level_)
        , code_(other.code_)
    {
        other.storage_ = {};
        other.size_ = 0;
        other.capacity_ = SSO_SIZE;
        other.is_large_ = false;
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
            is_large_ = other.is_large_;
            min_level_ = other.min_level_;
            code_ = other.code_;
            other.storage_ = {};
            other.size_ = 0;
            other.capacity_ = SSO_SIZE;
            other.is_large_ = false;
        }
        return *this;
    }

    operating_message(const operating_message& other) noexcept
        : storage_{}
        , size_(0)
        , capacity_(SSO_SIZE)
        , switch_(other.switch_)
        , is_large_(false)
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
            is_large_ = false;
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
// 全部 noexcept, 无异常; 无 std 容器; 复用 dense/ring_buffer/time/fnv1a
// =============================================================================

#include "dense.hpp"
#include "ring_buffer.hpp"
#include "time.hpp"

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

// === P0: 消息前缀栈 (模块化前缀, RAII 推入/弹出) ===

[[nodiscard]] inline dense<std::string_view>& om_prefix_stack() noexcept
{
    static dense<std::string_view> s;
    return s;
}

struct om_prefix
{
    om_prefix(std::string_view prefix) noexcept
    {
        dense<std::string_view>& s = om_prefix_stack();
        s.increase_capacity(s.size() + 1);
        s.push_back(prefix);
    }
    ~om_prefix() noexcept
    {
        om_prefix_stack().pop_back();
    }
    om_prefix(const om_prefix&) = delete;
    om_prefix& operator=(const om_prefix&) = delete;
};

// 带前缀写入: 依次追加所有前缀后调用 write_message
template<typename... Args>
void om_write_prefixed(operating_message& om, bool sw, Args&&... args) noexcept
{
    const dense<std::string_view>& s = om_prefix_stack();
    for (size_t i = 0; i < s.size(); ++i)
    {
        om += s[i];
    }
    om.write_message(sw, std::forward<Args>(args)...);
}

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

// === P1: 错误码映射表 (code → 可读名/描述) ===

struct om_error_entry
{
    uint16_t code;
    std::string_view name;
    std::string_view description;
};

[[nodiscard]] inline const dense<om_error_entry>& om_error_table() noexcept
{
    static const dense<om_error_entry> table = []() {
        dense<om_error_entry> t;
        t.reserve_exact(16);
        t.push_back({om_err_type_mismatch,    "type_mismatch",    "类型不匹配"});
        t.push_back({om_err_invalid_entity,   "invalid_entity",   "实体无效"});
        t.push_back({om_err_version_mismatch, "version_mismatch", "版本不匹配"});
        t.push_back({om_err_out_of_range,     "out_of_range",     "索引越界"});
        t.push_back({om_err_null_pointer,     "null_pointer",     "空指针"});
        t.push_back({om_err_capacity_exceeded,"capacity_exceeded","容量超限"});
        t.push_back({om_err_not_found,        "not_found",        "未找到"});
        t.push_back({om_err_already_exists,   "already_exists",   "已存在"});
        return t;
    }();
    return table;
}

[[nodiscard]] inline std::string_view om_error_name(uint16_t code) noexcept
{
    const dense<om_error_entry>& t = om_error_table();
    for (size_t i = 0; i < t.size(); ++i)
    {
        if (t[i].code == code)
        {
            return t[i].name;
        }
    }
    return "unknown";
}

[[nodiscard]] inline std::string_view om_error_desc(uint16_t code) noexcept
{
    const dense<om_error_entry>& t = om_error_table();
    for (size_t i = 0; i < t.size(); ++i)
    {
        if (t[i].code == code)
        {
            return t[i].description;
        }
    }
    return "未知错误";
}

// === P1: 一次性消息去重 (同 key 仅输出一次, 避免刷屏) ===

[[nodiscard]] inline dense<uint64_t>& om_dedup_set() noexcept
{
    static dense<uint64_t> s;
    return s;
}

// 返回 true 表示首次 (应输出), false 表示重复 (应跳过)
[[nodiscard]] inline bool om_once(std::string_view key) noexcept
{
    uint64_t h = fnv1a_runtime(key.data(), key.size());
    dense<uint64_t>& set = om_dedup_set();
    for (size_t i = 0; i < set.size(); ++i)
    {
        if (set[i] == h)
        {
            return false;
        }
    }
    set.increase_capacity(set.size() + 1);
    set.push_back(h);
    return true;
}

// 清空去重集合 (允许同 key 再次输出)
inline void om_dedup_clear() noexcept
{
    om_dedup_set().clear();
}

// === P1: 消息计数统计 ===

struct om_stats
{
    std::array<uint32_t, 4> count_by_level{};
    std::array<uint32_t, 4> size_by_level{};
    uint32_t success_count{0};
    uint32_t failure_count{0};

    void reset() noexcept
    {
        count_by_level = {};
        size_by_level = {};
        success_count = 0;
        failure_count = 0;
    }
};

[[nodiscard]] inline om_stats& global_om_stats() noexcept
{
    static om_stats s;
    return s;
}

// 记录一条操作结果到全局统计
inline void om_stats_record(const operating_message& om,
                            msg_level lv = msg_level::info) noexcept
{
    om_stats& s = global_om_stats();
    uint8_t idx = static_cast<uint8_t>(lv);
    if (idx < 4)
    {
        ++s.count_by_level[idx];
        s.size_by_level[idx] += static_cast<uint32_t>(om.message_size());
    }
    if (om)
    {
        ++s.success_count;
    }
    else
    {
        ++s.failure_count;
    }
}

// === P1: RAII 作用域计时器 (墙钟时间) ===

struct om_scope_timer
{
    operating_message& om_;
    const char* name_;
    stopwatch sw_;

    om_scope_timer(operating_message& om, const char* name) noexcept
        : om_(om), name_(name), sw_() {}

    ~om_scope_timer() noexcept
    {
        double us = sw_.us();
        om_.write_message_fmt_runtime(true, "[{}] 耗时 {:.1f}us", name_, us);
    }
    om_scope_timer(const om_scope_timer&) = delete;
    om_scope_timer& operator=(const om_scope_timer&) = delete;
};

// RAII 作用域计时器 (CPU 周期, 精度更高)
struct om_scope_cycles
{
    operating_message& om_;
    const char* name_;
    stopwatch sw_;

    om_scope_cycles(operating_message& om, const char* name) noexcept
        : om_(om), name_(name), sw_() {}

    ~om_scope_cycles() noexcept
    {
        uint64_t cyc = sw_.cycles();
        om_.write_message_fmt_runtime(true, "[{}] {} cycles", name_, cyc);
    }
    om_scope_cycles(const om_scope_cycles&) = delete;
    om_scope_cycles& operator=(const om_scope_cycles&) = delete;
};

// === P2: 消息历史记录 (ring_buffer 存储, 静态池复用) ===

// 历史记录条目: trivially copyable, ring_buffer 快路径零析构
struct om_record
{
    msg_level level;
    uint16_t code;
    uint16_t msg_len;
    char msg_buf[256];
};

using om_history = ring_buffer<om_record, 1024>;

// 推入一条消息到历史
inline void om_history_push(om_history& hist, const operating_message& om,
                            msg_level lv = msg_level::info) noexcept
{
    om_record rec;
    rec.level = lv;
    rec.code = om.code();
    std::string_view sv = om.read_message();
    rec.msg_len = static_cast<uint16_t>(sv.size() < 256 ? sv.size() : 256);
    std::memcpy(rec.msg_buf, sv.data(), rec.msg_len);
    (void)hist.push(rec);
}

// 排空历史, 逐条调用 handler(level, code, msg_view)
template<typename Func>
size_t om_history_flush(om_history& hist, Func&& handler) noexcept
{
    return hist.drain([&](const om_record& rec) {
        std::string_view sv(rec.msg_buf, rec.msg_len);
        handler(rec.level, rec.code, sv);
    });
}

// === P2: 消息订阅/回调 (包装器模式, 不修改 operating_message) ===

using om_sink_fn = void (*)(msg_level, uint16_t, std::string_view);

// 消息记录器: 包装 operating_message, 每次 write 后触发回调 + 推入历史
class om_logger
{
    operating_message om_;
    om_history* sink_;
    om_sink_fn callback_;

public:
    om_logger(om_history* sink = nullptr, om_sink_fn cb = nullptr) noexcept
        : sink_(sink), callback_(cb) {}

    operating_message& inner() noexcept { return om_; }

    // 代理 write_message_level, 写入后触发订阅
    template<typename... Args>
    void write(msg_level lv, bool sw, Args&&... args) noexcept
    {
        om_.write_message_level(lv, sw, std::forward<Args>(args)...);
        if (callback_)
        {
            callback_(lv, om_.code(), om_.read_message());
        }
        if (sink_)
        {
            om_history_push(*sink_, om_, lv);
        }
    }

    // 代理 write_message_fmt_runtime_level, 写入后触发订阅
    template<typename... Args>
    void write_fmt(msg_level lv, bool sw,
                   std::string_view fmt, Args&&... args) noexcept
    {
        om_.write_message_fmt_runtime_level(lv, sw, fmt,
                                            std::forward<Args>(args)...);
        if (callback_)
        {
            callback_(lv, om_.code(), om_.read_message());
        }
        if (sink_)
        {
            om_history_push(*sink_, om_, lv);
        }
    }

    [[nodiscard]] operator bool() const noexcept { return (bool)om_; }
    [[nodiscard]] uint16_t code() const noexcept { return om_.code(); }
    [[nodiscard]] std::string_view read_message() const noexcept { return om_.read_message(); }
    [[nodiscard]] operating_message&& take() noexcept { return std::move(om_); }
};

// === P2: 操作耗时统计 (离线 stats, 存全部样本) ===

class om_latency_tracker
{
    dense<double> samples_us_;

public:
    void reserve(size_t n) noexcept
    {
        samples_us_.reserve_exact(n);
    }

    // 测量一次操作耗时 (us)
    template<typename Func>
    void measure(Func&& fn) noexcept
    {
        stopwatch sw;
        fn();
        samples_us_.push_back(sw.us());
    }

    // 计算统计量 (会排序内部样本)
    [[nodiscard]] stats compute() const noexcept
    {
        if (samples_us_.empty())
        {
            return {};
        }
        return compute_stats(samples_us_);
    }

    void reset() noexcept { samples_us_.clear(); }
    [[nodiscard]] size_t count() const noexcept { return samples_us_.size(); }
};

// === P2: 在线延迟监控 (P² 分位数, O(1) 空间) ===

class om_latency_monitor
{
    p2_quantile p50_{0.50};
    p2_quantile p95_{0.95};
    p2_quantile p99_{0.99};

public:
    void record_us(double us) noexcept
    {
        p50_.add(us);
        p95_.add(us);
        p99_.add(us);
    }

    template<typename Func>
    void measure(Func&& fn) noexcept
    {
        stopwatch sw;
        fn();
        record_us(sw.us());
    }

    [[nodiscard]] double p50() const noexcept { return p50_.estimate(); }
    [[nodiscard]] double p95() const noexcept { return p95_.estimate(); }
    [[nodiscard]] double p99() const noexcept { return p99_.estimate(); }
    [[nodiscard]] size_t count() const noexcept { return p50_.count(); }

    void reset() noexcept
    {
        p50_.reset();
        p95_.reset();
        p99_.reset();
    }
};

// === P2: 延迟异常检测 (基于 P² p99 动态阈值, 复用 p2_quantile) ===
// 注: 以纳秒为单元, 样本不足 min_samples 时不判定

struct om_latency_anomaly_detector
{
    p2_quantile p99_{0.99};
    double multiplier = 3.0;
    size_t min_samples = 100;

    void add(double ns) noexcept { p99_.add(ns); }

    [[nodiscard]] double anomaly_threshold() const noexcept
    {
        return p99_.estimate() * multiplier;
    }

    [[nodiscard]] bool is_anomaly(double ns) const noexcept
    {
        if (p99_.count() < min_samples)
        {
            return false;
        }
        return ns > anomaly_threshold();
    }

    void reset() noexcept { p99_.reset(); }
};

[[nodiscard]] inline om_latency_anomaly_detector& om_anomaly_detector() noexcept
{
    static om_latency_anomaly_detector det;
    return det;
}

// 测量操作, 自动喂入异常检测器, 返回是否异常 (耗时单位: 纳秒)
template<typename Func>
bool om_measure_and_check(Func&& fn) noexcept
{
    stopwatch sw;
    fn();
    double ns = sw.ns();
    om_anomaly_detector().add(ns);
    return om_anomaly_detector().is_anomaly(ns);
}

// === P2: 错误恢复策略表 ===

using om_recovery_fn = bool (*)(const operating_message&);

struct om_recovery_entry
{
    uint16_t code;
    om_recovery_fn fn;
};

[[nodiscard]] inline dense<om_recovery_entry>& om_recovery_table() noexcept
{
    static dense<om_recovery_entry> t;
    return t;
}

inline void om_register_recovery(uint16_t code, om_recovery_fn fn) noexcept
{
    dense<om_recovery_entry>& t = om_recovery_table();
    t.increase_capacity(t.size() + 1);
    t.push_back({code, fn});
}

// 尝试恢复: 查表调用匹配 code 的恢复函数, 返回是否恢复成功
[[nodiscard]] inline bool om_try_recover(const operating_message& err) noexcept
{
    dense<om_recovery_entry>& t = om_recovery_table();
    for (size_t i = 0; i < t.size(); ++i)
    {
        if (err.is_code(t[i].code))
        {
            return t[i].fn(err);
        }
    }
    return false;
}
