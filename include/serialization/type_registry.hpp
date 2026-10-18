// type_registry.hpp - 稳定类型名 + 类型工厂表 + 枚举/实体引用字段注册
#pragma once

#include "../part/type_id.hpp"
#include "../part/dense.hpp"
#include "../part/codec/archive_codec.hpp"
#include "../reflection/query.hpp"
#include "../reflection/meta.hpp"
#include <cstdint>
#include <string>
#include <string_view>
#include <type_traits>

namespace serialize {

struct load_context;

namespace detail {

struct enum_entry {
    int         type_id;
    int         underlying_type_id;
};

inline dense<enum_entry>& enum_registry() noexcept {
    static dense<enum_entry> registry;
    return registry;
}

struct entity_field_info {
    int         type_id;
    const char* field_name;
    uint32_t    offset;
};

inline dense<entity_field_info>& entity_field_registry() noexcept {
    static dense<entity_field_info> registry;
    return registry;
}

// 运行时类型工厂条目: 类型擦除的归档加载入口 + 用户 to_json 入口
struct type_factory_entry {
    int          type_id;
    std::string_view name;
    bool(*archive_load_fn)(load_context&, archive_reader&, uint32_t saved_cv);
    bool         has_user_json;
    std::string(*json_to_fn)(const void* p);
    size_t       type_size;
    bool         is_trivially_copyable;
};

inline dense<type_factory_entry>& type_factory_registry() noexcept {
    static dense<type_factory_entry> registry;
    return registry;
}

// type_id → registry 索引的稠密映射 (按 type_id 索引, -1 表示未注册)
inline dense<int32_t>& factory_id_index() noexcept {
    static dense<int32_t> index;
    return index;
}

inline uint32_t ensure_factory_entry(int tid, std::string_view name,
                                      size_t type_size, bool trivial) noexcept {
    auto& reg = type_factory_registry();
    auto& idx = factory_id_index();
    if (tid >= 0 && tid < static_cast<int>(idx.size())
        && idx[tid] >= 0 && static_cast<size_t>(idx[tid]) < reg.size())
    {
        return static_cast<uint32_t>(idx[tid]);
    }
    type_factory_entry entry{};
    entry.type_id = tid;
    entry.name = name;
    entry.type_size = type_size;
    entry.is_trivially_copyable = trivial;
    if (static_cast<int>(idx.size()) <= tid)
    {
        idx.resize_fill(static_cast<size_t>(tid) + 1, -1);
    }
    const uint32_t new_idx = static_cast<uint32_t>(reg.size());
    idx[tid] = static_cast<int32_t>(new_idx);
    reg.push_back(entry);
    return new_idx;
}

[[nodiscard]] inline const type_factory_entry* find_factory_by_id(int tid) noexcept {
    auto& idx = factory_id_index();
    if (tid < 0 || tid >= static_cast<int>(idx.size())) return nullptr;
    const int32_t i = idx[tid];
    auto& reg = type_factory_registry();
    if (i < 0 || static_cast<size_t>(i) >= reg.size()) return nullptr;
    return &reg[i];
}

[[nodiscard]] inline const type_factory_entry* find_factory_by_name(std::string_view name) noexcept {
    if (name.empty()) return nullptr;
    const int id = type_id::get_def_type_id(name);
    if (id < 0) return nullptr;
    return find_factory_by_id(id);
}

// 稳定类型名解析: 优先 def 名字表, 其次反射元信息名
[[nodiscard]] inline std::string_view stable_type_name(int tid) noexcept {
    std::string_view n = type_id::get_def_type_name(tid);
    if (!n.empty()) {
        return n;
    }
    const reflect::type_meta* m = reflect::global().get_type(tid);
    if (m != nullptr && m->name != nullptr) {
        return std::string_view(m->name);
    }
    return {};
}

} // namespace detail

template<typename T>
void register_type_name(const char* stable_name) noexcept {
    type_id::bind_def_name(stable_name, type_id::get_type_id<T>());
}

template<typename T>
void register_type_alias(const char* old_name) noexcept {
    type_id::bind_def_name(old_name, type_id::get_type_id<T>());
}

[[nodiscard]] inline std::string_view lookup_type_name(int tid) noexcept {
    return type_id::get_def_type_name(tid);
}

[[nodiscard]] inline int lookup_type_id(std::string_view name) noexcept {
    return type_id::get_def_type_id(name);
}

// 枚举注册 (序列化为整数)
template<typename T>
void register_enum() noexcept {
    static_assert(std::is_enum_v<T>);
    int tid = type_id::get_type_id<T>();
    using underlying = std::underlying_type_t<T>;
    int utid = type_id::get_type_id<underlying>();
    auto& reg = detail::enum_registry();
    for (size_t i = 0; i < reg.size(); ++i)
    {
        if (reg[i].type_id == tid)
        {
            reg[i].underlying_type_id = utid;
            return;
        }
    }
    reg.push_back({tid, utid});
}

[[nodiscard]] inline int lookup_enum_underlying(int tid) noexcept {
    auto& reg = detail::enum_registry();
    for (size_t i = 0; i < reg.size(); ++i)
    {
        if (reg[i].type_id == tid)
        {
            return reg[i].underlying_type_id;
        }
    }
    return -1;
}

template<typename T>
void register_entity_field(const char* field_name) noexcept {
    auto qv = reflect::try_get<T>();
    if (!qv.valid())
    {
        return;
    }
    const auto* fm = qv.field_by_name(field_name);
    if (!fm)
    {
        return;
    }
    int tid = type_id::get_type_id<T>();
    auto& reg = detail::entity_field_registry();
    for (size_t i = 0; i < reg.size(); ++i)
    {
        if (reg[i].type_id == tid && std::string_view(reg[i].field_name) == field_name)
        {
            reg[i].offset = fm->offset;
            return;
        }
    }
    reg.push_back({tid, field_name, fm->offset});
}

template<typename T>
[[nodiscard]] inline std::string_view get_type_name() noexcept {
    std::string_view stable = lookup_type_name(type_id::get_type_id<T>());
    if (!stable.empty())
    {
        return stable;
    }
    return detail::stable_type_name(type_id::get_type_id<T>());
}

} // namespace serialize
