#pragma once

// full_archive.hpp - 完整 ECS 存档门面 / schema 导出
// 命名空间: serialize

#include <cstddef>
#include <cstdint>
#include <fstream>
#include <string>
#include <string_view>

#include "part/codec/archive_codec.hpp"
#include "part/codec/codec_registry.hpp"
#include "part/codec/json_io.hpp"
#include "part/dense.hpp"
#include "part/operating_message.hpp"
#include "ecs/manager.hpp"
#include "serialization/archive/payload_codec.hpp"
#include "serialization/base/entity_identity.hpp"
#include "serialization/base/model.hpp"
#include "serialization/codec/field_codec.hpp"
#include "serialization/codec/type_registry.hpp"

// schema_export.hpp - 跨语言 schema 导出: 由稳定类型名 + 反射元数据生成 .proto / .fbs


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
    ids.emplace_back(tid);
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
    comps.emplace_back(component_desc{tid, user_json ? static_cast<uint8_t>(1)
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

// full_archive.hpp - 完整 ECS 存档门面 (运行期枚举, 免 Ts..., 实体索引保序)


namespace serialize {

class full_archive
{
    ecs::manager& mgr_;
    serialize_limits limits_;
    detail::archive_header header_;
    const serialize_filter* filter_ = nullptr;
    load_mode load_mode_ = load_mode::replace;
    load_policy load_policy_ = load_policy::strict;
    serialize_stats stats_;
    dense<detail::metadata_entry> metadata_;
    codec_bridge bridge_;
    progress_fn progress_cb_ = nullptr;

    struct cv_pair
    {
        std::string name;
        uint32_t    version;
    };

    [[nodiscard]] static uint32_t find_cv(const dense<cv_pair>& list, std::string_view name) noexcept
    {
        for (size_t i = 0; i < list.size(); ++i)
        {
            if (list[i].name == name)
            {
                return list[i].version;
            }
        }
        return 0;
    }

public:
    explicit full_archive(ecs::manager& m) noexcept : mgr_(m) {}

    template<typename... Ts>
    full_archive& register_components() noexcept
    {
        serialize::register_components<Ts...>();
        return *this;
    }

    void set_archive_version(uint32_t v) noexcept { header_.archive_version = v; }
    void set_engine_version(uint32_t v) noexcept { header_.engine_version = v; }
    [[nodiscard]] uint32_t archive_version() const noexcept { return header_.archive_version; }
    [[nodiscard]] uint32_t engine_version() const noexcept { return header_.engine_version; }

    void set_metadata(const std::string& key, const std::string& value) noexcept
    {
        for (size_t i = 0; i < metadata_.size(); ++i)
        {
            if (metadata_[i].key == key)
            {
                metadata_[i].value = value;
                return;
            }
        }
        metadata_.emplace_back(detail::metadata_entry{key, value});
    }

    [[nodiscard]] const std::string* get_metadata(const std::string& key) const noexcept
    {
        for (size_t i = 0; i < metadata_.size(); ++i)
        {
            if (metadata_[i].key == key)
            {
                return &metadata_[i].value;
            }
        }
        return nullptr;
    }

    [[nodiscard]] const dense<detail::metadata_entry>& all_metadata() const noexcept
    {
        return metadata_;
    }

    void set_checksum_enabled(bool e) noexcept { bridge_.enable_checksum = e; }
    [[nodiscard]] bool is_checksum_enabled() const noexcept { return bridge_.enable_checksum; }

    full_archive& set_filter(const serialize_filter* f) noexcept { filter_ = f; return *this; }
    [[nodiscard]] const serialize_filter* filter() const noexcept { return filter_; }

    full_archive& set_load_mode(load_mode m) noexcept { load_mode_ = m; return *this; }
    [[nodiscard]] load_mode get_load_mode() const noexcept { return load_mode_; }

    full_archive& set_load_policy(load_policy p) noexcept { load_policy_ = p; return *this; }
    [[nodiscard]] load_policy get_load_policy() const noexcept { return load_policy_; }

    void set_progress_callback(progress_fn cb) noexcept { progress_cb_ = cb; }
    void on_save(transform_fn cb) noexcept { bridge_.on_save_cb = cb; }
    void on_load(transform_fn cb) noexcept { bridge_.on_load_cb = cb; }

    void set_compression(compress_fn c, decompress_fn d) noexcept
    {
        bridge_.compress_cb = c;
        bridge_.decompress_cb = d;
    }

    void set_encryption(encrypt_fn e, decrypt_fn d) noexcept
    {
        bridge_.encrypt_cb = e;
        bridge_.decrypt_cb = d;
    }

    [[nodiscard]] serialize_limits& limits() noexcept { return limits_; }
    [[nodiscard]] const serialize_limits& limits() const noexcept { return limits_; }
    [[nodiscard]] const serialize_stats& last_stats() const noexcept { return stats_; }

    [[nodiscard]] dense<schema_export::component_desc> component_schema() const noexcept
    {
        dense<schema_export::component_desc> comps;
        entity_source::for_each_registered_type([&](const detail::type_factory_entry& entry) {
            if (entry.archive_load_fn == nullptr)
            {
                return;
            }
            comps.emplace_back(schema_export::component_desc{
                entry.type_id, entry.has_user_json ? static_cast<uint8_t>(1)
                                                   : static_cast<uint8_t>(0)});
        });
        entity_source::for_each_def_type([&](int tid) {
            comps.emplace_back(schema_export::component_desc{tid, 0});
        });
        return comps;
    }

    [[nodiscard]] std::string schema_proto() const noexcept
    {
        return schema_export::build_proto(component_schema());
    }

    [[nodiscard]] std::string schema_fbs() const noexcept
    {
        return schema_export::build_fbs(component_schema());
    }

    operating_message save_to_string(std::string& out, archive_format fmt) noexcept
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
        stats_.reset();
        w->begin_object();
        w->key("version"); w->write_u32(header_.archive_version);
        w->key("engine");  w->write_u32(header_.engine_version);
        w->key("meta");    w->write_raw(meta_json());
        w->key("cv");      w->write_raw(cv_json());
        w->key("entities"); w->begin_array(entity_source::count_selected_entities(mgr_, filter_));
        entity_source::write_entities(*w, mgr_, filter_, stats_.entity_count);
        w->end_array();
        w->key("components"); w->begin_object();
        const bool components_ok = payload_codec::write_components(*w, mgr_, filter_, stats_);
        w->end_object();
        w->end_object();

        out = w->take();
        codec->destroy_writer(w);
        if (!components_ok)
        {
            out.clear();
            error.write_failure(om_err_unsupported_operation.code, msg_level::warn, msg::here());
            return error;
        }
        bridge_.wrap_save(out);
        stats_.total_bytes = out.size();
        stats_.archive_version = header_.archive_version;
        return error;
    }

    operating_message save_to_file(const std::string& path, archive_format fmt) noexcept
    {
        std::string data;
        operating_message error = save_to_string(data, fmt);
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

    operating_message load_from_string(std::string_view data) noexcept
    {
        operating_message error;
        std::string content(data);
        error = bridge_.unwrap_load(content);
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
        operating_message body_error = load_body(*r);
        codec->destroy_reader(r);
        return body_error;
    }

    operating_message load_from_file(const std::string& path) noexcept
    {
        operating_message error;
        std::ifstream f(path, std::ios::binary | std::ios::ate);
        if (!f)
        {
            error.write_failure(om_err_io_failed.code, msg_level::warn, msg::here());
            return error;
        }
        std::streamsize sz = f.tellg();
        if (sz < 0 || static_cast<size_t>(sz) > limits_.max_file_size)
        {
            error.write_failure(om_err_capacity_exceeded.code, msg_level::warn, msg::here());
            return error;
        }
        f.seekg(0, std::ios::beg);
        std::string content;
        content.resize(static_cast<size_t>(sz));
        f.read(content.data(), sz);
        return load_from_string(content);
    }

private:
    [[nodiscard]] std::string meta_json() const noexcept
    {
        json_writer jw;
        jw.begin_object();
        for (size_t i = 0; i < metadata_.size(); ++i)
        {
            jw.key(metadata_[i].key).value(metadata_[i].value);
        }
        jw.end_object();
        return jw.take();
    }

    [[nodiscard]] std::string cv_json() const noexcept
    {
        json_writer jw;
        jw.begin_object();
        entity_source::for_each_registered_type([&](const detail::type_factory_entry& entry) {
            if (entry.archive_load_fn == nullptr)
            {
                return;
            }
            const uint32_t cv = lookup_component_version(entry.type_id);
            if (cv > 0)
            {
                jw.key(std::string(entry.name)).value(cv);
            }
        });
        entity_source::for_each_def_type([&](int tid) {
            const uint32_t cv = lookup_component_version(tid);
            if (cv == 0)
            {
                return;
            }
            std::string_view nm = type_id::get_def_type_name(tid);
            if (!nm.empty())
            {
                jw.key(std::string(nm)).value(cv);
            }
        });
        jw.end_object();
        return jw.take();
    }

    operating_message load_body(archive_reader& r) noexcept
    {
        operating_message error;
        if (!r.enter_object())
        {
            if (!r.has_error())
            {
                return error;
            }
            error = r.last_error();
            return error;
        }

        r.next_key();
        const uint32_t archive_ver = r.read_u32();
        if (archive_ver > header_.archive_version)
        {
            error.write_failure(om_err_version_mismatch.code, msg_level::warn, msg::here());
            return error;
        }
        r.next_key();
        (void)r.read_u32();

        r.next_key();
        std::string meta_json(r.read_string_view());
        if (load_mode_ == load_mode::replace)
        {
            metadata_.clear();
        }
        if (!meta_json.empty())
        {
            json_reader mr(meta_json);
            if (mr.enter_object())
            {
                std::string_view mk;
                while (!(mk = mr.next_key()).empty())
                {
                    set_metadata(std::string(mk), mr.read_string());
                }
            }
        }

        r.next_key();
        dense<cv_pair> cv_list;
        std::string cv_json(r.read_string_view());
        if (!cv_json.empty())
        {
            json_reader cr(cv_json);
            if (cr.enter_object())
            {
                std::string_view ck;
                while (!(ck = cr.next_key()).empty())
                {
                    cv_list.emplace_back(cv_pair{std::string(ck), cr.read_uint32()});
                }
            }
        }

        r.next_key();
        entity_identity::saved_entity_set saved;
        if (!entity_identity::read_entities(r, limits_.max_entity_count, saved))
        {
            error.write_failure(om_err_decode_failed.code, msg_level::warn, msg::here());
            return error;
        }

        detail::entity_remap remap;
        if (load_mode_ == load_mode::replace)
        {
            mgr_.clear();
            entity_identity::rebuild_entities(mgr_, saved, remap);
        }
        else
        {
            entity_identity::append_entities(mgr_, saved, remap);
        }
        stats_.entity_count = saved.entries.size();

        r.next_key();
        if (!r.enter_object())
        {
            error.write_failure(om_err_decode_failed.code, msg_level::warn, msg::here());
            return error;
        }

        load_context ctx;
        ctx.mgr = &mgr_;
        ctx.remap = &remap;
        ctx.policy = load_policy_;
        ctx.stats = &stats_;

        size_t total = 0;
        entity_source::for_each_registered_type([&](const detail::type_factory_entry& entry) {
            if (entry.archive_load_fn != nullptr)
            {
                ++total;
            }
        });
        entity_source::for_each_def_type([&](int) { ++total; });

        bool ok = true;
        size_t processed = 0;
        entity_source::for_each_registered_type([&](const detail::type_factory_entry& entry) {
            if (!ok)
            {
                return;
            }
            if (entry.archive_load_fn == nullptr)
            {
                return;
            }
            const uint32_t saved_cv = find_cv(cv_list, entry.name);
            if (!entry.archive_load_fn(ctx, r, saved_cv))
            {
                ok = false;
                return;
            }
            ++processed;
            if (progress_cb_ != nullptr)
            {
                progress_cb_(processed, total);
            }
        });
        if (ok)
        {
            entity_source::for_each_def_type([&](int tid) {
                if (!ok)
                {
                    return;
                }
                if (!payload_codec::load_def_block(r, mgr_, tid, remap))
                {
                    ok = false;
                    return;
                }
                ++processed;
                if (progress_cb_ != nullptr)
                {
                    progress_cb_(processed, total);
                }
            });
        }
        r.leave_object();

        if (!ok)
        {
            error.write_failure(om_err_decode_failed.code, msg_level::warn, msg::here());
            return error;
        }
        return error;
    }
};

} // namespace serialize

