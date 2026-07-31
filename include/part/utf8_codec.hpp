#pragma once

// utf8_codec.hpp - UTF-8 编解码核心 (无依赖, 供 utf8pp / utf8_view 共享)

#include <cstdint>
#include <cstddef>
#include <cstring>
#include "force_inline.hpp"

namespace detail_utf8 {

// 序列长度查表: 0=非法首字节, 1=ASCII, 2/3/4=多字节序列
inline constexpr uint8_t k_utf8_seq_len[256] = {
    1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1, 1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
    1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1, 1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
    1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1, 1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
    1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1, 1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,
    2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2, 2,2,2,2,2,2,2,2,2,2,2,2,2,2,
    3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,
    4,4,4,4,4,4,4,4,
    0,0,0,0,0,0,0,0
};

[[nodiscard]] constexpr bool is_valid_codepoint(uint32_t cp) noexcept
{
    if (cp > 0x10FFFF) return false;
    if (cp >= 0xD800 && cp <= 0xDFFF) return false;
    return true;
}

[[nodiscard]] constexpr bool is_shortest_form(uint32_t cp, uint8_t seq_len) noexcept
{
    if (seq_len == 2 && cp < 0x80)   return false;
    if (seq_len == 3 && cp < 0x800)  return false;
    if (seq_len == 4 && cp < 0x10000) return false;
    return true;
}

// 单码点解码 (内联热点)
[[nodiscard]] FORCE_INLINE
bool utf8_decode_one(const uint8_t* p, const uint8_t* end,
                     uint32_t* out_cp, size_t* out_len) noexcept
{
    uint8_t lead = *p;
    *out_cp = 0xFFFD;
    *out_len = 1;

    // ASCII 快速路径
    if (lead < 0x80) [[likely]]
    {
        *out_cp = lead;
        return true;
    }

    uint8_t seq_len = k_utf8_seq_len[lead];
    if (seq_len == 0) return false;

    if (p + seq_len > end)
    {
        *out_len = static_cast<size_t>(end - p);
        return false;
    }

    // 校验 continuation 字节
    for (uint8_t i = 1; i < seq_len; ++i)
    {
        if ((p[i] & 0xC0) != 0x80) return false;
    }

    uint32_t cp = 0;
    if (seq_len == 2)
    {
        cp = (static_cast<uint32_t>(lead & 0x1F) << 6) | (p[1] & 0x3F);
    }
    else if (seq_len == 3)
    {
        cp = (static_cast<uint32_t>(lead & 0x0F) << 12)
           | (static_cast<uint32_t>(p[1] & 0x3F) << 6)
           | (p[2] & 0x3F);
    }
    else
    {
        cp = (static_cast<uint32_t>(lead & 0x07) << 18)
           | (static_cast<uint32_t>(p[1] & 0x3F) << 12)
           | (static_cast<uint32_t>(p[2] & 0x3F) << 6)
           | (p[3] & 0x3F);
    }

    if (!is_valid_codepoint(cp)) { *out_len = seq_len; return false; }
    if (!is_shortest_form(cp, seq_len)) { *out_len = seq_len; return false; }

    *out_cp = cp;
    *out_len = seq_len;
    return true;
}

// 单码点编码 (内联热点)
[[nodiscard]] FORCE_INLINE
bool utf8_encode_one(uint32_t cp, uint8_t* buf, size_t* out_len) noexcept
{
    if (cp >= 0x80) [[unlikely]]
    {
        if (!is_valid_codepoint(cp)) return false;
        if (cp < 0x800)
        {
            buf[0] = static_cast<uint8_t>(0xC0 | (cp >> 6));
            buf[1] = static_cast<uint8_t>(0x80 | (cp & 0x3F));
            *out_len = 2;
        }
        else if (cp < 0x10000)
        {
            buf[0] = static_cast<uint8_t>(0xE0 | (cp >> 12));
            buf[1] = static_cast<uint8_t>(0x80 | ((cp >> 6) & 0x3F));
            buf[2] = static_cast<uint8_t>(0x80 | (cp & 0x3F));
            *out_len = 3;
        }
        else
        {
            buf[0] = static_cast<uint8_t>(0xF0 | (cp >> 18));
            buf[1] = static_cast<uint8_t>(0x80 | ((cp >> 12) & 0x3F));
            buf[2] = static_cast<uint8_t>(0x80 | ((cp >> 6) & 0x3F));
            buf[3] = static_cast<uint8_t>(0x80 | (cp & 0x3F));
            *out_len = 4;
        }
        return true;
    }
    // ASCII 快速路径
    buf[0] = static_cast<uint8_t>(cp);
    *out_len = 1;
    return true;
}

// SIMD 风格码点计数: 统计非 continuation 字节
// 码点数 = 非 continuation 字节数 = !(b & 0xC0 == 0x80) 的字节数
// 8 字节一组批量处理
[[nodiscard]] FORCE_INLINE
size_t count_codepoints(const uint8_t* p, const uint8_t* end) noexcept
{
    size_t count = 0;
    // 8 字节 SIMD 分组
    while (p + 8 <= end)
    {
        uint64_t chunk;
        std::memcpy(&chunk, p, 8);
        for (int i = 0; i < 8; ++i)
        {
            uint8_t b = static_cast<uint8_t>(chunk >> (i * 8));
            if ((b & 0xC0) != 0x80) ++count;
        }
        p += 8;
    }
    // 尾部逐字节
    while (p < end)
    {
        if ((*p & 0xC0) != 0x80) ++count;
        ++p;
    }
    return count;
}

[[nodiscard]] FORCE_INLINE
const uint8_t* advance_codepoint(const uint8_t* p, const uint8_t* end) noexcept
{
    if (p >= end) return end;
    uint8_t lead = *p;
    uint8_t seq = k_utf8_seq_len[lead];
    if (seq == 0) seq = 1;
    const uint8_t* next = p + seq;
    return next > end ? end : next;
}

[[nodiscard]] FORCE_INLINE
const uint8_t* retreat_codepoint(const uint8_t* begin, const uint8_t* p) noexcept
{
    if (p <= begin) return begin;
    const uint8_t* q = p - 1;
    while (q > begin && (*q & 0xC0) == 0x80) --q;
    return q;
}

} // namespace detail_utf8

