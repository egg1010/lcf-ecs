// nonmember.hpp - 非成员函数 (operator+/流/swap/to_utf8pp/字面量/hash/比较/erase/formatter)
#pragma once


// === 非成员 operator+ 系列 ===
[[nodiscard]] inline utf8pp operator+(const utf8pp& lhs, const utf8pp& rhs)
{
    utf8pp r(lhs);
    r.append(rhs);
    return r;
}

[[nodiscard]] inline utf8pp operator+(const utf8pp& lhs, char32_t rhs)
{
    utf8pp r(lhs);
    r.push_back(rhs);
    return r;
}

[[nodiscard]] inline utf8pp operator+(char32_t lhs, const utf8pp& rhs)
{
    utf8pp r;
    r.push_back(lhs);
    r.append(rhs);
    return r;
}

[[nodiscard]] inline utf8pp operator+(const utf8pp& lhs, const char* rhs)
{
    utf8pp r(lhs);
    r.append(rhs);
    return r;
}

[[nodiscard]] inline utf8pp operator+(const char* lhs, const utf8pp& rhs)
{
    utf8pp r(lhs);
    r.append(rhs);
    return r;
}

[[nodiscard]] inline utf8pp operator+(const utf8pp& lhs, std::string_view rhs)
{
    utf8pp r(lhs);
    r.append(rhs);
    return r;
}

[[nodiscard]] inline utf8pp operator+(std::string_view lhs, const utf8pp& rhs)
{
    utf8pp r(lhs);
    r.append(rhs);
    return r;
}

[[nodiscard]] inline utf8pp operator+(const utf8pp& lhs, const char8_t* rhs)
{
    utf8pp r(lhs);
    r.append(rhs);
    return r;
}

[[nodiscard]] inline utf8pp operator+(const char8_t* lhs, const utf8pp& rhs)
{
    utf8pp r(lhs);
    r.append(rhs);
    return r;
}

// === 与 std::string / u8string / u32string 互操作的 operator+ ===
[[nodiscard]] inline utf8pp operator+(const utf8pp& lhs, const std::string& rhs)
{
    utf8pp r(lhs);
    r.append(rhs.data(), rhs.size());
    return r;
}
[[nodiscard]] inline utf8pp operator+(const std::string& lhs, const utf8pp& rhs)
{
    utf8pp r(lhs.data(), lhs.size());
    r.append(rhs);
    return r;
}
[[nodiscard]] inline utf8pp operator+(const utf8pp& lhs, const std::u8string& rhs)
{
    utf8pp r(lhs);
    r.append(reinterpret_cast<const char*>(rhs.data()), rhs.size());
    return r;
}
[[nodiscard]] inline utf8pp operator+(const std::u8string& lhs, const utf8pp& rhs)
{
    utf8pp r(reinterpret_cast<const char*>(lhs.data()), lhs.size());
    r.append(rhs);
    return r;
}
[[nodiscard]] inline utf8pp operator+(const utf8pp& lhs, const std::u32string& rhs)
{
    utf8pp r(lhs);
    for (char32_t c : rhs) r.push_back(c);
    return r;
}
[[nodiscard]] inline utf8pp operator+(const std::u32string& lhs, const utf8pp& rhs)
{
    utf8pp r(lhs.data(), lhs.size());
    r.append(rhs);
    return r;
}

// === 流操作符 ===
inline std::ostream& operator<<(std::ostream& os, const utf8pp& s)
{
    os.write(s.data(), static_cast<std::streamsize>(s.byte_size()));
    return os;
}

inline std::istream& operator>>(std::istream& is, utf8pp& s)
{
    s.clear();
    char buf[4096];
    while (is.read(buf, sizeof(buf)) || is.gcount() > 0)
    {
        s.append(buf, static_cast<size_t>(is.gcount()));
    }
    return is;
}

