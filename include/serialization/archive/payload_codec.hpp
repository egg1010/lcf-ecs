#pragma once

// payload_codec.hpp - 组件载荷编解码 / 对象与数组归档
// 命名空间: serialize

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include "part/codec/archive_codec.hpp"
#include "part/codec/codec_json.hpp"
#include "part/codec/codec_registry.hpp"
#include "part/dense.hpp"
#include "part/operating_message.hpp"
#include "part/safety.hpp"
#include "part/type/type_id.hpp"
#include "ecs/manager.hpp"
#include "reflection/core/query.hpp"
#include "serialization/base/entity_identity.hpp"
#include "serialization/base/model.hpp"
#include "serialization/codec/field_codec.hpp"
#include "serialization/codec/type_registry.hpp"

// object_archive.hpp - 与 ECS 解耦的通用对象序列化 (单对象 / 对象数组)


namespace serialize {

namespace detail {

// 载荷规则与组件一致: 用户 to_json > 反射字段 > 原始字节
template<typename T>
bool write_object_value(archive_writer& w, const T& v) noexcept
{
    if constexpr (field_codec::has_json_serialize<T>)
    {
        std::string j = v.to_json();
        w.write_raw(j);
        return true;
    }
    else if constexpr (std::is_trivially_copyable_v<T>)
    {
        if (field_codec::is_reflected<T>())
        {
            return field_codec::to_archive(w, v);
        }
        w.write_bytes(&v, sizeof(T));
        return true;
    }
    else
    {
        if (field_codec::is_reflected<T>())
        {
            return field_codec::to_archive(w, v);
        }
        w.write_bytes(nullptr, 0);
        return true;
    }
}

template<typename T>
bool read_object_value(archive_reader& r, T& v) noexcept
{
    if constexpr (field_codec::has_json_deserialize<T>)
    {
        v.from_json(r.read_bytes_view(0));
        return !r.has_error();
    }
    else if constexpr (std::is_trivially_copyable_v<T>)
    {
        if (field_codec::is_reflected<T>())
        {
            return field_codec::from_archive(r, v);
        }
        const std::string_view bytes = r.read_bytes_view(sizeof(T));
        if (bytes.size() != sizeof(T))
        {
            return false;
        }
        std::memcpy(&v, bytes.data(), sizeof(T));
        return true;
    }
    else
    {
        if (field_codec::is_reflected<T>())
        {
            return field_codec::from_archive(r, v);
        }
        r.skip_value();
        return false;
    }
}

} // namespace detail

class object_archive
{
    template<typename T>
    operating_message read_body(archive_reader& r, dense<T>& out) noexcept
    {
        operating_message error;
        if (!r.enter_object())
        {
            if (!r.has_error())
            {
                return error;
            }
            return r.last_error();
        }

        r.next_key();
        const uint32_t fv = r.read_u32();
        if (fv > format_version)
        {
            error.write_failure(om_err_version_mismatch.code, msg_level::warn, msg::here());
            return error;
        }

        r.next_key();
        if (!r.enter_array())
        {
            error.write_failure(om_err_decode_failed.code, msg_level::warn, msg::here());
            return error;
        }
        while (r.next_element())
        {
            if (out.size() >= limits.max_array_elements)
            {
                error.write_failure(om_err_capacity_exceeded.code, msg_level::warn, msg::here());
                return error;
            }
            T v{};
            if (!detail::read_object_value<T>(r, v))
            {
                error.write_failure(om_err_decode_failed.code, msg_level::warn, msg::here());
                return error;
            }
            r.end_element();
            out.emplace_back(std::move(v));
        }
        r.leave_array();
        r.leave_object();
        return error;
    }

    [[nodiscard]] static bool read_all(const std::string& path, std::string& content,
                                       size_t max_file_size) noexcept
    {
        std::ifstream f(path, std::ios::binary | std::ios::ate);
        if (!f)
        {
            return false;
        }
        const std::streamsize sz = f.tellg();
        if (sz < 0 || static_cast<size_t>(sz) > max_file_size)
        {
            return false;
        }
        f.seekg(0, std::ios::beg);
        content.resize(static_cast<size_t>(sz));
        f.read(content.data(), sz);
        return true;
    }

public:
    codec_bridge bridge;
    safety_limits limits;
    uint32_t format_version = 1;

