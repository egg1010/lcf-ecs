// codec_bridge.hpp - 格式分派 + 变换链 (校验 → 压缩 → 加密)
#pragma once

#include "../part/codec/archive_codec.hpp"
#include "../part/codec/codec_registry.hpp"
#include "../part/crc32c.hpp"
#include "../part/operating_message.hpp"
#include "model.hpp"
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>

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

    [[nodiscard]] operating_message unwrap_load(std::string& content) noexcept
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
            operating_message r = strip_checksum(content);
            if (!r)
            {
                return r;
            }
        }
        if (decompress_cb != nullptr && !is_binary_format(content))
        {
            content = decompress_cb(content);
            if (enable_checksum)
            {
                operating_message r = strip_checksum(content);
                if (!r)
                {
                    return r;
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

    [[nodiscard]] static operating_message strip_checksum(std::string& content) noexcept
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
            operating_message res;
            res.write(false, "CRC32C 校验失败: 存档可能已损坏");
            return res;
        }
        content.erase(0, 8);
        return operating_message{};
    }
};

} // namespace serialize
