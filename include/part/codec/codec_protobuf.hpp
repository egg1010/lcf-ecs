// codec_protobuf.hpp - Protobuf 官方 wire format 编码器 (不依赖 libprotobuf)
// 无 magic 前缀: 输出为标准 protobuf message (可直接被官方工具链按 .proto 解析)
// schema 约定 (字段编号固定): 根 f1=version f2=engine f3=meta f4=cv f5=entities f6=components
//   实体 message: f1=i f2=v f3=f f4=t f5=l f6=g
//   实体容器 message: f1=Entity (repeated)
//   组件表 message: 类型数组按注册顺序编号 1..N; 数组容器内 f1=条目 (repeated)
//   组件条目 message: f1=i f2=v f3=d
#pragma once

#include "archive_codec.hpp"
#include "../safety.hpp"
#include "../operating_message.hpp"
#include "../dense.hpp"
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>

namespace pb_detail {

enum wire_type : uint32_t {
    varint           = 0,
    fixed64          = 1,
    length_delimited = 2,
    fixed32          = 5,
};

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
    err = true;
    return 0;
}

inline void write_tag(std::string& buf, uint32_t field_num, uint32_t wt) noexcept {
    write_varint(buf, (static_cast<uint64_t>(field_num) << 3) | wt);
}

inline void write_fixed32(std::string& buf, uint32_t v) noexcept {
    size_t old = buf.size();
    buf.resize(old + 4);
    detail::write_le(v, buf.data() + old);
}

inline void write_fixed64(std::string& buf, uint64_t v) noexcept {
    size_t old = buf.size();
    buf.resize(old + 8);
    detail::write_le(v, buf.data() + old);
}

inline void write_value_field(std::string& buf, uint32_t field, uint32_t wt,
                              uint64_t v) noexcept {
    write_tag(buf, field, wt);
    write_varint(buf, v);
}

} // namespace pb_detail

class protobuf_archive_writer final : public archive_writer
{
    struct frame {
        std::string buf;
        uint32_t    pending = 0;    // key() 指定的字段编号
        uint32_t    field = 0;      // message/array 自身字段编号
        uint32_t    next_dyn = 0;   // 动态字段编号计数
        bool        is_array = false;
    };
    dense<frame> stack_;
    bool root_begin_ = false;

    [[nodiscard]] static uint32_t resolve_field(frame& f, std::string_view /*k*/) noexcept {
        return ++f.next_dyn;
    }

    [[nodiscard]] frame& top() noexcept { return stack_.back(); }

    [[nodiscard]] uint32_t emit_field() noexcept {
        frame& f = top();
        return f.is_array ? 1u : f.pending;
    }

    void emit_scalar(uint32_t wt, uint64_t v) noexcept {
        frame& f = top();
        uint32_t field = emit_field();
        pb_detail::write_tag(f.buf, field, wt);
        if (wt == pb_detail::wire_type::fixed32)
        {
            pb_detail::write_fixed32(f.buf, static_cast<uint32_t>(v));
        }
        else if (wt == pb_detail::wire_type::fixed64)
        {
            pb_detail::write_fixed64(f.buf, v);
        }
        else
        {
            pb_detail::write_varint(f.buf, v);
        }
    }

public:
    protobuf_archive_writer() noexcept {
        stack_.push_back(frame{});
    }

    void begin_object() noexcept override {
        if (stack_.size() == 1 && !root_begin_)
        {
            root_begin_ = true;
            return;
        }
        frame child;
        child.field = emit_field();
        stack_.push_back(std::move(child));
    }

    void end_object() noexcept override {
        if (stack_.size() <= 1)
        {
            return;
        }
        frame cur = std::move(stack_.back());
        stack_.pop_back();
        frame& parent = stack_.back();
        pb_detail::write_tag(parent.buf, cur.field, pb_detail::wire_type::length_delimited);
        pb_detail::write_varint(parent.buf, cur.buf.size());
        parent.buf.append(cur.buf);
    }

    void begin_array(size_t /*count*/) noexcept override {
        frame child;
        child.field = emit_field();
        child.is_array = true;
        stack_.push_back(std::move(child));
    }

    void end_array() noexcept override {
        if (stack_.size() <= 1)
        {
            return;
        }
        frame cur = std::move(stack_.back());
        stack_.pop_back();
        frame& parent = stack_.back();
        pb_detail::write_tag(parent.buf, cur.field, pb_detail::wire_type::length_delimited);
        pb_detail::write_varint(parent.buf, cur.buf.size());
        parent.buf.append(cur.buf);
    }

    void key(std::string_view k) noexcept override {
        top().pending = resolve_field(top(), k);
    }

