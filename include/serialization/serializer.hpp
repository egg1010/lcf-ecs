// serializer.hpp - 序列化器主类
// 集成: JSON/Binary 双格式, 两阶段加载, 版本控制, 过滤器, 单实体,
//       迁移, 元数据, 增量, 追加加载, 进度回调, 统计, 回调钩子, 压缩
#pragma once

#include "archive_types.hpp"
#include "../part/codec/archive_codec.hpp"
#include "../part/codec/codec_registry.hpp"
#include "archive_logic.hpp"
#include "../component.hpp"
#include "../part/operating_message.hpp"
#include "../part/codec/json_writer.hpp"
#include "../part/codec/json_reader.hpp"
#include "../part/dense.hpp"
#include "../part/safety.hpp"
#include "type_name.hpp"
#include "reflect_bridge.hpp"
#include "../part/codec/binary_writer.hpp"
#include "../part/codec/binary_reader.hpp"
#include "filter.hpp"
#include "../part/migration.hpp"
#include "../part/stats.hpp"
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <type_traits>
#include <typeinfo>
#include <cstdint>
#include <utility>

namespace serialize {

using detail::archive_header;
using detail::entity_remap;
using detail::metadata_entry;

enum class load_mode : uint8_t {
    replace,
    append,
    merge
};

// #D3 加载策略: strict 遇错即失败, best_effort 跳过损坏组件继续加载
enum class load_policy : uint8_t {
    strict,
    best_effort
};

using progress_fn = void(*)(size_t current, size_t total) noexcept;
using transform_fn = void(*)(std::string& data) noexcept;
using compress_fn = std::string(*)(const std::string&) noexcept;
using decompress_fn = std::string(*)(const std::string&) noexcept;
// #A2 加密/解密回调 (与 compress_fn 同签名)
// 时序: save serialize→compress→encrypt→on_save; load 反向
// 加密破坏 magic, 解密必须在 detect_codec 之前
using encrypt_fn = std::string(*)(const std::string&) noexcept;
using decrypt_fn = std::string(*)(const std::string&) noexcept;

// ============================================================================
// 序列化器主类
// ============================================================================
class serialization
{
private:
    ecs::manager& mgr_;
    serialize_limits limits_;
    detail::archive_header header_;
    const serialize_filter* filter_ = nullptr;
    load_mode load_mode_ = load_mode::append;
    load_policy load_policy_ = load_policy::strict;
    serialize_stats stats_;
    dense<detail::metadata_entry> metadata_;
    dense<uint64_t> last_type_versions_;

    progress_fn progress_cb_ = nullptr;
    transform_fn on_save_cb_ = nullptr;
    transform_fn on_load_cb_ = nullptr;
    compress_fn compress_cb_ = nullptr;
    decompress_fn decompress_cb_ = nullptr;
    // #A2 加密/解密回调
    encrypt_fn encrypt_cb_ = nullptr;
    decrypt_fn decrypt_cb_ = nullptr;
    // #A1 CRC32C 校验开关 (默认启用, 8 字节 LCCS 前缀)
    bool enable_checksum_ = true;

public:
    explicit serialization(ecs::manager& m) noexcept
        : mgr_(m) {}

    [[nodiscard]] serialize_limits& limits() noexcept { return limits_; }
    [[nodiscard]] const serialize_limits& limits() const noexcept { return limits_; }

    [[nodiscard]] uint32_t archive_version() const noexcept { return header_.archive_version; }
    void set_archive_version(uint32_t v) noexcept { header_.archive_version = v; }
    [[nodiscard]] uint32_t engine_version() const noexcept { return header_.engine_version; }
    void set_engine_version(uint32_t v) noexcept { header_.engine_version = v; }

    enum class format : uint8_t { json, binary, protobuf, flatbuffer };

    // === 过滤器 ===
    serialization& set_filter(const serialize_filter* f) noexcept { filter_ = f; return *this; }
    [[nodiscard]] const serialize_filter* filter() const noexcept { return filter_; }

    // === 加载模式 ===
    serialization& set_load_mode(load_mode m) noexcept { load_mode_ = m; return *this; }
    [[nodiscard]] load_mode get_load_mode() const noexcept { return load_mode_; }

    // === #D3 加载策略 ===
    // strict: 遇到损坏组件立即返回错误 (默认)
    // best_effort: 跳过损坏组件继续加载, 跳过数记入 stats_.skipped_count
    serialization& set_load_policy(load_policy p) noexcept { load_policy_ = p; return *this; }
    [[nodiscard]] load_policy get_load_policy() const noexcept { return load_policy_; }

    // === 统计 ===
    [[nodiscard]] const serialize_stats& last_stats() const noexcept { return stats_; }

    // === 进度回调 ===
    void set_progress_callback(progress_fn cb) noexcept { progress_cb_ = cb; }

    // === 变换钩子 ===
    void on_save(transform_fn cb) noexcept { on_save_cb_ = cb; }
    void on_load(transform_fn cb) noexcept { on_load_cb_ = cb; }

    // === 压缩 ===
    void set_compression(compress_fn c, decompress_fn d) noexcept {
        compress_cb_ = c; decompress_cb_ = d;
    }

    // === #A2 加密 ===
    // 加密破坏 magic, 解密必须在 detect_codec 之前
    void set_encryption(encrypt_fn e, decrypt_fn d) noexcept {
        encrypt_cb_ = e; decrypt_cb_ = d;
    }

    // === #A1 CRC32C 校验 ===
    // 启用后数据前添加 8 字节 LCCS 前缀, 加载时自动校验
    void set_checksum_enabled(bool e) noexcept { enable_checksum_ = e; }
    [[nodiscard]] bool is_checksum_enabled() const noexcept { return enable_checksum_; }

    // === 元数据 ===
    serialization& set_metadata(const std::string& key, const std::string& value) noexcept {
        for (size_t i = 0; i < metadata_.size(); ++i)
        {
            if (metadata_[i].key == key)
            {
                metadata_[i].value = value;
                return *this;
            }
        }
        metadata_.push_back({key, value});
        return *this;
    }
    [[nodiscard]] const std::string* get_metadata(const std::string& key) const noexcept {
        for (size_t i = 0; i < metadata_.size(); ++i)
        {
            if (metadata_[i].key == key)
            {
                return &metadata_[i].value;
            }
        }
        return nullptr;
    }
    [[nodiscard]] const dense<detail::metadata_entry>& all_metadata() const noexcept { return metadata_; }

    // ====================================================================
    // 保存到文件
    // ====================================================================
    template<typename... Ts>
    operating_message save_to_file(const std::string& path,
                                    format fmt = format::json) noexcept {
        std::string data;
        operating_message r = save_to_string<Ts...>(data, fmt);
        if (!r)
        {
            return r;
        }
        std::ofstream f(path, std::ios::binary | std::ios::trunc);
        if (!f)
        {
            r.write_message(false, "无法打开文件: ", path);
            return r;
        }
        f.write(data.data(), static_cast<std::streamsize>(data.size()));
        f.close();
        return r;
    }

    // ====================================================================
    // 保存到字符串
    // ====================================================================
    template<typename... Ts>
    operating_message save_to_string(std::string& out,
                                     format fmt = format::json) noexcept {
        // #E 双轨: Ts... 路径同时幂等注册到 registry (供运行时路径使用)
        (register_factory_for_type<Ts>(), ...);

        stats_.reset();
        operating_message r;
        if (fmt == format::binary)
        {
            r = save_to_binary<Ts...>(out);
        }
        else if (fmt == format::protobuf)
        {
            r = save_via_codec<Ts...>(out, *get_codec(codec_index::protobuf));
        }
        else if (fmt == format::flatbuffer)
        {
            r = save_via_codec<Ts...>(out, *get_codec(codec_index::flatbuffer));
        }
        else
        {
            r = save_to_json<Ts...>(out);
        }
        // 变换顺序固定, 勿调 (完整时序见 usage 12.14.3)
        if (r && enable_checksum_)
        {
            apply_checksum_wrapper(out);
        }
        if (r && compress_cb_)
        {
            out = compress_cb_(out);
        }
        if (r && encrypt_cb_)
        {
            out = encrypt_cb_(out);
        }
        if (r && on_save_cb_)
        {
            on_save_cb_(out);
        }
        stats_.total_bytes = out.size();
        stats_.archive_version = header_.archive_version;
        return r;
    }

    // ====================================================================
    // 从文件加载
    // ====================================================================
    template<typename... Ts>
    operating_message load_from_file(const std::string& path) noexcept {
        std::ifstream f(path, std::ios::binary | std::ios::ate);
        if (!f)
        {
            operating_message r;
            r.write_message(false, "无法打开文件: ", path);
            return r;
        }
        std::streamsize sz = f.tellg();
        if (static_cast<size_t>(sz) > limits_.max_file_size)
        {
            operating_message r;
            r.write_message(false, "文件过大: ", path);
            return r;
        }
        f.seekg(0, std::ios::beg);
        std::string content;
        content.resize(static_cast<size_t>(sz));
        if (!f.read(content.data(), sz))
        {
            operating_message r; r.write_message(false, "读取失败: ", path); return r;
        }
        return load_from_string<Ts...>(content);
    }

    // 仅校验
    operating_message validate_file(const std::string& path) const noexcept {
        std::ifstream f(path, std::ios::binary | std::ios::ate);
        if (!f)
        {
            operating_message r;
            r.write_message(false, "无法打开文件: ", path);
            return r;
        }
        std::streamsize sz = f.tellg();
        if (static_cast<size_t>(sz) > limits_.max_file_size)
        {
            operating_message r;
            r.write_message(false, "文件过大: ", path);
            return r;
        }
        f.seekg(0, std::ios::beg);
        std::string content;
        content.resize(static_cast<size_t>(sz));
        if (!f.read(content.data(), sz))
        {
            operating_message r; r.write_message(false, "读取失败: ", path); return r;
        }
        return validate_string(content);
    }

    // ====================================================================
    // 从字符串加载 (自动检测格式)
    // ====================================================================
    template<typename... Ts>
    operating_message load_from_string(std::string_view data) noexcept {
        static_assert((json_serializable<Ts> && ...),
            "所有组件类型必须满足 json_serializable");

        // #E 双轨: Ts... 路径同时幂等注册到 registry (供运行时路径使用)
        (register_factory_for_type<Ts>(), ...);

        std::string content(data);
        if (on_load_cb_)
        {
            on_load_cb_(content);
        }
        // #A2 解密 (必须在 detect_codec 之前, 加密破坏 magic)
        if (decrypt_cb_)
        {
            content = decrypt_cb_(content);
        }
        // #A1 校验和剥离 (无压缩时 LCCS 直接可见)
        if (enable_checksum_)
        {
            operating_message cs_r = strip_and_validate_checksum(content);
            if (!cs_r)
            {
                return cs_r;
            }
        }
        if (decompress_cb_ && !is_binary_format(content))
        {
            content = decompress_cb_(content);
            // #A1 校验和剥离 (压缩数据解压后 LCCS 可见)
            if (enable_checksum_)
            {
                operating_message cs_r = strip_and_validate_checksum(content);
                if (!cs_r)
                {
                    return cs_r;
                }
            }
        }

        stats_.reset();

        if (load_mode_ == load_mode::replace)
        {
            clear_all_entities();
        }

        // 自动检测格式
        const archive_codec* codec = detect_codec(content);
        if (codec)
        {
            // 判断格式
            if (std::memcmp(codec->magic, "LCE1", 4) == 0)
            {
                return load_from_binary<Ts...>(content);
            }
            if (std::memcmp(codec->magic, "LCPB", 4) == 0 ||
                std::memcmp(codec->magic, "LCFB", 4) == 0)
            {
                return load_via_codec<Ts...>(content, *codec);
            }
            // JSON (magic[0] == '{')
            return load_from_json<Ts...>(content);
        }

        // 兜底: 尝试 JSON
        return load_from_json<Ts...>(content);
    }

