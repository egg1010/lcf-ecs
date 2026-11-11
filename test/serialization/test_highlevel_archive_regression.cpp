// test_highlevel_archive_regression.cpp - 完整存档独立回归测试
#include "test_common.hpp"
#include "include/serialization/archive/full_archive.hpp"

#include <cstdio>
#include <fstream>
#include <string>

struct schema_plain_component
{
    int value;
};

struct schema_dash_component
{
    int value;
};

struct schema_underscore_component
{
    int value;
};

struct archive_regression_component
{
    int value;
};

struct schema_late_component
{
    int value;
};

static std::string empty_transform(const std::string&) noexcept
{
    return {};
}

static void clear_transform(std::string& data) noexcept
{
    data.clear();
}

static bool make_archive(std::string& data)
{
    ecs::manager mgr;
    const auto e = mgr.create_entity();
    (void)mgr.add(e, archive_regression_component{7});
    serialize::full_archive writer(mgr);
    writer.register_components<archive_regression_component>();
    writer.set_checksum_enabled(false);
    writer.set_metadata("keep", "archive");
    writer.set_metadata("incoming", "new");
    return writer.save_to_string(data, serialize::archive_format::json);
}

static bool replace_raw_field(std::string& data, const std::string& key,
                              const std::string& next_key, const std::string& value)
{
    const std::string marker = "\"" + key + "\":";
    const std::string next_marker = ",\"" + next_key + "\":";
    const size_t begin = data.find(marker);
    if (begin == std::string::npos)
    {
        return false;
    }
    const size_t value_begin = begin + marker.size();
    const size_t end = data.find(next_marker, value_begin);
    if (end == std::string::npos)
    {
        return false;
    }
    data.replace(value_begin, end - value_begin, value);
    return true;
}

