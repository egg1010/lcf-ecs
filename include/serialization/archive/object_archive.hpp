// object_archive.hpp - 与 ECS 解耦的通用对象序列化 (单对象 / 对象数组)
#pragma once

#include "part/dense.hpp"
#include "part/safety.hpp"
#include "part/operating_message.hpp"
#include "part/codec/archive_codec.hpp"
#include "part/codec/codec_registry.hpp"
#include "serialization/base/model.hpp"
#include "serialization/codec/codec_bridge.hpp"
#include "serialization/codec/field_codec.hpp"
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

namespace serialize {

namespace detail {

// 载荷规则与组件一致: 用户 to_json > 反射字段 > 原始字节
template<typename T>
bool write_object_value(archive_writer& w, const T& v) noexcept
{
    if constexpr (field_codec::has_json_serialize<T>)
    {
        std::string j = v.to_json();
        w.write_raw(j);
        return true;
    }
    else if constexpr (std::is_trivially_copyable_v<T>)
    {
        if (field_codec::is_reflected<T>())
        {
            return field_codec::to_archive(w, v);
        }
        w.write_bytes(&v, sizeof(T));
        return true;
    }
    else
    {
        if (field_codec::is_reflected<T>())
        {
            return field_codec::to_archive(w, v);
        }
        w.write_bytes(nullptr, 0);
        return true;
    }
}

template<typename T>
bool read_object_value(archive_reader& r, T& v) noexcept
{
    if constexpr (field_codec::has_json_deserialize<T>)
    {
        v.from_json(r.read_bytes_view(0));
        return !r.has_error();
    }
    else if constexpr (std::is_trivially_copyable_v<T>)
    {
        if (field_codec::is_reflected<T>())
        {
            return field_codec::from_archive(r, v);
        }
        const std::string_view bytes = r.read_bytes_view(sizeof(T));
        if (bytes.size() != sizeof(T))
        {
            return false;
        }
        std::memcpy(&v, bytes.data(), sizeof(T));
        return true;
    }
    else
    {
        if (field_codec::is_reflected<T>())
        {
            return field_codec::from_archive(r, v);
        }
        r.skip_value();
        return false;
    }
}

} // namespace detail

class object_archive
{
    template<typename T>
    operating_message read_body(archive_reader& r, dense<T>& out) noexcept
    {
        if (!r.enter_object())
        {
            return r.has_error() ? r.last_error() : operating_message{};
        }

        r.next_key();
        const uint32_t fv = r.read_u32();
        if (fv > format_version)
        {
            operating_message res;
            res.write(false, "对象存档版本 ", fv, " 高于当前支持版本 ",
                            std::to_string(format_version));
            return res;
        }

        r.next_key();
        if (!r.enter_array())
        {
            operating_message res;
            res.write(false, "对象数组缺失");
            return res;
        }
        while (r.next_element())
        {
            if (out.size() >= limits.max_array_elements)
            {
                operating_message res;
                res.write(false, "对象数量超限");
                return res;
            }
            T v{};
            if (!detail::read_object_value<T>(r, v))
            {
                operating_message res;
                res.write(false, "对象载荷解析失败");
                return res;
            }
            r.end_element();
            out.push_back(std::move(v));
        }
        r.leave_array();
        r.leave_object();
        return operating_message{};
    }

    [[nodiscard]] static bool read_all(const std::string& path, std::string& content,
                                       size_t max_file_size) noexcept
    {
        std::ifstream f(path, std::ios::binary | std::ios::ate);
        if (!f)
        {
            return false;
        }
        const std::streamsize sz = f.tellg();
        if (sz < 0 || static_cast<size_t>(sz) > max_file_size)
        {
            return false;
        }
        f.seekg(0, std::ios::beg);
        content.resize(static_cast<size_t>(sz));
        f.read(content.data(), sz);
        return true;
    }

public:
    codec_bridge bridge;
    safety_limits limits;
    uint32_t format_version = 1;

    template<typename T>
    operating_message save_array(std::string& out, archive_format fmt,
                                 const T* items, size_t count) noexcept
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
        w->begin_object();
        w->key("v"); w->write_u32(format_version);
        w->key("items"); w->begin_array(count);
        bool ok = true;
        for (size_t i = 0; i < count; ++i)
        {
            if (!detail::write_object_value<T>(*w, items[i]))
            {
                ok = false;
            }
        }
        w->end_array();
        w->end_object();

        out = w->take();
        codec->destroy_writer(w);
        if (!ok)
        {
            out.clear();
            operating_message res;
            res.write(false, "对象存在不支持的字段类型: 序列化失败");
            return res;
        }
        bridge.wrap_save(out);
        return operating_message{};
    }

    template<typename T>
    operating_message load_array(std::string_view data, dense<T>& out) noexcept
    {
        out.clear();
        std::string content(data);
        operating_message u = bridge.unwrap_load(content);
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
        operating_message result = read_body<T>(*r, out);
        codec->destroy_reader(r);
        return result;
    }

    template<typename T>
    operating_message save_object(std::string& out, archive_format fmt, const T& obj) noexcept
    {
        return save_array(out, fmt, &obj, 1);
    }

    template<typename T>
    operating_message load_object(std::string_view data, T& obj) noexcept
    {
        dense<T> items;
        operating_message r = load_array(data, items);
        if (!r)
        {
            return r;
        }
        if (items.size() != 1)
        {
            operating_message res;
            res.write(false, "对象数量不符: ", items.size());
            return res;
        }
        obj = std::move(items[0]);
        return operating_message{};
    }

    template<typename T>
    operating_message save_array_to_file(const std::string& path, archive_format fmt,
                                         const T* items, size_t count) noexcept
    {
        std::string data;
        operating_message r = save_array(data, fmt, items, count);
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

    template<typename T>
    operating_message load_array_from_file(const std::string& path, dense<T>& out) noexcept
    {
        std::string content;
        if (!read_all(path, content, limits.max_file_size))
        {
            operating_message res;
            res.write(false, "无法读取文件: ", path);
            return res;
        }
        return load_array(content, out);
    }

    template<typename T>
    operating_message save_object_to_file(const std::string& path, archive_format fmt,
                                          const T& obj) noexcept
    {
        return save_array_to_file(path, fmt, &obj, 1);
    }

    template<typename T>
    operating_message load_object_from_file(const std::string& path, T& obj) noexcept
    {
        std::string content;
        if (!read_all(path, content, limits.max_file_size))
        {
            operating_message res;
            res.write(false, "无法读取文件: ", path);
            return res;
        }
        return load_object(content, obj);
    }
};

} // namespace serialize