    // 仅校验
    operating_message validate_string(std::string_view data) const noexcept {
        const archive_codec* codec = detect_codec(data);
        if (!codec)
        {
            operating_message res;
            res.write_message(false, "未知格式");
            return res;
        }
        // JSON 特殊处理 (按字段名校验)
        if (std::memcmp(codec->magic, "{", 1) == 0)
        {
            json_reader r(data);
            if (!r.enter_object())
            {
                return r.last_error();
            }
            std::string_view key;
            while (!(key = r.next_key()).empty())
            {
                if (!r.skip_value())
                {
                    return r.last_error();
                }
            }
            if (r.has_error())
            {
                return r.last_error();
            }
            return operating_message{};
        }
        // 二进制/protobuf/flatbuffer: 调用各格式的深度校验
        return codec->validate(data);
    }

    // ====================================================================
    // 单实体序列化
    // ====================================================================
    template<typename... Ts>
    operating_message save_entity(ecs::entity e, std::string& out) noexcept {
        serialize_filter ef;
        ef.use_whitelist = true;
        ef.entity_whitelist.push_back(e.parts_.index_);
        const serialize_filter* old = filter_;
        filter_ = &ef;
        auto r = save_to_string<Ts...>(out);
        filter_ = old;
        return r;
    }

    template<typename... Ts>
    operating_message load_entity(std::string_view json, ecs::entity& out_e) noexcept {
        load_mode old_mode = load_mode_;
        load_mode_ = load_mode::append;
        auto r = load_from_string<Ts...>(json);
        load_mode_ = old_mode;
        // 返回最后一个创建的实体
        out_e = ecs::entity{};
        return r;
    }

    // ====================================================================
    // 增量序列化 (只保存变化的类型)
    // ====================================================================
    template<typename... Ts>
    operating_message save_changed(std::string& out,
                                    format fmt = format::json) noexcept {
        if (last_type_versions_.size() == 0)
        {
            // 首次保存,全量
            constexpr size_t n = sizeof...(Ts);
            for (size_t i = 0; i < n; ++i)
            {
                last_type_versions_.push_back(0);
            }
        }
        // 检查哪些类型有变化
        bool any_changed = false;
        size_t idx = 0;
        ((check_type_changed<Ts>(idx, any_changed)), ...);

        if (!any_changed)
        {
            out = "{}";
            return operating_message{};
        }
        return save_to_string<Ts...>(out, fmt);
    }

    // ====================================================================
    // 便捷静态接口
    // ====================================================================
    template<typename... Ts>
    [[nodiscard]] static operating_message save(ecs::manager& m, const std::string& path,
                                                  format fmt = format::json) noexcept {
        return serialization(m).save_to_file<Ts...>(path, fmt);
    }

    template<typename... Ts>
    [[nodiscard]] static operating_message load(ecs::manager& m, const std::string& path) noexcept {
        return serialization(m).load_from_file<Ts...>(path);
    }

    // ====================================================================
    // #E 运行时路径 (无 Ts..., 遍历 type_factory_registry)
    // 双轨架构: 编译期 Ts... fold 零开销 + 运行时 registry 灵活
    // ====================================================================

    // 运行时保存到字符串 (遍历已注册的类型工厂)
    [[nodiscard]] operating_message save_to_string_runtime(std::string& out,
                                                             format fmt = format::json) noexcept {
        stats_.reset();
        operating_message r;
        if (fmt == format::binary)
        {
            r = save_to_binary_runtime(out);
        }
        else
        {
            r = save_to_json_runtime(out);
        }
        // 变换顺序固定, 勿调 (完整时序见 usage 12.14.3)
        if (r && enable_checksum_)
        {
            apply_checksum_wrapper(out);
        }
        if (r && compress_cb_)
        {
            out = compress_cb_(out);
        }
        if (r && encrypt_cb_)
        {
            out = encrypt_cb_(out);
        }
        if (r && on_save_cb_)
        {
            on_save_cb_(out);
        }
        stats_.total_bytes = out.size();
        stats_.archive_version = header_.archive_version;
        return r;
    }

    // 运行时从字符串加载 (按存档类型名查 registry)
    [[nodiscard]] operating_message load_from_string_runtime(std::string_view data) noexcept {
        std::string content(data);
        if (on_load_cb_)
        {
            on_load_cb_(content);
        }
        // #A2 解密
        if (decrypt_cb_)
        {
            content = decrypt_cb_(content);
        }
        // #A1 校验和剥离 (无压缩时)
        if (enable_checksum_)
        {
            operating_message cs_r = strip_and_validate_checksum(content);
            if (!cs_r)
            {
                return cs_r;
            }
        }
        if (decompress_cb_ && !is_binary_format(content))
        {
            content = decompress_cb_(content);
            // #A1 校验和剥离 (解压后)
            if (enable_checksum_)
            {
                operating_message cs_r = strip_and_validate_checksum(content);
                if (!cs_r)
                {
                    return cs_r;
                }
            }
        }

        stats_.reset();

        if (load_mode_ == load_mode::replace)
        {
            clear_all_entities();
        }

        const archive_codec* codec = detect_codec(content);
        if (codec)
        {
            if (std::memcmp(codec->magic, "LCE1", 4) == 0)
            {
                return load_from_binary_runtime(content);
            }
            // JSON (magic[0] == '{')
            return load_from_json_runtime(content);
        }
        return load_from_json_runtime(content);
    }

    // 运行时保存到文件
    [[nodiscard]] operating_message save_to_file_runtime(const std::string& path,
                                                          format fmt = format::json) noexcept {
        std::string data;
        operating_message r = save_to_string_runtime(data, fmt);
        if (!r)
        {
            return r;
        }
        std::ofstream f(path, std::ios::binary | std::ios::trunc);
        if (!f)
        {
            r.write_message(false, "无法打开文件: ", path);
            return r;
        }
        f.write(data.data(), static_cast<std::streamsize>(data.size()));
        f.close();
        return r;
    }

    // 运行时从文件加载
    [[nodiscard]] operating_message load_from_file_runtime(const std::string& path) noexcept {
        std::ifstream f(path, std::ios::binary | std::ios::ate);
        if (!f)
        {
            operating_message r;
            r.write_message(false, "无法打开文件: ", path);
            return r;
        }
        std::streamsize sz = f.tellg();
        if (static_cast<size_t>(sz) > limits_.max_file_size)
        {
            operating_message r;
            r.write_message(false, "文件过大: ", path);
            return r;
        }
        f.seekg(0, std::ios::beg);
        std::string content;
        content.resize(static_cast<size_t>(sz));
        if (!f.read(content.data(), sz))
        {
            operating_message r; r.write_message(false, "读取失败: ", path); return r;
        }
        return load_from_string_runtime(content);
    }

    // ====================================================================
    // #D1 悬空引用置零
    // 加载后扫描所有已注册的实体引用字段, 将指向不存在/已销毁实体的引用置零
    // 返回被置零的引用数量, 调用方可据此判断存档完整性
    // ====================================================================
    [[nodiscard]] size_t validate_references() noexcept
    {
        size_t nullified = 0;
        auto& field_reg = detail::entity_field_registry();
        if (field_reg.size() == 0)
        {
            return 0;
        }

        // 按 type_id 分组遍历 (避免重复查找 single_class_set)
        int cur_tid = -1;
        ecs::single_class_set* set = nullptr;
        dense<uint32_t> offsets;

        for (size_t i = 0; i < field_reg.size(); ++i)
        {
            int tid = field_reg[i].type_id;
            if (tid != cur_tid)
            {
                // 处理上一个类型的引用
                if (set && offsets.size() > 0)
                {
                    nullified += validate_references_for_set(set, offsets);
                }
                cur_tid = tid;
                set = mgr_.get_single_class_set_by_id(tid);
                offsets.clear();
            }
            offsets.push_back(field_reg[i].offset);
        }
        // 处理最后一个类型
        if (set && offsets.size() > 0)
        {
            nullified += validate_references_for_set(set, offsets);
        }
        return nullified;
    }

private:
    // 扫描单个 single_class_set 中的实体引用字段
    size_t validate_references_for_set(ecs::single_class_set* set,
                                        const dense<uint32_t>& offsets) noexcept
    {
        char* pool_base = static_cast<char*>(set->get_raw_pool_data());
        if (!pool_base)
        {
            return 0;
        }
        size_t count = set->size();
        size_t elem_size = set->get_component_size();
        size_t nullified = 0;
        for (size_t k = 0; k < count; ++k)
        {
            char* base = pool_base + k * elem_size;
            for (size_t o = 0; o < offsets.size(); ++o)
            {
                ecs::entity* ref = reinterpret_cast<ecs::entity*>(base + offsets[o]);
                if (!ref->is_valid())
                {
                    continue;
                }
                // 检查引用的实体是否存活 (index 在范围内 + version 匹配)
                if (!mgr_.is_entity_valid(*ref))
                {
                    *ref = ecs::entity{};
                    ++nullified;
                }
            }
        }
        return nullified;
    }

    [[nodiscard]] static bool is_binary_format(std::string_view data) noexcept {
        return data.size() >= 4 && std::memcmp(data.data(), "LCE1", 4) == 0;
    }

    // #D3 best_effort 策略快速检查
    [[nodiscard]] bool should_skip_errors() const noexcept {
        return load_policy_ == load_policy::best_effort;
    }

    // === #A1 LCCS 校验和前缀 (8 字节: magic "LCCS" + uint32 checksum) ===
    // 布局/管线见 usage 12.14.3; 无前缀时 strip 为 no-op (兼容旧存档)
    void apply_checksum_wrapper(std::string& out) noexcept
    {
        uint32_t cs = detail::compute_crc32c(out);
        std::string wrapped;
        wrapped.reserve(out.size() + 8);
        wrapped.append("LCCS", 4);
        wrapped.append(reinterpret_cast<const char*>(&cs), 4);
        wrapped.append(out);
        out = std::move(wrapped);
    }

    // 探测式剥离: 前 4 字节非 LCCS 即原样放行, 校验失败返回错误
    [[nodiscard]] operating_message strip_and_validate_checksum(std::string& content) noexcept
    {
        if (content.size() < 8)
        {
            return operating_message{};
        }
        if (std::memcmp(content.data(), "LCCS", 4) != 0)
        {
            return operating_message{};
        }
        uint32_t stored_cs;
        std::memcpy(&stored_cs, content.data() + 4, 4);
        size_t payload_len = content.size() - 8;
        uint32_t actual_cs = detail::compute_crc32c(content.data() + 8, payload_len);
        if (stored_cs != actual_cs)
        {
            operating_message res;
            res.write_message(false, "CRC32C 校验失败: 存档可能已损坏");
            return res;
        }
        content.erase(0, 8);
        return operating_message{};
    }

