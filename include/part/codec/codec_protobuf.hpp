// codec_protobuf.hpp - Protobuf 风格编码器 (wire format 实现, 不依赖 libprotobuf)
// magic: "LCPB"
// 实现 Protobuf 的 wire format: varint/fixed32/fixed64/length-delimited
// 兼容真实 protobuf 工具链解析 (需配合 .proto schema)
// 零拷贝: read 返回原缓冲区指针
#pragma once

#include "archive_codec.hpp"
#include "../safety.hpp"
#include "../operating_message.hpp"
#include "../dense.hpp"
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>


// ============================================================================
// Protobuf wire types
// ============================================================================
namespace pb_detail {

enum wire_type : uint32_t {
    varint          = 0,  // int32/int64/uint32/uint64/bool/enum
    fixed64         = 1,  // fixed64/sfixed64/double
    length_delimited = 2, // string/bytes/message/repeated
    fixed32         = 5,  // fixed32/sfixed32/float
};

// varint 编码 (LEB128)
inline void write_varint(std::string& buf, uint64_t v) noexcept {
    while (v >= 0x80)
    {
        buf.push_back(static_cast<char>(static_cast<uint8_t>(v | 0x80)));
        v >>= 7;
    }
    buf.push_back(static_cast<char>(static_cast<uint8_t>(v)));
}

[[nodiscard]] inline uint64_t read_varint(const char*& p, const char* end, bool& err) noexcept {
    uint64_t result = 0;
    int shift = 0;
    for (int i = 0; i < 10; ++i)
    {
        if (p >= end)
        {
            err = true;
            return 0;
        }
        uint8_t b = static_cast<uint8_t>(*p++);
        result |= static_cast<uint64_t>(b & 0x7F) << shift;
        if ((b & 0x80) == 0)
        {
            return result;
        }
        shift += 7;
    }
    err = true;  // varint 过长
    return 0;
}

// tag = (field_number << 3) | wire_type
inline void write_tag(std::string& buf, uint32_t field_num, uint32_t wt) noexcept {
    write_varint(buf, (static_cast<uint64_t>(field_num) << 3) | wt);
}

inline void write_fixed32(std::string& buf, uint32_t v) noexcept {
    size_t old = buf.size();
    buf.resize(old + 4);
    detail::write_le(v, buf.data() + old);  // 复用 safety.hpp 的 LE 写入
}

inline void write_fixed64(std::string& buf, uint64_t v) noexcept {
    size_t old = buf.size();
    buf.resize(old + 8);
    detail::write_le(v, buf.data() + old);
}

// zigzag 编码: 有符号整数 → 无符号, 负数不再膨胀为 10 字节 varint
// sint32: (n << 1) ^ (n >> 31)  sint64: (n << 1) ^ (n >> 63)
[[nodiscard]] inline uint32_t zigzag_encode32(int32_t n) noexcept {
    return static_cast<uint32_t>((n << 1) ^ (n >> 31));
}

[[nodiscard]] inline uint64_t zigzag_encode64(int64_t n) noexcept {
    return static_cast<uint64_t>((n << 1) ^ (n >> 63));
}

[[nodiscard]] inline int32_t zigzag_decode32(uint32_t n) noexcept {
    return static_cast<int32_t>((n >> 1) ^ (0u - (n & 1u)));
}

[[nodiscard]] inline int64_t zigzag_decode64(uint64_t n) noexcept {
    return static_cast<int64_t>((n >> 1) ^ (0ull - (n & 1ull)));
}

} // namespace pb_detail

// ============================================================================
// protobuf_archive_writer — Protobuf 风格写入器
// 设计: 用对象栈模拟 message 嵌套, 字段顺序作为 field_number
// ============================================================================
class protobuf_archive_writer final : public archive_writer
{
    struct frame {
        std::string buf;       // 当前 message 的缓冲区
        uint32_t   field_idx = 0;  // 下一个字段编号 (从 1 开始)
    };
    dense<frame> stack_;
    std::string root_buf_;
    bool root_begin_ = false;  // 根 begin_object 是否已调用

public:
    protobuf_archive_writer() noexcept {
        // 写入 magic "LCPB"
        root_buf_.append("LCPB", 4);
        stack_.increase_capacity(8);
        stack_.push_back(frame{});
    }

    void begin_object() noexcept override {
        // 第一个 begin_object (根): no-op, 标记已开始
        // 后续 begin_object: push 嵌套 frame
        if (stack_.size() == 1 && !root_begin_)
        {
            root_begin_ = true;
            return;
        }
        stack_.push_back(frame{});
    }