// === getline: 从流读取一行 (分隔符默认 '\n', 遇到 EOF 或分隔符停止) ===
inline std::istream& getline(std::istream& is, utf8pp& s, char delim = '\n')
{
    s.clear();
    char c;
    bool any = false;
    while (is.get(c))
    {
        any = true;
        if (c == delim) break;
        s.push_back(static_cast<char32_t>(static_cast<unsigned char>(c)));
    }
    if (!any && !is.good())
    {
        // 完全未读到且流已结束: 设置失败位 (与 std::getline 一致)
        is.setstate(std::ios::failbit);
    }
    return is;
}

// === 非成员 swap ===
inline void swap(utf8pp& lhs, utf8pp& rhs) noexcept { lhs.swap(rhs); }

// === 数字 → utf8pp (类似 std::to_string, 无异常) ===
[[nodiscard]] inline utf8pp to_utf8pp(int v)
{
    char buf[16];
    int n = std::snprintf(buf, sizeof(buf), "%d", v);
    return (n > 0) ? utf8pp(buf, static_cast<size_t>(n)) : utf8pp();
}

[[nodiscard]] inline utf8pp to_utf8pp(long v)
{
    char buf[24];
    int n = std::snprintf(buf, sizeof(buf), "%ld", v);
    return (n > 0) ? utf8pp(buf, static_cast<size_t>(n)) : utf8pp();
}

[[nodiscard]] inline utf8pp to_utf8pp(long long v)
{
    char buf[24];
    int n = std::snprintf(buf, sizeof(buf), "%lld", v);
    return (n > 0) ? utf8pp(buf, static_cast<size_t>(n)) : utf8pp();
}

[[nodiscard]] inline utf8pp to_utf8pp(unsigned v)
{
    char buf[16];
    int n = std::snprintf(buf, sizeof(buf), "%u", v);
    return (n > 0) ? utf8pp(buf, static_cast<size_t>(n)) : utf8pp();
}

[[nodiscard]] inline utf8pp to_utf8pp(unsigned long v)
{
    char buf[24];
    int n = std::snprintf(buf, sizeof(buf), "%lu", v);
    return (n > 0) ? utf8pp(buf, static_cast<size_t>(n)) : utf8pp();
}

[[nodiscard]] inline utf8pp to_utf8pp(unsigned long long v)
{
    char buf[24];
    int n = std::snprintf(buf, sizeof(buf), "%llu", v);
    return (n > 0) ? utf8pp(buf, static_cast<size_t>(n)) : utf8pp();
}

[[nodiscard]] inline utf8pp to_utf8pp(float v)
{
    char buf[32];
    int n = std::snprintf(buf, sizeof(buf), "%g", static_cast<double>(v));
    return (n > 0) ? utf8pp(buf, static_cast<size_t>(n)) : utf8pp();
}

[[nodiscard]] inline utf8pp to_utf8pp(double v)
{
    char buf[32];
    int n = std::snprintf(buf, sizeof(buf), "%g", v);
    return (n > 0) ? utf8pp(buf, static_cast<size_t>(n)) : utf8pp();
}

[[nodiscard]] inline utf8pp to_utf8pp(long double v)
{
    char buf[48];
    int n = std::snprintf(buf, sizeof(buf), "%Lg", v);
    return (n > 0) ? utf8pp(buf, static_cast<size_t>(n)) : utf8pp();
}

// === utf8pp::format (printf 风格, 内部 vsnprintf; 不抛异常, 失败返回空串) ===
[[nodiscard]] inline utf8pp utf8pp_format(const char* fmt, ...)
{
    if (!fmt) return utf8pp();
    std::va_list ap;
    va_start(ap, fmt);
    char buf[1024];
    int n = std::vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (n < 0) return utf8pp();
    if (static_cast<size_t>(n) < sizeof(buf)) return utf8pp(buf, static_cast<size_t>(n));
    // 大于栈缓冲: 动态分配
    std::string tmp(static_cast<size_t>(n), '\0');
    va_start(ap, fmt);
    int n2 = std::vsnprintf(tmp.data(), tmp.size() + 1, fmt, ap);
    va_end(ap);
    if (n2 < 0) return utf8pp();
    return utf8pp(tmp.data(), static_cast<size_t>(n2));
}

