#pragma once

// 模块 UTF-8 编解码核心 (无依赖, 供 utf8pp / utf8_view 共享)

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <bit>
#include "../force_inline.hpp"

// 指令集 SSE2 检测: x86/x64 启用, ARM64 标量回退
#if defined(__SSE2__) || (defined(_MSC_VER) && (defined(_M_IX86) || defined(_M_X64)))
#include <emmintrin.h>
#define LCF_UTF8_HAS_SSE2 1
#else
#define LCF_UTF8_HAS_SSE2 0
#endif

namespace detail_utf8 {

// 序列长度查表: 0=非法首字节, 1=纯 ASCII, 2/3/4=多字节序列
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

    // 纯 ASCII 快速路径
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

    // 校验 continuation 字节并解码: 按 seq_len 展开, OR 合并校验减少分支
    uint32_t cp = 0;
    if (seq_len == 2)
    {
        uint32_t b1 = p[1];
        if (((b1 ^ 0x80) & 0xC0) != 0) return false;
        cp = (static_cast<uint32_t>(lead & 0x1F) << 6) | (b1 & 0x3F);
    }
    else if (seq_len == 3)
    {
        uint32_t b1 = p[1], b2 = p[2];
        if ((((b1 ^ 0x80) | (b2 ^ 0x80)) & 0xC0) != 0) return false;
        cp = (static_cast<uint32_t>(lead & 0x0F) << 12)
           | (static_cast<uint32_t>(b1 & 0x3F) << 6)
           | (b2 & 0x3F);
    }
    else
    {
        uint32_t b1 = p[1], b2 = p[2], b3 = p[3];
        if ((((b1 ^ 0x80) | (b2 ^ 0x80) | (b3 ^ 0x80)) & 0xC0) != 0) return false;
        cp = (static_cast<uint32_t>(lead & 0x07) << 18)
           | (static_cast<uint32_t>(b1 & 0x3F) << 12)
           | (static_cast<uint32_t>(b2 & 0x3F) << 6)
           | (b3 & 0x3F);
    }

    if (!is_valid_codepoint(cp)) { *out_len = seq_len; return false; }
    if (!is_shortest_form(cp, seq_len)) { *out_len = seq_len; return false; }

    *out_cp = cp;
    *out_len = seq_len;
    return true;
}

// 无校验快速解码: 仅查表+位移, 跳过 continuation/codepoint/shortest_form 校验
// 用于已确认合法 UTF-8 的迭代器/遍历热路径 (比 utf8_decode_one 少 2-3 个分支)
[[nodiscard]] FORCE_INLINE
char32_t utf8_decode_unchecked(const uint8_t* p) noexcept
{
    uint8_t lead = *p;
    if (lead < 0x80) return static_cast<char32_t>(lead);
    uint8_t seq_len = k_utf8_seq_len[lead];
    if (seq_len == 2)
    {
        return static_cast<char32_t>(
            (static_cast<uint32_t>(lead & 0x1F) << 6) | (p[1] & 0x3F));
    }
    if (seq_len == 3)
    {
        return static_cast<char32_t>(
            (static_cast<uint32_t>(lead & 0x0F) << 12)
            | (static_cast<uint32_t>(p[1] & 0x3F) << 6)
            | (p[2] & 0x3F));
    }
    if (seq_len == 4)
    {
        return static_cast<char32_t>(
            (static_cast<uint32_t>(lead & 0x07) << 18)
            | (static_cast<uint32_t>(p[1] & 0x3F) << 12)
            | (static_cast<uint32_t>(p[2] & 0x3F) << 6)
            | (p[3] & 0x3F));
    }
    return 0xFFFD;
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
    // 纯 ASCII 快速路径
    buf[0] = static_cast<uint8_t>(cp);
    *out_len = 1;
    return true;
}