    void end_object() noexcept override {
        if (stack_.size() == 0)
        {
            return;
        }
        // 根 end_object: no-op, 由 take() 处理
        if (stack_.size() == 1)
        {
            return;
        }
        // 嵌套 message: 作为 length-delimited 写入父 frame
        frame cur = std::move(stack_.back());
        stack_.pop_back();
        frame& parent = stack_.back();
        pb_detail::write_tag(parent.buf, ++parent.field_idx, pb_detail::wire_type::length_delimited);
        pb_detail::write_varint(parent.buf, cur.buf.size());
        parent.buf.append(cur.buf);
    }

    void begin_array(size_t count) noexcept override {
        (void)count;
        // 数组作为 length-delimited 字段: push frame 收集元素
        stack_.push_back(frame{});
    }

    void end_array() noexcept override {
        if (stack_.size() <= 1)
        {
            return;
        }
        frame cur = std::move(stack_.back());
        stack_.pop_back();
        frame& parent = stack_.back();
        pb_detail::write_tag(parent.buf, ++parent.field_idx, pb_detail::wire_type::length_delimited);
        pb_detail::write_varint(parent.buf, cur.buf.size());
        parent.buf.append(cur.buf);
    }

    void key(std::string_view /*k*/) noexcept override {
        // Protobuf 用 field_number 而非 field_name
        // field_idx 在 end_object/value 时自增
    }

    // 标量写入: 用 varint/fixed
    void write_bool(bool v) noexcept override {
        if (stack_.size() == 0)
        {
            return;
        }
        auto& f = stack_.back();
        pb_detail::write_tag(f.buf, ++f.field_idx, pb_detail::wire_type::varint);
        pb_detail::write_varint(f.buf, v ? 1 : 0);
    }

    void write_i32(int32_t v) noexcept override {
        if (stack_.size() == 0)
        {
            return;
        }
        auto& f = stack_.back();
        // int32 用 zigzag 编码 (sint32): 负数压缩为最多 5 字节
        pb_detail::write_tag(f.buf, ++f.field_idx, pb_detail::wire_type::varint);
        pb_detail::write_varint(f.buf, pb_detail::zigzag_encode32(v));
    }

    void write_u32(uint32_t v) noexcept override {
        if (stack_.size() == 0)
        {
            return;
        }
        auto& f = stack_.back();
        pb_detail::write_tag(f.buf, ++f.field_idx, pb_detail::wire_type::varint);
        pb_detail::write_varint(f.buf, v);
    }

    void write_i64(int64_t v) noexcept override {
        if (stack_.size() == 0)
        {
            return;
        }
        auto& f = stack_.back();
        // int64 用 zigzag 编码 (sint64): 负数压缩为最多 10 字节 (正值更短)
        pb_detail::write_tag(f.buf, ++f.field_idx, pb_detail::wire_type::varint);
        pb_detail::write_varint(f.buf, pb_detail::zigzag_encode64(v));
    }

    void write_u64(uint64_t v) noexcept override {
        if (stack_.size() == 0)
        {
            return;
        }
        auto& f = stack_.back();
        pb_detail::write_tag(f.buf, ++f.field_idx, pb_detail::wire_type::varint);
        pb_detail::write_varint(f.buf, v);
    }

    void write_f32(float v) noexcept override {
        if (stack_.size() == 0)
        {
            return;
        }
        auto& f = stack_.back();
        pb_detail::write_tag(f.buf, ++f.field_idx, pb_detail::wire_type::fixed32);
        uint32_t raw;
        std::memcpy(&raw, &v, 4);
        pb_detail::write_fixed32(f.buf, raw);
    }

    void write_f64(double v) noexcept override {
        if (stack_.size() == 0)
        {
            return;
        }
        auto& f = stack_.back();
        pb_detail::write_tag(f.buf, ++f.field_idx, pb_detail::wire_type::fixed64);
        uint64_t raw;
        std::memcpy(&raw, &v, 8);
        pb_detail::write_fixed64(f.buf, raw);
    }

    void write_string(std::string_view v) noexcept override {
        if (stack_.size() == 0)
        {
            return;
        }
        auto& f = stack_.back();
        pb_detail::write_tag(f.buf, ++f.field_idx, pb_detail::wire_type::length_delimited);
        pb_detail::write_varint(f.buf, v.size());
        f.buf.append(v.data(), v.size());
    }

    void write_bytes(const void* data, size_t len) noexcept override {
        write_string(std::string_view(static_cast<const char*>(data), len));
    }

