// entity_identity.hpp - 实体索引保序重建 (预分配 + 顺序领取 + 空槽回收)
#pragma once

#include "../part/dense.hpp"
#include "../part/codec/archive_codec.hpp"
#include "../ecs/component.hpp"
#include "../ecs/entity_manager.hpp"
#include "../ecs/entity.hpp"
#include "model.hpp"
#include <cstddef>
#include <cstdint>

namespace serialize {

namespace entity_identity {

struct saved_entity
{
    uint32_t index;
    uint32_t version;
    uint32_t flags;
    uint32_t tag;
    uint32_t layer;
    uint32_t group_id;
};

struct saved_entity_set
{
    dense<saved_entity> entries;
    dense<uint8_t>      alive_mark;
    uint32_t            max_index = 0;

    void add(const saved_entity& e) noexcept
    {
        entries.push_back(e);
        if (e.index >= alive_mark.size())
        {
            alive_mark.resize_fill(static_cast<size_t>(e.index) + 1, 0);
        }
        alive_mark[e.index] = 1;
        if (e.index > max_index)
        {
            max_index = e.index;
        }
    }

    void reset() noexcept
    {
        entries.clear();
        alive_mark.clear();
        max_index = 0;
    }
};

inline bool rebuild_entities(ecs::manager& mgr, const saved_entity_set& saved,
                             detail::entity_remap& remap) noexcept
{
    const uint32_t max_idx = saved.max_index;
    if (max_idx == 0)
    {
        return true;
    }
    mgr.preallocate_entities(max_idx);

    dense<ecs::entity> created;
    created.resize_fill(static_cast<size_t>(max_idx) + 1, ecs::entity{});
    for (uint32_t i = 1; i <= max_idx; ++i)
    {
        created[i] = mgr.create_entity();
    }

    for (size_t k = 0; k < saved.entries.size(); ++k)
    {
        const saved_entity& s = saved.entries[k];
        if (s.index == 0 || s.index > max_idx)
        {
            continue;
        }
        remap.set(s.index, created[s.index], s.version);
        ecs::entity_state& st = mgr.get_entity_state(s.index);
        st.flags = s.flags;
        st.tag = s.tag;
        st.layer = s.layer;
        st.group_id = s.group_id;
    }

    for (uint32_t i = 1; i <= max_idx; ++i)
    {
        if (i < saved.alive_mark.size() && saved.alive_mark[i] != 0)
        {
            continue;
        }
        ecs::entity e = created[i];
        mgr.delete_entity(e);
    }
    return true;
}

// 追加模式: 不保持索引, 按顺序新建实体
inline bool append_entities(ecs::manager& mgr, const saved_entity_set& saved,
                            detail::entity_remap& remap) noexcept
{
    for (size_t k = 0; k < saved.entries.size(); ++k)
    {
        const saved_entity& s = saved.entries[k];
        ecs::entity e = mgr.create_entity();
        remap.set(s.index, e, s.version);
        ecs::entity_state& st = mgr.get_entity_state(e.parts_.index_);
        st.flags = s.flags;
        st.tag = s.tag;
        st.layer = s.layer;
        st.group_id = s.group_id;
    }
    return true;
}

// 读取存档实体表 (i/v/f/t/l/g 固定字段序)
inline bool read_entities(archive_reader& r, size_t max_entity_count,
                          saved_entity_set& saved) noexcept
{
    if (!r.enter_array())
    {
        return false;
    }
    size_t count = 0;
    while (r.next_element())
    {
        if (++count > max_entity_count)
        {
            return false;
        }
        if (!r.enter_object())
        {
            return false;
        }
        saved_entity s{};
        r.next_key(); s.index = r.read_u32();
        // 索引决定 alive_mark / 实体预分配 / 重映射表的规模, 必须在入口设上界
        if (s.index >= max_entity_count)
        {
            return false;
        }
        r.next_key(); s.version = r.read_u32();
        r.next_key(); s.flags = r.read_u32();
        r.next_key(); s.tag = r.read_u32();
        r.next_key(); s.layer = r.read_u32();
        r.next_key(); s.group_id = r.read_u32();
        r.leave_object();
        r.end_element();
        saved.add(s);
    }
    r.leave_array();
    return true;
}

} // namespace entity_identity

} // namespace serialize
