#pragma once

// entity_identity.hpp - 实体保存 / 索引保序重建 / 选择性过滤
// 命名空间: serialize

#include <cstddef>
#include <cstdint>

#include "part/dense.hpp"
#include "part/codec/archive_codec.hpp"
#include "ecs/manager.hpp"
#include "ecs/entity/entity_manager.hpp"
#include "ecs/entity/entity.hpp"
#include "serialization/base/model.hpp"

// filter.hpp - 选择性序列化过滤器


namespace serialize {

struct serialize_filter
{
    bool use_layer = false;
    uint32_t layer = 0;

    bool use_tag = false;
    uint32_t tag = 0;

    bool use_group = false;
    uint32_t group_id = 0;

    bool use_flags = false;
    uint32_t flags_mask = 0;
    uint32_t flags_value = 0;

    dense<uint32_t> entity_whitelist;
    bool use_whitelist = false;

    [[nodiscard]] bool matches(const ecs::entity_state& state) const noexcept
    {
        if (use_layer && state.layer != layer)
        {
            return false;
        }
        if (use_tag && state.tag != tag)
        {
            return false;
        }
        if (use_group && state.group_id != group_id)
        {
            return false;
        }
        if (use_flags && (state.flags & flags_mask) != flags_value)
        {
            return false;
        }
        return true;
    }

    [[nodiscard]] bool matches_entity(uint32_t idx, const ecs::entity_state& state) const noexcept
    {
        if (use_whitelist)
        {
            for (size_t i = 0; i < entity_whitelist.size(); ++i)
            {
                if (entity_whitelist[i] == idx)
                {
                    return matches(state);
                }
            }
            return false;
        }
        return matches(state);
    }

    serialize_filter& by_layer(uint32_t l) noexcept { use_layer = true; layer = l; return *this; }
    serialize_filter& by_tag(uint32_t t) noexcept { use_tag = true; tag = t; return *this; }
    serialize_filter& by_group(uint32_t g) noexcept { use_group = true; group_id = g; return *this; }
    serialize_filter& by_flags(uint32_t mask, uint32_t value) noexcept
    {
        use_flags = true; flags_mask = mask; flags_value = value; return *this;
    }
    serialize_filter& by_entities(dense<uint32_t>&& ids) noexcept
    {
        use_whitelist = true; entity_whitelist = std::move(ids); return *this;
    }
};

} // namespace serialize

// entity_identity.hpp - 实体索引保序重建 (预分配 + 顺序领取 + 空槽回收)


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
        entries.emplace_back(e);
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
    ecs::entity_manager& em = mgr.get_entity_manager();
    em.reset();
    if (max_idx == 0)
    {
        return true;
    }
    mgr.preallocate_entities(max_idx);

    for (uint32_t i = 1; i <= max_idx; ++i)
    {
        (void)mgr.create_entity();
    }

    for (size_t k = 0; k < saved.entries.size(); ++k)
    {
        const saved_entity& s = saved.entries[k];
        if (s.index == 0 || s.index > max_idx)
        {
            continue;
        }
        em.set_version_at(s.index, s.version);
        remap.set(s.index, ecs::entity(s.index, s.version), s.version);
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
        ecs::entity e(i, em.get_version_at(i));
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