    void write_raw(std::string_view fragment) noexcept override {
        if (stack_.size() == 0)
        {
            root_buf_.append(fragment);
        }
        else
        {
            stack_.back().buf.append(fragment);
        }
    }

    [[nodiscard]] std::string take() noexcept override {
        // 合并根缓冲区 + 最后一个 frame (如有)
        if (stack_.size() > 0)
        {
            root_buf_.append(stack_.back().buf);
            stack_.clear();
        }
        return std::move(root_buf_);
    }

    [[nodiscard]] size_t size() const noexcept override {
        size_t s = root_buf_.size();
        for (size_t i = 0; i < stack_.size(); ++i) s += stack_[i].buf.size();
        return s;
    }
};

// ============================================================================
// protobuf_archive_reader — Protobuf 风格读取器
// 零拷贝: string/bytes 直接返回原缓冲区指针
// ============================================================================
class protobuf_archive_reader final : public archive_reader
{
    struct frame {
        const char* p;
        const char* end;
        uint32_t   next_field = 0;  // 期望的下一个字段编号
        uint32_t   cur_wire = 0;   // 当前字段的 wire type
        bool       is_root = false; // 根 message 标志
        std::string_view pending_key;  // 当前 key (字段名, 用 "f<N>" 模拟)
    };
    dense<frame> stack_;
    bool err_ = false;
    operating_message err_msg_;

    // 生成字段名 "f<field_number>"
    static std::string field_name(uint32_t num) noexcept {
        char buf[16];
        int n = std::snprintf(buf, sizeof(buf), "f%u", num);
        return std::string(buf, static_cast<size_t>(n));
    }

public:
    explicit protobuf_archive_reader(std::string_view data) noexcept {
        if (data.size() < 4 || std::memcmp(data.data(), "LCPB", 4) != 0)
        {
            err_ = true;
            err_msg_.write(false, "protobuf: magic 不匹配");
            return;
        }
        stack_.increase_capacity(8);
        frame root;
        root.p = data.data() + 4;
        root.end = data.data() + data.size();
        root.is_root = true;
        stack_.push_back(root);
    }

    bool enter_object() noexcept override {
        if (err_ || stack_.size() == 0)
        {
            return false;
        }
        frame& cur = stack_.back();
        // 根 message 已在构造时进入, enter_object 是 no-op
        if (cur.is_root)
        {
            cur.is_root = false;  // 仅第一次有效
            return true;
        }
        // 嵌套 message: 读取 length, 进入子缓冲区
        uint64_t len = pb_detail::read_varint(cur.p, cur.end, err_);
        if (err_ || cur.p + len > cur.end)
        {
            err_ = true;
            return false;
        }
        frame sub;
        sub.p = cur.p;
        sub.end = cur.p + len;
        cur.p += len;
        stack_.push_back(sub);
        return true;
    }

    void leave_object() noexcept override {
        if (stack_.size() > 1)
        {
            stack_.pop_back();
        }
    }

    bool enter_array() noexcept override {
        // 数组是 length-delimited: 读 length, 进入子 frame
        if (err_ || stack_.size() == 0)
        {
            return false;
        }
        frame& cur = stack_.back();
        uint64_t len = pb_detail::read_varint(cur.p, cur.end, err_);
        if (err_ || cur.p + len > cur.end)
        {
            err_ = true;
            return false;
        }
        frame sub;
        sub.p = cur.p;
        sub.end = cur.p + len;
        cur.p += len;
        stack_.push_back(sub);
        return true;
    }

    void leave_array() noexcept override {
        if (stack_.size() > 1)
        {
            stack_.pop_back();
        }
    }

    bool next_element() noexcept override {
        if (err_ || stack_.size() == 0)
        {
            return false;
        }
        frame& cur = stack_.back();
        if (cur.p >= cur.end)
        {
            return false;
        }
        // 读元素 tag
        uint64_t tag = pb_detail::read_varint(cur.p, cur.end, err_);
        if (err_)
        {
            return false;
        }
        cur.cur_wire = static_cast<uint32_t>(tag & 0x7);
        return true;
    }

    void end_element() noexcept override {}

    [[nodiscard]] std::string_view next_key() noexcept override {
        if (err_ || stack_.size() == 0)
        {
            return {};
        }
        frame& cur = stack_.back();
        if (cur.p >= cur.end)
        {
            return {};
        }

        // 读取 tag
        uint64_t tag = pb_detail::read_varint(cur.p, cur.end, err_);
        if (err_)
        {
            return {};
        }

        cur.next_field = static_cast<uint32_t>(tag >> 3);
        cur.cur_wire = static_cast<uint32_t>(tag & 0x7);
        cur.pending_key = {};  // 清除缓存

        // 生成字段名
        cur_key_ = field_name(cur.next_field);
        return cur_key_;
    }

