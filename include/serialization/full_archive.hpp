// full_archive.hpp - 完整 ECS 存档门面 (运行期枚举, 免 Ts..., 实体索引保序)
#pragma once

#include "../part/codec/archive_codec.hpp"
#include "../part/codec/codec_registry.hpp"
#include "../part/codec/json_io.hpp"
#include "../part/dense.hpp"
#include "../part/operating_message.hpp"
#include "../ecs/component.hpp"
#include "model.hpp"
#include "codec_bridge.hpp"
#include "type_registry.hpp"
#include "payload_codec.hpp"
#include "entity_source.hpp"
#include "entity_identity.hpp"
#include "archive_filter.hpp"
#include "migration.hpp"
#include "stats.hpp"
#include "schema_export.hpp"
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <string>
#include <string_view>

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
        metadata_.push_back({key, value});
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
            comps.push_back(schema_export::component_desc{
                entry.type_id, entry.has_user_json ? static_cast<uint8_t>(1)
                                                   : static_cast<uint8_t>(0)});
        });
        entity_source::for_each_def_type([&](int tid) {
            comps.push_back(schema_export::component_desc{tid, 0});
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
        const archive_codec* codec = codec_bridge::pick(fmt);
        if (codec == nullptr)
        {
            operating_message res;
            res.write(false, "编码器不存在");
            return res;
        }
        archive_writer* w = codec->create_writer();
        if (w == nullptr)
        {
            operating_message res;
            res.write(false, "编码器创建失败");
            return res;
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
            operating_message res;
            res.write(false, "组件存在不支持的字段类型: 序列化失败");
            return res;
        }
        bridge_.wrap_save(out);
        stats_.total_bytes = out.size();
        stats_.archive_version = header_.archive_version;
        return operating_message{};
    }

    operating_message save_to_file(const std::string& path, archive_format fmt) noexcept
    {
        std::string data;
        operating_message r = save_to_string(data, fmt);
        if (!r)
        {
            return r;
        }
        std::ofstream f(path, std::ios::binary | std::ios::trunc);
        if (!f)
        {
            r.write(false, "文件写入失败: ", path);
            return r;
        }
        f.write(data.data(), static_cast<std::streamsize>(data.size()));
        return r;
    }

    operating_message load_from_string(std::string_view data) noexcept
    {
        std::string content(data);
        operating_message u = bridge_.unwrap_load(content);
        if (!u)
        {
            return u;
        }
        const archive_codec* codec = detect_codec(content);
        if (codec == nullptr)
        {
            operating_message res;
            res.write(false, "格式检测失败");
            return res;
        }
        archive_reader* r = codec->create_reader(content);
        if (r == nullptr || r->has_error())
        {
            if (r != nullptr)
            {
                codec->destroy_reader(r);
            }
            operating_message res;
            res.write(false, "格式校验失败");
            return res;
        }
        operating_message result = load_body(*r);
        codec->destroy_reader(r);
        return result;
    }

    operating_message load_from_file(const std::string& path) noexcept
    {
        std::ifstream f(path, std::ios::binary | std::ios::ate);
        if (!f)
        {
            operating_message res;
            res.write(false, "无法打开文件: ", path);
            return res;
        }
        std::streamsize sz = f.tellg();
        if (sz < 0 || static_cast<size_t>(sz) > limits_.max_file_size)
        {
            operating_message res;
            res.write(false, "文件过大: ", path);
            return res;
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
        if (!r.enter_object())
        {
            return r.has_error() ? r.last_error() : operating_message{};
        }

        r.next_key();
        const uint32_t archive_ver = r.read_u32();
        if (archive_ver > header_.archive_version)
        {
            operating_message res;
            res.write(false, "存档版本 ", archive_ver, " 高于当前支持版本 ",
                            std::to_string(header_.archive_version));
            return res;
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
                    cv_list.push_back({std::string(ck), cr.read_uint32()});
                }
            }
        }

        r.next_key();
        entity_identity::saved_entity_set saved;
        if (!entity_identity::read_entities(r, limits_.max_entity_count, saved))
        {
            operating_message res;
            res.write(false, "实体扫描失败");
            return res;
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
            operating_message res;
            res.write(false, "组件块缺失");
            return res;
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
        std::string failed_name;
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
                failed_name = std::string(entry.name);
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
                    failed_name = std::string(type_id::get_def_type_name(tid));
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
            operating_message res;
            res.write(false, "组件块解析失败: ", failed_name);
            return res;
        }
        return operating_message{};
    }
};

} // namespace serialize