    template<typename T>
    operating_message save_array(std::string& out, archive_format fmt,
                                 const T* items, size_t count) noexcept
    {
        operating_message error;
        const archive_codec* codec = codec_bridge::pick(fmt);
        if (codec == nullptr)
        {
            error.write_failure(om_err_not_found.code, msg_level::warn, msg::here());
            return error;
        }
        archive_writer* w = codec->create_writer();
        if (w == nullptr)
        {
            error.write_failure(om_err_capacity_exceeded.code, msg_level::warn, msg::here());
            return error;
        }
        w->begin_object();
        w->key("v"); w->write_u32(format_version);
        w->key("items"); w->begin_array(count);
        bool ok = true;
        for (size_t i = 0; i < count; ++i)
        {
            if (!detail::write_object_value<T>(*w, items[i]))
            {
                ok = false;
            }
        }
        w->end_array();
        w->end_object();

        out = w->take();
        codec->destroy_writer(w);
        if (!ok)
        {
            out.clear();
            error.write_failure(om_err_unsupported_operation.code, msg_level::warn, msg::here());
            return error;
        }
        bridge.wrap_save(out);
        return error;
    }

    template<typename T>
    operating_message load_array(std::string_view data, dense<T>& out) noexcept
    {
        operating_message error;
        out.clear();
        std::string content(data);
        error = bridge.unwrap_load(content);
        if (!error)
        {
            return error;
        }
        const archive_codec* codec = detect_codec(content);
        if (codec == nullptr)
        {
            error.write_failure(om_err_decode_failed.code, msg_level::warn, msg::here());
            return error;
        }
        archive_reader* r = codec->create_reader(content);
        if (r == nullptr || r->has_error())
        {
            if (r != nullptr)
            {
                codec->destroy_reader(r);
            }
            error.write_failure(om_err_checksum_failed.code, msg_level::warn, msg::here());
            return error;
        }
        operating_message body_error = read_body<T>(*r, out);
        codec->destroy_reader(r);
        return body_error;
    }

    template<typename T>
    operating_message save_object(std::string& out, archive_format fmt, const T& obj) noexcept
    {
        return save_array(out, fmt, &obj, 1);
    }

    template<typename T>
    operating_message load_object(std::string_view data, T& obj) noexcept
    {
        dense<T> items;
        operating_message error = load_array(data, items);
        if (!error)
        {
            return error;
        }
        if (items.size() != 1)
        {
            error.write_failure(om_err_invalid_argument.code, msg_level::warn, msg::here());
            return error;
        }
        obj = std::move(items[0]);
        return error;
    }

    template<typename T>
    operating_message save_array_to_file(const std::string& path, archive_format fmt,
                                         const T* items, size_t count) noexcept
    {
        std::string data;
        operating_message error = save_array(data, fmt, items, count);
        if (!error)
        {
            return error;
        }
        std::ofstream f(path, std::ios::binary | std::ios::trunc);
        if (!f)
        {
            error.write_failure(om_err_io_failed.code, msg_level::warn, msg::here());
            return error;
        }
        f.write(data.data(), static_cast<std::streamsize>(data.size()));
        return error;
    }

    template<typename T>
    operating_message load_array_from_file(const std::string& path, dense<T>& out) noexcept
    {
        operating_message error;
        std::string content;
        if (!read_all(path, content, limits.max_file_size))
        {
            error.write_failure(om_err_io_failed.code, msg_level::warn, msg::here());
            return error;
        }
        return load_array(content, out);
    }

    template<typename T>
    operating_message save_object_to_file(const std::string& path, archive_format fmt,
                                          const T& obj) noexcept
    {
        return save_array_to_file(path, fmt, &obj, 1);
    }

    template<typename T>
    operating_message load_object_from_file(const std::string& path, T& obj) noexcept
    {
        operating_message error;
        std::string content;
        if (!read_all(path, content, limits.max_file_size))
        {
            error.write_failure(om_err_io_failed.code, msg_level::warn, msg::here());
            return error;
        }
        return load_object(content, obj);
    }
};

} // namespace serialize

// payload_codec.hpp - 组件载荷编解码 (反射字段 / 用户 JSON / 原始字节) + 类型化加载


namespace serialize {

// 加载上下文: 取代 serialization* 的类型擦除参数载体
struct load_context
{
    ecs::manager* mgr = nullptr;
    const detail::entity_remap* remap = nullptr;
    load_policy policy = load_policy::strict;
    serialize_stats* stats = nullptr;

