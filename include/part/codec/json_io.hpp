// json_io.hpp - 轻量 JSON 读写器（合并自 json_writer + json_reader）
// 独立模块, 不依赖 ECS / 反射
#pragma once

#include <cstdint>
#include <cstdlib>
#include <charconv>
#include <cmath>
#include <cstring>
#include <string>
#include <string_view>

#include "../operating_message.hpp"

class json_writer
{
private:
    std::string buf_;
    bool need_comma_{false};
    bool pretty_{false};
    int   depth_{0};
    static constexpr size_t k_default_reserve = 4096;

    void write_indent() noexcept
    {
        if (!pretty_) return;
        for (int i = 0; i < depth_; ++i)
        {
            buf_.append("  ", 2);
        }
    }

    void on_value_start() noexcept
    {
        if (need_comma_) buf_.push_back(',');
        if (pretty_) buf_.push_back('\n');
        write_indent();
        need_comma_ = true;
    }

    void write_raw(std::string_view s) noexcept
    {
        buf_.append(s.data(), s.size());
    }

    // 字符串转义
    void write_escaped(std::string_view s) noexcept
    {
        buf_.reserve(buf_.size() + s.size() + 2);
        buf_.push_back('"');
        for (size_t i = 0; i < s.size(); ++i)
        {
            unsigned char c = static_cast<unsigned char>(s[i]);
            switch (c)
            {
                case '"':  buf_.append("\\\"", 2); break;
                case '\\': buf_.append("\\\\", 2); break;
                case '\b': buf_.append("\\b", 2); break;
                case '\f': buf_.append("\\f", 2); break;
                case '\n': buf_.append("\\n", 2); break;
                case '\r': buf_.append("\\r", 2); break;
                case '\t': buf_.append("\\t", 2); break;
                default:
                    if (c < 0x20)
                    {
                        char hex[8];
                        int n = std::snprintf(hex, sizeof(hex), "\\u%04x", c);
                        buf_.append(hex, static_cast<size_t>(n));
                    }
                    else
                    {
                        buf_.push_back(static_cast<char>(c));
                    }
                    break;
            }
        }
        buf_.push_back('"');
    }

    template<typename T>
    void write_num(T v) noexcept
    {
        char tmp[64];
        auto r = std::to_chars(tmp, tmp + sizeof(tmp), v);
        buf_.append(tmp, static_cast<size_t>(r.ptr - tmp));
    }

    // 浮点写出: NaN/Inf 非 JSON 合法 token, 写 null 保证外部解析器可读
    template<typename T>
    void write_float(T v) noexcept
    {
        if (!std::isfinite(v))
        {
            buf_.append("null", 4);
            return;
        }
        write_num(v);
    }

public:
    explicit json_writer(size_t reserve = k_default_reserve, bool pretty = false) noexcept
        : pretty_(pretty)
    {
        buf_.reserve(reserve);
    }

    json_writer& begin_object() noexcept
    {
        on_value_start();
        buf_.push_back('{');
        ++depth_;
        need_comma_ = false;
        return *this;
    }

    json_writer& end_object() noexcept
    {
        --depth_;
        if (pretty_ && need_comma_)
        {
            buf_.push_back('\n');
            write_indent();
        }
        buf_.push_back('}');
        need_comma_ = true;
        return *this;
    }

    json_writer& begin_array() noexcept
    {
        on_value_start();
        buf_.push_back('[');
        ++depth_;
        need_comma_ = false;
        return *this;
    }

    json_writer& end_array() noexcept
    {
        --depth_;
        if (pretty_ && need_comma_)
        {
            buf_.push_back('\n');
            write_indent();
        }
        buf_.push_back(']');
        need_comma_ = true;
        return *this;
    }

    // key 必须在 object 内调用, 后接 value
    json_writer& key(std::string_view k) noexcept
    {
        if (need_comma_) buf_.push_back(',');
        if (pretty_) buf_.push_back('\n');
        write_indent();
        write_escaped(k);
        buf_.push_back(':');
        if (pretty_) buf_.push_back(' ');
        need_comma_ = false;
        return *this;
    }