// 指令集 SSE2 码点计数: 统计非 continuation 字节 (= 码点数)
// 16 字节/迭代, pcmpeqb+pmovmskb 替代 SWAR popcount
// 纯 ASCII 块 (movemask==0) 跳过 cmpeq, 非 ASCII 块统计 cont 字节后相减
#if LCF_UTF8_HAS_SSE2
[[nodiscard]] FORCE_INLINE
size_t count_codepoints(const uint8_t* p, const uint8_t* end) noexcept
{
    size_t count = 0;
    const __m128i mask_C0 = _mm_set1_epi8(static_cast<char>(0xC0));
    const __m128i mask_80 = _mm_set1_epi8(static_cast<char>(0x80));
    // 32 字节主循环: 2x SSE2 (16+16)
    while (p + 32 <= end)
    {
        __m128i v0 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(p));
        __m128i v1 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(p + 16));
        // 纯 ASCII 快速路径: 原值 movemask==0 → 无字节 bit7 置位 → 全 ASCII
        uint32_t hi_mask = (static_cast<uint32_t>(_mm_movemask_epi8(v1)) << 16)
                         | static_cast<uint16_t>(_mm_movemask_epi8(v0));
        if (hi_mask == 0)
        {
            count += 32;
        }
        else
        {
            // 非 ASCII: 统计 continuation 字节, lead = 32 - cont
            __m128i m0 = _mm_and_si128(v0, mask_C0);
            __m128i m1 = _mm_and_si128(v1, mask_C0);
            __m128i cont0 = _mm_cmpeq_epi8(m0, mask_80);
            __m128i cont1 = _mm_cmpeq_epi8(m1, mask_80);
            uint32_t cont_mask = (static_cast<uint32_t>(_mm_movemask_epi8(cont1)) << 16)
                               | static_cast<uint16_t>(_mm_movemask_epi8(cont0));
            count += 32 - static_cast<size_t>(std::popcount(cont_mask));
        }
        p += 32;
    }
    // 尾部 16 字节 SSE2
    if (p + 16 <= end)
    {
        __m128i v = _mm_loadu_si128(reinterpret_cast<const __m128i*>(p));
        uint16_t hi_mask = static_cast<uint16_t>(_mm_movemask_epi8(v));
        if (hi_mask == 0)
        {
            count += 16;
        }
        else
        {
            __m128i m = _mm_and_si128(v, mask_C0);
            __m128i cont = _mm_cmpeq_epi8(m, mask_80);
            uint16_t cont_mask = static_cast<uint16_t>(_mm_movemask_epi8(cont));
            count += 16 - static_cast<size_t>(std::popcount(cont_mask));
        }
        p += 16;
    }
    // 尾部逐字节
    while (p < end)
    {
        if ((*p & 0xC0) != 0x80) ++count;
        ++p;
    }
    return count;
}
#else
// 算法 SWAR 码点计数 (ARM64 标量回退): 统计非 continuation 字节
// 32 字节主循环: 4 路并行 popcount
[[nodiscard]] FORCE_INLINE
size_t count_codepoints(const uint8_t* p, const uint8_t* end) noexcept
{
    size_t count = 0;
    while (p + 32 <= end)
    {
        uint64_t c0, c1, c2, c3;
        std::memcpy(&c0, p,      8);
        std::memcpy(&c1, p + 8,  8);
        std::memcpy(&c2, p + 16, 8);
        std::memcpy(&c3, p + 24, 8);
        uint64_t x0 = c0 ^ 0x8080808080808080ULL;
        uint64_t x1 = c1 ^ 0x8080808080808080ULL;
        uint64_t x2 = c2 ^ 0x8080808080808080ULL;
        uint64_t x3 = c3 ^ 0x8080808080808080ULL;
        uint64_t nc0 = (x0 | (x0 << 1)) & 0x8080808080808080ULL;
        uint64_t nc1 = (x1 | (x1 << 1)) & 0x8080808080808080ULL;
        uint64_t nc2 = (x2 | (x2 << 1)) & 0x8080808080808080ULL;
        uint64_t nc3 = (x3 | (x3 << 1)) & 0x8080808080808080ULL;
        count += static_cast<size_t>(std::popcount(nc0))
              + static_cast<size_t>(std::popcount(nc1))
              + static_cast<size_t>(std::popcount(nc2))
              + static_cast<size_t>(std::popcount(nc3));
        p += 32;
    }
    while (p + 8 <= end)
    {
        uint64_t chunk;
        std::memcpy(&chunk, p, 8);
        uint64_t x = chunk ^ 0x8080808080808080ULL;
        uint64_t non_cont = (x | (x << 1)) & 0x8080808080808080ULL;
        count += static_cast<size_t>(std::popcount(non_cont));
        p += 8;
    }
    while (p < end)
    {
        if ((*p & 0xC0) != 0x80) ++count;
        ++p;
    }
    return count;
}
#endif

