// safety.hpp - 安全限制 + 字节序处理 + Base64 编解码
#pragma once

#include "../part/dense.hpp"
#include <bit>
#include <cstring>
#include <string>
#include <string_view>
#include <cstdint>

namespace ecs {

// ============================================================================
// 安全限制
// ============================================================================
struct safety_limits {
    size_t   max_file_size       = 256 * 1024 * 1024;
    size_t   max_string_length    = 16 * 1024 * 1024;
    size_t   max_array_elements   = 10 * 1000 * 1000;
    size_t   max_object_fields    = 65536;
    uint32_t max_depth            = 64;
    size_t   max_entity_count     = 10 * 1000 * 1000;
};

namespace detail {

[[nodiscard]] constexpr bool is_little_endian() noexcept {
    return std::endian::native == std::endian::little;
}

template<typename T>
void write_le(T value, char* out) noexcept {
    static_assert(std::is_trivially_copyable_v<T>);
    if constexpr (is_little_endian()) {
        std::memcpy(out, &value, sizeof(T));
    } else {
        auto* bytes = reinterpret_cast<const unsigned char*>(&value);
        for (size_t i = 0; i < sizeof(T); ++i) {
            out[i] = static_cast<char>(bytes[sizeof(T) - 1 - i]);
        }
    }
}

template<typename T>
[[nodiscard]] T read_le(const char* src) noexcept {
    static_assert(std::is_trivially_copyable_v<T>);
    if constexpr (is_little_endian()) {
        T v;
        std::memcpy(&v, src, sizeof(T));
        return v;
    } else {
        T v;
        auto* bytes = reinterpret_cast<unsigned char*>(&v);
        for (size_t i = 0; i < sizeof(T); ++i) {
            bytes[i] = static_cast<unsigned char>(src[sizeof(T) - 1 - i]);
        }
        return v;
    }
}

// ============================================================================
// Base64 编解码 (RFC 4648, 无换行)
// ============================================================================
inline constexpr char b64_table[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

inline std::string base64_encode(const void* data, size_t len) noexcept
{
    const auto* p = static_cast<const unsigned char*>(data);
    std::string out;
    out.reserve(((len + 2) / 3) * 4);
    for (size_t i = 0; i < len; i += 3)
    {
        uint32_t n = static_cast<uint32_t>(p[i]) << 16;
        if (i + 1 < len) n |= static_cast<uint32_t>(p[i + 1]) << 8;
        if (i + 2 < len) n |= static_cast<uint32_t>(p[i + 2]);
        out.push_back(b64_table[(n >> 18) & 0x3F]);
        out.push_back(b64_table[(n >> 12) & 0x3F]);
        out.push_back(i + 1 < len ? b64_table[(n >> 6) & 0x3F] : '=');
        out.push_back(i + 2 < len ? b64_table[n & 0x3F] : '=');
    }
    return out;
}

inline int b64_decode_char(char c) noexcept
{
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

inline std::string base64_decode(std::string_view s) noexcept
{
    std::string out;
    out.reserve((s.size() / 4) * 3);
    uint32_t buf = 0;
    int bits = 0;
    for (char c : s)
    {
        if (c == '=' || c == '\n' || c == '\r' || c == ' ') continue;
        int v = b64_decode_char(c);
        if (v < 0) continue;
        buf = (buf << 6) | static_cast<uint32_t>(v);
        bits += 6;
        if (bits >= 8)
        {
            bits -= 8;
            out.push_back(static_cast<char>((buf >> bits) & 0xFF));
        }
    }
    return out;
}

} // namespace detail

// ============================================================================
// RLE 压缩 (Run-Length Encoding)
// 适用于含大量重复字节的二进制数据 (如 trivial 组件数组)
// 格式: 魔术字 "RLE1" + [count | byte] 对
//   count=0:   字面量 (输出单个 byte)
//   count=1+:  重复 (输出 count+1 个 byte)
// ============================================================================
namespace detail {

inline std::string rle_compress(const std::string& data) noexcept
{
    if (data.empty()) return "RLE1";
    std::string out;
    out.reserve(data.size() / 2 + 4);
    out.append("RLE1", 4);
    size_t i = 0;
    while (i < data.size())
    {
        unsigned char cur = static_cast<unsigned char>(data[i]);
        uint32_t run = 1;
        while (i + run < data.size() && run < 256 &&
               static_cast<unsigned char>(data[i + run]) == cur)
        {
            ++run;
        }
        if (run >= 3)
        {
            // 重复序列: [run-1 | byte] (输出 run 个 byte)
            out.push_back(static_cast<char>(run - 1));
            out.push_back(static_cast<char>(cur));
            i += run;
        }
        else
        {
            // 字面量: [0 | byte] (输出 1 个 byte)
            for (uint32_t k = 0; k < run; ++k)
            {
                out.push_back('\0');
                out.push_back(static_cast<char>(data[i + k]));
            }
            i += run;
        }
    }
    return out;
}

inline std::string rle_decompress(const std::string& data) noexcept
{
    // 检查魔术字节
    if (data.size() < 4 || data[0] != 'R' || data[1] != 'L' ||
        data[2] != 'E' || data[3] != '1')
    {
        return data; // 未压缩, 原样返回
    }
    std::string out;
    out.reserve(data.size() * 2);
    size_t i = 4;
    while (i + 1 < data.size())
    {
        uint8_t count = static_cast<uint8_t>(data[i]);
        unsigned char byte = static_cast<unsigned char>(data[i + 1]);
        if (count == 0)
        {
            // 字面量
            out.push_back(static_cast<char>(byte));
            i += 2;
        }
        else
        {
            // 重复 count+1 次
            uint32_t reps = static_cast<uint32_t>(count) + 1;
            for (uint32_t k = 0; k < reps; ++k)
            {
                out.push_back(static_cast<char>(byte));
            }
            i += 2;
        }
    }
    return out;
}

} // namespace detail

// 公开接口
[[nodiscard]] inline std::string rle_compress(const std::string& data) noexcept
{
    return detail::rle_compress(data);
}

[[nodiscard]] inline std::string rle_decompress(const std::string& data) noexcept
{
    return detail::rle_decompress(data);
}

} // namespace ecs
