// schema_export.hpp - 跨语言 schema 导出: 由稳定类型名 + 反射元数据生成 .proto / .fbs
#pragma once

#include "part/type/type_id.hpp"
#include "part/dense.hpp"
#include "reflection/core/query.hpp"
#include "reflection/base/meta.hpp"
#include "serialization/codec/type_registry.hpp"
#include "serialization/codec/field_codec.hpp"
#include <string>
#include <string_view>
#include <type_traits>

namespace serialize {

namespace schema_export {

struct component_desc
{
    int     type_id;
    uint8_t has_user_json;
};

namespace detail {

[[nodiscard]] inline std::string sanitize_identifier(std::string_view name) noexcept
{
    std::string out;
    for (char c : name)
    {
        bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
                  || (c >= '0' && c <= '9') || c == '_';
        out.push_back(ok ? c : '_');
    }
    if (out.empty())
    {
        out = "unnamed";
    }
    if (out[0] >= '0' && out[0] <= '9')
    {
        out.insert(out.begin(), '_');
    }
    return out;
}

[[nodiscard]] inline std::string type_message_name(int tid) noexcept
{
    std::string_view stable = lookup_type_name(tid);
    if (!stable.empty())
    {
        return sanitize_identifier(stable);
    }
    const reflect::type_meta* m = reflect::global().get_type(tid);
    if (m != nullptr && m->name != nullptr)
    {
        return sanitize_identifier(m->name);
    }
    return "Type";
}

inline bool append_unique(dense<int>& ids, int tid) noexcept
{
    for (size_t i = 0; i < ids.size(); ++i)
    {
        if (ids[i] == tid)
        {
            return false;
        }
    }
    ids.push_back(tid);
    return true;
}

inline void collect_reflected_types(int tid, dense<int>& ids) noexcept
{
    if (lookup_enum_underlying(tid) >= 0)
    {
        return;
    }
    if (reflect::global().get_type(tid) == nullptr)
    {
        return;
    }
    if (!append_unique(ids, tid))
    {
        return;
    }
    auto qv = reflect::query_view(reflect::global().get_type(tid));
    size_t n = qv.field_count();
    for (size_t i = 0; i < n; ++i)
    {
        collect_reflected_types(qv.field(i).type_id, ids);
    }
}

[[nodiscard]] inline std::string proto_scalar(int tid) noexcept
{
    if (tid == type_id::get_type_id<int32_t>() || tid == type_id::get_type_id<int8_t>()
        || tid == type_id::get_type_id<int16_t>())
    {
        return "int32";
    }
    if (tid == type_id::get_type_id<uint32_t>() || tid == type_id::get_type_id<uint8_t>()
        || tid == type_id::get_type_id<uint16_t>())
    {
        return "uint32";
    }
    if (tid == type_id::get_type_id<int64_t>())
    {
        return "int64";
    }
    if (tid == type_id::get_type_id<uint64_t>())
    {
        return "uint64";
    }
    if (tid == type_id::get_type_id<float>())
    {
        return "float";
    }
    if (tid == type_id::get_type_id<double>())
    {
        return "double";
    }
    if (tid == type_id::get_type_id<bool>())
    {
        return "bool";
    }
    if (tid == type_id::get_type_id<std::string>())
    {
        return "string";
    }
    return "bytes";
}

[[nodiscard]] inline std::string proto_type_of(int tid, bool& is_message) noexcept
{
    is_message = false;
    int enum_tid = lookup_enum_underlying(tid);
    if (enum_tid >= 0)
    {
        return proto_scalar(enum_tid);
    }
    if (reflect::global().get_type(tid) != nullptr)
    {
        is_message = true;
        return type_message_name(tid);
    }
    return proto_scalar(tid);
}

inline void emit_proto_struct(std::string& out, int tid) noexcept
{
    auto qv = reflect::query_view(reflect::global().get_type(tid));
    if (!qv.valid())
    {
        return;
    }
    std::string owner = type_message_name(tid);
    out += "message " + owner + " {\n";
    size_t n = qv.field_count();
    for (size_t i = 0; i < n; ++i)
    {
        const reflect::field_meta& fm = qv.field(i);
        std::string fname = sanitize_identifier(fm.name);
        std::string num = std::to_string(static_cast<uint32_t>(i + 1));
        bool is_message = false;
        std::string ty = proto_type_of(fm.type_id, is_message);
        if (fm.array_rank > 0)
        {
            out += "  " + owner + "_" + fname + "_array " + fname + " = " + num + ";\n";
        }
        else
        {
            out += "  " + ty + " " + fname + " = " + num + ";\n";
        }
    }
    out += "}\n\n";

    for (size_t i = 0; i < n; ++i)
    {
        const reflect::field_meta& fm = qv.field(i);
        if (fm.array_rank == 0)
        {
            continue;
        }
        bool is_message = false;
        std::string ty = proto_type_of(fm.type_id, is_message);
        std::string wrap = owner + "_" + sanitize_identifier(fm.name) + "_array";
        out += "message " + wrap + " {\n";
        out += "  repeated " + ty + " f1 = 1";
        if (!is_message)
        {
            out += " [packed = false]";
        }
        out += ";\n}\n\n";
    }
}

[[nodiscard]] inline std::string proto_d_type(const component_desc& c) noexcept
{
    if (c.has_user_json != 0)
    {
        return "string";
    }
    if (reflect::global().get_type(c.type_id) != nullptr)
    {
        return type_message_name(c.type_id);
    }
    return "bytes";
}

inline void append_proto_component(std::string& entries, std::string& container,
                                   uint32_t& field, const component_desc& c) noexcept
{
    std::string mname = type_message_name(c.type_id);
    entries += "message " + mname + "Entry {\n";
    entries += "  uint32 i = 1;\n";
    entries += "  uint32 v = 2;\n";
    entries += "  " + proto_d_type(c) + " d = 3;\n";
    entries += "}\n\n";
    container += "  repeated " + mname + "Entry " + mname + " = "
                 + std::to_string(field) + ";\n";
    ++field;
}

[[nodiscard]] inline std::string fbs_scalar(int tid) noexcept
{
    if (tid == type_id::get_type_id<int32_t>() || tid == type_id::get_type_id<int8_t>()
        || tid == type_id::get_type_id<int16_t>())
    {
        return "int";
    }
    if (tid == type_id::get_type_id<uint32_t>() || tid == type_id::get_type_id<uint8_t>()
        || tid == type_id::get_type_id<uint16_t>())
    {
        return "uint";
    }
    if (tid == type_id::get_type_id<int64_t>())
    {
        return "long";
    }
    if (tid == type_id::get_type_id<uint64_t>())
    {
        return "ulong";
    }
    if (tid == type_id::get_type_id<float>())
    {
        return "float";
    }
    if (tid == type_id::get_type_id<double>())
    {
        return "double";
    }
    if (tid == type_id::get_type_id<bool>())
    {
        return "bool";
    }
    if (tid == type_id::get_type_id<std::string>())
    {
        return "string";
    }
    return "uint";
}

[[nodiscard]] inline std::string fbs_field_type(int tid, bool& is_message) noexcept
{
    is_message = false;
    int enum_tid = lookup_enum_underlying(tid);
    if (enum_tid >= 0)
    {
        return fbs_scalar(enum_tid);
    }
    if (reflect::global().get_type(tid) != nullptr)
    {
        is_message = true;
        return type_message_name(tid);
    }
    return fbs_scalar(tid);
}

inline void emit_fbs_struct(std::string& out, int tid) noexcept
{
    auto qv = reflect::query_view(reflect::global().get_type(tid));
    if (!qv.valid())
    {
        return;
    }
    out += "table " + type_message_name(tid) + " {\n";
    size_t n = qv.field_count();
    for (size_t i = 0; i < n; ++i)
    {
        const reflect::field_meta& fm = qv.field(i);
        bool is_message = false;
        std::string ty = fbs_field_type(fm.type_id, is_message);
        std::string fname = sanitize_identifier(fm.name);
        if (fm.array_rank > 0)
        {
            out += "  " + fname + ":[" + ty + "];\n";
        }
        else
        {
            out += "  " + fname + ":" + ty + ";\n";
        }
    }
    out += "}\n\n";
}

[[nodiscard]] inline std::string fbs_d_type(const component_desc& c) noexcept
{
    if (c.has_user_json != 0)
    {
        return "string";
    }
    if (reflect::global().get_type(c.type_id) != nullptr)
    {
        return type_message_name(c.type_id);
    }
    return "[ubyte]";
}

inline void append_fbs_component(std::string& entries, std::string& container,
                                 const component_desc& c) noexcept
{
    std::string mname = type_message_name(c.type_id);
    entries += "table " + mname + "Entry {\n";
    entries += "  i:uint;\n";
    entries += "  v:uint;\n";
    entries += "  d:" + fbs_d_type(c) + ";\n";
    entries += "}\n\n";
    container += "  " + mname + ":[" + mname + "Entry];\n";
}

inline void append_component_desc(dense<int>& refl, dense<component_desc>& comps,
                                  int tid, bool user_json) noexcept
{
    collect_reflected_types(tid, refl);
    comps.push_back(component_desc{tid, user_json ? static_cast<uint8_t>(1)
                                                  : static_cast<uint8_t>(0)});
}

template<typename T>
inline void append_component_desc(dense<int>& refl, dense<component_desc>& comps) noexcept
{
    append_component_desc(refl, comps, type_id::get_type_id<T>(),
                          field_codec::has_json_serialize<T>);
}

} // namespace detail

[[nodiscard]] inline std::string build_proto(const dense<component_desc>& comps) noexcept
{
    using namespace detail;
    dense<int> refl;
    for (size_t i = 0; i < comps.size(); ++i)
    {
        collect_reflected_types(comps[i].type_id, refl);
    }

    std::string out;
    out += "syntax = \"proto3\";\n\n";
    out += "message EntityData {\n";
    out += "  uint32 i = 1;\n";
    out += "  uint32 v = 2;\n";
    out += "  uint32 f = 3;\n";
    out += "  uint32 t = 4;\n";
    out += "  uint32 l = 5;\n";
    out += "  uint32 g = 6;\n";
    out += "}\n\n";
    out += "message EntitiesContainer {\n";
    out += "  repeated EntityData entity = 1;\n";
    out += "}\n\n";

    for (size_t i = 0; i < refl.size(); ++i)
    {
        emit_proto_struct(out, refl[i]);
    }

    std::string entries;
    std::string container;
    uint32_t field = 1;
    for (size_t i = 0; i < comps.size(); ++i)
    {
        append_proto_component(entries, container, field, comps[i]);
    }
    out += entries;
    out += "message ComponentsContainer {\n";
    out += container;
    out += "}\n\n";

    out += "message Archive {\n";
    out += "  uint32 version = 1;\n";
    out += "  uint32 engine = 2;\n";
    out += "  string meta = 3;\n";
    out += "  string cv = 4;\n";
    out += "  EntitiesContainer entities = 5;\n";
    out += "  ComponentsContainer components = 6;\n";
    out += "}\n";
    return out;
}

[[nodiscard]] inline std::string build_fbs(const dense<component_desc>& comps) noexcept
{
    using namespace detail;
    dense<int> refl;
    for (size_t i = 0; i < comps.size(); ++i)
    {
        collect_reflected_types(comps[i].type_id, refl);
    }

    std::string out;
    out += "file_identifier \"LCFB\";\n\n";
    out += "table EntityData {\n";
    out += "  i:uint;\n";
    out += "  v:uint;\n";
    out += "  f:uint;\n";
    out += "  t:uint;\n";
    out += "  l:uint;\n";
    out += "  g:uint;\n";
    out += "}\n\n";

    for (size_t i = 0; i < refl.size(); ++i)
    {
        emit_fbs_struct(out, refl[i]);
    }

    std::string entries;
    std::string container;
    for (size_t i = 0; i < comps.size(); ++i)
    {
        append_fbs_component(entries, container, comps[i]);
    }
    out += entries;
    out += "table ComponentsContainer {\n";
    out += container;
    out += "}\n\n";

    out += "table Archive {\n";
    out += "  version:uint;\n";
    out += "  engine:uint;\n";
    out += "  meta:string;\n";
    out += "  cv:string;\n";
    out += "  entities:[EntityData];\n";
    out += "  components:ComponentsContainer;\n";
    out += "}\n\n";
    out += "root_type Archive;\n";
    return out;
}

template<typename... Ts>
[[nodiscard]] std::string to_proto() noexcept
{
    dense<int> refl;
    dense<component_desc> comps;
    (detail::append_component_desc<Ts>(refl, comps), ...);
    return build_proto(comps);
}

template<typename... Ts>
[[nodiscard]] std::string to_fbs() noexcept
{
    dense<int> refl;
    dense<component_desc> comps;
    (detail::append_component_desc<Ts>(refl, comps), ...);
    return build_fbs(comps);
}

} // namespace schema_export

} // namespace serialize