// 指令集 SSE2 码点计数 + ASCII 检测 (两阶段: 首个非 ASCII 块后跳过 ASCII 检测)
// 供 ensure_cp_count / init_from_utf8 使用: 避免二次扫描
// 变量 all_ascii = 全部字节 < 0x80 (码点数 = 字节数)
// 优化: 非 ASCII 文本 (如中文) 首块即确定 all_ascii=false, 后续块跳过 hi_mask 计算
//   节省 2x pmovmskb + or + cmp = ~3 周期/块, 192 块 (6KB) 省 ~60ns
#if LCF_UTF8_HAS_SSE2
[[nodiscard]] FORCE_INLINE
size_t count_codepoints_and_ascii(const uint8_t* p, const uint8_t* end, bool& all_ascii) noexcept
{
    size_t count = 0;
    all_ascii = true;
    const __m128i mask_C0 = _mm_set1_epi8(static_cast<char>(0xC0));
    const __m128i mask_80 = _mm_set1_epi8(static_cast<char>(0x80));
    // 阶段 1: ASCII 检测 + 计数 (首个非 ASCII 块后退出)
    while (p + 32 <= end)
    {
        __m128i v0 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(p));
        __m128i v1 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(p + 16));
        uint32_t hi_mask = (static_cast<uint32_t>(_mm_movemask_epi8(v1)) << 16)
                         | static_cast<uint16_t>(_mm_movemask_epi8(v0));
        if (hi_mask == 0)
        {
            count += 32;
            p += 32;
            continue;
        }
        // 首个非 ASCII 块: 计数并退出阶段 1
        all_ascii = false;
        __m128i m0 = _mm_and_si128(v0, mask_C0);
        __m128i m1 = _mm_and_si128(v1, mask_C0);
        __m128i cont0 = _mm_cmpeq_epi8(m0, mask_80);
        __m128i cont1 = _mm_cmpeq_epi8(m1, mask_80);
        uint32_t cont_mask = (static_cast<uint32_t>(_mm_movemask_epi8(cont1)) << 16)
                           | static_cast<uint16_t>(_mm_movemask_epi8(cont0));
        count += 32 - static_cast<size_t>(std::popcount(cont_mask));
        p += 32;
        break;
    }
    // 阶段 2: 非 ASCII 快速计数 (跳过 hi_mask 计算, all_ascii 已为 false)
    while (p + 32 <= end)
    {
        __m128i v0 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(p));
        __m128i v1 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(p + 16));
        __m128i m0 = _mm_and_si128(v0, mask_C0);
        __m128i m1 = _mm_and_si128(v1, mask_C0);
        __m128i cont0 = _mm_cmpeq_epi8(m0, mask_80);
        __m128i cont1 = _mm_cmpeq_epi8(m1, mask_80);
        uint32_t cont_mask = (static_cast<uint32_t>(_mm_movemask_epi8(cont1)) << 16)
                           | static_cast<uint16_t>(_mm_movemask_epi8(cont0));
        count += 32 - static_cast<size_t>(std::popcount(cont_mask));
        p += 32;
    }
    // 尾部 16 字节 SSE2
    if (p + 16 <= end)
    {
        __m128i v = _mm_loadu_si128(reinterpret_cast<const __m128i*>(p));
        uint16_t hi_mask = static_cast<uint16_t>(_mm_movemask_epi8(v));
        if (hi_mask == 0)
        {
            count += 16;
        }
        else
        {
            all_ascii = false;
            __m128i m = _mm_and_si128(v, mask_C0);
            __m128i cont = _mm_cmpeq_epi8(m, mask_80);
            uint16_t cont_mask = static_cast<uint16_t>(_mm_movemask_epi8(cont));
            count += 16 - static_cast<size_t>(std::popcount(cont_mask));
        }
        p += 16;
    }
    while (p < end)
    {
        if ((*p & 0xC0) != 0x80) ++count;
        if (*p & 0x80) all_ascii = false;
        ++p;
    }
    return count;
}