// utf8pp::format 的静态成员版本 (与 std::format 风格统一, 但用 printf 格式串)
// 用法: utf8pp::format("x=%d y=%s", 42, "hi")
[[nodiscard]] inline utf8pp utf8pp_vformat(const char* fmt, std::va_list ap)
{
    if (!fmt) return utf8pp();
    char buf[1024];
    int n = std::vsnprintf(buf, sizeof(buf), fmt, ap);
    if (n < 0) return utf8pp();
    if (static_cast<size_t>(n) < sizeof(buf)) return utf8pp(buf, static_cast<size_t>(n));
    std::string tmp(static_cast<size_t>(n), '\0');
    std::va_list ap2;
    va_copy(ap2, ap);
    int n2 = std::vsnprintf(tmp.data(), tmp.size() + 1, fmt, ap2);
    va_end(ap2);
    if (n2 < 0) return utf8pp();
    return utf8pp(tmp.data(), static_cast<size_t>(n2));
}

// utf8pp 类内静态方法的定义 (前向声明的 format/vformat)
[[nodiscard]] inline utf8pp utf8pp::format(const char* fmt, ...)
{
    if (!fmt) return utf8pp();
    std::va_list ap;
    va_start(ap, fmt);
    char buf[1024];
    int n = std::vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (n < 0) return utf8pp();
    if (static_cast<size_t>(n) < sizeof(buf)) return utf8pp(buf, static_cast<size_t>(n));
    std::string tmp(static_cast<size_t>(n), '\0');
    va_start(ap, fmt);
    int n2 = std::vsnprintf(tmp.data(), tmp.size() + 1, fmt, ap);
    va_end(ap);
    if (n2 < 0) return utf8pp();
    return utf8pp(tmp.data(), static_cast<size_t>(n2));
}

[[nodiscard]] inline utf8pp utf8pp::vformat(const char* fmt, std::va_list ap)
{
    return utf8pp_vformat(fmt, ap);
}

// === 字面量运算符 ===
[[nodiscard]] inline utf8pp operator""_u8(const char* s, size_t len)
{
    return utf8pp(s, len);
}

[[nodiscard]] inline utf8pp operator""_u8(const char8_t* s, size_t len)
{
    return utf8pp(reinterpret_cast<const char*>(s), len);
}

[[nodiscard]] inline utf8pp operator""_utf8(const char* s, size_t len)
{
    return utf8pp(s, len);
}

[[nodiscard]] inline utf8pp operator""_utf8(const char8_t* s, size_t len)
{
    return utf8pp(reinterpret_cast<const char*>(s), len);
}

// === std::hash 特化 ===
namespace std {
template <>
struct hash<utf8pp>
{
    size_t operator()(const utf8pp& s) const noexcept
    {
        // FNV-1a 字节哈希 (分布优于朴素 *31)
        size_t h = 14695981039346656037ULL;
        const char* p = s.data();
        size_t n = s.byte_size();
        for (size_t i = 0; i < n; ++i)
        {
            h ^= static_cast<size_t>(static_cast<unsigned char>(p[i]));
            h *= 1099511628211ULL;
        }
        return h;
    }
};

// === std::swap 特化 (与 std::swap(std::string&, std::string&) 对齐) ===
template <>
inline void swap<utf8pp>(utf8pp& a, utf8pp& b) noexcept(noexcept(a.swap(b)))
{
    a.swap(b);
}
} // namespace std

