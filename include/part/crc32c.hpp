// crc32c.hpp - CRC32C (Castagnoli) 校验: SSE4.2 硬件加速 + 软件回退
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>

#if defined(__SSE4_2__) || defined(__AVX2__) || (defined(_MSC_VER) && !defined(__clang__) && defined(_M_X64))
#define LCF_HAS_HW_CRC32C 1
#include <immintrin.h>
[[nodiscard]] inline uint32_t compute_crc32c(const char* data, size_t len) noexcept
{
    uint64_t crc = 0xFFFFFFFFFFFFFFFFULL;
    size_t i = 0;
    for (; i + 8 <= len; i += 8)
    {
        uint64_t v;
        std::memcpy(&v, data + i, 8);
        crc = _mm_crc32_u64(crc, v);
    }
    if (i + 4 <= len)
    {
        uint32_t v;
        std::memcpy(&v, data + i, 4);
        crc = _mm_crc32_u32(static_cast<uint32_t>(crc), v);
        i += 4;
    }
    for (; i < len; ++i)
    {
        crc = _mm_crc32_u8(static_cast<uint32_t>(crc), static_cast<uint8_t>(data[i]));
    }
    return static_cast<uint32_t>(crc ^ 0xFFFFFFFFFFFFFFFFULL);
}
#else
[[nodiscard]] inline uint32_t compute_crc32c(const char* data, size_t len) noexcept
{
    constexpr uint32_t CRC32C_POLY = 0x82F63B78u;
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; ++i)
    {
        crc ^= static_cast<uint8_t>(data[i]);
        for (int j = 0; j < 8; ++j)
        {
            crc = (crc >> 1) ^ (CRC32C_POLY & (0u - (crc & 1)));
        }
    }
    return crc ^ 0xFFFFFFFFu;
}
#endif

[[nodiscard]] inline uint32_t compute_crc32c(const std::string& s) noexcept
{
    return compute_crc32c(s.data(), s.size());
}