    // 校验下一个字段编号是否匹配预期 (protobuf/flatbuffer 用 "f<N>" 模拟字段名)
    // 用于 load_via_codec 检测 save 端字段顺序错位, 避免静默错位加载
    [[nodiscard]] static bool expect_field_number(archive_reader& r,
                                                    uint32_t expected) noexcept {
        std::string_view k = r.next_key();
        if (k.empty())
        {
            return false;
        }
        char buf[16];
        int n = std::snprintf(buf, sizeof(buf), "f%u", expected);
        return k == std::string_view(buf, static_cast<size_t>(n));
    }

    // 自动检测格式 (四种 magic)
    [[nodiscard]] static const archive_codec* detect_format(std::string_view data) noexcept {
        return detect_codec(data);
    }

    void clear_all_entities() noexcept {
        mgr_.clear();
    }

    template<typename T>
    void check_type_changed(size_t& idx, bool& any_changed) noexcept {
        const ecs::single_class_set* set = mgr_.get_single_class_set<T>();
        if (set)
        {
            uint64_t cur = set->get_pool_version();
            if (idx < last_type_versions_.size() && last_type_versions_[idx] != cur)
            {
                any_changed = true;
                last_type_versions_[idx] = cur;
            }
        }
        ++idx;
    }

    void report_progress(size_t cur, size_t total) noexcept {
        if (progress_cb_)
        {
            progress_cb_(cur, total);
        }
    }

    // ====================================================================
    // JSON 序列化
    // ====================================================================
    template<typename... Ts>
    operating_message save_to_json(std::string& out) noexcept {
        json_writer w(65536, false);
        w.begin_object();

        w.key("version").value(header_.archive_version);
        w.key("engine").value(header_.engine_version);

        if (metadata_.size() > 0)
        {
            w.key("meta").begin_object();
            for (size_t i = 0; i < metadata_.size(); ++i)
            {
                w.key(metadata_[i].key).value(metadata_[i].value);
            }
            w.end_object();
        }

        // 保存组件版本 (用于加载时迁移)
        bool has_cv = false;
        ((has_cv = has_cv || (lookup_component_version<Ts>() > 0)), ...);
        if (has_cv)
        {
            w.key("cv").begin_object();
            (save_component_version<Ts>(w), ...);
            w.end_object();
        }

        w.key("entities").begin_array();
        save_entities<Ts...>(w);
        w.end_array();

        w.key("components").begin_object();
        (save_one_type<Ts>(w), ...);
        w.end_object();

        w.end_object();
        out = w.take();
        return operating_message{};
    }

    template<typename T>
    void save_component_version(json_writer& w) noexcept {
        uint32_t cv = lookup_component_version<T>();
        if (cv > 0)
        {
            w.key(std::string(type_name<T>())).value(cv);
        }
    }

    template<typename... Ts>
    void save_entities(json_writer& w) noexcept {
        uint32_t max_idx = 0;
        bool any = false;
        ((collect_max_entity_idx<Ts>(max_idx, any)), ...);
        if (!any)
        {
            return;
        }

        dense<uint64_t> seen;
        size_t blocks = static_cast<size_t>(max_idx) / 64 + 1;
        for (size_t i = 0; i < blocks; ++i)
        {
            seen.push_back(0);
        }
        (save_unique_entities<Ts>(w, seen), ...);
    }

    template<typename T>
    void collect_max_entity_idx(uint32_t& max_idx, bool& any) noexcept {
        const ecs::single_class_set* set = mgr_.get_single_class_set<T>();
        if (!set || set->size() == 0)
        {
            return;
        }
        any = true;
        const auto& indices = set->get_entity_indices();
        size_t count = indices.size();
        for (size_t i = 0; i < count; ++i)
        {
            if (indices[i] > max_idx)
            {
                max_idx = indices[i];
            }
        }
    }

    template<typename T>
    void save_unique_entities(json_writer& w, dense<uint64_t>& seen) noexcept {
        const ecs::single_class_set* set = mgr_.get_single_class_set<T>();
        if (!set)
        {
            return;
        }
        const auto& indices  = set->get_entity_indices();
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
            w.key("i").value(idx);
            w.key("v").value(ver);
            w.key("f").value(state.flags);
            w.key("t").value(state.tag);
            w.key("l").value(state.layer);
            w.key("g").value(state.group_id);
            w.end_object();
        }
    }

    template<typename T>
    void save_one_type(json_writer& w) noexcept {
        std::string name = std::string(type_name<T>());
        w.key(name).begin_array();
        const ecs::single_class_set* set = mgr_.get_single_class_set<T>();
        if (!set)
        {
            w.end_array();
            return;
        }
        const auto& indices  = set->get_entity_indices();
        const auto& versions = set->get_entity_versions();
        const auto* pool     = set->get_typed_pool_ptr<T>();
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
            w.key("i").value(static_cast<uint32_t>(idx));
            w.key("v").value(static_cast<uint32_t>(versions[i]));
            if (comp)
            {
                w.key("d");
                serialize_value<T>(w, *comp);
                ++comp_count;
            }
            else
            {
                w.key("d").null();
            }
            w.end_object();
        }
        w.end_array();

