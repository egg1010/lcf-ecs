#pragma once

// type_registry.hpp - 稳定类型名 / 枚举登记 / 存档变换链
// 命名空间: serialize

#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <type_traits>

#include "part/codec/archive_codec.hpp"
#include "part/codec/codec_registry.hpp"
#include "part/crc32c.hpp"
#include "part/dense.hpp"
#include "part/id_table.hpp"
#include "part/operating_message.hpp"
#include "part/type/type_id.hpp"
#include "reflection/core/query.hpp"
#include "serialization/base/model.hpp"

// codec_bridge.hpp - 格式分派 + 变换链 (校验 → 压缩 → 加密)


namespace serialize {

using progress_fn = void(*)(size_t current, size_t total) noexcept;
using transform_fn = void(*)(std::string& data) noexcept;
using compress_fn = std::string(*)(const std::string&) noexcept;
using decompress_fn = std::string(*)(const std::string&) noexcept;
using encrypt_fn = std::string(*)(const std::string&) noexcept;
using decrypt_fn = std::string(*)(const std::string&) noexcept;

struct codec_bridge
{
    bool enable_checksum = true;
    transform_fn on_save_cb = nullptr;
    transform_fn on_load_cb = nullptr;
    compress_fn compress_cb = nullptr;
    decompress_fn decompress_cb = nullptr;
    encrypt_fn encrypt_cb = nullptr;
    decrypt_fn decrypt_cb = nullptr;

    [[nodiscard]] static const archive_codec* pick(archive_format fmt) noexcept
    {
        switch (fmt)
        {
            case archive_format::json:
                return get_codec(codec_index::json);
            case archive_format::binary:
                return get_codec(codec_index::binary);
            case archive_format::protobuf:
                return get_codec(codec_index::protobuf);
            case archive_format::flatbuffer:
                return get_codec(codec_index::flatbuffer);
        }
        return get_codec(codec_index::json);
    }

    [[nodiscard]] static bool is_binary_format(std::string_view data) noexcept
    {
        return data.size() >= 4 && std::memcmp(data.data(), "LCE1", 4) == 0;
    }

    void wrap_save(std::string& out) noexcept
    {
        if (enable_checksum)
        {
            add_checksum(out);
        }
        if (compress_cb != nullptr)
        {
            out = compress_cb(out);
        }
        if (encrypt_cb != nullptr)
        {
            out = encrypt_cb(out);
        }
        if (on_save_cb != nullptr)
        {
            on_save_cb(out);
        }
    }

    operating_message unwrap_load(std::string& content) noexcept
    {
        if (on_load_cb != nullptr)
        {
            on_load_cb(content);
        }
        if (decrypt_cb != nullptr)
        {
            content = decrypt_cb(content);
        }
        if (enable_checksum)
        {
            operating_message error = strip_checksum(content);
            if (!error)
            {
                return error;
            }
        }
        if (decompress_cb != nullptr && !is_binary_format(content))
        {
            content = decompress_cb(content);
            if (enable_checksum)
            {
                operating_message error = strip_checksum(content);
                if (!error)
                {
                    return error;
                }
            }
        }
        return operating_message{};
    }

    static void add_checksum(std::string& out) noexcept
    {
        uint32_t cs = compute_crc32c(out);
        std::string wrapped;
        wrapped.reserve(out.size() + 8);
        wrapped.append("LCCS", 4);
        wrapped.append(reinterpret_cast<const char*>(&cs), 4);
        wrapped.append(out);
        out = std::move(wrapped);
    }

    static operating_message strip_checksum(std::string& content) noexcept
    {
        if (content.size() < 8)
        {
            return operating_message{};
        }
        if (std::memcmp(content.data(), "LCCS", 4) != 0)
        {
            return operating_message{};
        }
        uint32_t stored_cs = 0;
        std::memcpy(&stored_cs, content.data() + 4, 4);
        const size_t payload_len = content.size() - 8;
        uint32_t actual_cs = compute_crc32c(content.data() + 8, payload_len);
        if (stored_cs != actual_cs)
        {
            operating_message error;
            error.write_failure(om_err_checksum_failed.code, msg_level::warn, msg::here());
            return error;
        }
        content.erase(0, 8);
        return operating_message{};
    }
};

} // namespace serialize

// type_registry.hpp - 稳定类型名 + 类型工厂表 + 枚举/实体引用字段注册


namespace serialize {

struct load_context;

namespace detail {

struct entity_field_info {
    int         type_id;
    const char* field_name;
    uint32_t    offset;
};

inline id_table<dense<entity_field_info>>& entity_field_registry() noexcept {
    static id_table<dense<entity_field_info>> registry;
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

inline id_table<type_factory_entry>& type_factory_registry() noexcept {
    static id_table<type_factory_entry> registry;
    return registry;
}

inline uint32_t ensure_factory_entry(int tid, std::string_view name,
                                      size_t type_size, bool trivial) noexcept {
    auto& reg = type_factory_registry();
    const int32_t existing = reg.index_of(tid);
    if (existing >= 0)
    {
        return static_cast<uint32_t>(existing);
    }
    type_factory_entry entry{};
    entry.type_id = tid;
    entry.name = name;
    entry.type_size = type_size;
    entry.is_trivially_copyable = trivial;
    (void)reg.ensure(tid, entry);
    const int32_t added = reg.index_of(tid);
    return added >= 0 ? static_cast<uint32_t>(added) : 0u;
}

[[nodiscard]] inline const type_factory_entry* find_factory_by_id(int tid) noexcept {
    return type_factory_registry().find(tid);
}

[[nodiscard]] inline const type_factory_entry* find_factory_by_name(std::string_view name) noexcept
{
    if (name.empty())
    {
        return nullptr;
    }
    const int id = type_id::get_def_type_id(name);
    if (id < 0)
    {
        return nullptr;
    }
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
    reflect::global().register_enum_type<T>();
}

[[nodiscard]] inline int lookup_enum_underlying(int tid) noexcept {
    const reflect::enum_meta* em = reflect::global().find_enum(tid);
    return em != nullptr ? em->underlying_type_id : -1;
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
    dense<detail::entity_field_info>* fields = reg.find(tid);
    if (fields == nullptr)
    {
        (void)reg.ensure(tid, dense<detail::entity_field_info>{});
        fields = reg.find(tid);
    }
    if (fields == nullptr)
    {
        return;
    }
    for (size_t i = 0; i < fields->size(); ++i)
    {
        if (std::string_view((*fields)[i].field_name) == field_name)
        {
            (*fields)[i].offset = fm->offset;
            return;
        }
    }
    fields->emplace_back(detail::entity_field_info{tid, field_name, fm->offset});
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