    [[nodiscard]] bool read_bool() noexcept override {
        return pb_detail::read_varint(cur_p(), cur_end(), err_) != 0;
    }

    [[nodiscard]] int32_t read_i32() noexcept override {
        uint32_t v = static_cast<uint32_t>(pb_detail::read_varint(cur_p(), cur_end(), err_));
        return pb_detail::zigzag_decode32(v);
    }

    [[nodiscard]] uint32_t read_u32() noexcept override {
        return static_cast<uint32_t>(pb_detail::read_varint(cur_p(), cur_end(), err_));
    }

    [[nodiscard]] int64_t read_i64() noexcept override {
        uint64_t v = pb_detail::read_varint(cur_p(), cur_end(), err_);
        return pb_detail::zigzag_decode64(v);
    }

    [[nodiscard]] uint64_t read_u64() noexcept override {
        return pb_detail::read_varint(cur_p(), cur_end(), err_);
    }

    [[nodiscard]] float read_f32() noexcept override {
        if (cur_p() + 4 > cur_end())
        {
            err_ = true;
            return 0.0f;
        }
        uint32_t raw = detail::read_le<uint32_t>(cur_p());
        advance(4);
        float v;
        std::memcpy(&v, &raw, 4);
        return v;
    }

    [[nodiscard]] double read_f64() noexcept override {
        if (cur_p() + 8 > cur_end())
        {
            err_ = true;
            return 0.0;
        }
        uint64_t raw = detail::read_le<uint64_t>(cur_p());
        advance(8);
        double v;
        std::memcpy(&v, &raw, 8);
        return v;
    }

    [[nodiscard]] std::string_view read_string_view() noexcept override {
        uint64_t len = pb_detail::read_varint(cur_p(), cur_end(), err_);
        if (err_ || cur_p() + len > cur_end())
        {
            err_ = true;
            return {};
        }
        std::string_view sv(cur_p(), static_cast<size_t>(len));
        advance(static_cast<size_t>(len));
        return sv;  // 零拷贝
    }

    [[nodiscard]] std::string_view read_bytes_view(size_t /*len*/) noexcept override {
        return read_string_view();  // bytes 与 string 同为 length-delimited
    }

    void skip_value() noexcept override {
        if (err_ || stack_.size() == 0)
        {
            return;
        }
        frame& cur = stack_.back();
        switch (cur.cur_wire) {
            case pb_detail::wire_type::varint:
                (void)pb_detail::read_varint(cur.p, cur.end, err_);
                break;
            case pb_detail::wire_type::fixed32:
                cur.p += 4;
                break;
            case pb_detail::wire_type::fixed64:
                cur.p += 8;
                break;
            case pb_detail::wire_type::length_delimited: {
                uint64_t len = pb_detail::read_varint(cur.p, cur.end, err_);
                if (!err_)
                {
                    cur.p += len;
                }
                break;
            }
            default:
                err_ = true;
                break;
        }
    }

    [[nodiscard]] bool has_error() const noexcept override { return err_; }
    [[nodiscard]] operating_message last_error() const noexcept override { return err_msg_; }
    [[nodiscard]] archive_type peek_type() const noexcept override {
        return archive_type::bytes_t;
    }

private:
    std::string cur_key_;

    const char*& cur_p() noexcept { return stack_.back().p; }
    const char* cur_end() noexcept { return stack_.back().end; }
    void advance(size_t n) noexcept { stack_.back().p += n; }
};

// 给 frame 添加 cur_wire 字段 - 修正: 在 struct 中补充
// (实际已在 struct 内, 此处仅声明)

// ============================================================================
// protobuf_codec — Protobuf 编解码器工厂
// ============================================================================
struct protobuf_codec final : archive_codec
{
    protobuf_codec() noexcept {
        magic[0] = 'L'; magic[1] = 'C'; magic[2] = 'P'; magic[3] = 'B';
    }

    [[nodiscard]] archive_writer* create_writer() const noexcept override {
        return new (std::nothrow) protobuf_archive_writer();
    }
    [[nodiscard]] archive_reader* create_reader(std::string_view data) const noexcept override {
        return new (std::nothrow) protobuf_archive_reader(data);
    }
    void destroy_writer(archive_writer* w) const noexcept override { delete w; }
    void destroy_reader(archive_reader* r) const noexcept override { delete r; }
};

