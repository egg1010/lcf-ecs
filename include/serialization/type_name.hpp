// type_name.hpp - 稳定类型名注册 + 实体引用字段注册 + 枚举注册
#pragma once

#include "../part/type_id.hpp"
#include "../part/dense.hpp"
#include "../reflection/query.hpp"
#include <cstring>
#include <cstdint>
#include <type_traits>

namespace ecs {

namespace detail {

struct type_name_entry {
    int         type_id;
    const char* name;
};

inline dense<type_name_entry>& type_name_registry() noexcept {
    static dense<type_name_entry> registry;
    return registry;
}

struct enum_entry {
    int         type_id;
    int         underlying_type_id;
};

inline dense<enum_entry>& enum_registry() noexcept {
    static dense<enum_entry> registry;
    return registry;
}

} // namespace detail

// 注册稳定类型名
template<typename T>
void register_type_name(const char* stable_name) noexcept {
    int tid = type_id::get_type_id<T>();
    auto& reg = detail::type_name_registry();
    for (size_t i = 0; i < reg.size(); ++i)
    {
        if (reg[i].type_id == tid)
        {
            reg[i].name = stable_name;
            return;
        }
    }
    reg.push_back({tid, stable_name});
}

[[nodiscard]] inline const char* lookup_type_name(int tid) noexcept {
    auto& reg = detail::type_name_registry();
    for (size_t i = 0; i < reg.size(); ++i)
    {
        if (reg[i].type_id == tid)
        {
            return reg[i].name;
        }
    }
    return nullptr;
}

[[nodiscard]] inline int lookup_type_id(const char* name) noexcept {
    auto& reg = detail::type_name_registry();
    for (size_t i = 0; i < reg.size(); ++i)
    {
        if (std::strcmp(reg[i].name, name) == 0)
        {
            return reg[i].type_id;
        }
    }
    return -1;
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

// ============================================================================
// 实体引用字段注册 (加载时重映射)
// ============================================================================
namespace detail {

struct entity_field_info {
    int         type_id;
    const char* field_name;
    uint32_t    offset;
};

inline dense<entity_field_info>& entity_field_registry() noexcept {
    static dense<entity_field_info> registry;
    return registry;
}

} // namespace detail

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
        if (reg[i].type_id == tid && std::strcmp(reg[i].field_name, field_name) == 0)
        {
            reg[i].offset = fm->offset;
            return;
        }
    }
    reg.push_back({tid, field_name, fm->offset});
}

// ============================================================================
// 类型名查询 (自由函数, 供 archive_logic 等模块复用)
// 优先返回注册的稳定名, 未注册则返回 typeid().name()
// ============================================================================
template<typename T>
[[nodiscard]] inline std::string_view get_type_name() noexcept {
    const char* stable = lookup_type_name(type_id::get_type_id<T>());
    if (stable)
    {
        return stable;
    }
    static std::string name = typeid(T).name();
    return name;
}

} // namespace ecs

