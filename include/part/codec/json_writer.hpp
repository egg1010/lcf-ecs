// json_writer.hpp - 轻量 JSON 流式写入器
// 独立模块, 不依赖 ECS / 反射
#pragma once

#include <string>
#include <string_view>
#include <cstdint>
#include <charconv>
#include <cmath>
#include <cstring>

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
    json_writer& value(float v) noexcept { on_value_start(); write_num(v); return *this; }
    json_writer& value(double v) noexcept { on_value_start(); write_num(v); return *this; }

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