// 融合扫描+拷贝: 单次遍历同时完成码点计数 + ASCII 检测 + memcpy
// 比分开的 count_codepoints_and_ascii + memcpy 少一次源数据读取
// 供 init_from_utf8 使用: 构造时源数据 s 在 cache, 融合后仅读一次
#if LCF_UTF8_HAS_SSE2
[[nodiscard]] FORCE_INLINE
size_t fused_count_copy_and_ascii(const uint8_t* src, uint8_t* dst, size_t len, bool& all_ascii) noexcept
{
    size_t count = 0;
    all_ascii = true;
    const uint8_t* p = src;
    const uint8_t* end = src + len;
    uint8_t* d = dst;
    const __m128i mask_C0 = _mm_set1_epi8(static_cast<char>(0xC0));
    const __m128i mask_80 = _mm_set1_epi8(static_cast<char>(0x80));
    // 阶段 1: ASCII 检测 + 计数 + 拷贝 (首个非 ASCII 块后退出)
    while (p + 32 <= end)
    {
        __m128i v0 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(p));
        __m128i v1 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(p + 16));
        _mm_storeu_si128(reinterpret_cast<__m128i*>(d), v0);
        _mm_storeu_si128(reinterpret_cast<__m128i*>(d + 16), v1);
        uint32_t hi_mask = (static_cast<uint32_t>(_mm_movemask_epi8(v1)) << 16)
                         | static_cast<uint16_t>(_mm_movemask_epi8(v0));
        if (hi_mask == 0)
        {
            count += 32;
            p += 32;
            d += 32;
            continue;
        }
        all_ascii = false;
        __m128i m0 = _mm_and_si128(v0, mask_C0);
        __m128i m1 = _mm_and_si128(v1, mask_C0);
        __m128i cont0 = _mm_cmpeq_epi8(m0, mask_80);
        __m128i cont1 = _mm_cmpeq_epi8(m1, mask_80);
        uint32_t cont_mask = (static_cast<uint32_t>(_mm_movemask_epi8(cont1)) << 16)
                           | static_cast<uint16_t>(_mm_movemask_epi8(cont0));
        count += 32 - static_cast<size_t>(std::popcount(cont_mask));
        p += 32;
        d += 32;
        // 阶段 2: 非 ASCII 快速计数 + 拷贝 (跳过 hi_mask 计算)
        while (p + 32 <= end)
        {
            __m128i v0b = _mm_loadu_si128(reinterpret_cast<const __m128i*>(p));
            __m128i v1b = _mm_loadu_si128(reinterpret_cast<const __m128i*>(p + 16));
            _mm_storeu_si128(reinterpret_cast<__m128i*>(d), v0b);
            _mm_storeu_si128(reinterpret_cast<__m128i*>(d + 16), v1b);
            __m128i m0b = _mm_and_si128(v0b, mask_C0);
            __m128i m1b = _mm_and_si128(v1b, mask_C0);
            __m128i cont0b = _mm_cmpeq_epi8(m0b, mask_80);
            __m128i cont1b = _mm_cmpeq_epi8(m1b, mask_80);
            uint32_t cont_maskb = (static_cast<uint32_t>(_mm_movemask_epi8(cont1b)) << 16)
                                 | static_cast<uint16_t>(_mm_movemask_epi8(cont0b));
            count += 32 - static_cast<size_t>(std::popcount(cont_maskb));
            p += 32;
            d += 32;
        }
        break;
    }
    // 尾部 16 字节
    if (p + 16 <= end)
    {
        __m128i v = _mm_loadu_si128(reinterpret_cast<const __m128i*>(p));
        _mm_storeu_si128(reinterpret_cast<__m128i*>(d), v);
        uint16_t hi_mask = static_cast<uint16_t>(_mm_movemask_epi8(v));
        if (hi_mask == 0)
        {
            count += 16;
        }
        else
        {
            all_ascii = false;
            __m128i m = _mm_and_si128(v, mask_C0);
            __m128i cont = _mm_cmpeq_epi8(m, mask_80);
            uint16_t cont_mask = static_cast<uint16_t>(_mm_movemask_epi8(cont));
            count += 16 - static_cast<size_t>(std::popcount(cont_mask));
        }
        p += 16;
        d += 16;
    }
    // 尾部逐字节
    while (p < end)
    {
        *d = *p;
        if ((*p & 0xC0) != 0x80) ++count;
        if (*p & 0x80) all_ascii = false;
        ++p;
        ++d;
    }
    return count;
}
#else
[[nodiscard]] FORCE_INLINE
size_t fused_count_copy_and_ascii(const uint8_t* src, uint8_t* dst, size_t len, bool& all_ascii) noexcept
{
    size_t count = 0;
    all_ascii = true;
    const uint8_t* p = src;
    const uint8_t* end = src + len;
    uint8_t* d = dst;
    while (p + 32 <= end)
    {
        uint64_t c0, c1, c2, c3;
        std::memcpy(&c0, p, 8);
        std::memcpy(&c1, p + 8, 8);
        std::memcpy(&c2, p + 16, 8);
        std::memcpy(&c3, p + 24, 8);
        std::memcpy(d, p, 32);
        uint64_t or_hi = (c0 | c1 | c2 | c3) & 0x8080808080808080ULL;
        if (or_hi)
        {
            all_ascii = false;
            uint64_t x0 = c0 ^ 0x8080808080808080ULL;
            uint64_t x1 = c1 ^ 0x8080808080808080ULL;
            uint64_t x2 = c2 ^ 0x8080808080808080ULL;
            uint64_t x3 = c3 ^ 0x8080808080808080ULL;
            uint64_t nc0 = (x0 | (x0 << 1)) & 0x8080808080808080ULL;
            uint64_t nc1 = (x1 | (x1 << 1)) & 0x8080808080808080ULL;
            uint64_t nc2 = (x2 | (x2 << 1)) & 0x8080808080808080ULL;
            uint64_t nc3 = (x3 | (x3 << 1)) & 0x8080808080808080ULL;
            count += static_cast<size_t>(std::popcount(nc0))
                  + static_cast<size_t>(std::popcount(nc1))
                  + static_cast<size_t>(std::popcount(nc2))
                  + static_cast<size_t>(std::popcount(nc3));
        }
        else
        {
            count += 32;
        }
        p += 32;
        d += 32;
    }
    while (p + 8 <= end)
    {
        uint64_t chunk;
        std::memcpy(&chunk, p, 8);
        std::memcpy(d, p, 8);
        uint64_t hi = chunk & 0x8080808080808080ULL;
        if (hi)
        {
            all_ascii = false;
            uint64_t x = chunk ^ 0x8080808080808080ULL;
            uint64_t non_cont = (x | (x << 1)) & 0x8080808080808080ULL;
            count += static_cast<size_t>(std::popcount(non_cont));
        }
        else
        {
            count += 8;
        }
        p += 8;
        d += 8;
    }
    while (p < end)
    {
        *d = *p;
        if ((*p & 0xC0) != 0x80) ++count;
        if (*p & 0x80) all_ascii = false;
        ++p;
        ++d;
    }
    return count;
}
#endif
#else
[[nodiscard]] FORCE_INLINE
size_t count_codepoints_and_ascii(const uint8_t* p, const uint8_t* end, bool& all_ascii) noexcept
{
    size_t count = 0;
    all_ascii = true;
    while (p + 32 <= end)
    {
        uint64_t c0, c1, c2, c3;
        std::memcpy(&c0, p,      8);
        std::memcpy(&c1, p + 8,  8);
        std::memcpy(&c2, p + 16, 8);
        std::memcpy(&c3, p + 24, 8);
        uint64_t or_hi = (c0 | c1 | c2 | c3) & 0x8080808080808080ULL;
        if (or_hi)
        {
            all_ascii = false;
            uint64_t x0 = c0 ^ 0x8080808080808080ULL;
            uint64_t x1 = c1 ^ 0x8080808080808080ULL;
            uint64_t x2 = c2 ^ 0x8080808080808080ULL;
            uint64_t x3 = c3 ^ 0x8080808080808080ULL;
            uint64_t nc0 = (x0 | (x0 << 1)) & 0x8080808080808080ULL;
            uint64_t nc1 = (x1 | (x1 << 1)) & 0x8080808080808080ULL;
            uint64_t nc2 = (x2 | (x2 << 1)) & 0x8080808080808080ULL;
            uint64_t nc3 = (x3 | (x3 << 1)) & 0x8080808080808080ULL;
            count += static_cast<size_t>(std::popcount(nc0))
                  + static_cast<size_t>(std::popcount(nc1))
                  + static_cast<size_t>(std::popcount(nc2))
                  + static_cast<size_t>(std::popcount(nc3));
        }
        else
        {
            count += 32;
        }
        p += 32;
    }
    while (p + 8 <= end)
    {
        uint64_t chunk;
        std::memcpy(&chunk, p, 8);
        uint64_t hi = chunk & 0x8080808080808080ULL;
        if (hi)
        {
            all_ascii = false;
            uint64_t x = chunk ^ 0x8080808080808080ULL;
            uint64_t non_cont = (x | (x << 1)) & 0x8080808080808080ULL;
            count += static_cast<size_t>(std::popcount(non_cont));
        }
        else
        {
            count += 8;
        }
        p += 8;
    }
    while (p < end)
    {
        if ((*p & 0xC0) != 0x80) ++count;
        if (*p & 0x80) all_ascii = false;
        ++p;
    }
    return count;
}
#endif

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

