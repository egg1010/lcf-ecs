// codec_binary.hpp - 原生二进制编码器 (适配 binary_writer/binary_reader)
// magic: "LCE1", 零拷贝: read_bytes_view 直接返回原缓冲区指针
#pragma once

#include "archive_codec.hpp"
#include "safety.hpp"
#include "binary_writer.hpp"
#include "binary_reader.hpp"
#include "operating_message.hpp"
#include <cstring>
#include <string>
#include <string_view>


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
    operating_message err_msg_;     // skip_value 等不支持操作的错误消息
    bool skip_unsupported_ = false; // 是否触发了不支持的 skip
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
        if (r_.remaining() < len)
        {
            r_.skip(len);
            return {};
        }
        std::string_view sv(r_.peek(), len);
        r_.skip(len);
        return sv;
    }

    void skip_value() noexcept override {
        // 二进制格式无类型信息, 无法安全跳过未知字段
        // 标记错误: 调用方应按已知字段类型使用 read_xxx 或 skip(n)
        skip_unsupported_ = true;
        err_msg_.write_message(false,
            "binary 格式不支持 skip_value (无类型信息), 请按字段类型读取");
    }

    [[nodiscard]] bool has_error() const noexcept override {
        return r_.has_error() || skip_unsupported_;
    }
    [[nodiscard]] operating_message last_error() const noexcept override {
        if (skip_unsupported_)
        {
            return err_msg_;
        }
        operating_message res;
        if (r_.has_error())
        {
            res.write_message(false, "binary_reader error");
        }
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

    // 深度结构校验: 遍历所有字段并验证长度边界
    [[nodiscard]] operating_message validate(std::string_view data) const noexcept override;
};

// ============================================================================
// binary_codec::validate — 深度结构校验
// 遍历存档头 + 元数据 + 实体 + 类型表, 验证长度边界与游标一致性
// ============================================================================
inline operating_message binary_codec::validate(std::string_view data) const noexcept
{
    binary_reader r(data);
    if (r.has_error())
    {
        operating_message res;
        res.write_message(false, "二进制 magic/头校验失败");
        return res;
    }

    // archive_ver + engine_ver
    (void)r.read_u32();
    (void)r.read_u32();
    if (r.has_error())
    {
        operating_message res;
        res.write_message(false, "二进制版本字段读取失败");
        return res;
    }

    // 元数据 (字符串)
    (void)r.read_string();
    if (r.has_error())
    {
        operating_message res;
        res.write_message(false, "二进制元数据字段读取失败");
        return res;
    }

    // 实体状态 (字符串)
    (void)r.read_string();
    if (r.has_error())
    {
        operating_message res;
        res.write_message(false, "二进制实体字段读取失败");
        return res;
    }

    // 类型表
    uint32_t type_count = r.read_u32();
    if (r.has_error())
    {
        operating_message res;
        res.write_message(false, "二进制类型计数读取失败");
        return res;
    }

    // 安全上限: 防止恶意 type_count 导致空转
    if (type_count > 65536)
    {
        operating_message res;
        res.write_message(false, "二进制类型计数异常: ", type_count);
        return res;
    }

    for (uint32_t i = 0; i < type_count; ++i)
    {
        (void)r.read_string();  // 类型名
        (void)r.read_u32();     // 组件版本
        uint32_t data_len = r.read_u32();
        if (r.has_error())
        {
            operating_message res;
            res.write_message(false, "二进制类型 #", i, " 头部读取失败");
            return res;
        }
        r.skip(data_len);
        if (r.has_error())
        {
            operating_message res;
            res.write_message(false, "二进制类型 #", i, " 数据越界 (len=", data_len, ")");
            return res;
        }
    }

    return operating_message{};
}

