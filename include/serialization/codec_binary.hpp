// codec_binary.hpp - 原生二进制编码器 (适配 binary_writer/binary_reader)
// magic: "LCE1", 零拷贝: read_bytes_view 直接返回原缓冲区指针
#pragma once

#include "archive_codec.hpp"
#include "safety.hpp"
#include "binary_writer.hpp"
#include "binary_reader.hpp"
#include "../part/operating_message.hpp"
#include <cstring>
#include <string>
#include <string_view>

namespace ecs {

// ============================================================================
// binary_archive_writer — 适配 binary_writer 实现 archive_writer
// ============================================================================
class binary_archive_writer final : public archive_writer
{
    binary_writer w_;
public:
    explicit binary_archive_writer() noexcept : w_() {}

    void begin_object() noexcept override {}
    void end_object() noexcept override {}
    void begin_array(size_t count) noexcept override { w_.begin_array(count); }
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
        w_.write_raw_bytes(data, len);
    }
    void write_raw(std::string_view fragment) noexcept override {
        w_.write_raw_bytes(fragment.data(), fragment.size());
    }

    [[nodiscard]] std::string take() noexcept override { return w_.take(); }
    [[nodiscard]] size_t size() const noexcept override { return w_.data().size(); }
};

// ============================================================================
// binary_archive_reader — 适配 binary_reader 实现 archive_reader
// 零拷贝: read_bytes_view 直接返回原缓冲区指针
// ============================================================================
class binary_archive_reader final : public archive_reader
{
    binary_reader r_;
public:
    explicit binary_archive_reader(std::string_view data) noexcept : r_(data) {}

    bool enter_object() noexcept override { return !r_.has_error(); }
    void leave_object() noexcept override {}
    bool enter_array() noexcept override {
        // 二进制数组: 读取元素数, 由调用方迭代
        array_len_ = r_.read_u32();
        array_idx_ = 0;
        return !r_.has_error();
    }
    void leave_array() noexcept override {}
    bool next_element() noexcept override {
        return array_idx_++ < array_len_;
    }
    void end_element() noexcept override {}

    [[nodiscard]] std::string_view next_key() noexcept override {
        // 二进制格式: key 是字符串
        return r_.read_string_view();
    }

    [[nodiscard]] bool read_bool() noexcept override { return r_.read_bool(); }
    [[nodiscard]] int32_t read_i32() noexcept override { return r_.read_i32(); }
    [[nodiscard]] uint32_t read_u32() noexcept override { return r_.read_u32(); }
    [[nodiscard]] int64_t read_i64() noexcept override { return r_.read_i64(); }
    [[nodiscard]] uint64_t read_u64() noexcept override { return r_.read_u64(); }
    [[nodiscard]] float read_f32() noexcept override { return r_.read_f32(); }
    [[nodiscard]] double read_f64() noexcept override { return r_.read_f64(); }
    [[nodiscard]] std::string_view read_string_view() noexcept override {
        return r_.read_string_view();
    }

    [[nodiscard]] std::string_view read_bytes_view(size_t len) noexcept override {
        // 零拷贝: 直接返回原缓冲区指针
        if (r_.remaining() < len) { r_.skip(len); return {}; }
        std::string_view sv(r_.peek(), len);
        r_.skip(len);
        return sv;
    }

    void skip_value() noexcept override {
        // 二进制 skip 需要知道类型, 简化: 由调用方按字段类型读取
        // 未知字段: 读取一个 u32 长度 + 跳过
        uint32_t len = r_.read_u32();
        r_.skip(len);
    }

    [[nodiscard]] bool has_error() const noexcept override { return r_.has_error(); }
    [[nodiscard]] operating_message last_error() const noexcept override {
        operating_message res;
        if (r_.has_error()) res.write_message(false, "binary_reader error");
        return res;
    }

    [[nodiscard]] archive_type peek_type() const noexcept override {
        return archive_type::bytes_t;  // 二进制路径不依赖 peek
    }

private:
    uint32_t array_len_ = 0;
    uint32_t array_idx_ = 0;
};

// ============================================================================
// binary_codec — 二进制编解码器工厂
// ============================================================================
struct binary_codec final : archive_codec
{
    binary_codec() noexcept {
        magic[0] = 'L'; magic[1] = 'C'; magic[2] = 'E'; magic[3] = '1';
    }

    [[nodiscard]] archive_writer* create_writer() const noexcept override {
        return new (std::nothrow) binary_archive_writer();
    }
    [[nodiscard]] archive_reader* create_reader(std::string_view data) const noexcept override {
        return new (std::nothrow) binary_archive_reader(data);
    }
    void destroy_writer(archive_writer* w) const noexcept override { delete w; }
    void destroy_reader(archive_reader* r) const noexcept override { delete r; }
};

} // namespace ecs