// 批量前进 n 个码点 (SWAR 加速 ASCII 段跳过)
// 比逐个 advance_codepoint 快: ASCII 段用 SWAR 一次跳 8 字节
[[nodiscard]] FORCE_INLINE
const uint8_t* advance_codepoints(const uint8_t* p, const uint8_t* end, size_t n) noexcept
{
    while (n > 0 && p < end)
    {
        // 纯 ASCII 快速路径: SWAR 批量跳过连续 ASCII
        if (*p < 0x80) [[likely]]
        {
            while (n >= 8 && p + 8 <= end)
            {
                uint64_t chunk;
                std::memcpy(&chunk, p, 8);
                uint64_t hi = chunk & 0x8080808080808080ULL;
                if (hi != 0)
                {
                    // 遇到非 ASCII, 跳过已有的 ASCII 字节
                    int bit = std::countr_zero(hi);
                    size_t ascii_in_chunk = static_cast<size_t>(bit) >> 3;
                    p += ascii_in_chunk;
                    n -= ascii_in_chunk;
                    break;
                }
                p += 8;
                n -= 8;
            }
            // 尾部逐字节 ASCII
            while (n > 0 && p < end && *p < 0x80)
            {
                ++p;
                --n;
            }
            if (p == end || n == 0) return p;
            if (*p >= 0x80)
            {
                // 落到多字节字符, 继续走 advance_codepoint
            }
            else
            {
                continue;  // 剩余 n > 0 但仍 ASCII
            }
        }
        p = advance_codepoint(p, end);
        --n;
    }
    return p;
}