    json_writer& value(std::string_view v) noexcept { on_value_start(); write_escaped(v); return *this; }
    json_writer& value(const char* v) noexcept { on_value_start(); write_escaped(v ? std::string_view(v) : std::string_view("")); return *this; }
    json_writer& value(const std::string& v) noexcept { on_value_start(); write_escaped(v); return *this; }
    json_writer& value(bool v) noexcept { on_value_start(); write_raw(v ? "true" : "false"); return *this; }
    json_writer& value(int32_t v) noexcept { on_value_start(); write_num(v); return *this; }
    json_writer& value(uint32_t v) noexcept { on_value_start(); write_num(v); return *this; }
    json_writer& value(int64_t v) noexcept { on_value_start(); write_num(v); return *this; }
    json_writer& value(uint64_t v) noexcept { on_value_start(); write_num(v); return *this; }
    json_writer& value(float v) noexcept { on_value_start(); write_float(v); return *this; }
    json_writer& value(double v) noexcept { on_value_start(); write_float(v); return *this; }

    json_writer& null() noexcept { on_value_start(); write_raw("null"); return *this; }

    // raw_value: 直接拼接合法 JSON 片段
    json_writer& raw_value(std::string_view json_fragment) noexcept
    {
        on_value_start();
        write_raw(json_fragment);
        return *this;
    }

    json_writer& raw_append(std::string_view s) noexcept { write_raw(s); return *this; }

    [[nodiscard]] const std::string& string() const noexcept { return buf_; }
    [[nodiscard]] std::string& string() noexcept { return buf_; }
    [[nodiscard]] std::string_view view() const noexcept { return buf_; }

    void clear() noexcept
    {
        buf_.clear();
        need_comma_ = false;
        depth_ = 0;
    }

    [[nodiscard]] size_t size() const noexcept { return buf_.size(); }
    [[nodiscard]] bool empty() const noexcept { return buf_.empty(); }

    std::string take() noexcept { return std::move(buf_); }
};

class json_reader
{
public:
    enum class token_type : uint8_t
    {
        none        = 0,
        object      = 1,
        array       = 2,
        string      = 3,
        number      = 4,
        bool_value  = 5,
        null_value  = 6,
        end_object  = 7,
        end_array   = 8
    };

private:
    std::string_view src_;
    const char* p_{src_.data()};
    const char* end_{src_.data() + src_.size()};
    operating_message err_;
    bool has_err_{false};
    int  depth_{0};
    static constexpr int k_max_depth = 256;
    bool obj_comma_[k_max_depth] = {};
    bool arr_comma_[k_max_depth] = {};

    static int hex_val(char c) noexcept
    {
        if (c >= '0' && c <= '9') { return c - '0'; }
        if (c >= 'a' && c <= 'f') { return c - 'a' + 10; }
        if (c >= 'A' && c <= 'F') { return c - 'A' + 10; }
        return -1;
    }

    void set_error(std::string_view msg) noexcept
    {
        if (!has_err_)
        {
            has_err_ = true;
            err_.write(false, "JSON 解析错误 @offset ", static_cast<size_t>(p_ - src_.data()),
                               ": ", msg);
        }
    }

    void skip_ws() noexcept
    {
        while (p_ < end_)
        {
            char c = *p_;
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') { ++p_; continue; }
            if (c == '/' && p_ + 1 < end_)
            {
                if (p_[1] == '/')
                {
                    p_ += 2;
                    while (p_ < end_ && *p_ != '\n') ++p_;
                    continue;
                }
                if (p_[1] == '*')
                {
                    p_ += 2;
                    while (p_ + 1 < end_ && !(p_[0] == '*' && p_[1] == '/')) ++p_;
                    if (p_ + 1 < end_) p_ += 2;
                    continue;
                }
            }
            break;
        }
    }

    [[nodiscard]] char peek() noexcept { skip_ws(); return (p_ < end_) ? *p_ : '\0'; }

    bool expect(char c) noexcept
    {
        skip_ws();
        if (p_ < end_ && *p_ == c) { ++p_; return true; }
        set_error(std::string("期望 '") + c + "'");
        return false;
    }

    bool match_keyword(std::string_view kw) noexcept
    {
        if (static_cast<size_t>(end_ - p_) < kw.size()) return false;
        for (size_t i = 0; i < kw.size(); ++i)
        {
            if (p_[i] != kw[i]) return false;
        }
        p_ += kw.size();
        return true;
    }

