#pragma once
#include <cstddef>

namespace memory {

// 所有自有池对 acquire 承诺的基础对齐下限
inline constexpr size_t base_alignment = 16;

struct memory_stats
{
    size_t reserved_bytes;       // 池当前从系统保留的总字节
    size_t live_bytes;           // 当前存活分配的可用字节
    size_t reusable_bytes;       // 当前可立即复用的空闲字节
    size_t peak_live_bytes;      // 历史峰值
    size_t acquire_count;        // 成功分配次数
    size_t release_count;        // 成功删除总次数 (软 + 硬)
    size_t soft_delete_count;    // 其中软删除次数
    size_t hard_delete_count;    // 其中硬删除次数
};

} // namespace memory
