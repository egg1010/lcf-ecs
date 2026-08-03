// archive_types.hpp - 归档公共类型定义
// 被 archive_logic.hpp 和 serializer.hpp 共享
#pragma once

#include "../part/dense.hpp"
#include "../part/safety.hpp"
#include <cstdint>
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

} // namespace detail
} // namespace serialize

