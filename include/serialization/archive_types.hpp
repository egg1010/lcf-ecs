// archive_types.hpp - 归档公共类型定义
// 被 archive_logic.hpp 和 serializer.hpp 共享
#pragma once

#include "../part/dense.hpp"
#include "../part/safety.hpp"
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>

namespace ecs { struct entity; }

namespace serialize {

// ECS 序列化安全限制 (扩展通用 safety_limits, 增加实体数量上限)
struct serialize_limits : safety_limits {
    size_t max_entity_count = 10 * 1000 * 1000;
};

namespace detail {

// 存档头信息
struct archive_header {
    uint32_t archive_version = 1;
    uint32_t engine_version  = 0;
    uint32_t checksum        = 0;  // #A1 CRC32C 校验和 (0=未校验)
};

// 实体重映射表 (加载时建立旧→新映射)
struct entity_remap {
    dense<ecs::entity> old_to_new;
    dense<uint32_t> old_versions;
};

// 元数据键值对
struct metadata_entry {
    std::string key;
    std::string value;
};

// ====================================================================
// #A1 硬件 CRC32C 计算 (SSE4.2 _mm_crc32_u64, AVX2 子集)
// 性能: ~30GB/s, 比软件 CRC32 快两个数量级, 零外部依赖
// 项目已要求 AVX2, SSE4.2 是其子集, 可无条件使用
// ====================================================================
#if defined(__SSE4_2__) || defined(__AVX2__) || (defined(_M_X64) && !defined(__MINGW32__))
#define LCF_HAS_HW_CRC32C 1
#include <immintrin.h>
[[nodiscard]] inline uint32_t compute_crc32c(const char* data, size_t len) noexcept
{
    uint64_t crc = 0xFFFFFFFFFFFFFFFFULL;
    // 8 字节步进
    size_t i = 0;
    for (; i + 8 <= len; i += 8)
    {
        uint64_t v;
        std::memcpy(&v, data + i, 8);
        crc = _mm_crc32_u64(crc, v);
    }
    // 尾部 4 字节
    if (i + 4 <= len)
    {
        uint32_t v;
        std::memcpy(&v, data + i, 4);
        crc = _mm_crc32_u32(static_cast<uint32_t>(crc), v);
        i += 4;
    }
    // 尾部单字节
    for (; i < len; ++i)
    {
        crc = _mm_crc32_u8(static_cast<uint32_t>(crc), static_cast<uint8_t>(data[i]));
    }
    return static_cast<uint32_t>(crc ^ 0xFFFFFFFFFFFFFFFFULL);
}
#else
// 软件回退 (无 SSE4.2 时)
[[nodiscard]] inline uint32_t compute_crc32c(const char* data, size_t len) noexcept
{
    // CRC32C 多项式 (Castagnoli)
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

// 便捷重载: 从 std::string 计算 CRC32C
[[nodiscard]] inline uint32_t compute_crc32c(const std::string& s) noexcept
{
    return compute_crc32c(s.data(), s.size());
}

} // namespace detail
} // namespace serialize