    void write_bool(bool v) noexcept override { emit_scalar(pb_detail::wire_type::varint, v ? 1 : 0); }
    void write_i32(int32_t v) noexcept override {
        emit_scalar(pb_detail::wire_type::varint,
                    static_cast<uint64_t>(static_cast<int64_t>(v)));
    }
    void write_u32(uint32_t v) noexcept override { emit_scalar(pb_detail::wire_type::varint, v); }
    void write_i64(int64_t v) noexcept override {
        emit_scalar(pb_detail::wire_type::varint, static_cast<uint64_t>(v));
    }
    void write_u64(uint64_t v) noexcept override { emit_scalar(pb_detail::wire_type::varint, v); }

    void write_f32(float v) noexcept override {
        uint32_t raw;
        std::memcpy(&raw, &v, 4);
        emit_scalar(pb_detail::wire_type::fixed32, raw);
    }
    void write_f64(double v) noexcept override {
        uint64_t raw;
        std::memcpy(&raw, &v, 8);
        emit_scalar(pb_detail::wire_type::fixed64, raw);
    }

    void write_string(std::string_view v) noexcept override {
        frame& f = top();
        pb_detail::write_tag(f.buf, emit_field(), pb_detail::wire_type::length_delimited);
        pb_detail::write_varint(f.buf, v.size());
        f.buf.append(v.data(), v.size());
    }

    void write_bytes(const void* data, size_t len) noexcept override {
        write_string(std::string_view(static_cast<const char*>(data), len));
    }

    void write_raw(std::string_view fragment) noexcept override {
        write_string(fragment);
    }

    [[nodiscard]] std::string take() noexcept override {
        return std::move(stack_[0].buf);
    }

    [[nodiscard]] size_t size() const noexcept override {
        size_t s = 0;
        for (size_t i = 0; i < stack_.size(); ++i)
        {
            s += stack_[i].buf.size();
        }
        return s;
    }
};

class protobuf_archive_reader final : public archive_reader
{
    struct frame {
        const char* p;
        const char* end;
        bool        is_array = false;
    };
    dense<frame> stack_;
    bool        err_ = false;
    bool        root_entered_ = false;
    uint32_t    cur_wire_ = 0;
    std::string cur_key_;

    [[nodiscard]] static std::string field_name(uint32_t num) noexcept {
        char buf[16];
        int n = std::snprintf(buf, sizeof(buf), "f%u", num);
        return std::string(buf, static_cast<size_t>(n));
    }

    bool push_sub() noexcept {
        frame& cur = stack_.back();
        uint64_t len = pb_detail::read_varint(cur.p, cur.end, err_);
        if (err_ || len > static_cast<uint64_t>(cur.end - cur.p))
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

public:
    explicit protobuf_archive_reader(std::string_view data) noexcept {
        stack_.increase_capacity(8);
        frame root;
        root.p = data.data();
        root.end = data.data() + data.size();
        stack_.push_back(root);
    }

    bool enter_object() noexcept override {
        if (err_)
        {
            return false;
        }
        if (!root_entered_)
        {
            root_entered_ = true;
            return true;
        }
        return push_sub();
    }

    void leave_object() noexcept override {
        if (stack_.size() > 1)
        {
            stack_.pop_back();
        }
    }

    bool enter_array() noexcept override {
        if (err_)
        {
            return false;
        }
        if (!root_entered_)
        {
            root_entered_ = true;
            return true;
        }
        if (!push_sub())
        {
            return false;
        }
        stack_.back().is_array = true;
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
        uint64_t tag = pb_detail::read_varint(cur.p, cur.end, err_);
        if (err_)
        {
            return false;
        }
        if ((tag >> 3) != 1)
        {
            err_ = true;
            return false;
        }
        cur_wire_ = static_cast<uint32_t>(tag & 0x7);
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
        uint64_t tag = pb_detail::read_varint(cur.p, cur.end, err_);
        if (err_)
        {
            return {};
        }
        cur_wire_ = static_cast<uint32_t>(tag & 0x7);
        cur_key_ = field_name(static_cast<uint32_t>(tag >> 3));
        return cur_key_;
    }

    [[nodiscard]] bool read_bool() noexcept override {
        frame& cur = stack_.back();
        return pb_detail::read_varint(cur.p, cur.end, err_) != 0;
    }

    [[nodiscard]] int32_t read_i32() noexcept override {
        frame& cur = stack_.back();
        return static_cast<int32_t>(pb_detail::read_varint(cur.p, cur.end, err_));
    }

    [[nodiscard]] uint32_t read_u32() noexcept override {
        frame& cur = stack_.back();
        return static_cast<uint32_t>(pb_detail::read_varint(cur.p, cur.end, err_));
    }

    [[nodiscard]] int64_t read_i64() noexcept override {
        frame& cur = stack_.back();
        return static_cast<int64_t>(pb_detail::read_varint(cur.p, cur.end, err_));
    }

    [[nodiscard]] uint64_t read_u64() noexcept override {
        frame& cur = stack_.back();
        return pb_detail::read_varint(cur.p, cur.end, err_);
    }

    [[nodiscard]] float read_f32() noexcept override {
        frame& cur = stack_.back();
        if (cur.p + 4 > cur.end)
        {
            err_ = true;
            return 0.0f;
        }
        uint32_t raw = detail::read_le<uint32_t>(cur.p);
        cur.p += 4;
        float v;
        std::memcpy(&v, &raw, 4);
        return v;
    }

    [[nodiscard]] double read_f64() noexcept override {
        frame& cur = stack_.back();
        if (cur.p + 8 > cur.end)
        {
            err_ = true;
            return 0.0;
        }
        uint64_t raw = detail::read_le<uint64_t>(cur.p);
        cur.p += 8;
        double v;
        std::memcpy(&v, &raw, 8);
        return v;
    }

    [[nodiscard]] std::string_view read_string_view() noexcept override {
        frame& cur = stack_.back();
        uint64_t len = pb_detail::read_varint(cur.p, cur.end, err_);
        if (err_ || len > static_cast<uint64_t>(cur.end - cur.p))
        {
            err_ = true;
            return {};
        }
        std::string_view sv(cur.p, static_cast<size_t>(len));
        cur.p += len;
        return sv;
    }

    [[nodiscard]] std::string_view read_bytes_view(size_t /*len*/) noexcept override {
        return read_string_view();
    }

    void skip_value() noexcept override {
        if (err_ || stack_.size() == 0)
        {
            return;
        }
        frame& cur = stack_.back();
        switch (cur_wire_)
        {
            case pb_detail::wire_type::varint:
                (void)pb_detail::read_varint(cur.p, cur.end, err_);
                break;
            case pb_detail::wire_type::fixed32:
                if (cur.p + 4 > cur.end) { err_ = true; return; }
                cur.p += 4;
                break;
            case pb_detail::wire_type::fixed64:
                if (cur.p + 8 > cur.end) { err_ = true; return; }
                cur.p += 8;
                break;
            case pb_detail::wire_type::length_delimited:
            {
                uint64_t len = pb_detail::read_varint(cur.p, cur.end, err_);
                if (err_ || len > static_cast<uint64_t>(cur.end - cur.p)) { err_ = true; return; }
                cur.p += len;
                break;
            }
            default:
                err_ = true;
                break;
        }
    }

    [[nodiscard]] bool has_error() const noexcept override { return err_; }
    [[nodiscard]] operating_message last_error() const noexcept override {
        operating_message m;
        if (err_)
        {
            m.write(false, "protobuf: wire format 解析错误");
        }
        return m;
    }
    [[nodiscard]] archive_type peek_type() const noexcept override {
        return archive_type::bytes_t;
    }
};

// ============================================================================
// protobuf_codec — 无 magic, 采用结构判定 (尝试按 wire format 解析整个 message)
// ============================================================================
struct protobuf_codec final : archive_codec
{
    protobuf_codec() noexcept {
        magic[0] = 0; magic[1] = 0; magic[2] = 0; magic[3] = 0;
    }