[[nodiscard]] FORCE_INLINE
const uint8_t* retreat_codepoint(const uint8_t* begin, const uint8_t* p) noexcept
{
    if (p <= begin) return begin;
    const uint8_t* q = p - 1;
    // 编码 UTF-8 最多 4 字节, continuation 最多 3 次; 限制回退避免越界读
    int n = 3;
    while (n > 0 && q > begin && (*q & 0xC0) == 0x80) { --q; --n; }
    return q;
}

} // 命名空间 detail_utf8

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

// 解码 UTF-8 → 码点数组 (ASCII 批量快速路径)
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
        // 纯 ASCII 快速路径: SWAR 批量扫描连续 ASCII 字节
        if (*p < 0x80) [[likely]]
        {
            const uint8_t* ascii_start = p;
            // 算法 SWAR: 8 字节一组, 找到首个非 ASCII 字节 (bit7=1)
            while (p + 8 <= end)
            {
                uint64_t chunk;
                std::memcpy(&chunk, p, 8);
                uint64_t hi = chunk & 0x8080808080808080ULL;
                if (hi == 0) { p += 8; continue; }
                // 找到首个 bit7=1 的字节
                int bit = std::countr_zero(hi);
                p += static_cast<size_t>(bit) >> 3;
                break;
            }
            // 尾部逐字节 (或未走 SWAR 的短块)
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

        // 纯 ASCII 快速路径: SWAR 批量扫描连续 ASCII 码点
        // 一次读 2 个 char32_t (8 字节), 用位掩码检测均为 ASCII
        //   码点 cp < 0x80 等价于 (cp & 0xFFFFFF80) == 0 (高 3 字节为 0 且低字节 < 0x80)
        if (cp < 0x80) [[likely]]
        {
            size_t j = i;
            // 2 码点一组 SWAR 扫描
            while (j + 2 <= src_len)
            {
                uint64_t chunk;
                std::memcpy(&chunk, src + j, 8);
                if ((chunk & 0xFFFFFF80FFFFFF80ULL) != 0) break;
                j += 2;
            }
            // 尾部逐个 (或非 2 对齐的剩余)
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
