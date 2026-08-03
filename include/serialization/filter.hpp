// filter.hpp - 选择性序列化过滤器
#pragma once

#include "../part/dense.hpp"
#include "../component.hpp"
#include <cstdint>

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
