// fnv1a.hpp - FNV-1a 哈希 (编译期 + 运行时)
// 无命名空间, 通用工具
#pragma once

#include <cstdint>
#include <cstddef>

// FNV-1a 64 位常量
inline constexpr uint64_t FNV_OFFSET_BASIS_64 = 14695981039346656037ULL;
inline constexpr uint64_t FNV_PRIME_64 = 1099511628211ULL;

// 编译期 FNV-1a (C++20 consteval)
consteval uint64_t fnv1a_consteval(const char* s) noexcept
{
    uint64_t h = FNV_OFFSET_BASIS_64;
    while (*s)
    {
        h ^= static_cast<uint8_t>(*s++);
        h *= FNV_PRIME_64;
    }
    return h;
}

// 运行时 FNV-1a
inline uint64_t fnv1a_runtime(const char* s) noexcept
{
    uint64_t h = FNV_OFFSET_BASIS_64;
    while (*s)
    {
        h ^= static_cast<uint8_t>(*s++);
        h *= FNV_PRIME_64;
    }
    return h;
}

// 带长度的运行时 FNV-1a
inline uint64_t fnv1a_runtime(const char* s, size_t len) noexcept
{
    uint64_t h = FNV_OFFSET_BASIS_64;
    for (size_t i = 0; i < len; ++i)
    {
        h ^= static_cast<uint8_t>(s[i]);
        h *= FNV_PRIME_64;
    }
    return h;
}