        // 统计
        stats_.per_type.push_back({name, comp_count, 0});
    }

    template<typename T>
    void serialize_value(json_writer& w, const T& comp) noexcept {
        if constexpr (reflect_bridge::has_json_serialize<T>)
        {
            w.raw_value(comp.to_json());
        }
        else if constexpr (std::is_trivially_copyable_v<T>)
        {
            if (reflect_bridge::is_reflected<T>())
            {
                reflect_bridge::to_json(w, comp);
            }
            else
            {
                w.value(::detail::base64_encode(&comp, sizeof(T)));
            }
        }
        else
        {
            if (reflect_bridge::is_reflected<T>())
            {
                reflect_bridge::to_json(w, comp);
            }
            else
            {
                w.null();
            }
        }
    }

    // ====================================================================
    // JSON 加载
    // ====================================================================
    template<typename... Ts>
    operating_message load_from_json(std::string_view json) noexcept {
        json_reader r(json);
        if (!r.enter_object())
        {
            return r.last_error();
        }

        detail::entity_remap remap;
        // 加载的组件版本映射 (类型名 → 版本)
        dense<detail::metadata_entry> saved_cv;

        std::string_view key;
        while (!(key = r.next_key()).empty())
        {
            if (key == "version")
            {
                uint32_t v = r.read_uint32();
                if (v > header_.archive_version)
                {
                    operating_message res;
                    res.write_message(false, "存档版本 ", v, " 高于当前支持版本 ",
                                    std::to_string(header_.archive_version));
                    return res;
                }
            }
            else if (key == "engine")
            {
                [[maybe_unused]] uint32_t v = r.read_uint32();
            }
            else if (key == "meta")
            {
                if (r.enter_object())
                {
                    std::string_view mk;
                    while (!(mk = r.next_key()).empty())
                    {
                        std::string val = r.read_string();
                        metadata_.push_back({std::string(mk), std::move(val)});
                    }
                }
            }
            else if (key == "cv")
            {
                // 读取组件版本
                if (r.enter_object())
                {
                    std::string_view cvk;
                    while (!(cvk = r.next_key()).empty())
                    {
                        uint32_t v = r.read_uint32();
                        saved_cv.push_back({std::string(cvk), std::to_string(v)});
                    }
                }
            }
            else if (key == "entities")
            {
                if (!scan_entities(r, remap))
                {
                    if (r.has_error())
                    {
                        return r.last_error();
                    }
                    operating_message res;
                    res.write_message(false, "实体扫描失败 (可能超过上限: ",
                                    std::to_string(limits_.max_entity_count), ")");
                    return res;
                }
            }
            else if (key == "components")
            {
                if (!r.enter_object())
                {
                    return r.last_error();
                }
                while (!(key = r.next_key()).empty())
                {
                    bool matched = false;
                    ((load_match_type<Ts>(r, key, matched, remap, saved_cv)), ...);
                    if (!matched)
                    {
                        if (!r.skip_value())
                        {
                            return r.last_error();
                        }
                    }
                }
            }
            else
            {
                if (!r.skip_value())
                {
                    return r.last_error();
                }
            }
        }
        if (r.has_error())
        {
            return r.last_error();
        }

        (remap_entity_fields<Ts>(remap), ...);

        stats_.entity_count = remap.old_to_new.size();
        return operating_message{};
    }

    // 查找已保存的组件版本
    [[nodiscard]] uint32_t find_saved_cv(const dense<detail::metadata_entry>& saved_cv,
                                          std::string_view name) const noexcept {
        for (size_t i = 0; i < saved_cv.size(); ++i)
        {
            if (saved_cv[i].key == name)
            {
                return static_cast<uint32_t>(std::stoul(saved_cv[i].value));
            }
        }
        return 1; // 默认版本 1
    }

    bool scan_entities(json_reader& r, detail::entity_remap& remap) noexcept {
        if (!r.enter_array())
        {
            return false;
        }
        size_t count = 0;
        while (r.next_element())
        {
            if (++count > limits_.max_entity_count)
            {
                return false;
            }
            if (!r.enter_object())
            {
                return false;
            }
            uint32_t idx = 0, ver = 0, flags = 0, tag = 0, layer = 0, group = 0;
            std::string_view key;
            while (!(key = r.next_key()).empty())
            {
                if (key == "i")
                {
                    idx = r.read_uint32();
                }
                else if (key == "v")
                {
                    ver = r.read_uint32();
                }
                else if (key == "f")
                {
                    flags = r.read_uint32();
                }
                else if (key == "t")
                {
                    tag = r.read_uint32();
                }
                else if (key == "l")
                {
                    layer = r.read_uint32();
                }
                else if (key == "g")
                {
                    group = r.read_uint32();
                }
                else
                {
                    r.skip_value();
                }
            }
            ecs::entity new_e = mgr_.create_entity();
            while (remap.old_to_new.size() <= idx)
            {
                remap.old_to_new.push_back(ecs::entity{});
                remap.old_versions.push_back(0);
            }
            remap.old_to_new[idx] = new_e;
            remap.old_versions[idx] = ver;

            auto& state = mgr_.get_entity_state(new_e.parts_.index_);
            state.flags = flags;
            state.tag = tag;
            state.layer = layer;
            state.group_id = group;
            r.end_element();
        }
        return true;
    }

    template<typename T>
    void load_match_type(json_reader& r, std::string_view saved_name,
                         bool& matched, const detail::entity_remap& remap,
                         const dense<detail::metadata_entry>& saved_cv) noexcept {
        if (matched)
        {
            return;
        }
        std::string_view name = type_name<T>();
        // #C3 支持类型别名匹配 (旧存档类型名 → 新类型)
        if (saved_name == name || detail::is_alias_of(type_id::get_type_id<T>(), saved_name))
        {
            matched = true;
            load_one_type_json<T>(r, remap, saved_cv);
        }
    }

    template<typename T>
    void load_one_type_json(json_reader& r, const detail::entity_remap& remap,
                             const dense<detail::metadata_entry>& saved_cv) noexcept {
        if (!r.enter_array())
        {
            return;
        }
        size_t comp_count = 0;
        // 查找存档中的组件版本和当前注册版本
        uint32_t saved_ver = find_saved_cv(saved_cv, std::string(type_name<T>()));
        uint32_t current_ver = lookup_component_version<T>();
        if (current_ver == 0)
        {
            current_ver = saved_ver; // 未注册则不迁移
        }
        int tid = type_id::get_type_id<T>();

        while (r.next_element())
        {
            if (!r.enter_object())
            {
                // #D3 best_effort: 跳过损坏元素
                if (should_skip_errors())
                {
                    ++stats_.skipped_count;
                    r.clear_error();
                    continue;
                }
                return;
            }
            uint32_t idx = 0;
            [[maybe_unused]] uint32_t ver = 0;
            bool has_data = false;
            std::string data_str;
            std::string_view key;
            while (!(key = r.next_key()).empty())
            {
                if (key == "i")
                {
                    idx = r.read_uint32();
                }
                else if (key == "v")
                {
                    ver = r.read_uint32();
                }
                else if (key == "d")
                {
                    has_data = true;
                    data_str = read_component_data<T>(r);
                }
                else
                {
                    r.skip_value();
                }
            }

            // #D3 best_effort: 读取错误时跳过
            if (r.has_error())
            {
                if (should_skip_errors())
                {
                    ++stats_.skipped_count;
                    r.clear_error();
                    r.end_element();
                    continue;
                }
                return;
            }

            if (idx >= remap.old_to_new.size())
            {
                r.end_element();
                continue;
            }
            ecs::entity e = remap.old_to_new[idx];
            if (!e.is_valid())
            {
                r.end_element();
                continue;
            }

            if (has_data && !data_str.empty() && data_str != "null")
            {
                // 应用迁移链
                if (saved_ver < current_ver)
                {
                    std::string migrated = migrate_component_string(
                        tid, saved_ver, current_ver, data_str);
                    data_str = std::move(migrated);
                }
                // #C1/#C2 应用字段 schema (rename/drop/default 注入)
                data_str = apply_field_schema(tid, data_str);
                construct_component<T>(e, data_str);
                ++comp_count;
            }
            r.end_element();
        }

        // 更新统计
        for (size_t i = 0; i < stats_.per_type.size(); ++i)
        {
            if (stats_.per_type[i].type_name == std::string(type_name<T>()))
            {
                stats_.per_type[i].component_count = comp_count;
                break;
            }
        }
    }

    template<typename T>
    std::string read_component_data(json_reader& r) noexcept {
        if constexpr (reflect_bridge::has_json_deserialize<T>)
        {
            return std::string(r.read_raw_value());
        }
        else if constexpr (std::is_trivially_copyable_v<T>)
        {
            if (reflect_bridge::is_reflected<T>())
            {
                return std::string(r.read_raw_value());
            }
            return r.read_string();
        }
        else
        {
            if (reflect_bridge::is_reflected<T>())
            {
                return std::string(r.read_raw_value());
            }
            r.skip_value();
            return {};
        }
    }

    template<typename T>
    void construct_component(ecs::entity e, const std::string& data_str) noexcept {
        if constexpr (reflect_bridge::has_json_deserialize<T>)
        {
            T comp{};
            comp.from_json(data_str);
            mgr_.add<T>(e, std::move(comp));
        }
        else if constexpr (std::is_trivially_copyable_v<T>)
        {
            if (reflect_bridge::is_reflected<T>())
            {
                T comp{};
                json_reader sub(data_str);
                reflect_bridge::from_json(sub, comp);
                mgr_.add<T>(e, std::move(comp));
            }
            else
            {
                T comp{};
                std::string decoded = ::detail::base64_decode(data_str);
                if (decoded.size() == sizeof(T))
                {
                    std::memcpy(&comp, decoded.data(), sizeof(T));
                }
                mgr_.add<T>(e, std::move(comp));
            }
        }
        else
        {
            if (reflect_bridge::is_reflected<T>())
            {
                T comp{};
                json_reader sub(data_str);
                reflect_bridge::from_json(sub, comp);
                mgr_.add<T>(e, std::move(comp));
            }
        }
    }

    // ====================================================================
    // 实体引用字段重映射
    // ====================================================================
    template<typename T>
    void remap_entity_fields(const detail::entity_remap& remap) noexcept {
        ecs::single_class_set* set = mgr_.get_single_class_set<T>();
        if (!set)
        {
            return;
        }
        int tid = type_id::get_type_id<T>();
        auto& reg = detail::entity_field_registry();
        dense<uint32_t> offsets;
        for (size_t i = 0; i < reg.size(); ++i)
        {
            if (reg[i].type_id == tid)
            {
                offsets.push_back(reg[i].offset);
            }
        }
        if (offsets.size() == 0)
        {
            return;
        }

        auto* pool = set->get_typed_pool_ptr<T>();
        if (!pool)
        {
            return;
        }
        size_t count = set->size();
        const auto& indices = set->get_entity_indices();
        for (size_t i = 0; i < count; ++i)
        {
            T& comp = (*pool)[i];
            uint32_t new_idx = indices[i];
            if (new_idx >= remap.old_to_new.size())
            {
                continue;
            }
            char* base = static_cast<char*>(static_cast<void*>(&comp));
            for (size_t k = 0; k < offsets.size(); ++k)
            {
                ecs::entity* ref = reinterpret_cast<ecs::entity*>(base + offsets[k]);
                if (!ref->is_valid())
                {
                    continue;
                }
                uint32_t old_idx = ref->parts_.index_;
                if (old_idx < remap.old_to_new.size())
                {
                    *ref = remap.old_to_new[old_idx];
                }
            }
        }
    }

    // ====================================================================
    // 二进制保存 (类型头含总字节数, 修复 skip bug)
    // ====================================================================
    template<typename... Ts>
    operating_message save_to_binary(std::string& out) noexcept {
        binary_writer bw;
        bw.value(header_.archive_version);
        bw.value(header_.engine_version);

        // 元数据
        std::string meta_buf;
        if (metadata_.size() > 0)
        {
            json_writer mw;
            mw.begin_object();
            for (size_t i = 0; i < metadata_.size(); ++i)
            {
                mw.key(metadata_[i].key).value(metadata_[i].value);
            }
            mw.end_object();
            meta_buf = mw.take();
        }
        bw.value(meta_buf);

        // 实体状态
        std::string entities_buf;
        {
            json_writer ew;
            ew.begin_array();
            save_entities<Ts...>(ew);
            ew.end_array();
            entities_buf = ew.take();
        }
        bw.value(entities_buf);

        const uint32_t type_count = static_cast<uint32_t>(sizeof...(Ts));
        bw.value(type_count);
        (save_one_type_binary<Ts>(bw), ...);

        out = bw.take();
        return operating_message{};
    }

    template<typename T>
    void save_one_type_binary(binary_writer& bw) noexcept {
        std::string_view name = type_name<T>();
        bw.value(name);

        // 组件版本
        uint32_t cv = lookup_component_version<T>();
        bw.value(cv);

        // 先写入临时缓冲区, 再写入总字节数
        binary_writer type_data;
        const ecs::single_class_set* set = mgr_.get_single_class_set<T>();
        size_t total = set ? set->size() : 0;

        // 先计算过滤后的实际数量
        size_t actual = 0;
        if (set)
        {
            const auto& indices = set->get_entity_indices();
            for (size_t i = 0; i < total; ++i)
            {
                uint32_t idx = indices[i];
                if (filter_ && !filter_->matches_entity(idx, mgr_.get_entity_state(idx)))
                {
                    continue;
                }
                ++actual;
            }
        }

        type_data.begin_array(actual);

        size_t comp_count = 0;
        if (set)
        {
            const auto& indices  = set->get_entity_indices();
            const auto& versions = set->get_entity_versions();
            const auto* pool     = set->get_typed_pool_ptr<T>();
            for (size_t i = 0; i < total; ++i)
            {
                uint32_t idx = indices[i];
                if (filter_ && !filter_->matches_entity(idx, mgr_.get_entity_state(idx)))
                {
                    continue;
                }
                type_data.value(idx);
                type_data.value(versions[i]);
                if (pool)
                {
                    const T& comp = (*pool)[i];
                    if constexpr (std::is_trivially_copyable_v<T>)
                    {
                        type_data.value_trivial(comp);
                    }
                    else if constexpr (reflect_bridge::has_json_serialize<T>)
                    {
                        std::string j = comp.to_json();
                        type_data.value(j);
                    }
                    else
                    {
                        json_writer w;
                        reflect_bridge::to_json(w, comp);
                        type_data.value(w.take());
                    }
                    ++comp_count;
                }
            }
        }

        std::string type_buf = type_data.take();
        // 跳过 8 字节 header (magic + endianness + version + reserved)
        const char* data_ptr = type_buf.data() + 8;
        size_t data_len = type_buf.size() - 8;
        bw.value(static_cast<uint32_t>(data_len));
        bw.write_raw_bytes(data_ptr, data_len);

        stats_.per_type.push_back({std::string(name), comp_count, data_len});
    }

    // ====================================================================
    // 通用编码器保存 (protobuf/flatbuffer, 通过 archive_writer 接口)
    // 固定字段顺序: f1 version, f2 engine, f3 meta, f4 cv, f5 entities, f6 components
    // ====================================================================
    template<typename... Ts>
    operating_message save_via_codec(std::string& out, const archive_codec& codec) noexcept {
        archive_writer* w = codec.create_writer();
        if (!w)
        {
            operating_message res;
            res.write_message(false, "编码器创建失败");
            return res;
        }

        archive_logic logic(mgr_);
        logic.set_filter(filter_);

        w->begin_object();

        // f1: version
        w->key("version");
        w->write_u32(header_.archive_version);
        // f2: engine
        w->key("engine");
        w->write_u32(header_.engine_version);
        // f3: meta (JSON 字符串, 保留键名)
        {
            json_writer mw;
            mw.begin_object();
            for (size_t i = 0; i < metadata_.size(); ++i)
            {
                mw.key(metadata_[i].key).value(metadata_[i].value);
            }
            mw.end_object();
            w->key("meta");
            w->write_string(mw.take());
        }
        // f4: cv (JSON 字符串, 保留类型名)
        {
            json_writer cw;
            cw.begin_object();
            (save_one_cv_via_codec<Ts>(cw), ...);
            cw.end_object();
            w->key("cv");
            w->write_string(cw.take());
        }
        // f5: entities + f6: components (复用 archive_logic)
        logic.save_entities<Ts...>(*w);
        logic.save_components<Ts...>(*w);

        w->end_object();
        out = w->take();
        codec.destroy_writer(w);

        stats_ = logic.stats();
        stats_.total_bytes = out.size();
        stats_.archive_version = header_.archive_version;
        return operating_message{};
    }

    template<typename T>
    void save_one_cv_via_codec(json_writer& w) noexcept {
        uint32_t cv = lookup_component_version<T>();
        w.key(std::string(get_type_name<T>()));
        w.value(cv);
    }

    // ====================================================================
    // 二进制加载
    // ====================================================================
    template<typename... Ts>
    operating_message load_from_binary(std::string_view data) noexcept {
        static_assert((json_serializable<Ts> && ...),
            "所有组件类型必须满足 json_serializable");

        binary_reader r(data);
        if (r.has_error())
        {
            operating_message res;
            res.write_message(false, "二进制格式校验失败");
            return res;
        }

        uint32_t archive_ver = r.read_u32();
        if (archive_ver > header_.archive_version)
        {
            operating_message res;
            res.write_message(false, "存档版本 ", archive_ver, " 高于当前支持版本 ",
                            std::to_string(header_.archive_version));
            return res;
        }
        [[maybe_unused]] uint32_t engine_ver = r.read_u32();

        // 元数据
        std::string meta_str = r.read_string();
        if (!meta_str.empty())
        {
            json_reader mr(meta_str);
            if (mr.enter_object())
            {
                std::string_view mk;
                while (!(mk = mr.next_key()).empty())
                {
                    std::string val = mr.read_string();
                    metadata_.push_back({std::string(mk), std::move(val)});
                }
            }
        }

        // 实体状态
        std::string entities_json = r.read_string();
        detail::entity_remap remap;
        if (!entities_json.empty())
        {
            json_reader er(entities_json);
            if (!scan_entities(er, remap))
            {
                operating_message res;
                res.write_message(false, "二进制存档实体扫描失败");
                return res;
            }
        }

        uint32_t type_count = r.read_u32();
        for (uint32_t t = 0; t < type_count; ++t)
        {
            std::string saved_name = r.read_string();
            uint32_t saved_cv = r.read_u32(); // 组件版本
            uint32_t type_data_len = r.read_u32();

            bool matched = false;
            ((load_match_type_binary<Ts>(r, saved_name, saved_cv, matched, remap)), ...);
            if (!matched)
            {
                r.skip(type_data_len);
            }
            if (r.has_error())
            {
                operating_message res;
                res.write_message(false, "二进制加载读取错误");
                return res;
            }
        }

        (remap_entity_fields<Ts>(remap), ...);

        stats_.entity_count = remap.old_to_new.size();
        return operating_message{};
    }

    template<typename T>
    void load_match_type_binary(binary_reader& r, const std::string& saved_name,
                                uint32_t saved_cv,
                                bool& matched, const detail::entity_remap& remap) noexcept {
        if (matched)
        {
            return;
        }
        std::string_view name = type_name<T>();
        // #C3 支持类型别名匹配
        if (saved_name == name || detail::is_alias_of(type_id::get_type_id<T>(), saved_name))
        {
            matched = true;
            load_one_type_binary<T>(r, remap, saved_cv);
        }
    }

    template<typename T>
    void load_one_type_binary(binary_reader& r, const detail::entity_remap& remap,
                                uint32_t saved_cv) noexcept {
        uint32_t current_cv = lookup_component_version<T>();
        if (current_cv == 0)
        {
            current_cv = saved_cv; // 未注册则不迁移
        }
        int tid = type_id::get_type_id<T>();

        uint32_t count = r.read_u32();
        for (uint32_t i = 0; i < count; ++i)
        {
            uint32_t idx = r.read_u32();
            [[maybe_unused]] uint32_t ver = r.read_u32();

            if (idx >= remap.old_to_new.size())
            {
                skip_binary_element<T>(r);
                continue;
            }
            ecs::entity e = remap.old_to_new[idx];
            if (!e.is_valid())
            {
                skip_binary_element<T>(r);
                continue;
            }

            if constexpr (std::is_trivially_copyable_v<T>)
            {
                T comp{};
                r.read_trivial(comp);
                if (!r.has_error())
                {
                    mgr_.add<T>(e, std::move(comp));
                }
            }
            else if constexpr (reflect_bridge::has_json_deserialize<T>)
            {
                std::string json_str = r.read_string();
                if (!r.has_error() && !json_str.empty())
                {
                    // 应用迁移
                    if (saved_cv < current_cv)
                    {
                        json_str = migrate_component_string(tid, saved_cv, current_cv, json_str);
                    }
                    // #C1/#C2 应用字段 schema
                    json_str = apply_field_schema(tid, json_str);
                    T comp{};
                    comp.from_json(json_str);
                    mgr_.add<T>(e, std::move(comp));
                }
            }
            else if constexpr (reflect_bridge::is_reflected<T>())
            {
                std::string json_str = r.read_string();
                if (!r.has_error() && !json_str.empty())
                {
                    if (saved_cv < current_cv)
                    {
                        json_str = migrate_component_string(tid, saved_cv, current_cv, json_str);
                    }
                    // #C1/#C2 应用字段 schema
                    json_str = apply_field_schema(tid, json_str);
                    T comp{};
                    json_reader sub(json_str);
                    reflect_bridge::from_json(sub, comp);
                    mgr_.add<T>(e, std::move(comp));
                }
            }
            else
            {
                [[maybe_unused]] std::string json_str = r.read_string();
            }
        }
    }

    template<typename T>
    void skip_binary_element(binary_reader& r) noexcept {
        if constexpr (std::is_trivially_copyable_v<T>)
        {
            r.skip(sizeof(T));
        }
        else
        {
            [[maybe_unused]] auto s = r.read_string();
        }
    }

    // ====================================================================
    // 通用编码器加载 (protobuf/flatbuffer, 通过 archive_reader 接口)
    // 按固定字段顺序读 (与 save_via_codec 配对, 不依赖字段名匹配)
    // ====================================================================
    template<typename... Ts>
    operating_message load_via_codec(std::string_view data, const archive_codec& codec) noexcept {
        archive_reader* r = codec.create_reader(data);
        if (!r || r->has_error())
        {
            if (r)
            {
                codec.destroy_reader(r);
            }
            operating_message res;
            res.write_message(false, "格式校验失败");
            return res;
        }

        if (!r->enter_object())
        {
            operating_message res = r->has_error() ? r->last_error() : operating_message{};
            if (!res)
            {
                res.write_message(false, "enter_object 失败");
            }
            codec.destroy_reader(r);
            return res;
        }

        detail::entity_remap remap;
        dense<detail::metadata_entry> saved_cv;

        // 按固定顺序读 (与 save_via_codec 的写入顺序配对)
        // 每个字段校验编号, 检测 save 端字段顺序错位
        // f1: version
        if (!expect_field_number(*r, 1))
        {
            codec.destroy_reader(r);
            operating_message res;
            res.write_message(false, "schema 校验失败: 期望字段 f1(version)");
            return res;
        }
        uint32_t archive_ver = r->read_u32();
        if (archive_ver > header_.archive_version)
        {
            codec.destroy_reader(r);
            operating_message res;
            res.write_message(false, "存档版本 ", archive_ver, " 高于当前支持版本 ",
                            std::to_string(header_.archive_version));
            return res;
        }
        // f2: engine
        if (!expect_field_number(*r, 2))
        {
            codec.destroy_reader(r);
            operating_message res;
            res.write_message(false, "schema 校验失败: 期望字段 f2(engine)");
            return res;
        }
        (void)r->read_u32();
        // f3: meta (JSON 字符串)
        if (!expect_field_number(*r, 3))
        {
            codec.destroy_reader(r);
            operating_message res;
            res.write_message(false, "schema 校验失败: 期望字段 f3(meta)");
            return res;
        }
        {
            std::string_view meta_json = r->read_string_view();
            if (!meta_json.empty())
            {
                json_reader mr(meta_json);
                if (mr.enter_object())
                {
                    std::string_view mk;
                    while (!(mk = mr.next_key()).empty())
                    {
                        std::string val = mr.read_string();
                        metadata_.push_back({std::string(mk), std::move(val)});
                    }
                }
            }
        }
        // f4: cv (JSON 字符串)
        if (!expect_field_number(*r, 4))
        {
            codec.destroy_reader(r);
            operating_message res;
            res.write_message(false, "schema 校验失败: 期望字段 f4(cv)");
            return res;
        }
        {
            std::string_view cv_json = r->read_string_view();
            if (!cv_json.empty())
            {
                json_reader cr(cv_json);
                if (cr.enter_object())
                {
                    std::string_view ck;
                    while (!(ck = cr.next_key()).empty())
                    {
                        uint32_t v = cr.read_uint32();
                        saved_cv.push_back({std::string(ck), std::to_string(v)});
                    }
                }
            }
        }
        // f5: entities
        if (!expect_field_number(*r, 5))
        {
            codec.destroy_reader(r);
            operating_message res;
            res.write_message(false, "schema 校验失败: 期望字段 f5(entities)");
            return res;
        }
        if (!scan_entities_via_codec(*r, remap))
        {
            codec.destroy_reader(r);
            operating_message res;
            res.write_message(false, "实体扫描失败 (可能超过上限: ",
                            std::to_string(limits_.max_entity_count), ")");
            return res;
        }
        // f6: components
        if (!expect_field_number(*r, 6))
        {
            codec.destroy_reader(r);
            operating_message res;
            res.write_message(false, "schema 校验失败: 期望字段 f6(components)");
            return res;
        }
        if (r->enter_object())
        {
            // 按 Ts 顺序读取 (protobuf 字段编号 f1, f2, ... 对应 Ts... 顺序)
            (load_one_type_via_codec<Ts>(*r, remap, saved_cv), ...);
            r->leave_object();
        }

        codec.destroy_reader(r);

        // 实体引用重映射
        (remap_entity_fields<Ts>(remap), ...);

        stats_.entity_count = remap.old_to_new.size();
        return operating_message{};
    }

    // 按固定顺序读实体字段: i(f1), v(f2), f(f3), t(f4), l(f5), g(f6)
    bool scan_entities_via_codec(archive_reader& r, detail::entity_remap& remap) noexcept {
        if (!r.enter_array())
        {
            return false;
        }
        size_t count = 0;
        while (r.next_element())
        {
            if (++count > limits_.max_entity_count)
            {
                return false;
            }
            if (!r.enter_object())
            {
                return false;
            }

            // 按固定顺序读: i(f1), v(f2), f(f3), t(f4), l(f5), g(f6)
            uint32_t idx = 0, ver = 0, flags = 0, tag = 0, layer = 0, group = 0;
            r.next_key(); idx = r.read_u32();
            r.next_key(); ver = r.read_u32();
            r.next_key(); flags = r.read_u32();
            r.next_key(); tag = r.read_u32();
            r.next_key(); layer = r.read_u32();
            r.next_key(); group = r.read_u32();
            r.leave_object();
            r.end_element();

            ecs::entity new_e = mgr_.create_entity();
            while (remap.old_to_new.size() <= idx)
            {
                remap.old_to_new.push_back(ecs::entity{});
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
        r.leave_array();
        return true;
    }

    // 按 Ts 顺序读取组件数组 (与 save_one_type 的写入顺序配对)
    template<typename T>
    void load_one_type_via_codec(archive_reader& r, const detail::entity_remap& remap,
                                  const dense<detail::metadata_entry>& saved_cv) noexcept {
        // 读取类型字段 key (protobuf 返回 "f1"/"f2"/..., JSON 返回类型名)
        r.next_key();
        if (!r.enter_array())
        {
            return;
        }

        uint32_t saved_ver = find_saved_cv(saved_cv, std::string(get_type_name<T>()));
        uint32_t current_ver = lookup_component_version<T>();
        if (current_ver == 0)
        {
            current_ver = saved_ver;
        }
        int tid = type_id::get_type_id<T>();
        size_t comp_count = 0;

        while (r.next_element())
        {
            if (!r.enter_object())
            {
                break;
            }

            // 按固定顺序读: i(f1), v(f2), d(f3)
            r.next_key();
            uint32_t idx = r.read_u32();
            r.next_key();
            (void)r.read_u32();  // version
            r.next_key();  // d

            if (idx >= remap.old_to_new.size())
            {
                r.leave_object();
                r.end_element();
                continue;
            }
            ecs::entity e = remap.old_to_new[idx];
            if (!e.is_valid())
            {
                r.leave_object();
                r.end_element();
                continue;
            }

            // 读取组件数据 (与 archive_logic.serialize_value 的写入方式配对)
            if constexpr (reflect_bridge::has_json_serialize<T>)
            {
                // to_json: write_raw 写入的 JSON 片段
                std::string_view raw = r.read_bytes_view(0);
                std::string data_str(raw);
                if (!data_str.empty() && data_str != "null")
                {
                    if (saved_ver < current_ver)
                    {
                        data_str = migrate_component_string(tid, saved_ver, current_ver, data_str);
                    }
                    construct_component<T>(e, data_str);
                    ++comp_count;
                }
            }
            else if constexpr (std::is_trivially_copyable_v<T>)
            {
                if (reflect_bridge::is_reflected<T>())
                {
                    // 反射: write_raw 写入的 JSON 片段
                    std::string_view raw = r.read_bytes_view(0);
                    std::string data_str(raw);
                    if (!data_str.empty())
                    {
                        if (saved_ver < current_ver)
                        {
                            data_str = migrate_component_string(tid, saved_ver, current_ver, data_str);
                        }
                        construct_component<T>(e, data_str);
                        ++comp_count;
                    }
                }
                else
                {
                    // trivially copyable: write_bytes 写入的原始字节
                    std::string_view bytes = r.read_bytes_view(sizeof(T));
                    if (bytes.size() == sizeof(T))
                    {
                        T comp{};
                        std::memcpy(&comp, bytes.data(), sizeof(T));
                        mgr_.add<T>(e, std::move(comp));
                        ++comp_count;
                    }
                }
            }
            else
            {
                if (reflect_bridge::is_reflected<T>())
                {
                    std::string_view raw = r.read_bytes_view(0);
                    std::string data_str(raw);
                    if (!data_str.empty())
                    {
                        if (saved_ver < current_ver)
                        {
                            data_str = migrate_component_string(tid, saved_ver, current_ver, data_str);
                        }
                        construct_component<T>(e, data_str);
                        ++comp_count;
                    }
                }
                else
                {
                    r.skip_value();
                }
            }
            r.leave_object();
            r.end_element();
        }
        r.leave_array();

        // 更新统计
        stats_.per_type.push_back({std::string(get_type_name<T>()), comp_count, 0});
    }

    // ====================================================================
    // 类型名查询
    // ====================================================================
    template<typename T>
    [[nodiscard]] static std::string_view type_name() noexcept {
        const char* stable = lookup_type_name(type_id::get_type_id<T>());
        if (stable)
        {
            return stable;
        }
        static std::string name = typeid(T).name();
        return name;
    }

    // ====================================================================
    // #E 运行时路径内部实现
    // trampoline 函数: 模板实例化, 将 save_one_type<T>/load_one_type_json<T>
    // 包装为类型擦除的函数指针, 存入 type_factory_entry
    // 编译器对每个 T 生成优化函数体, 运行时仅一次间接调用
    // ====================================================================

    // JSON save trampoline: 调用 save_one_type<T>(w)
    template<typename T>
    static void json_save_trampoline(serialization* self, void* w) noexcept {
        self->save_one_type<T>(*static_cast<json_writer*>(w));
    }

    // JSON load trampoline: 调用 load_one_type_json<T>(r, remap, saved_cv)
    // saved_cv 通过成员变量传递 (避免改变函数签名)
    template<typename T>
    static void json_load_trampoline(serialization* self, void* r,
                                      const detail::entity_remap* remap,
                                      uint32_t saved_cv) noexcept {
        // 构造临时 saved_cv 表 (单类型)
        dense<detail::metadata_entry> tmp_cv;
        tmp_cv.push_back({std::string(self->type_name<T>()), std::to_string(saved_cv)});
        self->load_one_type_json<T>(*static_cast<json_reader*>(r), *remap, tmp_cv);
    }

    // 二进制 save trampoline
    template<typename T>
    static void binary_save_trampoline(serialization* self, void* w) noexcept {
        self->save_one_type_binary<T>(*static_cast<binary_writer*>(w));
    }

    // 二进制 load trampoline
    template<typename T>
    static void binary_load_trampoline(serialization* self, void* r,
                                        const detail::entity_remap* remap,
                                        uint32_t saved_cv) noexcept {
        self->load_one_type_binary<T>(*static_cast<binary_reader*>(r), *remap, saved_cv);
    }

    // 注册类型工厂并关联 save_fn/load_fn (幂等)
    // 编译期 Ts... 路径和运行时路径共用此注册
    template<typename T>
    void register_factory_for_type() noexcept {
        // 先确保稳定类型名已注册
        int tid = type_id::get_type_id<T>();
        const char* name = lookup_type_name(tid);
        if (!name)
        {
            // 未注册稳定名, 用 typeid 兜底 (运行时 hash, 跨编译器不稳定)
            // 用户应先调 register_type_name<T>("StableName")
            static std::string fallback = typeid(T).name();
            name = fallback.c_str();
        }
        type_id::bind_def_name(name, tid);

        // 幂等注册 (id→索引映射维护于 ensure_factory_entry)
        const uint32_t idx = detail::ensure_factory_entry(
            tid, name, sizeof(T), std::is_trivially_copyable_v<T>);
        auto& entry = detail::type_factory_registry()[idx];
        entry.name = name;
        entry.save_fn = reinterpret_cast<void(*)(serialization*, void*)>(
            &json_save_trampoline<T>);
        entry.load_fn = reinterpret_cast<void(*)(serialization*, void*,
            const detail::entity_remap*, uint32_t)>(&json_load_trampoline<T>);
    }

    // ====================================================================
    // 运行时 JSON 保存 (遍历 registry, 跳过空 pool 的类型)
    // ====================================================================
    operating_message save_to_json_runtime(std::string& out) noexcept {
        json_writer w(65536, false);
        w.begin_object();

        w.key("version").value(header_.archive_version);
        w.key("engine").value(header_.engine_version);

        if (metadata_.size() > 0)
        {
            w.key("meta").begin_object();
            for (size_t i = 0; i < metadata_.size(); ++i)
            {
                w.key(metadata_[i].key).value(metadata_[i].value);
            }
            w.end_object();
        }

        // 遍历 registry 保存组件版本
        auto& reg = detail::type_factory_registry();
        bool has_cv = false;
        for (size_t i = 0; i < reg.size(); ++i)
        {
            if (lookup_component_version_by_tid(reg[i].type_id) > 0)
            {
                has_cv = true;
                break;
            }
        }
        if (has_cv)
        {
            w.key("cv").begin_object();
            for (size_t i = 0; i < reg.size(); ++i)
            {
                uint32_t cv = lookup_component_version_by_tid(reg[i].type_id);
                if (cv > 0)
                {
                    w.key(reg[i].name).value(cv);
                }
            }
            w.end_object();
        }

        // 保存实体 (遍历 registry 收集所有类型的实体)
        w.key("entities").begin_array();
        save_entities_runtime(w);
        w.end_array();

        // 保存组件 (遍历 registry, 调用 save_fn 函数指针)
        w.key("components").begin_object();
        for (size_t i = 0; i < reg.size(); ++i)
        {
            // 跳过空 pool 的类型 (通过 type_id 检查 pool 是否存在且非空)
            if (!has_registered_components(reg[i].type_id))
            {
                continue;
            }
            // 调用 save_fn trampoline (间接调用, ~1 cycle)
            reg[i].save_fn(this, &w);
        }
        w.end_object();

        w.end_object();
        out = w.take();
        return operating_message{};
    }

    // 运行时收集实体 (遍历所有已注册类型)
    void save_entities_runtime(json_writer& w) noexcept {
        auto& reg = detail::type_factory_registry();
        uint32_t max_idx = 0;
        bool any = false;
        for (size_t i = 0; i < reg.size(); ++i)
        {
            collect_max_entity_idx_by_tid(reg[i].type_id, max_idx, any);
        }
        if (!any)
        {
            return;
        }

        dense<uint64_t> seen;
        size_t blocks = static_cast<size_t>(max_idx) / 64 + 1;
        for (size_t i = 0; i < blocks; ++i)
        {
            seen.push_back(0);
        }
        for (size_t i = 0; i < reg.size(); ++i)
        {
            save_unique_entities_by_tid(reg[i].type_id, w, seen);
        }
    }

    // 按 type_id 收集最大实体索引
    void collect_max_entity_idx_by_tid(int tid, uint32_t& max_idx, bool& any) noexcept {
        const ecs::single_class_set* set = mgr_.get_single_class_set_by_id(tid);
        if (!set || set->size() == 0)
        {
            return;
        }
        any = true;
        const auto& indices = set->get_entity_indices();
        size_t count = indices.size();
        for (size_t i = 0; i < count; ++i)
        {
            if (indices[i] > max_idx)
            {
                max_idx = indices[i];
            }
        }
    }

    // 按 type_id 保存唯一实体
    void save_unique_entities_by_tid(int tid, json_writer& w, dense<uint64_t>& seen) noexcept {
        const ecs::single_class_set* set = mgr_.get_single_class_set_by_id(tid);
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
            w.key("i").value(idx);
            w.key("v").value(ver);
            w.key("f").value(state.flags);
            w.key("t").value(state.tag);
            w.key("l").value(state.layer);
            w.key("g").value(state.group_id);
            w.end_object();
        }
    }

    // 按 type_id 检查是否有已注册组件
    [[nodiscard]] bool has_registered_components(int tid) noexcept {
        const ecs::single_class_set* set = mgr_.get_single_class_set_by_id(tid);
        return set && set->size() > 0;
    }

    // 按 type_id 查找组件版本
    [[nodiscard]] uint32_t lookup_component_version_by_tid(int tid) const noexcept {
        // 复用 migration 模块的版本查询 (全局命名空间)
        return lookup_component_version(tid);
    }

    // ====================================================================
    // 运行时 JSON 加载 (按存档类型名查 registry, 调用 load_fn)
    // ====================================================================
    operating_message load_from_json_runtime(std::string_view json) noexcept {
        json_reader r(json);
        if (!r.enter_object())
        {
            return r.last_error();
        }

        detail::entity_remap remap;
        dense<detail::metadata_entry> saved_cv;

        std::string_view key;
        while (!(key = r.next_key()).empty())
        {
            if (key == "version")
            {
                uint32_t v = r.read_uint32();
                if (v > header_.archive_version)
                {
                    operating_message res;
                    res.write_message(false, "存档版本 ", v, " 高于当前支持版本 ",
                                    std::to_string(header_.archive_version));
                    return res;
                }
            }
            else if (key == "engine")
            {
                [[maybe_unused]] uint32_t v = r.read_uint32();
            }
            else if (key == "meta")
            {
                if (r.enter_object())
                {
                    std::string_view mk;
                    while (!(mk = r.next_key()).empty())
                    {
                        std::string val = r.read_string();
                        metadata_.push_back({std::string(mk), std::move(val)});
                    }
                }
            }
            else if (key == "cv")
            {
                if (r.enter_object())
                {
                    std::string_view cvk;
                    while (!(cvk = r.next_key()).empty())
                    {
                        uint32_t v = r.read_uint32();
                        saved_cv.push_back({std::string(cvk), std::to_string(v)});
                    }
                }
            }
            else if (key == "entities")
            {
                if (!scan_entities(r, remap))
                {
                    if (r.has_error())
                    {
                        return r.last_error();
                    }
                    operating_message res;
                    res.write_message(false, "实体扫描失败 (可能超过上限: ",
                                    std::to_string(limits_.max_entity_count), ")");
                    return res;
                }
            }
            else if (key == "components")
            {
                if (!r.enter_object())
                {
                    return r.last_error();
                }
                while (!(key = r.next_key()).empty())
                {
                    // 运行时按类型名查 registry (O(1) hash 查找)
                    const detail::type_factory_entry* fe =
                        detail::find_factory_by_name(key);
                    if (fe && fe->load_fn)
                    {
                        // 查找存档中该类型的版本
                        uint32_t saved_ver = find_saved_cv(saved_cv,
                            std::string(key));
                        // 调用 load_fn trampoline (间接调用)
                        fe->load_fn(this, &r, &remap, saved_ver);
                    }
                    else
                    {
                        // 未知类型, 跳过
                        if (!r.skip_value())
                        {
                            return r.last_error();
                        }
                    }
                }
            }
            else
            {
                if (!r.skip_value())
                {
                    return r.last_error();
                }
            }
        }
        if (r.has_error())
        {
            return r.last_error();
        }

        // 遍历 registry 重映射实体引用字段
        auto& reg = detail::type_factory_registry();
        for (size_t i = 0; i < reg.size(); ++i)
        {
            remap_entity_fields_by_tid(reg[i].type_id, remap);
        }

        stats_.entity_count = remap.old_to_new.size();
        return operating_message{};
    }

    // 按 type_id 重映射实体引用字段
    void remap_entity_fields_by_tid(int tid, const detail::entity_remap& remap) noexcept {
        ecs::single_class_set* set = mgr_.get_single_class_set_by_id(tid);
        if (!set)
        {
            return;
        }
        auto& reg = detail::entity_field_registry();
        dense<uint32_t> offsets;
        for (size_t i = 0; i < reg.size(); ++i)
        {
            if (reg[i].type_id == tid)
            {
                offsets.push_back(reg[i].offset);
            }
        }
        if (offsets.size() == 0)
        {
            return;
        }

        // 类型擦除的字节级操作 (通过 public getter 访问 pool 数据)
        char* pool_base = static_cast<char*>(set->get_raw_pool_data());
        if (!pool_base)
        {
            return;
        }
        size_t count = set->size();
        size_t elem_size = set->get_component_size();
        for (size_t i = 0; i < count; ++i)
        {
            char* base = pool_base + i * elem_size;
            for (size_t k = 0; k < offsets.size(); ++k)
            {
                ecs::entity* ref = reinterpret_cast<ecs::entity*>(base + offsets[k]);
                if (!ref->is_valid())
                {
                    continue;
                }
                uint32_t old_idx = ref->parts_.index_;
                if (old_idx < remap.old_to_new.size())
                {
                    *ref = remap.old_to_new[old_idx];
                }
            }
        }
    }

    // ====================================================================
    // 运行时二进制保存/加载 (遍历 registry)
    // 注: 二进制格式需要类型特化的 save_one_type_binary<T>, 运行时类型擦除
    //     需为每个类型注册 binary trampoline. 当前简化实现: 运行时仅支持 JSON.
    //     二进制运行时路径回退到 JSON 中间格式再转换.
    // ====================================================================
    operating_message save_to_binary_runtime(std::string& out) noexcept {
        // 运行时二进制暂不支持, 回退到 JSON
        operating_message r = save_to_json_runtime(out);
        if (!r)
        {
            return r;
        }
        r.write_message(false, "运行时二进制保存暂不支持, 请使用 JSON 格式或 Ts... 编译期路径");
        return r;
    }

    operating_message load_from_binary_runtime(std::string_view data) noexcept {
        // 运行时二进制暂不支持
        operating_message r;
        r.write_message(false, "运行时二进制加载暂不支持, 请使用 JSON 格式或 Ts... 编译期路径");
        return r;
    }

    // ====================================================================
    // #B1 流式保存到 ostream (减少峰值内存)
    // JSON 分段刷新, Binary 每类型独立缓冲, 峰值 = max(单段)
    // ====================================================================

    // 刷新 json_writer 内部缓冲到流并清空 (保留 need_comma_/depth_ 状态)
    static size_t flush_json_to_stream(std::ostream& os, json_writer& w) noexcept
    {
        std::string& buf = w.string();
        if (buf.empty())
        {
            return 0;
        }
        size_t sz = buf.size();
        os.write(buf.data(), static_cast<std::streamsize>(sz));
        buf.clear();
        return sz;
    }

    template<typename... Ts>
    operating_message save_to_stream(std::ostream& os, format fmt = format::json) noexcept
    {
        (register_factory_for_type<Ts>(), ...);
        stats_.reset();
        operating_message r;

        if (fmt == format::binary)
        {
            r = save_to_stream_binary<Ts...>(os);
        }
        else if (fmt == format::json)
        {
            r = save_to_stream_json<Ts...>(os);
        }
        else
        {
            // protobuf/flatbuffer 无流式实现, 回退到内存构建
            std::string buf;
            r = save_to_string<Ts...>(buf, fmt);
            if (r)
            {
                os.write(buf.data(), static_cast<std::streamsize>(buf.size()));
                stats_.total_bytes = buf.size();
            }
        }

        if (r)
        {
            stats_.archive_version = header_.archive_version;
        }
        return r;
    }

    template<typename... Ts>
    operating_message save_to_stream_json(std::ostream& os) noexcept
    {
        json_writer w(65536, false);
        size_t bytes = 0;

        w.begin_object();
        w.key("version").value(header_.archive_version);
        w.key("engine").value(header_.engine_version);
        bytes += flush_json_to_stream(os, w);

        if (metadata_.size() > 0)
        {
            w.key("meta").begin_object();
            for (size_t i = 0; i < metadata_.size(); ++i)
            {
                w.key(metadata_[i].key).value(metadata_[i].value);
            }
            w.end_object();
            bytes += flush_json_to_stream(os, w);
        }

        // 组件版本
        bool has_cv = false;
        ((has_cv = has_cv || (lookup_component_version<Ts>() > 0)), ...);
        if (has_cv)
        {
            w.key("cv").begin_object();
            (save_component_version<Ts>(w), ...);
            w.end_object();
            bytes += flush_json_to_stream(os, w);
        }

        w.key("entities").begin_array();
        save_entities<Ts...>(w);
        w.end_array();
        bytes += flush_json_to_stream(os, w);

        w.key("components").begin_object();
        bytes += flush_json_to_stream(os, w);

        int dummy[] = {
            (save_one_type<Ts>(w), bytes += flush_json_to_stream(os, w), 0)...
        };
        (void)dummy;

        w.end_object();
        w.end_object();
        bytes += flush_json_to_stream(os, w);

        stats_.total_bytes = bytes;
        return operating_message{};
    }

    template<typename... Ts>
    operating_message save_to_stream_binary(std::ostream& os) noexcept
    {
        size_t bytes = 0;

        binary_writer bw;
        bw.value(header_.archive_version);
        bw.value(header_.engine_version);

        std::string meta_buf;
        if (metadata_.size() > 0)
        {
            json_writer mw;
            mw.begin_object();
            for (size_t i = 0; i < metadata_.size(); ++i)
            {
                mw.key(metadata_[i].key).value(metadata_[i].value);
            }
            mw.end_object();
            meta_buf = mw.take();
        }
        bw.value(meta_buf);

        std::string entities_buf;
        {
            json_writer ew;
            ew.begin_array();
            save_entities<Ts...>(ew);
            ew.end_array();
            entities_buf = ew.take();
        }
        bw.value(entities_buf);

        const uint32_t type_count = static_cast<uint32_t>(sizeof...(Ts));
        bw.value(type_count);

        {
            const auto& d = bw.data();
            os.write(d.data(), static_cast<std::streamsize>(d.size()));
            bytes += d.size();
        }

        int dummy[] = {
            (save_one_type_binary_stream<Ts>(os, bytes), 0)...
        };
        (void)dummy;

        stats_.total_bytes = bytes;
        return operating_message{};
    }

    // 跳过 8 字节 header (magic + endianness + version + reserved)
    template<typename T>
    void save_one_type_binary_stream(std::ostream& os, size_t& bytes_written) noexcept
    {
        binary_writer bw;
        save_one_type_binary<T>(bw);
        const auto& d = bw.data();
        constexpr size_t header_sz = 8;
        size_t len = d.size() - header_sz;
        os.write(d.data() + header_sz, static_cast<std::streamsize>(len));
        bytes_written += len;
    }

    // ====================================================================
    // #B3 存档分块 archive_index (单文件, 支持选择性加载)
    // 格式: [magic "LCAX" 4B][ver 4B][engine 4B][fmt 1B][reserved 3B]
    //       [chunk_count 4B][index_table N×56B][chunk_data...]
    // 块类型: __meta__ / __entities__ / __cv__ / <类型名>
    // ====================================================================

    struct archive_chunk_entry
    {
        uint64_t name_hash = 0;    // fnv1a_runtime(name)
        uint64_t offset = 0;       // 文件绝对偏移
        uint64_t size = 0;
        uint32_t comp_count = 0;   // 类型块组件数, 0=非类型块
        char name[32] = {};        // null 结尾
    };

    static constexpr char ARCHIVE_INDEX_MAGIC[4] = {'L', 'C', 'A', 'X'};
    static constexpr size_t ARCHIVE_HEADER_SIZE = 4 + 4 + 4 + 1 + 3 + 4;
    static constexpr size_t CHUNK_ENTRY_SIZE = sizeof(archive_chunk_entry);

    template<typename... Ts>
    operating_message save_to_archive(const std::string& path) noexcept
    {
        (register_factory_for_type<Ts>(), ...);
        stats_.reset();

        std::ofstream f(path, std::ios::binary | std::ios::trunc);
        if (!f)
        {
            operating_message r;
            r.write_message(false, "无法创建文件: ", path);
            return r;
        }

        dense<archive_chunk_entry> chunks;

        // chunk_count 未知, 预留 max_chunks 槽位 (类型块 + meta + entities + cv)
        constexpr size_t max_chunks = sizeof...(Ts) + 3;

        f.write(ARCHIVE_INDEX_MAGIC, 4);
        uint32_t av = header_.archive_version;
        uint32_t ev = header_.engine_version;
        f.write(reinterpret_cast<const char*>(&av), 4);
        f.write(reinterpret_cast<const char*>(&ev), 4);
        uint8_t fmt_byte = 0; // JSON
        f.write(reinterpret_cast<const char*>(&fmt_byte), 1);
        char reserved[3] = {0, 0, 0};
        f.write(reserved, 3);
        uint32_t placeholder_count = 0;
        f.write(reinterpret_cast<const char*>(&placeholder_count), 4);

        // 预留 index 区域 (稍后回填)
        std::string index_placeholder(max_chunks * CHUNK_ENTRY_SIZE, '\0');
        f.write(index_placeholder.data(), static_cast<std::streamsize>(index_placeholder.size()));

        if (metadata_.size() > 0)
        {
            json_writer mw;
            mw.begin_object();
            for (size_t i = 0; i < metadata_.size(); ++i)
            {
                mw.key(metadata_[i].key).value(metadata_[i].value);
            }
            mw.end_object();
            std::string data = mw.take();
            uint64_t offset = static_cast<uint64_t>(f.tellp());
            f.write(data.data(), static_cast<std::streamsize>(data.size()));

            archive_chunk_entry e;
            e.name_hash = fnv1a_runtime("__meta__");
            e.offset = offset;
            e.size = data.size();
            std::strncpy(e.name, "__meta__", 31);
            chunks.push_back(e);
        }

        // 组件版本块
        bool has_cv = false;
        ((has_cv = has_cv || (lookup_component_version<Ts>() > 0)), ...);
        if (has_cv)
        {
            json_writer cw;
            cw.begin_object();
            (save_component_version<Ts>(cw), ...);
            cw.end_object();
            std::string data = cw.take();
            uint64_t offset = static_cast<uint64_t>(f.tellp());
            f.write(data.data(), static_cast<std::streamsize>(data.size()));

            archive_chunk_entry e;
            e.name_hash = fnv1a_runtime("__cv__");
            e.offset = offset;
            e.size = data.size();
            std::strncpy(e.name, "__cv__", 31);
            chunks.push_back(e);
        }

        // 实体块
        {
            json_writer ew;
            ew.begin_array();
            save_entities<Ts...>(ew);
            ew.end_array();
            std::string data = ew.take();
            uint64_t offset = static_cast<uint64_t>(f.tellp());
            f.write(data.data(), static_cast<std::streamsize>(data.size()));

            archive_chunk_entry e;
            e.name_hash = fnv1a_runtime("__entities__");
            e.offset = offset;
            e.size = data.size();
            std::strncpy(e.name, "__entities__", 31);
            chunks.push_back(e);
            stats_.entity_count = 0;
        }

        // 类型块
        int dummy[] = {
            (save_type_chunk<Ts>(f, chunks), 0)...
        };
        (void)dummy;

        // 回填 chunk_count 和 index 表
        uint32_t chunk_count = static_cast<uint32_t>(chunks.size());
        // chunk_count 偏移: magic(4) + archive_ver(4) + engine_ver(4) + fmt(1) + reserved(3) = 16
        f.seekp(16);
        f.write(reinterpret_cast<const char*>(&chunk_count), 4);

        // index 紧跟 header, 覆盖占位零
        f.seekp(ARCHIVE_HEADER_SIZE, std::ios::beg);
        for (size_t i = 0; i < chunks.size(); ++i)
        {
            f.write(reinterpret_cast<const char*>(&chunks[i]), CHUNK_ENTRY_SIZE);
        }

        f.flush();
        stats_.total_bytes = static_cast<size_t>(f.tellp());
        stats_.archive_version = header_.archive_version;
        return operating_message{};
    }

    template<typename T>
    void save_type_chunk(std::ofstream& f, dense<archive_chunk_entry>& chunks) noexcept
    {
        json_writer w;
        w.begin_array();
        const ecs::single_class_set* set = mgr_.get_single_class_set<T>();
        size_t comp_count = 0;
        if (set)
        {
            const auto& indices  = set->get_entity_indices();
            const auto& versions = set->get_entity_versions();
            const auto* pool     = set->get_typed_pool_ptr<T>();
            size_t count = set->size();
            for (size_t i = 0; i < count; ++i)
            {
                uint32_t idx = indices[i];
                if (filter_ && !filter_->matches_entity(idx, mgr_.get_entity_state(idx)))
                {
                    continue;
                }
                const T* comp = pool ? &(*pool)[i] : nullptr;
                w.begin_object();
                w.key("i").value(static_cast<uint32_t>(idx));
                w.key("v").value(static_cast<uint32_t>(versions[i]));
                if (comp)
                {
                    w.key("d");
                    serialize_value<T>(w, *comp);
                    ++comp_count;
                }
                else
                {
                    w.key("d").null();
                }
                w.end_object();
            }
        }
        w.end_array();

        std::string data = w.take();
        uint64_t offset = static_cast<uint64_t>(f.tellp());
        f.write(data.data(), static_cast<std::streamsize>(data.size()));

        std::string_view tname = type_name<T>();
        archive_chunk_entry e;
        e.name_hash = fnv1a_runtime(tname.data(), tname.size());
        e.offset = offset;
        e.size = data.size();
        e.comp_count = static_cast<uint32_t>(comp_count);
        std::strncpy(e.name, std::string(tname).c_str(), 31);
        e.name[31] = '\0';
        chunks.push_back(e);

        stats_.per_type.push_back({std::string(tname), comp_count, data.size()});
    }

    // 仅读取索引, 不加载数据
    [[nodiscard]] dense<archive_chunk_entry> read_archive_index(const std::string& path) noexcept
    {
        dense<archive_chunk_entry> result;
        std::ifstream f(path, std::ios::binary);
        if (!f)
        {
            return result;
        }

        char magic[4];
        f.read(magic, 4);
        if (std::memcmp(magic, ARCHIVE_INDEX_MAGIC, 4) != 0)
        {
            return result;
        }

        uint32_t av, ev;
        f.read(reinterpret_cast<char*>(&av), 4);
        f.read(reinterpret_cast<char*>(&ev), 4);
        uint8_t fmt_byte;
        f.read(reinterpret_cast<char*>(&fmt_byte), 1);
        f.ignore(3);
        uint32_t chunk_count;
        f.read(reinterpret_cast<char*>(&chunk_count), 4);

        for (uint32_t i = 0; i < chunk_count; ++i)
        {
            archive_chunk_entry e;
            f.read(reinterpret_cast<char*>(&e), CHUNK_ENTRY_SIZE);
            result.push_back(e);
        }

        return result;
    }

    // 选择性加载: 只读 Ts... 中存在的类型块, 复用 scan_entities + load_one_type_json
    template<typename... Ts>
    operating_message load_from_archive(const std::string& path) noexcept
    {
        static_assert((json_serializable<Ts> && ...),
            "所有组件类型必须满足 json_serializable");

        (register_factory_for_type<Ts>(), ...);

        std::ifstream f(path, std::ios::binary);
        if (!f)
        {
            operating_message r;
            r.write_message(false, "无法打开文件: ", path);
            return r;
        }

        char magic[4];
        f.read(magic, 4);
        if (std::memcmp(magic, ARCHIVE_INDEX_MAGIC, 4) != 0)
        {
            operating_message r;
            r.write_message(false, "非分块存档格式 (缺少 LCAX magic)");
            return r;
        }

        uint32_t av, ev;
        f.read(reinterpret_cast<char*>(&av), 4);
        f.read(reinterpret_cast<char*>(&ev), 4);
        uint8_t fmt_byte;
        f.read(reinterpret_cast<char*>(&fmt_byte), 1);
        f.ignore(3);
        uint32_t chunk_count;
        f.read(reinterpret_cast<char*>(&chunk_count), 4);

        // 读取索引
        dense<archive_chunk_entry> chunks;
        for (uint32_t i = 0; i < chunk_count; ++i)
        {
            archive_chunk_entry e;
            f.read(reinterpret_cast<char*>(&e), CHUNK_ENTRY_SIZE);
            chunks.push_back(e);
        }

        stats_.reset();
        if (load_mode_ == load_mode::replace)
        {
            clear_all_entities();
        }

        detail::entity_remap remap;
        dense<detail::metadata_entry> saved_cv;

        // 按 name 读取块数据
        auto read_chunk = [&](const char* name) -> std::string {
            for (size_t i = 0; i < chunks.size(); ++i)
            {
                if (std::strcmp(chunks[i].name, name) == 0)
                {
                    std::string data(chunks[i].size, '\0');
                    f.seekg(static_cast<std::streamoff>(chunks[i].offset));
                    f.read(data.data(), static_cast<std::streamsize>(chunks[i].size));
                    return data;
                }
            }
            return {};
        };

        std::string meta_json = read_chunk("__meta__");
        if (!meta_json.empty())
        {
            json_reader mr(meta_json);
            if (mr.enter_object())
            {
                std::string_view mk;
                while (!(mk = mr.next_key()).empty())
                {
                    std::string val = mr.read_string();
                    metadata_.push_back({std::string(mk), std::move(val)});
                }
            }
        }

        std::string cv_json = read_chunk("__cv__");
        if (!cv_json.empty())
        {
            json_reader cr(cv_json);
            if (cr.enter_object())
            {
                std::string_view k;
                while (!(k = cr.next_key()).empty())
                {
                    uint32_t v = cr.read_uint32();
                    saved_cv.push_back({std::string(k), std::to_string(v)});
                }
            }
        }

        // 扫描实体 (复用 scan_entities)
        std::string entities_json = read_chunk("__entities__");
        if (!entities_json.empty())
        {
            json_reader er(entities_json);
            if (!scan_entities(er, remap))
            {
                if (er.has_error())
                {
                    return er.last_error();
                }
                operating_message r;
                r.write_message(false, "实体扫描失败 (可能超过上限: ",
                                std::to_string(limits_.max_entity_count), ")");
                return r;
            }
            stats_.entity_count = remap.old_to_new.size();
        }

        // 选择性加载类型块 (复用 load_one_type_json)
        int dummy[] = {
            (load_type_from_archive<Ts>(f, chunks, remap, saved_cv), 0)...
        };
        (void)dummy;

        (remap_entity_fields<Ts>(remap), ...);

        stats_.archive_version = av;
        return operating_message{};
    }

    // 按 hash + 别名匹配块, 复用 load_one_type_json
    template<typename T>
    void load_type_from_archive(std::ifstream& f,
                                const dense<archive_chunk_entry>& chunks,
                                const detail::entity_remap& remap,
                                const dense<detail::metadata_entry>& saved_cv) noexcept
    {
        std::string_view tname = type_name<T>();
        uint64_t target_hash = fnv1a_runtime(tname.data(), tname.size());

        const archive_chunk_entry* target = nullptr;
        for (size_t i = 0; i < chunks.size(); ++i)
        {
            if (chunks[i].name_hash == target_hash)
            {
                target = &chunks[i];
                break;
            }
        }

        // 兼容别名: 旧类型名 → 新类型
        if (!target)
        {
            for (size_t i = 0; i < chunks.size(); ++i)
            {
                if (detail::is_alias_of(type_id::get_type_id<T>(), chunks[i].name))
                {
                    target = &chunks[i];
                    break;
                }
            }
        }

        if (!target)
        {
            return; // 存档无此类型, 跳过
        }

        std::string data(target->size, '\0');
        f.seekg(static_cast<std::streamoff>(target->offset));
        f.read(data.data(), static_cast<std::streamsize>(target->size));

        // load_one_type_json 内部已处理版本迁移 + 字段 schema + best_effort
        json_reader r(data);
        load_one_type_json<T>(r, remap, saved_cv);
    }
};

} // namespace serialize
