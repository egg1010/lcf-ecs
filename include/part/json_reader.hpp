// json_reader.hpp - 轻量递归下降 JSON 解析器
// 游标式 API, 零拷贝 (无转义时直接返回 string_view), 不抛异常
// 独立模块, 不依赖 ECS / 反射
#pragma once

#include <string>
#include <string_view>
#include <cstdint>
#include <charconv>
#include <cstdlib>
#include "operating_message.hpp"

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

    void set_error(std::string_view msg) noexcept
    {
        if (!has_err_)
        {
            has_err_ = true;
            err_.write_message(false, "JSON 解析错误 @offset ", static_cast<size_t>(p_ - src_.data()),
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
                        char hex[5] = {p_[0], p_[1], p_[2], p_[3], 0};
                        p_ += 4;
                        unsigned code = static_cast<unsigned>(std::strtoul(hex, nullptr, 16));
                        if (code < 0x80)
                        {
                            decode_buf.push_back(static_cast<char>(code));
                        }
                        else if (code < 0x800)
                        {
                            decode_buf.push_back(static_cast<char>(0xC0 | (code >> 6)));
                            decode_buf.push_back(static_cast<char>(0x80 | (code & 0x3F)));
                        }
                        else
                        {
                            decode_buf.push_back(static_cast<char>(0xE0 | (code >> 12)));
                            decode_buf.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
                            decode_buf.push_back(static_cast<char>(0x80 | (code & 0x3F)));
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

    // === 容器导航 ===

    bool enter_object() noexcept
    {
        if (!expect('{')) return false;
        if (++depth_ > k_max_depth) { set_error("嵌套过深"); return false; }
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

    // 返回空 view 表示无更多键或到达 '}' (容忍尾随逗号)
    std::string_view next_key() noexcept
    {
        skip_ws();
        if (p_ < end_ && *p_ == '}')
        {
            ++p_;
            --depth_;
            need_comma_consumed_ = false;
            return std::string_view{};
        }
        if (p_ < end_ && *p_ == ',')
        {
            ++p_;
            skip_ws();
            if (p_ < end_ && *p_ == '}')
            {
                ++p_;
                --depth_;
                need_comma_consumed_ = false;
                return std::string_view{};
            }
        }
        std::string tmp; std::string_view k;
        if (!parse_string(tmp, k)) return std::string_view{};
        if (!expect(':')) return std::string_view{};
        return k;
    }

    // === Array 元素遍历 ===

    // 是否有下一个元素, 自动消费逗号, 返回 false 表示数组结束 (容忍尾随逗号)
    bool next_element() noexcept
    {
        skip_ws();
        if (p_ < end_ && *p_ == ']')
        {
            ++p_;
            --depth_;
            return false;
        }
        if (p_ < end_ && *p_ == ',')
        {
            ++p_;
            skip_ws();
            if (p_ < end_ && *p_ == ']')
            {
                ++p_;
                --depth_;
                return false;
            }
        }
        return true;
    }

    void end_element() noexcept
    {
        skip_ws();
        if (p_ < end_ && *p_ == ',') ++p_;
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

    int32_t read_int32() noexcept
    {
        std::string_view s;
        if (!parse_number_raw(s)) return 0;
        int32_t v = 0;
        auto r = std::from_chars(s.data(), s.data() + s.size(), v);
        if (r.ec != std::errc{}) { set_error("int32 解析失败"); return 0; }
        return v;
    }

    uint32_t read_uint32() noexcept
    {
        std::string_view s;
        if (!parse_number_raw(s)) return 0;
        uint32_t v = 0;
        auto r = std::from_chars(s.data(), s.data() + s.size(), v);
        if (r.ec != std::errc{}) { set_error("uint32 解析失败"); return 0; }
        return v;
    }

    int64_t read_int64() noexcept
    {
        std::string_view s;
        if (!parse_number_raw(s)) return 0;
        int64_t v = 0;
        auto r = std::from_chars(s.data(), s.data() + s.size(), v);
        if (r.ec != std::errc{}) { set_error("int64 解析失败"); return 0; }
        return v;
    }

    uint64_t read_uint64() noexcept
    {
        std::string_view s;
        if (!parse_number_raw(s)) return 0;
        uint64_t v = 0;
        auto r = std::from_chars(s.data(), s.data() + s.size(), v);
        if (r.ec != std::errc{}) { set_error("uint64 解析失败"); return 0; }
        return v;
    }

    float read_float() noexcept
    {
        std::string_view s;
        if (!parse_number_raw(s)) return 0.0f;
        float v = 0.0f;
        auto r = std::from_chars(s.data(), s.data() + s.size(), v);
        if (r.ec != std::errc{}) { set_error("float 解析失败"); return 0.0f; }
        return v;
    }

    double read_double() noexcept
    {
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
            default:
            {
                std::string_view s;
                return parse_number_raw(s);
            }
        }
    }

    [[nodiscard]] token_type peek_next_type() noexcept { return peek_token(); }

private:
    bool need_comma_consumed_{false};
};
