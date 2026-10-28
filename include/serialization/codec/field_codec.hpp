#pragma once

// field_codec.hpp - 字段编解码 / 运行期实体枚举
// 命名空间: serialize

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <type_traits>

#include "part/codec/archive_codec.hpp"
#include "part/dense.hpp"
#include "part/type/type_id.hpp"
#include "reflection/core/query.hpp"
#include "ecs/manager.hpp"
#include "ecs/entity/entity_manager.hpp"
#include "ecs/storage/single_class_set.hpp"
#include "serialization/base/entity_identity.hpp"
#include "serialization/codec/type_registry.hpp"

// entity_source.hpp - 运行期 ECS 枚举 (注册类型 / def 类型 / 集合槽位 / 存活实体)


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

// field_codec.hpp - 反射字段 ↔ archive_writer/archive_reader (唯一字段实现)


namespace serialize {

namespace field_codec {

template<typename T>
concept has_json_serialize = requires(const T& v) {
    { v.to_json() } -> std::convertible_to<std::string>;
};

template<typename T>
concept has_json_deserialize = requires(T& v, std::string_view s) {
    v.from_json(s);
};

template<typename T>
[[nodiscard]] inline bool is_reflected() noexcept
{
    return reflect::try_get<T>().valid();
}

[[nodiscard]] inline bool is_type_reflected(int tid) noexcept
{
    return reflect::global().get_type(tid) != nullptr;
}

inline bool write_as_int(archive_writer& w, const void* p, int int_tid) noexcept
{
    if (int_tid == type_id::get_type_id<int32_t>())
    {
        w.write_i32(*static_cast<const int32_t*>(p));
    }
    else if (int_tid == type_id::get_type_id<uint32_t>())
    {
        w.write_u32(*static_cast<const uint32_t*>(p));
    }
    else if (int_tid == type_id::get_type_id<int64_t>())
    {
        w.write_i64(*static_cast<const int64_t*>(p));
    }
    else if (int_tid == type_id::get_type_id<uint64_t>())
    {
        w.write_u64(*static_cast<const uint64_t*>(p));
    }
    else if (int_tid == type_id::get_type_id<int16_t>())
    {
        w.write_i32(static_cast<int32_t>(*static_cast<const int16_t*>(p)));
    }
    else if (int_tid == type_id::get_type_id<uint16_t>())
    {
        w.write_u32(static_cast<uint32_t>(*static_cast<const uint16_t*>(p)));
    }
    else if (int_tid == type_id::get_type_id<int8_t>())
    {
        w.write_i32(static_cast<int32_t>(*static_cast<const int8_t*>(p)));
    }
    else if (int_tid == type_id::get_type_id<uint8_t>())
    {
        w.write_u32(static_cast<uint32_t>(*static_cast<const uint8_t*>(p)));
    }
    else
    {
        return false;
    }
    return true;
}

inline bool read_as_int(archive_reader& r, void* p, int int_tid) noexcept
{
    if (int_tid == type_id::get_type_id<int32_t>())
    {
        *static_cast<int32_t*>(p) = r.read_i32();
    }
    else if (int_tid == type_id::get_type_id<uint32_t>())
    {
        *static_cast<uint32_t*>(p) = r.read_u32();
    }
    else if (int_tid == type_id::get_type_id<int64_t>())
    {
        *static_cast<int64_t*>(p) = r.read_i64();
    }
    else if (int_tid == type_id::get_type_id<uint64_t>())
    {
        *static_cast<uint64_t*>(p) = r.read_u64();
    }
    else if (int_tid == type_id::get_type_id<int16_t>())
    {
        *static_cast<int16_t*>(p) = static_cast<int16_t>(r.read_i32());
    }
    else if (int_tid == type_id::get_type_id<uint16_t>())
    {
        *static_cast<uint16_t*>(p) = static_cast<uint16_t>(r.read_u32());
    }
    else if (int_tid == type_id::get_type_id<int8_t>())
    {
        *static_cast<int8_t*>(p) = static_cast<int8_t>(r.read_i32());
    }
    else if (int_tid == type_id::get_type_id<uint8_t>())
    {
        *static_cast<uint8_t*>(p) = static_cast<uint8_t>(r.read_u32());
    }
    else
    {
        r.skip_value();
        return false;
    }
    return true;
}

inline bool write_basic(archive_writer& w, const void* p, int tid) noexcept
{
    if (tid == type_id::get_type_id<int32_t>())
    {
        w.write_i32(*static_cast<const int32_t*>(p));
    }
    else if (tid == type_id::get_type_id<uint32_t>())
    {
        w.write_u32(*static_cast<const uint32_t*>(p));
    }
    else if (tid == type_id::get_type_id<int64_t>())
    {
        w.write_i64(*static_cast<const int64_t*>(p));
    }
    else if (tid == type_id::get_type_id<uint64_t>())
    {
        w.write_u64(*static_cast<const uint64_t*>(p));
    }
    else if (tid == type_id::get_type_id<int16_t>())
    {
        w.write_i32(static_cast<int32_t>(*static_cast<const int16_t*>(p)));
    }
    else if (tid == type_id::get_type_id<uint16_t>())
    {
        w.write_u32(static_cast<uint32_t>(*static_cast<const uint16_t*>(p)));
    }
    else if (tid == type_id::get_type_id<int8_t>())
    {
        w.write_i32(static_cast<int32_t>(*static_cast<const int8_t*>(p)));
    }
    else if (tid == type_id::get_type_id<uint8_t>())
    {
        w.write_u32(static_cast<uint32_t>(*static_cast<const uint8_t*>(p)));
    }
    else if (tid == type_id::get_type_id<float>())
    {
        w.write_f32(*static_cast<const float*>(p));
    }
    else if (tid == type_id::get_type_id<double>())
    {
        w.write_f64(*static_cast<const double*>(p));
    }
    else if (tid == type_id::get_type_id<bool>())
    {
        w.write_bool(*static_cast<const bool*>(p));
    }
    else if (tid == type_id::get_type_id<std::string>())
    {
        w.write_string(*static_cast<const std::string*>(p));
    }
    else
    {
        int enum_tid = lookup_enum_underlying(tid);
        if (enum_tid < 0)
        {
            return false;
        }
        return write_as_int(w, p, enum_tid);
    }
    return true;
}

inline bool read_basic(archive_reader& r, void* p, int tid) noexcept
{
    if (tid == type_id::get_type_id<int32_t>())
    {
        *static_cast<int32_t*>(p) = r.read_i32();
    }
    else if (tid == type_id::get_type_id<uint32_t>())
    {
        *static_cast<uint32_t*>(p) = r.read_u32();
    }
    else if (tid == type_id::get_type_id<int64_t>())
    {
        *static_cast<int64_t*>(p) = r.read_i64();
    }
    else if (tid == type_id::get_type_id<uint64_t>())
    {
        *static_cast<uint64_t*>(p) = r.read_u64();
    }
    else if (tid == type_id::get_type_id<int16_t>())
    {
        *static_cast<int16_t*>(p) = static_cast<int16_t>(r.read_i32());
    }
    else if (tid == type_id::get_type_id<uint16_t>())
    {
        *static_cast<uint16_t*>(p) = static_cast<uint16_t>(r.read_u32());
    }
    else if (tid == type_id::get_type_id<int8_t>())
    {
        *static_cast<int8_t*>(p) = static_cast<int8_t>(r.read_i32());
    }
    else if (tid == type_id::get_type_id<uint8_t>())
    {
        *static_cast<uint8_t*>(p) = static_cast<uint8_t>(r.read_u32());
    }
    else if (tid == type_id::get_type_id<float>())
    {
        *static_cast<float*>(p) = r.read_f32();
    }
    else if (tid == type_id::get_type_id<double>())
    {
        *static_cast<double*>(p) = r.read_f64();
    }
    else if (tid == type_id::get_type_id<bool>())
    {
        *static_cast<bool*>(p) = r.read_bool();
    }
    else if (tid == type_id::get_type_id<std::string>())
    {
        *static_cast<std::string*>(p) = r.read_string();
    }
    else
    {
        int enum_tid = lookup_enum_underlying(tid);
        if (enum_tid < 0)
        {
            r.skip_value();
            return false;
        }
        return read_as_int(r, p, enum_tid);
    }
    return true;
}

// flatbuffer 向量元素编码宽度: 窄整型经 4 字节写出, 宽类型 8 字节, bool 1 字节
[[nodiscard]] inline size_t element_encode_size(int tid) noexcept
{
    int use = tid;
    int enum_tid = lookup_enum_underlying(tid);
    if (enum_tid >= 0)
    {
        use = enum_tid;
    }
    if (use == type_id::get_type_id<int64_t>() || use == type_id::get_type_id<uint64_t>()
        || use == type_id::get_type_id<double>())
    {
        return 8;
    }
    if (use == type_id::get_type_id<bool>())
    {
        return 1;
    }
    return 4;
}

inline bool write_field_meta(archive_writer& w, const char* name, const char* base,
                             const reflect::field_meta& fm) noexcept
{
    w.key(name);
    const char* field_ptr = base + fm.offset;

    if (fm.array_rank > 0)
    {
        int elem_enum = lookup_enum_underlying(fm.type_id);
        bool is_elem_object = (elem_enum < 0) && is_type_reflected(fm.type_id);
        reflect::query_view elem_qv;
        if (is_elem_object)
        {
            elem_qv = reflect::query_view(reflect::global().get_type(fm.type_id));
        }
        bool ok = true;
        w.begin_array(fm.total_elements);
        for (uint32_t i = 0; i < fm.total_elements; ++i)
        {
            const char* elem = field_ptr + i * fm.element_stride;
            if (is_elem_object && elem_qv.valid())
            {
                w.begin_object();
                size_t n = elem_qv.field_count();
                for (size_t j = 0; j < n; ++j)
                {
                    const reflect::field_meta& sub_fm = elem_qv.field(j);
                    if (!write_field_meta(w, sub_fm.name, elem, sub_fm))
                    {
                        ok = false;
                    }
                }
                w.end_object();
            }
            else if (!write_basic(w, elem, fm.type_id))
            {
                ok = false;
            }
        }
        w.end_array();
        return ok;
    }

    int enum_tid = lookup_enum_underlying(fm.type_id);
    if (enum_tid >= 0)
    {
        return write_as_int(w, field_ptr, enum_tid);
    }

    if (is_type_reflected(fm.type_id))
    {
        auto qv = reflect::query_view(reflect::global().get_type(fm.type_id));
        if (qv.valid())
        {
            bool ok = true;
            w.begin_object();
            size_t n = qv.field_count();
            for (size_t i = 0; i < n; ++i)
            {
                const reflect::field_meta& sub_fm = qv.field(i);
                if (!write_field_meta(w, sub_fm.name, field_ptr, sub_fm))
                {
                    ok = false;
                }
            }
            w.end_object();
            return ok;
        }
    }

    return write_basic(w, field_ptr, fm.type_id);
}

inline bool read_field_meta(archive_reader& r, char* base, const reflect::field_meta& fm) noexcept;

inline bool read_object(archive_reader& r, char* base, reflect::query_view qv) noexcept
{
    if (!r.enter_object())
    {
        return false;
    }
    bool ok = true;
    const size_t n = qv.field_count();
    for (size_t i = 0; i < n; ++i)
    {
        if (r.next_key().empty())
        {
            break;
        }
        if (!read_field_meta(r, base, qv.field(i)))
        {
            ok = false;
        }
    }
    r.leave_object();
    return ok;
}

inline bool read_field_meta(archive_reader& r, char* base, const reflect::field_meta& fm) noexcept
{
    char* field_ptr = base + fm.offset;

    if (fm.array_rank > 0)
    {
        if (!r.enter_array())
        {
            return false;
        }
        r.set_element_size(element_encode_size(fm.type_id));
        int elem_enum = lookup_enum_underlying(fm.type_id);
        bool is_elem_object = (elem_enum < 0) && is_type_reflected(fm.type_id);
        reflect::query_view elem_qv;
        if (is_elem_object)
        {
            elem_qv = reflect::query_view(reflect::global().get_type(fm.type_id));
        }
        bool ok = true;
        uint32_t i = 0;
        while (r.next_element())
        {
            if (i < fm.total_elements)
            {
                char* elem = field_ptr + i * fm.element_stride;
                if (is_elem_object && elem_qv.valid())
                {
                    if (!read_object(r, elem, elem_qv))
                    {
                        ok = false;
                    }
                }
                else if (!read_basic(r, elem, fm.type_id))
                {
                    ok = false;
                }
                ++i;
            }
            else
            {
                r.skip_value();
            }
            r.end_element();
        }
        r.leave_array();
        return ok;
    }

    int enum_tid = lookup_enum_underlying(fm.type_id);
    if (enum_tid >= 0)
    {
        return read_as_int(r, field_ptr, enum_tid);
    }

    if (is_type_reflected(fm.type_id))
    {
        auto qv = reflect::query_view(reflect::global().get_type(fm.type_id));
        if (qv.valid())
        {
            return read_object(r, field_ptr, qv);
        }
    }

    return read_basic(r, field_ptr, fm.type_id);
}

template<typename T>
bool to_archive(archive_writer& w, const T& obj) noexcept
{
    auto qv = reflect::try_get<T>();
    if (!qv.valid())
    {
        return false;
    }
    bool ok = true;
    w.begin_object();
    const char* base = static_cast<const char*>(static_cast<const void*>(&obj));
    size_t n = qv.field_count();
    for (size_t i = 0; i < n; ++i)
    {
        const reflect::field_meta& fm = qv.field(i);
        if (!write_field_meta(w, fm.name, base, fm))
        {
            ok = false;
        }
    }
    w.end_object();
    return ok;
}

template<typename T>
bool from_archive(archive_reader& r, T& obj) noexcept
{
    auto qv = reflect::try_get<T>();
    if (!qv.valid())
    {
        return false;
    }
    return read_object(r, reinterpret_cast<char*>(&obj), qv) && !r.has_error();
}

} // namespace field_codec

} // namespace serialize