    // 解析字符串 (含转义解码, 兼容双引号/单引号)
    bool parse_string(std::string& decode_buf, std::string_view& out_view) noexcept
    {
        skip_ws();
        if (p_ >= end_ || (*p_ != '"' && *p_ != '\'')) { set_error("期望字符串"); return false; }
        char quote = *p_;
        ++p_;
        const char* start = p_;
        decode_buf.clear();
        bool has_escape = false;
        while (p_ < end_)
        {
            char c = *p_;
            if (c == quote)
            {
                if (!has_escape)
                {
                    out_view = std::string_view(start, static_cast<size_t>(p_ - start));
                }
                else
                {
                    out_view = decode_buf;
                }
                ++p_;
                return true;
            }
            if (c == '\\')
            {
                if (!has_escape)
                {
                    has_escape = true;
                    decode_buf.append(start, static_cast<size_t>(p_ - start));
                }
                ++p_;
                if (p_ >= end_) { set_error("字符串未闭合"); return false; }
                char esc = *p_;
                ++p_;
                switch (esc)
                {
                    case '"':  decode_buf.push_back('"'); break;
                    case '\\': decode_buf.push_back('\\'); break;
                    case '/':  decode_buf.push_back('/'); break;
                    case 'b':  decode_buf.push_back('\b'); break;
                    case 'f':  decode_buf.push_back('\f'); break;
                    case 'n':  decode_buf.push_back('\n'); break;
                    case 'r':  decode_buf.push_back('\r'); break;
                    case 't':  decode_buf.push_back('\t'); break;
                    case 'u':
                    {
                        if (end_ - p_ < 4) { set_error("\\u 转义不完整"); return false; }
                        unsigned cp = 0;
                        for (int k = 0; k < 4; ++k)
                        {
                            int hv = hex_val(p_[k]);
                            if (hv < 0) { set_error("\\u 非法十六进制"); return false; }
                            cp = (cp << 4) | static_cast<unsigned>(hv);
                        }
                        p_ += 4;
                        if (cp >= 0xD800 && cp <= 0xDBFF)
                        {
                            if (end_ - p_ < 6 || p_[0] != '\\' || p_[1] != 'u')
                            {
                                set_error("孤立高代理项");
                                return false;
                            }
                            unsigned lo = 0;
                            for (int k = 0; k < 4; ++k)
                            {
                                int hv = hex_val(p_[2 + k]);
                                if (hv < 0) { set_error("\\u 非法十六进制"); return false; }
                                lo = (lo << 4) | static_cast<unsigned>(hv);
                            }
                            if (lo < 0xDC00 || lo > 0xDFFF)
                            {
                                set_error("非法代理项对");
                                return false;
                            }
                            p_ += 6;
                            cp = 0x10000u + ((cp - 0xD800u) << 10) + (lo - 0xDC00u);
                            decode_buf.push_back(static_cast<char>(0xF0 | (cp >> 18)));
                            decode_buf.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
                            decode_buf.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
                            decode_buf.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
                        }
                        else if (cp >= 0xDC00 && cp <= 0xDFFF)
                        {
                            set_error("孤立低代理项");
                            return false;
                        }
                        else if (cp < 0x80)
                        {
                            decode_buf.push_back(static_cast<char>(cp));
                        }
                        else if (cp < 0x800)
                        {
                            decode_buf.push_back(static_cast<char>(0xC0 | (cp >> 6)));
                            decode_buf.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
                        }
                        else
                        {
                            decode_buf.push_back(static_cast<char>(0xE0 | (cp >> 12)));
                            decode_buf.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
                            decode_buf.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
                        }
                        break;
                    }
                    default: set_error("无效转义字符"); return false;
                }
            }
            else
            {
                if (has_escape) decode_buf.push_back(c);
                ++p_;
            }
        }
        set_error("字符串未闭合");
        return false;
    }

    bool parse_number_raw(std::string_view& out) noexcept
    {
        skip_ws();
        const char* start = p_;
        if (p_ < end_ && (*p_ == '-' || *p_ == '+')) ++p_;
        while (p_ < end_)
        {
            char c = *p_;
            if ((c >= '0' && c <= '9') || c == '.' || c == 'e' || c == 'E' || c == '+' || c == '-')
            {
                ++p_;
            }
            else break;
        }
        if (p_ == start) { set_error("无效数字"); return false; }
        // std::from_chars 不支持前导 '+', 跳过以兼容用户手写
        if (*start == '+') ++start;
        out = std::string_view(start, static_cast<size_t>(p_ - start));
        return true;
    }