// === 非成员对称比较运算符 (避免 const char*/std::string lhs 时构造临时 utf8pp) ===
[[nodiscard]] inline bool operator==(const char* lhs, const utf8pp& rhs) noexcept { return rhs == lhs; }
[[nodiscard]] inline bool operator==(std::string_view lhs, const utf8pp& rhs) noexcept { return rhs == lhs; }
[[nodiscard]] inline bool operator==(const std::string& lhs, const utf8pp& rhs) noexcept { return rhs == lhs; }
[[nodiscard]] inline bool operator==(char32_t lhs, const utf8pp& rhs) noexcept { return rhs == lhs; }

[[nodiscard]] inline bool operator!=(const char* lhs, const utf8pp& rhs) noexcept { return !(rhs == lhs); }
[[nodiscard]] inline bool operator!=(std::string_view lhs, const utf8pp& rhs) noexcept { return !(rhs == lhs); }
[[nodiscard]] inline bool operator!=(const std::string& lhs, const utf8pp& rhs) noexcept { return !(rhs == lhs); }
[[nodiscard]] inline bool operator!=(char32_t lhs, const utf8pp& rhs) noexcept { return !(rhs == lhs); }

[[nodiscard]] inline std::strong_ordering operator<=>(const char* lhs, const utf8pp& rhs) noexcept
{
    int c = rhs.compare(lhs);
    if (c < 0) return std::strong_ordering::greater;
    if (c > 0) return std::strong_ordering::less;
    return std::strong_ordering::equal;
}
[[nodiscard]] inline std::strong_ordering operator<=>(std::string_view lhs, const utf8pp& rhs) noexcept
{
    int c = rhs.compare(lhs);
    if (c < 0) return std::strong_ordering::greater;
    if (c > 0) return std::strong_ordering::less;
    return std::strong_ordering::equal;
}
[[nodiscard]] inline std::strong_ordering operator<=>(const std::string& lhs, const utf8pp& rhs) noexcept
{
    int c = rhs.compare(lhs);
    if (c < 0) return std::strong_ordering::greater;
    if (c > 0) return std::strong_ordering::less;
    return std::strong_ordering::equal;
}

// === C++20 std::erase / std::erase_if 自由函数 (与 std::erase(std::string, value) 对齐) ===
// 注意: utf8pp 的 erase(cp_idx, n) 是码点级; 这里提供单码点 erase 和谓词 erase_if
// 放在全局命名空间 (utf8pp 非 std 容器, 不放入 std; 但与 std::erase 用法一致)
[[nodiscard]] inline size_t erase(utf8pp& s, char32_t cp)
{
    size_t removed = 0;
    size_t pos = s.find(cp);
    while (pos != utf8pp::npos)
    {
        s.erase(pos, 1);
        ++removed;
        pos = s.find(cp, pos);
    }
    return removed;
}

template <typename Pred>
[[nodiscard]] inline size_t erase_if(utf8pp& s, Pred pred)
{
    size_t removed = 0;
    size_t i = 0;
    while (i < s.size())
    {
        if (pred(s[i])) { s.erase(i, 1); ++removed; }
        else ++i;
    }
    return removed;
}

// === std::formatter 特化 (C++20 std::format 支持, 可选) ===
// 支持: 原始输出 / width / fill / align / 大小写转换
// 用法:
//   std::format("{}",         utf8pp("Hello"))      → "Hello"
//   std::format("{:10}",      utf8pp("Hi"))         → "Hi        "
//   std::format("{:<10}",     utf8pp("Hi"))         → "Hi        "
//   std::format("{:>10}",     utf8pp("Hi"))         → "        Hi"
//   std::format("{:^10}",     utf8pp("Hi"))         → "    Hi    "
//   std::format("{:*<10}",    utf8pp("Hi"))         → "Hi********"
//   std::format("{:L}",       utf8pp("Hello"))      → "hello"  (小写)
//   std::format("{:U}",       utf8pp("Hello"))      → "HELLO"  (大写)
//   std::format("{:T}",       utf8pp("hello world"))→ "Hello World"
//   std::format("{:C10}",     utf8pp("你好"))       → "  你好  " (按 display_width 对齐)
#if __cpp_lib_format >= 201907L
#include <format>
template <>
struct std::formatter<utf8pp>
{
    char fill_{' '};
    char align_{0};     // 0 = 默认 (字符串左对齐), '<' '>' '^'
    int width_{-1};
    bool upper_{false}; // U: 大写
    bool lower_{false}; // L: 小写
    bool title_{false}; // T: 标题大小写
    bool disp_w_{false};// C: 按 display_width 对齐 (East Asian Width 感知)

