// archive_logic.hpp - 公共逻辑层
// 提取与格式无关的逻辑: 实体收集、过滤、版本检查、组件序列化分发
// 通过 archive_writer/reader 抽象接口操作, 各格式编码器各自实现接口
#pragma once

#include "../part/archive_codec.hpp"
#include "archive_types.hpp"
#include "../component.hpp"
#include "../part/operating_message.hpp"
#include "../part/dense.hpp"
#include "../part/safety.hpp"
#include "type_name.hpp"
#include "reflect_bridge.hpp"
#include "filter.hpp"
#include "../part/migration.hpp"
#include "../part/stats.hpp"
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

namespace ecs {

// ============================================================================
// archive_logic — 公共逻辑层
// 通过 archive_writer/reader 接口操作, 实现逻辑复用 + 格式切换零拷贝
// ============================================================================
class archive_logic
{
    manager& mgr_;
    const serialize_filter* filter_ = nullptr;
    serialize_stats stats_;

public:
    explicit archive_logic(manager& m) noexcept : mgr_(m) {}

    void set_filter(const serialize_filter* f) noexcept { filter_ = f; }
    [[nodiscard]] const serialize_filter* filter() const noexcept { return filter_; }
    [[nodiscard]] const serialize_stats& stats() const noexcept { return stats_; }
    void reset_stats() noexcept { stats_.reset(); }

    // ========================================================================
    // 保存逻辑 (通过 archive_writer 接口, 与格式无关)
    // ========================================================================

    // 保存存档头 (版本 + 元数据)
    void save_header(archive_writer& w, uint32_t archive_ver, uint32_t engine_ver,
                     const dense<detail::metadata_entry>& metadata) noexcept {
        w.begin_object();
        w.key("version"); w.write_u32(archive_ver);
        w.key("engine");  w.write_u32(engine_ver);

        // 元数据
        if (metadata.size() > 0)
        {
            w.key("meta");
            w.begin_object();
            for (size_t i = 0; i < metadata.size(); ++i)
            {
                w.key(metadata[i].key);
                w.write_string(metadata[i].value);
            }
            w.end_object();
        }
    }

    // 保存组件版本表 (cv)
    template<typename... Ts>
    void save_component_versions(archive_writer& w) noexcept {
        bool has_cv = false;
        ((has_cv = has_cv || (lookup_component_version<Ts>() > 0)), ...);
        if (!has_cv)
        {
            return;
        }

        w.key("cv");
        w.begin_object();
        (save_one_cv<Ts>(w), ...);
        w.end_object();
    }

    template<typename T>
    void save_one_cv(archive_writer& w) noexcept {
        uint32_t cv = lookup_component_version<T>();
        if (cv > 0)
        {
            w.key(std::string(get_type_name<T>()));
            w.write_u32(cv);
        }
    }

    // 保存实体状态 (复用过滤 + 去重逻辑)
    template<typename... Ts>
    void save_entities(archive_writer& w) noexcept {
        uint32_t max_idx = 0;
        bool any = false;
        ((collect_max_entity_idx<Ts>(max_idx, any)), ...);
        if (!any)
        {
            return;
        }

        dense<uint64_t> seen;
        size_t blocks = static_cast<size_t>(max_idx) / 64 + 1;
        for (size_t i = 0; i < blocks; ++i) seen.push_back(0);

        w.key("entities");
        w.begin_array(0);
        (save_unique_entities<Ts>(w, seen), ...);
        w.end_array();
    }

    template<typename T>
    void collect_max_entity_idx(uint32_t& max_idx, bool& any) noexcept {
        const single_class_set* set = mgr_.get_single_class_set<T>();
        if (!set || set->size() == 0)
        {
            return;
        }
        any = true;
        const auto& indices = set->get_entity_indices();
        for (size_t i = 0; i < indices.size(); ++i)
        {
            if (indices[i] > max_idx)
            {
                max_idx = indices[i];
            }
        }
    }

    template<typename T>
    void save_unique_entities(archive_writer& w, dense<uint64_t>& seen) noexcept {
        const single_class_set* set = mgr_.get_single_class_set<T>();
        if (!set)
        {
            return;
        }
        const auto& indices = set->get_entity_indices();
        const auto& versions = set->get_entity_versions();
        size_t count = set->size();
        for (size_t i = 0; i < count; ++i)
        {
            uint32_t idx = indices[i];
            uint32_t ver = versions[i];
            if (filter_ && !filter_->matches_entity(idx, mgr_.get_entity_state(idx)))
            {
                continue;
            }
            size_t block = static_cast<size_t>(idx) / 64;
            uint64_t bit = static_cast<uint64_t>(1) << (idx % 64);
            if (seen[block] & bit)
            {
                continue;
            }
            seen[block] |= bit;

            const auto& state = mgr_.get_entity_state(idx);
            w.begin_object();
            w.key("i"); w.write_u32(idx);
            w.key("v"); w.write_u32(ver);
            w.key("f"); w.write_u32(state.flags);
            w.key("t"); w.write_u32(state.tag);
            w.key("l"); w.write_u32(state.layer);
            w.key("g"); w.write_u32(state.group_id);
            w.end_object();
        }
    }

    // 保存组件数据 (复用过滤 + 序列化分发)
    template<typename... Ts>
    void save_components(archive_writer& w) noexcept {
        w.key("components");
        w.begin_object();
        (save_one_type<Ts>(w), ...);
        w.end_object();
    }

