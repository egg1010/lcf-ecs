// model.hpp - 存档模型: 安全限制 / 存档头 / 实体重映射 / 元数据
#pragma once

#include "part/dense.hpp"
#include "part/id_table.hpp"
#include "part/safety.hpp"
#include "ecs/entity/entity.hpp"
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <string_view>

namespace serialize {

// ECS 序列化安全限制 (扩展通用 safety_limits, 增加实体数量上限)
struct serialize_limits : safety_limits {
    size_t max_entity_count = 10 * 1000 * 1000;
};

enum class archive_format : uint8_t {
    json,
    binary,
    protobuf,
    flatbuffer
};

enum class load_mode : uint8_t {
    replace,
    append,
    merge
};

enum class load_policy : uint8_t {
    strict,
    best_effort
};

namespace detail {

// 存档头信息
struct archive_header {
    uint32_t archive_version = 1;
    uint32_t engine_version  = 0;
};

// 实体重映射表 (旧ID→新实体, 以旧 id 直接索引)
struct remap_entry
{
    ecs::entity entity{};
    uint32_t version{0};
};

struct entity_remap
{
    // 旧 id 直接作为条目下标, 超出 int 范围视为损坏存档
    static constexpr uint32_t max_old_id = static_cast<uint32_t>(std::numeric_limits<int>::max());

    id_table<remap_entry> map_{};
    dense<uint8_t> new_mark{};        // 下标=新实体 index

    [[nodiscard]] size_t size() const noexcept { return map_.size(); }
    [[nodiscard]] bool empty() const noexcept { return map_.empty(); }

    void set(uint32_t old_id, ecs::entity e, uint32_t ver) noexcept
    {
        if (old_id > max_old_id)
        {
            return;
        }
        (void)map_.ensure(static_cast<int>(old_id), remap_entry{e, ver});
        const uint32_t nidx = e.parts_.index_;
        if (new_mark.size() <= nidx)
        {
            new_mark.resize_fill(static_cast<size_t>(nidx) + 1, 0);
        }
        new_mark[nidx] = 1;
    }

    [[nodiscard]] bool find(uint32_t old_id, ecs::entity& out) const noexcept
    {
        if (old_id > max_old_id)
        {
            return false;
        }
        const remap_entry* entry = map_.find(static_cast<int>(old_id));
        if (entry == nullptr)
        {
            return false;
        }
        out = entry->entity;
        return true;
    }

    [[nodiscard]] uint32_t version(uint32_t old_id) const noexcept
    {
        if (old_id > max_old_id)
        {
            return 0;
        }
        const remap_entry* entry = map_.find(static_cast<int>(old_id));
        return entry != nullptr ? entry->version : 0;
    }

    [[nodiscard]] bool is_new_entity(uint32_t new_idx) const noexcept
    {
        return new_idx < new_mark.size() && new_mark[new_idx] != 0;
    }
};

// 元数据键值对
struct metadata_entry {
    std::string key;
    std::string value;
};

} // namespace detail
} // namespace serialize