    token_type peek_token() noexcept
    {
        skip_ws();
        if (p_ >= end_) return token_type::none;
        char c = *p_;
        switch (c)
        {
            case '{': return token_type::object;
            case '[': return token_type::array;
            case '"': case '\'': return token_type::string;
            case 't': case 'f': return token_type::bool_value;
            case 'n': return token_type::null_value;
            case '}': return token_type::end_object;
            case ']': return token_type::end_array;
            default:  return token_type::number;
        }
    }

public:
    explicit json_reader(std::string_view src) noexcept
        : src_(src), p_(src.data()), end_(src.data() + src.size()) {}

    json_reader(std::string_view src, size_t start, size_t len) noexcept
        : src_(src.substr(start, len)), p_(src_.data()), end_(src_.data() + src_.size()) {}

    [[nodiscard]] bool has_error() const noexcept { return has_err_; }
    [[nodiscard]] operating_message last_error() const noexcept { return err_; }
    // #D3 清除错误状态, 用于 best_effort 模式恢复继续解析
    void clear_error() noexcept { has_err_ = false; }
    [[nodiscard]] bool at_end() noexcept { skip_ws(); return p_ >= end_; }

    // === 容器导航 ===

    bool enter_object() noexcept
    {
        if (!expect('{')) return false;
        if (++depth_ > k_max_depth) { set_error("嵌套过深"); return false; }
        obj_comma_[depth_ - 1] = false;
        return true;
    }

    bool exit_object() noexcept
    {
        while (p_ < end_)
        {
            skip_ws();
            if (p_ < end_ && *p_ == '}')
            {
                ++p_;
                --depth_;
                return true;
            }
            std::string tmp; std::string_view k;
            if (!parse_string(tmp, k)) return false;
            if (!expect(':')) return false;
            if (!skip_value()) return false;
            skip_ws();
            if (p_ < end_ && *p_ == ',') ++p_;
        }
        set_error("object 未闭合");
        return false;
    }

    bool enter_array() noexcept
    {
        if (!expect('[')) return false;
        if (++depth_ > k_max_depth) { set_error("嵌套过深"); return false; }
        arr_comma_[depth_ - 1] = false;
        return true;
    }

    bool exit_array() noexcept
    {
        while (p_ < end_)
        {
            skip_ws();
            if (p_ < end_ && *p_ == ']')
            {
                ++p_;
                --depth_;
                return true;
            }
            if (!skip_value()) return false;
            skip_ws();
            if (p_ < end_ && *p_ == ',') ++p_;
        }
        set_error("array 未闭合");
        return false;
    }

    // === Object key 遍历 ===

    // 返回空 view 表示无更多键或到达 '}'
    std::string_view next_key() noexcept
    {
        skip_ws();
        if (p_ < end_ && *p_ == '}')
        {
            ++p_;
            --depth_;
            return std::string_view{};
        }
        const int d = depth_ - 1;
        if (d >= 0 && d < k_max_depth && obj_comma_[d])
        {
            if (p_ >= end_ || *p_ != ',')
            {
                set_error("对象缺少逗号分隔");
                return std::string_view{};
            }
            ++p_;
            skip_ws();
            if (p_ < end_ && *p_ == '}')
            {
                set_error("对象尾随逗号");
                return std::string_view{};
            }
        }
        std::string_view k;
        if (!parse_string(key_buf_, k)) return std::string_view{};
        if (!expect(':')) return std::string_view{};
        if (d >= 0 && d < k_max_depth) { obj_comma_[d] = true; }
        return k;
    }

    // === Array 元素遍历 ===

    // 是否有下一个元素, 返回 false 表示数组结束
    bool next_element() noexcept
    {
        skip_ws();
        if (p_ < end_ && *p_ == ']')
        {
            ++p_;
            --depth_;
            return false;
        }
        const int d = depth_ - 1;
        if (d >= 0 && d < k_max_depth && arr_comma_[d])
        {
            if (p_ >= end_ || *p_ != ',')
            {
                set_error("数组缺少逗号分隔");
                return false;
            }
            ++p_;
            skip_ws();
            if (p_ < end_ && *p_ == ']')
            {
                set_error("数组尾随逗号");
                return false;
            }
        }
        return true;
    }

    void end_element() noexcept
    {
        const int d = depth_ - 1;
        if (d >= 0 && d < k_max_depth) { arr_comma_[d] = true; }
    }

    // === 值读取 ===

    bool read_bool() noexcept
    {
        skip_ws();
        if (match_keyword("true")) return true;
        if (match_keyword("false")) return false;
        set_error("期望 bool");
        return false;
    }