    template<typename T>
    void save_one_type(archive_writer& w) noexcept {
        std::string name = std::string(get_type_name<T>());
        w.key(name);
        w.begin_array(0);

        const single_class_set* set = mgr_.get_single_class_set<T>();
        if (!set)
        {
            w.end_array();
            return;
        }

        const auto& indices = set->get_entity_indices();
        const auto& versions = set->get_entity_versions();
        const auto* pool = set->get_typed_pool_ptr<T>();
        size_t count = set->size();
        size_t comp_count = 0;

        for (size_t i = 0; i < count; ++i)
        {
            uint32_t idx = indices[i];
            if (filter_ && !filter_->matches_entity(idx, mgr_.get_entity_state(idx)))
            {
                continue;
            }
            const T* comp = pool ? &(*pool)[i] : nullptr;
            w.begin_object();
            w.key("i"); w.write_u32(static_cast<uint32_t>(idx));
            w.key("v"); w.write_u32(static_cast<uint32_t>(versions[i]));
            if (comp)
            {
                w.key("d");
                serialize_value<T>(w, *comp);
                ++comp_count;
            }
            else
            {
                w.key("d"); w.write_bytes(nullptr, 0);
            }
            w.end_object();
        }
        w.end_array();

        stats_.per_type.push_back({name, comp_count, 0});
    }

    // 组件序列化分发 (优先级: to_json > 反射 > base64)
    // 反射桥接当前仅支持 json_writer, 通过临时缓冲区适配到 archive_writer
    // (避免修改 reflect_bridge.hpp, 保持向后兼容)
    template<typename T>
    void serialize_value(archive_writer& w, const T& comp) noexcept {
        if constexpr (reflect_bridge::has_json_serialize<T>)
        {
            // 用户自定义 to_json: 写为 raw 片段 (各格式编码器自行处理)
            std::string j = comp.to_json();
            w.write_raw(j);
        }
        else if constexpr (std::is_trivially_copyable_v<T>)
        {
            if (reflect_bridge::is_reflected<T>())
            {
                // 反射桥接: 用 json_writer 生成 JSON 片段, 再 write_raw
                json_writer jw;
                reflect_bridge::to_json(jw, comp);
                w.write_raw(jw.take());
            }
            else
            {
                // trivially copyable: 直接写原始字节
                w.write_bytes(&comp, sizeof(T));
            }
        }
        else
        {
            if (reflect_bridge::is_reflected<T>())
            {
                json_writer jw;
                reflect_bridge::to_json(jw, comp);
                w.write_raw(jw.take());
            }
            else
            {
                w.write_bytes(nullptr, 0);
            }
        }
    }

    // ========================================================================
    // 加载逻辑 (通过 archive_reader 接口, 与格式无关)
    // ========================================================================

    // 加载存档头, 返回 (archive_ver, engine_ver)
    std::pair<uint32_t, uint32_t> load_header(archive_reader& r,
                                               uint32_t max_archive_ver,
                                               dense<detail::metadata_entry>& metadata,
                                               operating_message& err) noexcept {
        uint32_t archive_ver = 0, engine_ver = 0;
        if (!r.enter_object())
        {
            err = r.last_error();
            return {0, 0};
        }

        std::string_view k;
        while (!(k = r.next_key()).empty())
        {
            if (k == "version")
            {
                archive_ver = r.read_u32();
                if (archive_ver > max_archive_ver)
                {
                    err.write_message(false, "存档版本 ", archive_ver,
                                    " 高于当前支持版本 ", max_archive_ver);
                    return {0, 0};
                }
            }
            else if (k == "engine")
            {
                engine_ver = r.read_u32();
            }
            else if (k == "meta")
            {
                if (r.enter_object())
                {
                    std::string_view mk;
                    while (!(mk = r.next_key()).empty())
                    {
                        std::string val = r.read_string();
                        metadata.push_back({std::string(mk), std::move(val)});
                    }
                }
            }
            else if (k == "cv")
            {
                // 组件版本表 (由 load_components 处理, 这里跳过)
                r.skip_value();
            }
            else if (k == "entities" || k == "components")
            {
                // 由后续步骤处理
                r.skip_value();
            }
            else
            {
                r.skip_value();
            }
        }
        return {archive_ver, engine_ver};
    }

    // 扫描实体, 创建新实体并建立 remap
    bool scan_entities(archive_reader& r, detail::entity_remap& remap,
                       size_t max_entity_count) noexcept {
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

            uint32_t idx = 0, ver = 0, flags = 0, tag = 0, layer = 0, group = 0;
            std::string_view k;
            while (!(k = r.next_key()).empty())
            {
                if (k == "i")
                {
                    idx = r.read_u32();
                }
                else if (k == "v")
                {
                    ver = r.read_u32();
                }
                else if (k == "f")
                {
                    flags = r.read_u32();
                }
                else if (k == "t")
                {
                    tag = r.read_u32();
                }
                else if (k == "l")
                {
                    layer = r.read_u32();
                }
                else if (k == "g")
                {
                    group = r.read_u32();
                }
                else
                {
                    r.skip_value();
                }
            }
            r.end_element();

            entity new_e = mgr_.create_entity();
            while (remap.old_to_new.size() <= idx)
            {
                remap.old_to_new.push_back(entity{});
                remap.old_versions.push_back(0);
            }
            remap.old_to_new[idx] = new_e;
            remap.old_versions[idx] = ver;

            auto& state = mgr_.get_entity_state(new_e.parts_.index_);
            state.flags = flags;
            state.tag = tag;
            state.layer = layer;
            state.group_id = group;
        }
        return true;
    }
};

} // namespace ecs
