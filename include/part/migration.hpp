// migration.hpp - 字段级迁移 + 组件版本控制
#pragma once

#include "dense.hpp"
#include "type_id.hpp"
#include "codec/json_writer.hpp"
#include "codec/json_reader.hpp"
#include <cstdint>
#include <cstring>
#include <functional>

namespace detail {

struct migration_entry
{
    int type_id;
    uint32_t from_version;
    uint32_t to_version;
    void (*migrate)(json_reader& old, json_writer& neu) noexcept;
};

struct component_version_entry
{
    int type_id;
    uint32_t version;
};

inline dense<migration_entry>& migration_registry() noexcept
{
    static dense<migration_entry> reg;
    return reg;
}

inline dense<component_version_entry>& component_version_registry() noexcept
{
    static dense<component_version_entry> reg;
    return reg;
}

} // namespace detail

template<typename T>
void register_component_version(uint32_t version) noexcept
{
    int tid = type_id::get_type_id<T>();
    auto& reg = detail::component_version_registry();
    for (size_t i = 0; i < reg.size(); ++i)
    {
        if (reg[i].type_id == tid)
        {
            reg[i].version = version;
            return;
        }
    }
    reg.push_back({tid, version});
}

[[nodiscard]] inline uint32_t lookup_component_version(int tid) noexcept
{
    auto& reg = detail::component_version_registry();
    for (size_t i = 0; i < reg.size(); ++i)
    {
        if (reg[i].type_id == tid)
        {
            return reg[i].version;
        }
    }
    return 0;
}

template<typename T>
[[nodiscard]] inline uint32_t lookup_component_version() noexcept
{
    return lookup_component_version(type_id::get_type_id<T>());
}

template<typename T>
void register_migration(uint32_t from, uint32_t to,
                        void (*migrate)(json_reader& old, json_writer& neu) noexcept) noexcept
{
    int tid = type_id::get_type_id<T>();
    auto& reg = detail::migration_registry();
    reg.push_back({tid, from, to, migrate});
}

// 对组件数据执行迁移链: from_version → to_version
// old_data: 旧版本 JSON 字符串
// 输出: 新版本 JSON 字符串 (写入 neu)
inline bool migrate_component(int tid, uint32_t from_ver, uint32_t to_ver,
                               std::string_view old_data, json_writer& neu) noexcept
{
    if (from_ver >= to_ver)
    {
        neu.raw_value(old_data);
        return true;
    }
    auto& reg = detail::migration_registry();
    std::string cur_data(old_data);

    uint32_t cur = from_ver;
    while (cur < to_ver)
    {
        bool found = false;
        for (size_t i = 0; i < reg.size(); ++i)
        {
            if (reg[i].type_id == tid && reg[i].from_version == cur && reg[i].to_version == cur + 1)
            {
                json_reader old_r(cur_data);
                json_writer new_w;
                reg[i].migrate(old_r, new_w);
                cur_data = new_w.take();
                ++cur;
                found = true;
                break;
            }
        }
        if (!found)
        {
            // 无匹配迁移函数, 保留原数据
            neu.raw_value(cur_data);
            return false;
        }
    }

    neu.raw_value(cur_data);
    return true;
}

// 便捷版本: 返回迁移后的字符串
[[nodiscard]] inline std::string migrate_component_string(
    int tid, uint32_t from_ver, uint32_t to_ver,
    std::string_view old_data) noexcept
{
    json_writer w;
    if (migrate_component(tid, from_ver, to_ver, old_data, w))
    {
        return w.take();
    }
    return std::string(old_data);
}

// ============================================================================
// #C1 声明式字段 rename/drop + #C2 默认值注入
// 字段级 schema 演进, 不依赖组件版本号, 每次加载自动应用
// 用法:
//   register_field_rename<T>("old_hp", "hp");     // 旧字段名 → 新字段名
//   register_field_drop<T>("deprecated_flag");    // 加载时跳过此字段
//   register_field_default<T>("max_mp", "100");   // 缺失字段注入默认值
// ============================================================================

