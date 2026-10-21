#pragma once

#include <bit>
#include <cstddef>
#include <cstdint>

// bit_ops - 纯标准库位运算与哈希（通用工具，无命名空间）

// 纯标准库位运算与哈希：不包含任何平台头文件
inline std::uint64_t mix_bits(std::uint64_t value) noexcept
{
    value += 0x9E3779B97F4A7C15ull;
    value = (value ^ (value >> 30)) * 0xBF58476D1CE4E5B9ull;
    value = (value ^ (value >> 27)) * 0x94D049BB133111EBull;
    return value ^ (value >> 31);
}

inline std::uint32_t count_trailing_zeros(std::uint64_t value) noexcept
{
    if (value == 0)
    {
        return 64;
    }
    return static_cast<std::uint32_t>(std::countr_zero(value));
}

inline std::uint32_t count_leading_zeros(std::uint64_t value) noexcept
{
    return static_cast<std::uint32_t>(std::countl_zero(value));
}

inline std::uint32_t popcount(std::uint64_t value) noexcept
{
    return static_cast<std::uint32_t>(std::popcount(value));
}

struct fast_rand
{
    std::uint64_t state_{0};

    explicit fast_rand(std::uint64_t seed) noexcept
        : state_(seed == 0 ? 0x9E3779B97F4A7C15ull : seed)
    {
    }

    std::uint64_t next() noexcept
    {
        std::uint64_t x = state_;
        x ^= x << 13;
        x ^= x >> 7;
        x ^= x << 17;
        state_ = x;
        return x;
    }
};

// 对齐与 2 的幂取整：全库共用，避免各模块各写一份
constexpr std::size_t align_up(std::size_t value, std::size_t alignment) noexcept
{
    return alignment == 0 ? value : value + ((alignment - (value % alignment)) % alignment);
}

constexpr bool is_power_of_two(std::size_t value) noexcept
{
    return value != 0 && (value & (value - 1)) == 0;
}

constexpr std::size_t round_up_power_of_two(std::size_t value) noexcept
{
    if (value <= 1)
    {
        return 1;
    }
    --value;
    for (std::size_t shift = 1; shift < sizeof(std::size_t) * 8; shift <<= 1)
    {
        value |= value >> shift;
    }
    return value + 1;
}
