// codec_registry.hpp - 编码器注册表 + 格式自动检测
// 管理所有 archive_codec 工厂, 按 magic 头自动选择解码器
#pragma once

#include "archive_codec.hpp"
#include "codec_json.hpp"
#include "codec_binary.hpp"
#include "codec_protobuf.hpp"
#include "codec_flatbuffer.hpp"
#include "dense.hpp"
#include <string>
#include <string_view>
#include <cstring>

// ============================================================================
// codec_registry — 编解码器注册表
// 每种格式注册一个 archive_codec 工厂, 通过 magic 头自动检测格式
// ============================================================================
class codec_registry
{
    dense<const archive_codec*> codecs_;

    codec_registry() noexcept {
        // 注册内置格式: JSON / Binary / Protobuf / FlatBuffer
        static json_codec       json_c;
        static binary_codec     bin_c;
        static protobuf_codec   pb_c;
        static flatbuffer_codec fb_c;

        codecs_.push_back(&json_c);
        codecs_.push_back(&bin_c);
        codecs_.push_back(&pb_c);
        codecs_.push_back(&fb_c);
    }

public:
    [[nodiscard]] static codec_registry& instance() noexcept {
        static codec_registry reg;
        return reg;
    }

    // 注册自定义格式
    void register_codec(const archive_codec* c) noexcept {
        codecs_.push_back(c);
    }

    // 按 magic 头检测格式, 返回对应 codec (无匹配返回 JSON 作为兜底)
    [[nodiscard]] const archive_codec* detect(std::string_view data) const noexcept {
        for (size_t i = 0; i < codecs_.size(); ++i)
        {
            if (codecs_[i]->matches(data))
            {
                return codecs_[i];
            }
        }
        // 兜底: JSON (首个注册的 codec)
        return codecs_.size() > 0 ? codecs_[0] : nullptr;
    }

    // 按索引获取 (0=JSON, 1=Binary, 2=Protobuf, 3=FlatBuffer)
    [[nodiscard]] const archive_codec* get(size_t idx) const noexcept {
        return idx < codecs_.size() ? codecs_[idx] : nullptr;
    }

    [[nodiscard]] size_t count() const noexcept { return codecs_.size(); }
};

// 格式枚举 (与 codec_registry 索引对应)
namespace codec_index {
    constexpr size_t json        = 0;
    constexpr size_t binary      = 1;
    constexpr size_t protobuf    = 2;
    constexpr size_t flatbuffer  = 3;
}

// 按格式枚举获取 codec
[[nodiscard]] inline const archive_codec* get_codec(size_t fmt_idx) noexcept {
    return codec_registry::instance().get(fmt_idx);
}

// 按 magic 头自动检测并获取 codec
[[nodiscard]] inline const archive_codec* detect_codec(std::string_view data) noexcept {
    return codec_registry::instance().detect(data);
}