    [[nodiscard]] bool skip_errors() const noexcept
    {
        return policy == load_policy::best_effort;
    }
};

namespace payload_codec {

using field_codec::has_json_serialize;
using field_codec::has_json_deserialize;
using field_codec::is_reflected;

template<typename T>
void remap_component_refs(T& comp, const detail::entity_remap& remap) noexcept
{
    int tid = type_id::get_type_id<T>();
    const dense<detail::entity_field_info>* fields = detail::entity_field_registry().find(tid);
    if (fields == nullptr)
    {
        return;
    }
    char* base = reinterpret_cast<char*>(static_cast<void*>(&comp));
    for (size_t i = 0; i < fields->size(); ++i)
    {
        ecs::entity* ref = reinterpret_cast<ecs::entity*>(base + (*fields)[i].offset);
        if (!ref->is_valid())
        {
            continue;
        }
        ecs::entity mapped{};
        if (remap.find(ref->parts_.index_, mapped))
        {
            *ref = mapped;
        }
        else
        {
            *ref = ecs::entity{};
        }
    }
}

inline bool write_payload(archive_writer& w, const detail::type_factory_entry& entry,
                          const void* data) noexcept
{
    if (entry.has_user_json && entry.json_to_fn != nullptr)
    {
        std::string j = entry.json_to_fn(data);
        w.write_raw(j);
        return true;
    }
    const reflect::type_meta* m = reflect::global().get_type(entry.type_id);
    if (m != nullptr)
    {
        reflect::query_view qv(m);
        const char* base = static_cast<const char*>(data);
        bool ok = true;
        w.begin_object();
        size_t n = qv.field_count();
        for (size_t i = 0; i < n; ++i)
        {
            const reflect::field_meta& fm = qv.field(i);
            if (!field_codec::write_field_meta(w, fm.name, base, fm))
            {
                ok = false;
            }
        }
        w.end_object();
        return ok;
    }
    if (entry.is_trivially_copyable)
    {
        w.write_bytes(data, entry.type_size);
        return true;
    }
    w.write_bytes(nullptr, 0);
    return true;
}

inline void write_raw_payload(archive_writer& w, const void* data, size_t size) noexcept
{
    w.write_bytes(data, size);
}

template<typename T>
bool construct_component(load_context& ctx, ecs::entity e, std::string_view data_str) noexcept
{
    if constexpr (has_json_deserialize<T>)
    {
        T comp{};
        comp.from_json(data_str);
        remap_component_refs<T>(comp, *ctx.remap);
        operating_message error;
        (void)ctx.mgr->add_impl<T>(e, std::move(comp), error);
        return true;
    }
    else if constexpr (std::is_trivially_copyable_v<T>)
    {
        if (is_reflected<T>())
        {
            T comp{};
            json_archive_reader sub(data_str);
            field_codec::from_archive(sub, comp);
            if (sub.has_error())
            {
                return false;
            }
            remap_component_refs<T>(comp, *ctx.remap);
            operating_message error;
        (void)ctx.mgr->add_impl<T>(e, std::move(comp), error);
            return true;
        }
        else
        {
            std::string decoded;
            if (!::detail::base64_decode_strict(data_str, decoded))
            {
                return false;
            }
            if (decoded.size() != sizeof(T))
            {
                return false;
            }
            T comp{};
            std::memcpy(&comp, decoded.data(), sizeof(T));
            remap_component_refs<T>(comp, *ctx.remap);
            operating_message error;
        (void)ctx.mgr->add_impl<T>(e, std::move(comp), error);
            return true;
        }
    }
    else
    {
        if (is_reflected<T>())
        {
            T comp{};
            json_archive_reader sub(data_str);
            field_codec::from_archive(sub, comp);
            if (sub.has_error())
            {
                return false;
            }
            remap_component_refs<T>(comp, *ctx.remap);
            operating_message error;
        (void)ctx.mgr->add_impl<T>(e, std::move(comp), error);
            return true;
        }
        return false;
    }
}

template<typename T>
bool load_typed_component(archive_reader& r, load_context& ctx, ecs::entity e, int tid,
                          uint32_t saved_ver, uint32_t current_ver) noexcept
{
    (void)tid;
    (void)saved_ver;
    (void)current_ver;
    T comp{};
    if (!field_codec::from_archive(r, comp))
    {
        return false;
    }
    remap_component_refs<T>(comp, *ctx.remap);
    operating_message error;
    (void)ctx.mgr->add_impl<T>(e, std::move(comp), error);
    return true;
}

template<typename T>
bool load_one_type(load_context& ctx, archive_reader& r, uint32_t saved_ver) noexcept
{
    r.next_key();
    if (!r.enter_array())
    {
        return false;
    }

    int tid = type_id::get_type_id<T>();
    uint32_t current_ver = lookup_component_version<T>();
    if (current_ver == 0)
    {
        current_ver = saved_ver;
    }
    const detail::entity_remap& remap = *ctx.remap;
    size_t comp_count = 0;

    while (r.next_element())
    {
        if (!r.enter_object())
        {
            break;
        }
        r.next_key();
        const uint32_t idx = r.read_u32();
        r.next_key();
        (void)r.read_u32();
        r.next_key();

        ecs::entity e{};
        if (!remap.find(idx, e) || !e.is_valid())
        {
            r.skip_value();
            r.leave_object();
            r.end_element();
            continue;
        }

        if constexpr (has_json_serialize<T>)
        {
            std::string_view raw = r.read_bytes_view(0);
            std::string data_str(raw);
            if (!data_str.empty() && data_str != "null")
            {
                if (!construct_component<T>(ctx, e, data_str))
                {
                    if (!ctx.skip_errors())
                    {
                        return false;
                    }
                    if (ctx.stats != nullptr)
                    {
                        ++ctx.stats->skipped_count;
                    }
                }
                else
                {
                    ++comp_count;
                }
            }
        }
        else if constexpr (std::is_trivially_copyable_v<T>)
        {
            if (is_reflected<T>())
            {
                if (!load_typed_component<T>(r, ctx, e, tid, saved_ver, current_ver))
                {
                    if (!ctx.skip_errors())
                    {
                        return false;
                    }
                    if (ctx.stats != nullptr)
                    {
                        ++ctx.stats->skipped_count;
                    }
                }
                else
                {
                    ++comp_count;
                }
            }
            else
            {
                std::string_view bytes = r.read_bytes_view(sizeof(T));
                if (bytes.size() != sizeof(T))
                {
                    if (!ctx.skip_errors())
                    {
                        return false;
                    }
                    if (ctx.stats != nullptr)
                    {
                        ++ctx.stats->skipped_count;
                    }
                }
                else
                {
                    T comp{};
                    std::memcpy(&comp, bytes.data(), sizeof(T));
                    remap_component_refs<T>(comp, remap);
                    operating_message error;
        (void)ctx.mgr->add_impl<T>(e, std::move(comp), error);
                    ++comp_count;
                }
            }
        }
        else
        {
            if (is_reflected<T>())
            {
                if (!load_typed_component<T>(r, ctx, e, tid, saved_ver, current_ver))
                {
                    if (!ctx.skip_errors())
                    {
                        return false;
                    }
                    if (ctx.stats != nullptr)
                    {
                        ++ctx.stats->skipped_count;
                    }
                }
                else
                {
                    ++comp_count;
                }
            }
            else
            {
                r.skip_value();
            }
        }
        r.leave_object();
        r.end_element();
    }
    r.leave_array();

    if (ctx.stats != nullptr)
    {
        ctx.stats->per_type.emplace_back(serialize_stats::type_stats{std::string(get_type_name<T>()), comp_count, 0});
    }
    return true;
}

template<typename T>
std::string user_json_trampoline(const void* p) noexcept
{
    if constexpr (has_json_serialize<T>)
    {
        return static_cast<const T*>(p)->to_json();
    }
    return {};
}

template<typename T>
bool load_trampoline(load_context& ctx, archive_reader& r, uint32_t saved_cv) noexcept
{
    return load_one_type<T>(ctx, r, saved_cv);
}

template<typename T>
void register_component() noexcept
{
    int tid = type_id::get_type_id<T>();
    std::string_view name = detail::stable_type_name(tid);
    if (name.empty())
    {
        static std::string fallback = typeid(T).name();
        name = fallback;
    }
    type_id::bind_def_name(name, tid);

    (void)detail::ensure_factory_entry(tid, name, sizeof(T), std::is_trivially_copyable_v<T>);
    detail::type_factory_entry* entry = detail::type_factory_registry().find(tid);
    if (entry == nullptr)
    {
        return;
    }
    entry->name = name;
    entry->archive_load_fn = &load_trampoline<T>;
    entry->has_user_json = has_json_serialize<T>;
    entry->json_to_fn = &user_json_trampoline<T>;
}

[[nodiscard]] inline bool slot_selected(const entity_source::slot_view& sv, ecs::manager& mgr,
                                        const serialize_filter* filter) noexcept
{
    if (filter == nullptr)
    {
        return true;
    }
    return filter->matches_entity(sv.entity_index, mgr.get_entity_state(sv.entity_index));
}

[[nodiscard]] inline size_t count_slots(const ecs::single_class_set& set, ecs::manager& mgr,
                                        const serialize_filter* filter) noexcept
{
    size_t c = 0;
    entity_source::for_each_slot(set, [&](const entity_source::slot_view& sv) {
        if (slot_selected(sv, mgr, filter))
        {
            ++c;
        }
    });
    return c;
}

inline bool write_components(archive_writer& w, ecs::manager& mgr,
                             const serialize_filter* filter, serialize_stats& stats) noexcept
{
    bool ok = true;
    entity_source::for_each_registered_type([&](const detail::type_factory_entry& entry) {
        if (entry.archive_load_fn == nullptr)
        {
            return;
        }
        w.key(entry.name);
        const ecs::single_class_set* set = entity_source::active_set(mgr, entry.type_id);
        w.begin_array(set == nullptr ? 0 : count_slots(*set, mgr, filter));
        size_t count = 0;
        if (set != nullptr)
        {
            entity_source::for_each_slot(*set, [&](const entity_source::slot_view& sv) {
                if (!slot_selected(sv, mgr, filter))
                {
                    return;
                }
                w.begin_object();
                w.key("i"); w.write_u32(sv.entity_index);
                w.key("v"); w.write_u32(sv.entity_version);
                w.key("d");
                if (!write_payload(w, entry, sv.data))
                {
                    ok = false;
                }
                w.end_object();
                ++count;
            });
        }
        w.end_array();
        stats.per_type.emplace_back(serialize_stats::type_stats{std::string(entry.name), count, 0});
    });

    entity_source::for_each_def_type([&](int tid) {
        std::string_view nm = type_id::get_def_type_name(tid);
        w.key(nm);
        const ecs::single_class_set* set = entity_source::active_set(mgr, tid);
        w.begin_array(set == nullptr ? 0 : count_slots(*set, mgr, filter));
        size_t count = 0;
        if (set != nullptr)
        {
            const size_t size = set->get_component_size();
            entity_source::for_each_slot(*set, [&](const entity_source::slot_view& sv) {
                if (!slot_selected(sv, mgr, filter))
                {
                    return;
                }
                w.begin_object();
                w.key("i"); w.write_u32(sv.entity_index);
                w.key("v"); w.write_u32(sv.entity_version);
                w.key("d");
                write_raw_payload(w, sv.data, size);
                w.end_object();
                ++count;
            });
        }
        w.end_array();
        stats.per_type.emplace_back(serialize_stats::type_stats{std::string(nm), count, 0});
    });
    return ok;
}

inline bool load_def_block(archive_reader& r, ecs::manager& mgr, int tid,
                           const detail::entity_remap& remap) noexcept
{
    r.next_key();
    if (!r.enter_array())
    {
        return false;
    }
    const type_def* d = type_id::get_type_def(tid);
    if (d == nullptr || d->size == 0)
    {
        return false;
    }
    while (r.next_element())
    {
        if (!r.enter_object())
        {
            return false;
        }
        r.next_key();
        const uint32_t idx = r.read_u32();
        r.next_key();
        (void)r.read_u32();
        r.next_key();
        std::string_view bytes = r.read_bytes_view(d->size);
        r.leave_object();
        r.end_element();
        if (r.has_error())
        {
            return false;
        }
        if (bytes.size() != d->size)
        {
            continue;
        }
        ecs::entity e{};
        if (!remap.find(idx, e) || !e.is_valid())
        {
            continue;
        }
        operating_message error;
        (void)mgr.add_def_impl(e, tid, bytes.data(), error);
    }
    r.leave_array();
    return true;
}

} // namespace payload_codec

template<typename... Ts>
void register_components() noexcept
{
    (payload_codec::register_component<Ts>(), ...);
}

} // namespace serialize