namespace detail {

struct field_rename_entry {
    int         type_id;
    const char* old_name;
    const char* new_name;
};

struct field_drop_entry {
    int         type_id;
    const char* field_name;
};

struct field_default_entry {
    int         type_id;
    const char* field_name;
    std::string default_json;  // JSON 值片段, e.g. "100", "\"hello\"", "{\"x\":1}"
};

inline dense<field_rename_entry>& field_rename_registry() noexcept {
    static dense<field_rename_entry> reg;
    return reg;
}

inline dense<field_drop_entry>& field_drop_registry() noexcept {
    static dense<field_drop_entry> reg;
    return reg;
}

inline dense<field_default_entry>& field_default_registry() noexcept {
    static dense<field_default_entry> reg;
    return reg;
}

// 检查 type_id 是否有任何字段 schema 注册 (用于快速跳过)
[[nodiscard]] inline bool has_field_schema(int tid) noexcept {
    auto& renames = field_rename_registry();
    for (size_t i = 0; i < renames.size(); ++i) {
        if (renames[i].type_id == tid) return true;
    }
    auto& drops = field_drop_registry();
    for (size_t i = 0; i < drops.size(); ++i) {
        if (drops[i].type_id == tid) return true;
    }
    auto& defaults = field_default_registry();
    for (size_t i = 0; i < defaults.size(); ++i) {
        if (defaults[i].type_id == tid) return true;
    }
    return false;
}

// 查找字段重命名: old_name → new_name (nullptr 表示无重命名)
[[nodiscard]] inline const char* lookup_field_rename(int tid, std::string_view old_name) noexcept {
    auto& reg = field_rename_registry();
    for (size_t i = 0; i < reg.size(); ++i) {
        if (reg[i].type_id == tid && std::string_view(reg[i].old_name) == old_name) {
            return reg[i].new_name;
        }
    }
    return nullptr;
}

// 检查字段是否应被丢弃
[[nodiscard]] inline bool is_field_dropped(int tid, std::string_view name) noexcept {
    auto& reg = field_drop_registry();
    for (size_t i = 0; i < reg.size(); ++i) {
        if (reg[i].type_id == tid && std::string_view(reg[i].field_name) == name) {
            return true;
        }
    }
    return false;
}

} // namespace detail

// 注册字段重命名
template<typename T>
void register_field_rename(const char* old_name, const char* new_name) noexcept {
    int tid = type_id::get_type_id<T>();
    auto& reg = detail::field_rename_registry();
    for (size_t i = 0; i < reg.size(); ++i) {
        if (reg[i].type_id == tid && std::string_view(reg[i].old_name) == old_name) {
            reg[i].new_name = new_name;
            return;
        }
    }
    reg.push_back({tid, old_name, new_name});
}

// 注册字段丢弃 (加载时跳过)
template<typename T>
void register_field_drop(const char* field_name) noexcept {
    int tid = type_id::get_type_id<T>();
    auto& reg = detail::field_drop_registry();
    for (size_t i = 0; i < reg.size(); ++i) {
        if (reg[i].type_id == tid && std::string_view(reg[i].field_name) == field_name) {
            return;
        }
    }
    reg.push_back({tid, field_name});
}

// 注册字段默认值 (缺失时注入, default_json 为 JSON 值片段)
template<typename T>
void register_field_default(const char* field_name, std::string default_json) noexcept {
    int tid = type_id::get_type_id<T>();
    auto& reg = detail::field_default_registry();
    for (size_t i = 0; i < reg.size(); ++i) {
        if (reg[i].type_id == tid && std::string_view(reg[i].field_name) == field_name) {
            reg[i].default_json = std::move(default_json);
            return;
        }
    }
    reg.push_back({tid, field_name, std::move(default_json)});
}

// 应用字段 schema: 无注册时返回原字符串 (零开销快速路径)
[[nodiscard]] inline std::string apply_field_schema(int tid, std::string_view json) noexcept {
    if (!detail::has_field_schema(tid)) {
        return std::string(json);
    }

    json_reader r(json);
    if (!r.enter_object()) {
        return std::string(json);
    }

    json_writer w;
    w.begin_object();

    // 记录已出现字段名, 用于后续默认值注入判断
    dense<std::string_view> seen_fields;

    std::string_view key;
    while (!(key = r.next_key()).empty()) {
        if (detail::is_field_dropped(tid, key)) {
            r.skip_value();
            continue;
        }
        const char* new_name = detail::lookup_field_rename(tid, key);
        std::string_view out_name = new_name ? std::string_view(new_name) : key;

        std::string_view raw = r.read_raw_value();
        w.key(std::string(out_name));
        w.raw_value(raw);
        seen_fields.push_back(out_name);
    }

    // #C2 注入缺失字段的默认值
    auto& defaults = detail::field_default_registry();
    for (size_t i = 0; i < defaults.size(); ++i) {
        if (defaults[i].type_id != tid) continue;
        bool found = false;
        for (size_t j = 0; j < seen_fields.size(); ++j) {
            if (seen_fields[j] == defaults[i].field_name) {
                found = true;
                break;
            }
        }
        if (!found) {
            w.key(defaults[i].field_name);
            w.raw_value(defaults[i].default_json);
        }
    }

    w.end_object();
    return w.take();
}