    bool is_null() noexcept
    {
        skip_ws();
        return match_keyword("null");
    }

    // 官方 JSON 无 NaN/Infinity token, 写侧映射为 null; 读侧 null 取默认值
    int32_t read_int32() noexcept
    {
        if (is_null()) return 0;
        std::string_view s;
        if (!parse_number_raw(s)) return 0;
        int32_t v = 0;
        auto r = std::from_chars(s.data(), s.data() + s.size(), v);
        if (r.ec != std::errc{}) { set_error("int32 解析失败"); return 0; }
        return v;
    }

    uint32_t read_uint32() noexcept
    {
        if (is_null()) return 0;
        std::string_view s;
        if (!parse_number_raw(s)) return 0;
        uint32_t v = 0;
        auto r = std::from_chars(s.data(), s.data() + s.size(), v);
        if (r.ec != std::errc{}) { set_error("uint32 解析失败"); return 0; }
        return v;
    }

    int64_t read_int64() noexcept
    {
        if (is_null()) return 0;
        std::string_view s;
        if (!parse_number_raw(s)) return 0;
        int64_t v = 0;
        auto r = std::from_chars(s.data(), s.data() + s.size(), v);
        if (r.ec != std::errc{}) { set_error("int64 解析失败"); return 0; }
        return v;
    }

    uint64_t read_uint64() noexcept
    {
        if (is_null()) return 0;
        std::string_view s;
        if (!parse_number_raw(s)) return 0;
        uint64_t v = 0;
        auto r = std::from_chars(s.data(), s.data() + s.size(), v);
        if (r.ec != std::errc{}) { set_error("uint64 解析失败"); return 0; }
        return v;
    }

    float read_float() noexcept
    {
        if (is_null()) return 0.0f;
        std::string_view s;
        if (!parse_number_raw(s)) return 0.0f;
        float v = 0.0f;
        auto r = std::from_chars(s.data(), s.data() + s.size(), v);
        if (r.ec != std::errc{}) { set_error("float 解析失败"); return 0.0f; }
        return v;
    }

    double read_double() noexcept
    {
        if (is_null()) return 0.0;
        std::string_view s;
        if (!parse_number_raw(s)) return 0.0;
        double v = 0.0;
        auto r = std::from_chars(s.data(), s.data() + s.size(), v);
        if (r.ec != std::errc{}) { set_error("double 解析失败"); return 0.0; }
        return v;
    }

    bool read_string(std::string& decode_buf, std::string_view& out_view) noexcept
    {
        return parse_string(decode_buf, out_view);
    }

    std::string read_string() noexcept
    {
        std::string tmp; std::string_view v;
        if (!parse_string(tmp, v)) return {};
        return std::string(v);
    }

    std::string_view read_raw_value() noexcept
    {
        skip_ws();
        const char* start = p_;
        if (!skip_value()) return {};
        return std::string_view(start, static_cast<size_t>(p_ - start));
    }

    bool skip_value() noexcept
    {
        skip_ws();
        if (p_ >= end_) { set_error("无值可跳过"); return false; }
        char c = *p_;
        switch (c)
        {
            case '"': case '\'':
            {
                std::string tmp; std::string_view v;
                return parse_string(tmp, v);
            }
            case '{':
            {
                if (!enter_object()) return false;
                while (true)
                {
                    skip_ws();
                    if (p_ < end_ && *p_ == '}') { ++p_; --depth_; return true; }
                    std::string tmp; std::string_view k;
                    if (!parse_string(tmp, k)) return false;
                    if (!expect(':')) return false;
                    if (!skip_value()) return false;
                    skip_ws();
                    if (p_ < end_ && *p_ == ',') ++p_;
                }
            }
            case '[':
            {
                if (!enter_array()) return false;
                while (true)
                {
                    skip_ws();
                    if (p_ < end_ && *p_ == ']') { ++p_; --depth_; return true; }
                    if (!skip_value()) return false;
                    skip_ws();
                    if (p_ < end_ && *p_ == ',') ++p_;
                }
            }
            case 't': return match_keyword("true");
            case 'f': return match_keyword("false");
            case 'n': return match_keyword("null");
            default:
            {
                std::string_view s;
                return parse_number_raw(s);
            }
        }
    }

    [[nodiscard]] token_type peek_next_type() noexcept { return peek_token(); }

private:
    std::string key_buf_;  // next_key 转义解码缓冲 (保证返回视图稳定)
};