// === 编解码函数 (对外导出) ===

[[nodiscard]] constexpr FORCE_INLINE
char32_t to_char(uint32_t cp) noexcept
{
    return static_cast<char32_t>(cp);
}

[[nodiscard]] constexpr FORCE_INLINE
uint32_t to_int(char32_t ch) noexcept
{
    return static_cast<uint32_t>(ch);
}

// UTF-8 → 码点数组 (ASCII 批量快速路径)
[[nodiscard]] FORCE_INLINE
size_t utf8_to_codepoints(const char* __restrict src, size_t src_len,
                           uint32_t* __restrict out, size_t out_cap,
                           bool* out_has_err = nullptr) noexcept
{
    const uint8_t* p = reinterpret_cast<const uint8_t*>(src);
    const uint8_t* end = p + src_len;
    size_t count = 0;
    bool has_err = false;

    while (p < end)
    {
        // ASCII 快速路径: 批量处理连续 ASCII
        if (*p < 0x80) [[likely]]
        {
            const uint8_t* ascii_start = p;
            while (p < end && *p < 0x80) ++p;
            size_t ascii_n = static_cast<size_t>(p - ascii_start);
            if (count + ascii_n <= out_cap)
            {
                for (size_t i = 0; i < ascii_n; ++i)
                {
                    out[count + i] = ascii_start[i];
                }
            }
            count += ascii_n;
            continue;
        }

        uint32_t cp = 0;
        size_t len = 0;
        bool ok = detail_utf8::utf8_decode_one(p, end, &cp, &len);
        if (!ok) has_err = true;
        if (count < out_cap) out[count] = cp;
        ++count;
        p += len;
    }

    if (out_has_err) *out_has_err = has_err;
    return count;
}

[[nodiscard]] FORCE_INLINE
size_t codepoints_to_char32(const uint32_t* cps, size_t cp_count,
                             char32_t* __restrict out, size_t out_cap,
                             bool* out_has_err = nullptr) noexcept
{
    bool has_err = false;
    for (size_t i = 0; i < cp_count; ++i)
    {
        uint32_t cp = cps[i];
        if (!detail_utf8::is_valid_codepoint(cp))
        {
            has_err = true;
            cp = 0xFFFD;
        }
        if (i < out_cap) out[i] = static_cast<char32_t>(cp);
    }
    if (out_has_err) *out_has_err = has_err;
    return cp_count;
}

// 码点数组 → UTF-8 (批量编码, 内联写入, ASCII 批量快速路径)
[[nodiscard]] FORCE_INLINE
size_t char32_to_utf8(const char32_t* src, size_t src_len,
                       char* __restrict out, size_t out_cap,
                       bool* out_has_err = nullptr) noexcept
{
    uint8_t* o = reinterpret_cast<uint8_t*>(out);
    size_t pos = 0;
    bool has_err = false;

    for (size_t i = 0; i < src_len; ++i)
    {
        uint32_t cp = static_cast<uint32_t>(src[i]);

        // ASCII 快速路径: 批量处理连续 ASCII 码点
        if (cp < 0x80) [[likely]]
        {
            size_t j = i + 1;
            while (j < src_len && static_cast<uint32_t>(src[j]) < 0x80) ++j;
            size_t ascii_n = j - i;
            if (pos + ascii_n <= out_cap)
            {
                for (size_t k = 0; k < ascii_n; ++k)
                {
                    o[pos + k] = static_cast<uint8_t>(src[i + k]);
                }
            }
            pos += ascii_n;
            i = j - 1;
            continue;
        }

        if (!detail_utf8::is_valid_codepoint(cp))
        {
            has_err = true;
            cp = 0xFFFD;
        }

        // 内联编码直接写入输出缓冲区
        if (cp < 0x800)
        {
            if (pos + 2 <= out_cap)
            {
                o[pos]     = static_cast<uint8_t>(0xC0 | (cp >> 6));
                o[pos + 1] = static_cast<uint8_t>(0x80 | (cp & 0x3F));
            }
            pos += 2;
        }
        else if (cp < 0x10000)
        {
            if (pos + 3 <= out_cap)
            {
                o[pos]     = static_cast<uint8_t>(0xE0 | (cp >> 12));
                o[pos + 1] = static_cast<uint8_t>(0x80 | ((cp >> 6) & 0x3F));
                o[pos + 2] = static_cast<uint8_t>(0x80 | (cp & 0x3F));
            }
            pos += 3;
        }
        else
        {
            if (pos + 4 <= out_cap)
            {
                o[pos]     = static_cast<uint8_t>(0xF0 | (cp >> 18));
                o[pos + 1] = static_cast<uint8_t>(0x80 | ((cp >> 12) & 0x3F));
                o[pos + 2] = static_cast<uint8_t>(0x80 | ((cp >> 6) & 0x3F));
                o[pos + 3] = static_cast<uint8_t>(0x80 | (cp & 0x3F));
            }
            pos += 4;
        }
    }
    if (out_has_err) *out_has_err = has_err;
    return pos;
}