int main()
{
    t_begin("test_highlevel_archive_regression — 完整存档独立回归测试");

    serialize::register_type_name<schema_plain_component>("schema_plain_component");
    serialize::register_type_name<schema_dash_component>("schema-collision");
    serialize::register_type_name<schema_underscore_component>("schema_collision");
    auto& reflection = reflect::global();
    reflection.register_type_only<archive_regression_component>("archive_regression_component");
    reflection.register_member_runtime<archive_regression_component>(
        "archive_regression_component", "value", &archive_regression_component::value);
    serialize::register_component_version<archive_regression_component>(3);

    t_section(1, "Protobuf schema 数组包装与名称消歧");
    {
        const int dash_id = type_id::get_type_id<schema_dash_component>();
        const int underscore_id = type_id::get_type_id<schema_underscore_component>();
        const std::string dash_name = "schema_collision_" + std::to_string(dash_id);
        const std::string underscore_name =
            "schema_collision_" + std::to_string(underscore_id);
        const std::string proto = serialize::schema_export::to_proto<
            schema_plain_component, schema_dash_component, schema_underscore_component>();

        t_check("普通组件 Array 文本结构",
                proto.find("message schema_plain_componentArray {\n"
                           "  repeated schema_plain_componentEntry f1 = 1;\n"
                           "}") != std::string::npos);
        t_check("组件字段改为 Array 类型",
                proto.find("  schema_plain_componentArray schema_plain_component = 1;")
                    != std::string::npos);
        t_check("普通已有名称保持不变",
                proto.find("message schema_plain_componentEntry {") != std::string::npos
                && proto.find("  schema_plain_componentArray schema_plain_component = 1;")
                    != std::string::npos);
        t_check("sanitize 重名追加稳定类型 ID",
                proto.find("message " + dash_name + "Entry {") != std::string::npos
                && proto.find("message " + underscore_name + "Entry {")
                    != std::string::npos);
        t_check("冲突名不再保留未消歧条目",
                proto.find("message schema_collisionEntry {") == std::string::npos);
        t_check("冲突组件 Array 同步消歧",
                proto.find("message " + dash_name + "Array {\n  repeated "
                           + dash_name + "Entry f1 = 1;\n}") != std::string::npos
                && proto.find("  " + underscore_name + "Array " + underscore_name
                              + " = 3;") != std::string::npos);

        const std::string fbs = serialize::schema_export::to_fbs<
            schema_plain_component, schema_dash_component, schema_underscore_component>();
        t_check("FlatBuffer 名称同步消歧",
                fbs.find("table " + dash_name + "Entry {") != std::string::npos
                && fbs.find("  " + underscore_name + ":[" + underscore_name
                            + "Entry];") != std::string::npos);
        t_check("FlatBuffer 普通名称保持不变",
                fbs.find("  schema_plain_component:[schema_plain_componentEntry];")
                    != std::string::npos);
    }

    t_section(2, "加载统计在每次加载及失败后重置");
    {
        std::string data;
        t_check("统计测试存档成功", make_archive(data));
        ecs::manager mgr;
        serialize::full_archive reader(mgr);
        reader.register_components<archive_regression_component>();
        const operating_message loaded = reader.load_from_string(data);
        t_check("成功加载产生统计",
                loaded && reader.last_stats().entity_count == 1
                && reader.last_stats().total_bytes == data.size()
                && reader.last_stats().archive_version == 1
                && reader.last_stats().per_type.size() == 1
                && reader.last_stats().per_type[0].component_count == 1);

        std::string corrupted = data;
        const std::string needle = "\"value\":7";
        const size_t value_pos = corrupted.find(needle);
        if (value_pos != std::string::npos)
        {
            corrupted.replace(value_pos, needle.size(), "\"value\":\"bad\"");
        }
        const operating_message failed = reader.load_from_string(corrupted);
        t_check("组件中途损坏返回解码失败",
                value_pos != std::string::npos && !failed
                && failed.is_code(om_err_decode_failed));
        const serialize_stats& after_failure = reader.last_stats();
        t_check("中途失败不保留部分统计",
                after_failure.entity_count == 0 && after_failure.total_bytes == 0
                && after_failure.skipped_count == 0 && after_failure.per_type.empty());

        t_check("重新成功加载", reader.load_from_string(data));
        const operating_message early_failure = reader.load_from_string("{bad}");
        t_check("入口失败返回错误", !early_failure);
        t_check("每次加载开始清除旧统计",
                reader.last_stats().entity_count == 0
                && reader.last_stats().per_type.empty());
    }

    t_section(3, "损坏 metadata/cv 内嵌 JSON 明确失败");
    {
        std::string data;
        t_check("内嵌 JSON 测试存档成功", make_archive(data));
        ecs::manager mgr;
        serialize::full_archive reader(mgr);
        reader.register_components<archive_regression_component>();
        reader.set_metadata("keep", "original");

        std::string bad_meta = data;
        const bool meta_replaced = replace_raw_field(bad_meta, "meta", "cv", "\"{bad}\"");
        const operating_message meta_result = reader.load_from_string(bad_meta);
        t_check("损坏 metadata 返回 decode_failed",
                meta_replaced && !meta_result
                && meta_result.is_code(om_err_decode_failed));
        const std::string* keep = reader.get_metadata("keep");
        t_check("损坏 metadata 不提交部分数据",
                keep != nullptr && *keep == "original"
                && reader.get_metadata("incoming") == nullptr);

        std::string bad_cv = data;
        const bool cv_replaced = replace_raw_field(bad_cv, "cv", "entities", "\"{bad}\"");
        const operating_message cv_result = reader.load_from_string(bad_cv);
        t_check("损坏 cv 返回 decode_failed",
                cv_replaced && !cv_result && cv_result.is_code(om_err_decode_failed));

        std::string trailing_meta = data;
        const bool trailing_replaced =
            replace_raw_field(trailing_meta, "meta", "cv", "\"{}x\"");
        const operating_message trailing_result = reader.load_from_string(trailing_meta);
        t_check("内嵌 JSON 尾随垃圾同样失败",
                trailing_replaced && !trailing_result
                && trailing_result.is_code(om_err_decode_failed));

        std::string empty_meta = data;
        const bool empty_replaced = replace_raw_field(empty_meta, "meta", "cv", "\"\"");
        const operating_message empty_result = reader.load_from_string(empty_meta);
        t_check("空 metadata 不作为空表接受",
                empty_replaced && !empty_result
                && empty_result.is_code(om_err_decode_failed));
    }

    t_section(4, "文件保存替换基本路径");
    {
        const std::string path = "test_archive_regression_save.data";
        const std::string temp_path = path + ".tmp";
        (void)std::remove(path.c_str());
        (void)std::remove(temp_path.c_str());
        {
            std::ofstream old_file(path, std::ios::binary | std::ios::trunc);
            old_file << "old-content";
        }

        ecs::manager mgr;
        const auto e = mgr.create_entity();
        (void)mgr.add(e, archive_regression_component{9});
        serialize::full_archive writer(mgr);
        writer.register_components<archive_regression_component>();
        writer.set_checksum_enabled(false);
        writer.set_metadata("saved", "yes");
        const operating_message saved =
            writer.save_to_file(path, serialize::archive_format::json);
        t_check("同目录临时文件保存成功", saved);
        {
            std::ifstream temp_file(temp_path, std::ios::binary);
            t_check("成功后临时文件已清理", !temp_file.is_open());
        }

        ecs::manager loaded_mgr;
        serialize::full_archive reader(loaded_mgr);
        reader.register_components<archive_regression_component>();
        const operating_message loaded = reader.load_from_file(path);
        const auto* set = loaded_mgr.get_single_class_set<archive_regression_component>();
        const std::string* saved_metadata = reader.get_metadata("saved");
        t_check("旧文件被完整替换且可加载",
                loaded && set != nullptr && set->size() == 1
                && saved_metadata != nullptr && *saved_metadata == "yes");

        (void)std::remove(path.c_str());
        (void)std::remove(temp_path.c_str());
    }

    t_section(5, "强制校验、filter 所有权与尾随数据");
    {
        std::string data;
        t_check("校验配置测试存档成功", make_archive(data));
        ecs::manager mgr;
        serialize::full_archive reader(mgr);
        reader.register_components<archive_regression_component>();
        reader.set_checksum_required(true);
        t_check("缺少 checksum 被强制拒绝", !reader.load_from_string(data));
        reader.set_checksum_required(false);
        t_check("兼容模式允许无 checksum", reader.load_from_string(data));
        t_check("完整存档尾随垃圾被拒绝", !reader.load_from_string(data + "x"));

        ecs::manager source;
        const auto first = source.create_entity();
        const auto second = source.create_entity();
        source.get_entity_state_by_index(first.parts_.index_).tag = 1;
        source.get_entity_state_by_index(second.parts_.index_).tag = 2;
        serialize::full_archive writer(source);
        serialize::serialize_filter filter;
        filter.by_tag(1);
        writer.set_filter(&filter);
        filter.by_tag(2);
        std::string filtered;
        t_check("filter 修改原对象不影响存档", writer.save_to_string(
                filtered, serialize::archive_format::json));

        ecs::manager filtered_target;
        serialize::full_archive filtered_reader(filtered_target);
        t_check("filter 按保存时副本生效", filtered_reader.load_from_string(filtered)
                && filtered_target.alive_count() == 1);
    }

    t_section(6, "实体表错误在修改 world 前失败");
    {
        std::string data;
        t_check("实体表负例存档成功", make_archive(data));
        const size_t index_pos = data.find("\"i\":1");
        t_check("定位实体索引", index_pos != std::string::npos);
        if (index_pos != std::string::npos)
        {
            data.replace(index_pos, 5, "\"i\":0");
        }

        ecs::manager target;
        const auto existing = target.create_entity();
        (void)target.add(existing, archive_regression_component{99});
        serialize::full_archive reader(target);
        reader.register_components<archive_regression_component>();
        const operating_message result = reader.load_from_string(data);
        t_check("零实体索引明确失败", !result);
        t_check("实体表失败保留原 world", target.alive_count() == 1
                && target.is_entity_valid(existing)
                && target.component_count<archive_regression_component>() == 1);
    }

    t_section(7, "schema 指纹拒绝注册集合变化");
    {
        std::string data;
        t_check("指纹测试存档成功", make_archive(data));
        ecs::manager late_world;
        serialize::full_archive late_writer(late_world);
        late_writer.register_components<schema_late_component>();

        ecs::manager target;
        const auto existing = target.create_entity();
        (void)target.add(existing, archive_regression_component{55});
        serialize::full_archive reader(target);
        reader.register_components<archive_regression_component>();
        const operating_message result = reader.load_from_string(data);
        t_check("schema 变化明确返回版本错误", !result
                && result.is_code(om_err_version_mismatch));
        t_check("schema 失败不修改原 world", target.alive_count() == 1
                && target.is_entity_valid(existing)
                && target.get_ptr<archive_regression_component>(existing)->value == 55);
    }

    t_section(8, "变换链空结果显式失败");
    {
        ecs::manager source;
        const auto e = source.create_entity();
        (void)source.add(e, archive_regression_component{1});
        serialize::full_archive writer(source);
        writer.register_components<archive_regression_component>();
        writer.set_compression(&empty_transform, nullptr);
        std::string data;
        t_check("压缩产生空结果时保存失败", !writer.save_to_string(
                data, serialize::archive_format::json));

        serialize::full_archive custom_writer(source);
        custom_writer.on_save(&clear_transform);
        t_check("自定义保存变换产生空结果时失败", !custom_writer.save_to_string(
                data, serialize::archive_format::json));

        t_check("无变换存档成功", make_archive(data));
        ecs::manager target;
        serialize::full_archive reader(target);
        reader.register_components<archive_regression_component>();
        reader.on_load(&clear_transform);
        t_check("自定义加载变换产生空结果时失败", !reader.load_from_string(data));
    }

    return t_end("archive regression ");
}
