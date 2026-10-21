// entity_source.hpp - 运行期 ECS 枚举 (注册类型 / def 类型 / 集合槽位 / 存活实体)
#pragma once

#include "../part/type/type_id.hpp"
#include "../part/dense.hpp"
#include "../part/codec/archive_codec.hpp"
#include "../ecs/component.hpp"
#include "../ecs/single_class_set.hpp"
#include "../ecs/entity_manager.hpp"
#include "type_registry.hpp"
#include "archive_filter.hpp"
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace serialize {

namespace entity_source {

struct slot_view
{
    uint32_t    entity_index;
    uint32_t    entity_version;
    const void* data;
};

template<typename F>
inline void for_each_registered_type(F&& fn) noexcept
{
    detail::type_factory_registry().for_each(
        [&fn](int, const detail::type_factory_entry& entry) noexcept { fn(entry); });
}

template<typename F>
inline void for_each_def_type(F&& fn) noexcept
{
    const int max_id = type_id::current_max_id();
    for (int tid = 1; tid <= max_id; ++tid)
    {
        if (type_id::get_type_def(tid) != nullptr)
        {
            fn(tid);
        }
    }
}

template<typename F>
inline void for_each_alive_entity(ecs::manager& mgr, F&& fn) noexcept
{
    const ecs::entity_manager& em = mgr.get_entity_manager();
    const uint32_t count = em.entity_index_count();
    for (uint32_t idx = 0; idx < count; ++idx)
    {
        if (!em.is_alive(idx))
        {
            continue;
        }
        fn(idx, em.get_version_at(idx), mgr.get_entity_state(idx));
    }
}

template<typename F>
inline void for_each_slot(const ecs::single_class_set& set, F&& fn) noexcept
{
    const size_t component_size = set.get_component_size();
    const char* base = static_cast<const char*>(set.get_raw_pool_data());
    const dense<uint32_t>& indices = set.get_entity_indices();
    const dense<uint32_t>& versions = set.get_entity_versions();
    const size_t n = set.size();
    for (size_t i = 0; i < n; ++i)
    {
        if (!set.is_dense_slot_live(i))
        {
            continue;
        }
        slot_view sv;
        sv.entity_index = indices[i];
        sv.entity_version = versions[i];
        sv.data = base + i * component_size;
        fn(sv);
    }
}

[[nodiscard]] inline size_t live_slot_count(const ecs::single_class_set& set) noexcept
{
    const size_t n = set.size();
    size_t c = 0;
    for (size_t i = 0; i < n; ++i)
    {
        if (set.is_dense_slot_live(i))
        {
            ++c;
        }
    }
    return c;
}

[[nodiscard]] inline size_t alive_entity_count(ecs::manager& mgr) noexcept
{
    const ecs::entity_manager& em = mgr.get_entity_manager();
    const uint32_t count = em.entity_index_count();
    size_t c = 0;
    for (uint32_t i = 0; i < count; ++i)
    {
        if (em.is_alive(i))
        {
            ++c;
        }
    }
    return c;
}

[[nodiscard]] inline const ecs::single_class_set* active_set(ecs::manager& mgr, int tid) noexcept
{
    const ecs::single_class_set* set = mgr.get_single_class_set_by_id(tid);
    if (set == nullptr || set->size() == 0 || set->get_type_id_value() != tid)
    {
        return nullptr;
    }
    return set;
}

[[nodiscard]] inline bool entity_selected(const serialize_filter* filter, uint32_t idx,
                                          const ecs::entity_state& st) noexcept
{
    if (filter == nullptr)
    {
        return true;
    }
    return filter->matches_entity(idx, st);
}

[[nodiscard]] inline size_t count_selected_entities(ecs::manager& mgr,
                                                    const serialize_filter* filter) noexcept
{
    size_t c = 0;
    for_each_alive_entity(mgr, [&](uint32_t idx, uint32_t, const ecs::entity_state& st) {
        if (entity_selected(filter, idx, st))
        {
            ++c;
        }
    });
    return c;
}

inline void write_entities(archive_writer& w, ecs::manager& mgr,
                           const serialize_filter* filter, size_t& count) noexcept
{
    for_each_alive_entity(mgr, [&](uint32_t idx, uint32_t ver, const ecs::entity_state& st) {
        if (!entity_selected(filter, idx, st))
        {
            return;
        }
        w.begin_object();
        w.key("i"); w.write_u32(idx);
        w.key("v"); w.write_u32(ver);
        w.key("f"); w.write_u32(st.flags);
        w.key("t"); w.write_u32(st.tag);
        w.key("l"); w.write_u32(st.layer);
        w.key("g"); w.write_u32(st.group_id);
        w.end_object();
        ++count;
    });
}

} // namespace entity_source

} // namespace serialize
