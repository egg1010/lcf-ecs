// migration.hpp - 字段级迁移 + 组件版本控制
#pragma once

#include "dense.hpp"
#include "type_id.hpp"
#include "json_writer.hpp"
#include "json_reader.hpp"
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