    constexpr auto parse(std::format_parse_context& ctx)
    {
        auto it = ctx.begin();
        auto end = ctx.end();
        // [[fill]align][width]['.' precision][type]
        // 处理 fill+align
        if (it != end && (*it == '<' || *it == '>' || *it == '^'))
        {
            align_ = *it;
            ++it;
        }
        else if (it + 1 != end &&
                 *it != '<' && *it != '>' && *it != '^' && *it != '{' &&
                 (it[1] == '<' || it[1] == '>' || it[1] == '^'))
        {
            fill_ = *it;
            align_ = it[1];
            it += 2;
        }
        // width (数字)
        if (it != end && (*it >= '0' && *it <= '9'))
        {
            int w = 0;
            while (it != end && *it >= '0' && *it <= '9')
            {
                w = w * 10 + (*it - '0');
                ++it;
            }
            width_ = w;
        }
        // type 标志 (大小写 / display_width)
        while (it != end && *it != '}')
        {
            if (*it == 'U') upper_ = true;
            else if (*it == 'L') lower_ = true;
            else if (*it == 'T') title_ = true;
            else if (*it == 'C') disp_w_ = true;
            else if (*it == 's') { /* default */ }
            else std::abort(); // 非法格式说明符, 编程错误
            ++it;
        }
        return it;
    }

    auto format(const utf8pp& s, std::format_context& ctx) const
    {
        // 1. 先做大小写转换 (产生临时 utf8pp)
        utf8pp tmp;
        if (upper_)      tmp = utf8pp(s).to_upper();
        else if (lower_) tmp = utf8pp(s).to_lower();
        else if (title_) tmp = utf8pp(s).to_title();
        else             tmp = s;
        const utf8pp& out = tmp;

        // 2. 无 width, 直接输出字节
        if (width_ < 0)
        {
            return std::format_to(ctx.out(), "{}",
                std::string_view(out.data(), out.byte_size()));
        }

        // 3. 有 width, 按对齐方式填充
        size_t cur_w = disp_w_ ? out.display_width() : out.size();
        size_t target_w = static_cast<size_t>(width_);
        if (cur_w >= target_w)
        {
            return std::format_to(ctx.out(), "{}",
                std::string_view(out.data(), out.byte_size()));
        }

        // 计算填充字符 (仅支持 ASCII fill, 因 std::format spec fill 字符为单字节)
        char fill_buf[1] = {fill_};
        size_t fill_len = 1;
        size_t total_pad = target_w - cur_w;
        size_t left_pad = 0, right_pad = 0;
        char eff_align = (align_ == 0) ? '<' : align_; // 字符串默认左对齐
        if (eff_align == '<') right_pad = total_pad;
        else if (eff_align == '>') left_pad = total_pad;
        else { left_pad = total_pad / 2; right_pad = total_pad - left_pad; }

        auto out_it = ctx.out();
        for (size_t i = 0; i < left_pad; ++i)
            out_it = std::format_to(out_it, "{}", std::string_view(fill_buf, fill_len));
        out_it = std::format_to(out_it, "{}",
            std::string_view(out.data(), out.byte_size()));
        for (size_t i = 0; i < right_pad; ++i)
            out_it = std::format_to(out_it, "{}", std::string_view(fill_buf, fill_len));
        return out_it;
    }
};
#endif
