// codec_json.hpp - JSON 编码器 (适配 json_writer/json_reader 到 archive_writer/reader)
// 零拷贝: read_string_view 返回原缓冲区切片
#pragma once

#include "archive_codec.hpp"
#include "../safety.hpp"
#include "json_writer.hpp"
#include "json_reader.hpp"
#include "../operating_message.hpp"
#include <cstring>
#include <string>
#include <string_view>


// ============================================================================
// json_archive_writer — 适配 json_writer 实现 archive_writer
// ============================================================================
class json_archive_writer final : public archive_writer
{
    json_writer w_;
public:
    explicit json_archive_writer(size_t reserve = 65536, bool pretty = false) noexcept
        : w_(reserve, pretty) {}

    void begin_object() noexcept override { w_.begin_object(); }
    void end_object() noexcept override { w_.end_object(); }
    void begin_array(size_t /*count*/) noexcept override { w_.begin_array(); }
    void end_array() noexcept override { w_.end_array(); }
    void key(std::string_view k) noexcept override { w_.key(k); }

    void write_bool(bool v) noexcept override { w_.value(v); }
    void write_i32(int32_t v) noexcept override { w_.value(v); }
    void write_u32(uint32_t v) noexcept override { w_.value(v); }
    void write_i64(int64_t v) noexcept override { w_.value(v); }
    void write_u64(uint64_t v) noexcept override { w_.value(v); }
    void write_f32(float v) noexcept override { w_.value(v); }
    void write_f64(double v) noexcept override { w_.value(v); }
    void write_string(std::string_view v) noexcept override { w_.value(v); }
    void write_bytes(const void* data, size_t len) noexcept override {
        // 字节用 base64 编码
        w_.value(detail::base64_encode(data, len));
    }
    void write_raw(std::string_view fragment) noexcept override { w_.raw_value(fragment); }

    [[nodiscard]] std::string take() noexcept override { return w_.take(); }
    [[nodiscard]] size_t size() const noexcept override { return w_.size(); }
};

// ============================================================================
// json_archive_reader — 适配 json_reader 实现 archive_reader
// 零拷贝: read_string_view 直接返回原 JSON 缓冲区切片
// ============================================================================
class json_archive_reader final : public archive_reader
{
    json_reader r_;
public:
    explicit json_archive_reader(std::string_view data) noexcept : r_(data) {}

    bool enter_object() noexcept override { return r_.enter_object(); }
    void leave_object() noexcept override { /* json_reader 无显式 leave */ }
    bool enter_array() noexcept override { return r_.enter_array(); }
    void leave_array() noexcept override { /* json_reader 无显式 leave */ }
    bool next_element() noexcept override { return r_.next_element(); }
    void end_element() noexcept override { r_.end_element(); }

    [[nodiscard]] std::string_view next_key() noexcept override { return r_.next_key(); }

    [[nodiscard]] bool read_bool() noexcept override { return r_.read_bool(); }
    [[nodiscard]] int32_t read_i32() noexcept override { return r_.read_int32(); }
    [[nodiscard]] uint32_t read_u32() noexcept override { return r_.read_uint32(); }
    [[nodiscard]] int64_t read_i64() noexcept override { return r_.read_int64(); }
    [[nodiscard]] uint64_t read_u64() noexcept override { return r_.read_uint64(); }
    [[nodiscard]] float read_f32() noexcept override { return static_cast<float>(r_.read_double()); }
    [[nodiscard]] double read_f64() noexcept override { return r_.read_double(); }
    [[nodiscard]] std::string_view read_string_view() noexcept override { return r_.read_string(); }

    [[nodiscard]] std::string_view read_bytes_view(size_t len) noexcept override {
        // JSON 路径: bytes 存为 base64 字符串, 解码后返回池化缓冲区
        // 空间换速度: 所有解码结果累积在 tmp_pool_ 中, reader 生命周期内有效
        // 调用方可安全持有返回的 string_view 直到 reader 销毁
        (void)len;  // JSON 长度由流内 base64 内容决定
        std::string_view b64 = r_.read_string();
        std::string decoded = detail::base64_decode(b64);
        // 追加到池, 返回稳定视图 (避免下次调用覆盖)
        tmp_pool_.push_back(std::move(decoded));
        return std::string_view(tmp_pool_.back());
    }

    void skip_value() noexcept override { r_.skip_value(); }

    [[nodiscard]] bool has_error() const noexcept override { return r_.has_error(); }
    [[nodiscard]] operating_message last_error() const noexcept override { return r_.last_error(); }

    [[nodiscard]] archive_type peek_type() const noexcept override {
        // JSON 简化: 按当前字符判断
        return archive_type::null_t;  // JSON 路径不依赖 peek, 调用方按字段名读取
    }

private:
    dense<std::string> tmp_pool_;  // base64 解码缓冲池 (空间换速度, 全生命周期有效)
};

// ============================================================================
// json_codec — JSON 编解码器工厂
// ============================================================================
struct json_codec final : archive_codec
{
    json_codec() noexcept {
        magic[0] = '{';  // JSON 无固定 magic, 用首字符 '{' 检测
        magic[1] = magic[2] = magic[3] = 0;
    }

    [[nodiscard]] archive_writer* create_writer() const noexcept override {
        return new (std::nothrow) json_archive_writer();
    }
    [[nodiscard]] archive_reader* create_reader(std::string_view data) const noexcept override {
        return new (std::nothrow) json_archive_reader(data);
    }
    void destroy_writer(archive_writer* w) const noexcept override { delete w; }
    void destroy_reader(archive_reader* r) const noexcept override { delete r; }

    // JSON 检测: 首个非空白字符为 '{' 或 '['
    [[nodiscard]] bool matches(std::string_view data) const noexcept {
        for (size_t i = 0; i < data.size() && i < 16; ++i)
        {
            char c = data[i];
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r')
            {
                continue;
            }
            return c == '{' || c == '[';
        }
        return false;
    }
};