    [[nodiscard]] archive_writer* create_writer() const noexcept override {
        return new (std::nothrow) protobuf_archive_writer();
    }
    [[nodiscard]] archive_reader* create_reader(std::string_view data) const noexcept override {
        return new (std::nothrow) protobuf_archive_reader(data);
    }
    void destroy_writer(archive_writer* w) const noexcept override { delete w; }
    void destroy_reader(archive_reader* r) const noexcept override { delete r; }

    [[nodiscard]] bool matches(std::string_view data) const noexcept override {
        if (data.empty())
        {
            return false;
        }
        if (data.size() >= 8 && data[4] == 'L' && data[5] == 'C' && data[6] == 'F' && data[7] == 'B')
        {
            return false;
        }
        const char* p = data.data();
        const char* end = p + data.size();
        bool err = false;
        size_t fields = 0;
        while (p < end)
        {
            uint64_t tag = pb_detail::read_varint(p, end, err);
            if (err)
            {
                return false;
            }
            uint32_t field = static_cast<uint32_t>(tag >> 3);
            uint32_t wt = static_cast<uint32_t>(tag & 0x7);
            if (field == 0)
            {
                return false;
            }
            ++fields;
            switch (wt)
            {
                case pb_detail::wire_type::varint:
                    (void)pb_detail::read_varint(p, end, err);
                    break;
                case pb_detail::wire_type::fixed32:
                    if (p + 4 > end) { return false; }
                    p += 4;
                    break;
                case pb_detail::wire_type::fixed64:
                    if (p + 8 > end) { return false; }
                    p += 8;
                    break;
                case pb_detail::wire_type::length_delimited:
                {
                    uint64_t len = pb_detail::read_varint(p, end, err);
                    if (err || len > static_cast<uint64_t>(end - p)) { return false; }
                    p += len;
                    break;
                }
                default:
                    return false;
            }
            if (err)
            {
                return false;
            }
        }
        return fields > 0;
    }

    [[nodiscard]] operating_message validate(std::string_view data) const noexcept override {
        operating_message m;
        if (!matches(data))
        {
            m.write(false, "protobuf wire format 校验失败");
        }
        return m;
    }
};
