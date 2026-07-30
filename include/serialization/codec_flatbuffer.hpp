// codec_flatbuffer.hpp - FlatBuffers 风格偏移表编码器 (自研, 不依赖 flatbuffers 库)
// magic: "LCFB"
// 设计: vtable + data section, 读取时零拷贝 O(1) 按字段编号访问
// 与真实 FlatBuffers 格式不兼容, 但实现其零拷贝设计理念
#pragma once

#include "archive_codec.hpp"
#include "safety.hpp"
#include "../part/operating_message.hpp"
#include "../part/dense.hpp"
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>

namespace ecs {

// ============================================================================
// 布局设计:
//   [Header 16B]  magic(4) + version(4) + vtable_off(4) + data_size(4)
//   [Data Section]  实际数据 (标量内联, 字符串/bytes 存偏移)
//   [VTable]      field_id → data_offset 映射表 (写入时构建, 读取时查找)
//
// 读取流程 (零拷贝):
//   1. 解析 Header, 定位 VTable
//   2. 按字段编号查 VTable 得 data_offset
//   3. 直接从 Data Section 读取, 无需解析
// ============================================================================

namespace fb_detail {

// LE 写入辅助: 追加到 buf
inline void append_le32(std::string& buf, uint32_t v) noexcept {
    size_t old = buf.size();
    buf.resize(old + 4);
    detail::write_le(v, buf.data() + old);
}

inline void append_le64(std::string& buf, uint64_t v) noexcept {
    size_t old = buf.size();
    buf.resize(old + 8);
    detail::write_le(v, buf.data() + old);
}

// LE 写入到固定缓冲区
inline void write_le32(char* out, uint32_t v) noexcept { detail::write_le(v, out); }
inline void write_le64(char* out, uint64_t v) noexcept { detail::write_le(v, out); }

struct header {
    char     magic[4];      // "LCFB"
    uint32_t version;        // 格式版本
    uint32_t vtable_offset; // VTable 相对起始的偏移
    uint32_t data_size;     // Data Section 大小
};
static_assert(sizeof(header) == 16);

// VTable 条目: field_id (4B) + offset (4B) + type (1B) + size (3B)
struct vtable_entry {
    uint32_t field_id;   // 字段编号 (从 1 开始)
    uint32_t offset;     // 相对 Data Section 起始的偏移
    uint8_t  type;       // archive_type
    uint8_t  reserved[3];
    uint32_t size;       // 数据大小 (0 表示标量, 用 type 判断)
};
static_assert(sizeof(vtable_entry) == 16);

} // namespace fb_detail

// ============================================================================
// flatbuffer_archive_writer — 两阶段写入器
// 阶段 1: 收集字段数据 + 元数据
// 阶段 2: take() 时组装 vtable + data section
// ============================================================================
class flatbuffer_archive_writer final : public archive_writer
{
    struct field_entry {
        uint32_t field_id = 0;   // 字段编号 (从 1 开始, 按 key 顺序递增)
        uint8_t  type = 0;       // archive_type
        uint32_t size = 0;       // 数据大小 (标量为 sizeof, 字符串为长度)
        std::string data;        // 字段数据 (标量序列化为字节)
    };

    struct frame {
        dense<field_entry> fields;
        uint32_t next_field_id = 1;  // 当前 frame 下一个字段编号
    };

    dense<frame> stack_;
    bool root_begin_ = false;  // 根 begin_object 是否已调用

public:
    flatbuffer_archive_writer() noexcept {
        stack_.increase_capacity(8);
        stack_.push_back(frame{});
    }

    void begin_object() noexcept override {
        // 第一个 begin_object (根): no-op, 标记已开始
        if (stack_.size() == 1 && !root_begin_) {
            root_begin_ = true;
            return;
        }
        stack_.push_back(frame{});
    }

    void end_object() noexcept override {
        if (stack_.size() == 0) return;
        // 根 end_object: no-op, 由 take() 处理
        if (stack_.size() == 1) {
            return;
        }
        // 嵌套对象: 序列化为 bytes, 作为父 frame 的一个字段
        frame cur = std::move(stack_.back());
        stack_.pop_back();

        // 组装当前 message 的 data + vtable
        std::string msg_data = assemble_frame(cur);

        // 作为父 frame 的 length-delimited 字段
        frame& parent = stack_.back();
        field_entry fe;
        fe.field_id = parent.next_field_id++;
        fe.type = static_cast<uint8_t>(archive_type::bytes_t);
        fe.size = static_cast<uint32_t>(msg_data.size());
        fe.data = std::move(msg_data);
        parent.fields.push_back(std::move(fe));
    }

    void begin_array(size_t /*count*/) noexcept override {
        // 数组作为 length-delimited: push frame 收集元素
        stack_.push_back(frame{});
    }

    void end_array() noexcept override {
        if (stack_.size() <= 1) return;
        frame cur = std::move(stack_.back());
        stack_.pop_back();
        std::string msg_data = assemble_frame(cur);
        frame& parent = stack_.back();
        field_entry fe;
        fe.field_id = parent.next_field_id++;
        fe.type = static_cast<uint8_t>(archive_type::bytes_t);
        fe.size = static_cast<uint32_t>(msg_data.size());
        fe.data = std::move(msg_data);
        parent.fields.push_back(std::move(fe));
    }

    void key(std::string_view /*k*/) noexcept override {
        // 字段名忽略, 用 field_id 顺序编号
    }

    void write_bool(bool v) noexcept override { add_scalar(archive_type::bool_t, &v, 1); }
    void write_i32(int32_t v) noexcept override { add_scalar(archive_type::int32_t, &v, 4); }
    void write_u32(uint32_t v) noexcept override { add_scalar(archive_type::uint32_t, &v, 4); }
    void write_i64(int64_t v) noexcept override { add_scalar(archive_type::int64_t, &v, 8); }
    void write_u64(uint64_t v) noexcept override { add_scalar(archive_type::uint64_t, &v, 8); }
    void write_f32(float v) noexcept override { add_scalar(archive_type::float32_t, &v, 4); }
    void write_f64(double v) noexcept override { add_scalar(archive_type::float64_t, &v, 8); }

    void write_string(std::string_view v) noexcept override {
        add_length_delimited(archive_type::string_t, v.data(), v.size());
    }

    void write_bytes(const void* data, size_t len) noexcept override {
        add_length_delimited(archive_type::bytes_t, data, len);
    }

    void write_raw(std::string_view fragment) noexcept override {
        // raw 写入: 作为 bytes 字段
        add_length_delimited(archive_type::bytes_t, fragment.data(), fragment.size());
    }

    [[nodiscard]] std::string take() noexcept override {
        if (stack_.size() == 0) return {};

        // 组装根 frame
        frame root = std::move(stack_.back());
        stack_.clear();
        std::string data = assemble_frame(root);

        // 添加 magic header
        std::string result;
        result.reserve(data.size() + 16);
        result.append("LCFB", 4);
        // version
        fb_detail::append_le32(result, 1);
        // vtable_offset (Header 之后就是 vtable, 这里简化: vtable 在 data 之前)
        fb_detail::append_le32(result, 16);
        // data_size
        fb_detail::append_le32(result, static_cast<uint32_t>(data.size()));
        // data section
        result.append(data);
        return result;
    }

    [[nodiscard]] size_t size() const noexcept override {
        size_t s = 16;
        if (stack_.size() > 0) {
            for (size_t i = 0; i < stack_.back().fields.size(); ++i) {
                s += stack_.back().fields[i].data.size();
            }
        }
        return s;
    }

private:
    void add_scalar(archive_type type, const void* data, size_t size) noexcept {
        if (stack_.size() == 0) return;
        frame& f = stack_.back();
        field_entry fe;
        fe.field_id = f.next_field_id++;
        fe.type = static_cast<uint8_t>(type);
        fe.size = static_cast<uint32_t>(size);
        fe.data.assign(static_cast<const char*>(data), size);
        f.fields.push_back(std::move(fe));
    }

    void add_length_delimited(archive_type type, const void* data, size_t size) noexcept {
        if (stack_.size() == 0) return;
        frame& f = stack_.back();
        field_entry fe;
        fe.field_id = f.next_field_id++;
        fe.type = static_cast<uint8_t>(type);
        fe.size = static_cast<uint32_t>(size);
        fe.data.assign(static_cast<const char*>(data), size);
        f.fields.push_back(std::move(fe));
    }

    // 组装单个 frame 的 data + vtable
    std::string assemble_frame(const frame& f) const noexcept {
        std::string data_section;
        std::string vtable_section;

        uint32_t field_count = static_cast<uint32_t>(f.fields.size());
        // vtable: count(4B) + entries[count]
        fb_detail::append_le32(vtable_section, field_count);

        for (size_t i = 0; i < f.fields.size(); ++i) {
            const auto& fe = f.fields[i];
            uint32_t offset = static_cast<uint32_t>(data_section.size());

            // 写入 data section
            if (fe.type == static_cast<uint8_t>(archive_type::string_t) ||
                fe.type == static_cast<uint8_t>(archive_type::bytes_t)) {
                // length-delimited: 先长度后数据
                fb_detail::append_le32(data_section, fe.size);
                data_section.append(fe.data);
            } else {
                // 标量: 直接写入 (LE)
                data_section.append(fe.data);
            }

            // 写入 vtable entry
            fb_detail::vtable_entry ve;
            ve.field_id = fe.field_id;
            ve.offset = offset;
            ve.type = fe.type;
            ve.reserved[0] = ve.reserved[1] = ve.reserved[2] = 0;
            ve.size = fe.size;
            vtable_section.append(reinterpret_cast<const char*>(&ve), sizeof(ve));
        }

        // 最终: vtable + data
        vtable_section.append(data_section);
        return vtable_section;
    }
};

// ============================================================================
// flatbuffer_archive_reader — 零拷贝读取器
// O(1) 按字段编号访问, 无需解析整个文档
// ============================================================================
class flatbuffer_archive_reader final : public archive_reader
{
    const char* base_ = nullptr;    // 指向 Header 之后
    const char* vtable_ = nullptr;  // vtable 起始
    const char* data_ = nullptr;    // data section 起始
    uint32_t   field_count_ = 0;
    bool       err_ = false;
    operating_message err_msg_;

    // 当前对象栈 (用于嵌套)
    struct frame {
        const char* vtable;
        const char* data;
        uint32_t   field_count;
        uint32_t   cur_field_idx;  // next_key 迭代用
        bool       is_root = false;
    };
    dense<frame> stack_;
    uint32_t cur_lookup_field_ = 0;  // next_key 返回的字段编号
    const fb_detail::vtable_entry* cur_entry_ = nullptr;
    bool root_entered_ = false;

public:
    explicit flatbuffer_archive_reader(std::string_view data) noexcept {
        if (data.size() < sizeof(fb_detail::header)) {
            err_ = true;
            err_msg_.write_message(false, "flatbuffer: 数据过短");
            return;
        }

        fb_detail::header hdr;
        std::memcpy(&hdr, data.data(), sizeof(hdr));

        if (std::memcmp(hdr.magic, "LCFB", 4) != 0) {
            err_ = true;
            err_msg_.write_message(false, "flatbuffer: magic 不匹配");
            return;
        }

        // 读取 LE 字段
        uint32_t version = detail::read_le<uint32_t>(data.data() + 4);
        uint32_t vtable_off = detail::read_le<uint32_t>(data.data() + 8);
        uint32_t data_size = detail::read_le<uint32_t>(data.data() + 12);
        (void)version;
        (void)data_size;

        const char* vtable_start = data.data() + vtable_off;
        field_count_ = detail::read_le<uint32_t>(vtable_start);
        const char* data_start = vtable_start + 4 + field_count_ * sizeof(fb_detail::vtable_entry);

        base_ = data.data();
        vtable_ = vtable_start;
        data_ = data_start;

        stack_.increase_capacity(8);
        frame root;
        root.vtable = vtable_;
        root.data = data_;
        root.field_count = field_count_;
        root.cur_field_idx = 0;
        root.is_root = true;
        stack_.push_back(root);
    }

    bool enter_object() noexcept override {
        if (err_) return false;
        // 根 frame: enter_object 是 no-op (已在构造时进入)
        if (stack_.size() == 1 && stack_.back().is_root && !root_entered_) {
            root_entered_ = true;
            return true;
        }
        // 嵌套 message: 当前字段是 bytes, 作为子 message 读取
        if (!cur_entry_) return false;
        const char* sub_data = stack_.back().data + cur_entry_->offset;
        uint32_t sub_len = detail::read_le<uint32_t>(sub_data);  // 子消息总长 (校验用)
        (void)sub_len;
        sub_data += 4;  // 跳过长度前缀

        const char* sub_vtable = sub_data;
        uint32_t sub_field_count = detail::read_le<uint32_t>(sub_vtable);
        const char* sub_data_start = sub_vtable + 4 + sub_field_count * sizeof(fb_detail::vtable_entry);

        frame sub;
        sub.vtable = sub_vtable;
        sub.data = sub_data_start;
        sub.field_count = sub_field_count;
        sub.cur_field_idx = 0;
        stack_.push_back(sub);
        return true;
    }

    void leave_object() noexcept override {
        if (stack_.size() > 1) stack_.pop_back();
    }

    bool enter_array() noexcept override {
        if (err_) return false;
        // 数组是 bytes 字段 (length-delimited): 直接走嵌套逻辑, 不走根分支
        if (!cur_entry_) return false;
        const char* sub_data = stack_.back().data + cur_entry_->offset;
        uint32_t sub_len = detail::read_le<uint32_t>(sub_data);
        (void)sub_len;
        sub_data += 4;
        const char* sub_vtable = sub_data;
        uint32_t sub_field_count = detail::read_le<uint32_t>(sub_vtable);
        const char* sub_data_start = sub_vtable + 4 + sub_field_count * sizeof(fb_detail::vtable_entry);
        frame sub;
        sub.vtable = sub_vtable;
        sub.data = sub_data_start;
        sub.field_count = sub_field_count;
        sub.cur_field_idx = 0;
        stack_.push_back(sub);
        return true;
    }

    void leave_array() noexcept override {
        leave_object();
    }

    bool next_element() noexcept override {
        if (err_ || stack_.size() == 0) return false;
        frame& cur = stack_.back();
        if (cur.cur_field_idx >= cur.field_count) return false;
        // 读取 vtable entry (类似 next_key, 但不返回 key)
        const char* entry_ptr = cur.vtable + 4 + cur.cur_field_idx * sizeof(fb_detail::vtable_entry);
        cur_entry_ = reinterpret_cast<const fb_detail::vtable_entry*>(entry_ptr);
        ++cur.cur_field_idx;
        return true;
    }

    void end_element() noexcept override {}

    [[nodiscard]] std::string_view next_key() noexcept override {
        if (err_ || stack_.size() == 0) return {};
        frame& cur = stack_.back();
        if (cur.cur_field_idx >= cur.field_count) return {};

        // 读取 vtable entry
        const char* entry_ptr = cur.vtable + 4 + cur.cur_field_idx * sizeof(fb_detail::vtable_entry);
        cur_entry_ = reinterpret_cast<const fb_detail::vtable_entry*>(entry_ptr);
        cur_lookup_field_ = cur_entry_->field_id;
        ++cur.cur_field_idx;

        // 生成字段名 "f<field_id>"
        char buf[16];
        int n = std::snprintf(buf, sizeof(buf), "f%u", cur_lookup_field_);
        cur_key_ = std::string(buf, static_cast<size_t>(n));
        return cur_key_;
    }

    [[nodiscard]] bool read_bool() noexcept override {
        if (!cur_entry_) return false;
        return *reinterpret_cast<const uint8_t*>(stack_.back().data + cur_entry_->offset) != 0;
    }

    [[nodiscard]] int32_t read_i32() noexcept override {
        if (!cur_entry_) return 0;
        return detail::read_le<int32_t>(stack_.back().data + cur_entry_->offset);
    }

    [[nodiscard]] uint32_t read_u32() noexcept override {
        if (!cur_entry_) return 0;
        return detail::read_le<uint32_t>(stack_.back().data + cur_entry_->offset);
    }

    [[nodiscard]] int64_t read_i64() noexcept override {
        if (!cur_entry_) return 0;
        return detail::read_le<int64_t>(stack_.back().data + cur_entry_->offset);
    }

    [[nodiscard]] uint64_t read_u64() noexcept override {
        if (!cur_entry_) return 0;
        return detail::read_le<uint64_t>(stack_.back().data + cur_entry_->offset);
    }

    [[nodiscard]] float read_f32() noexcept override {
        if (!cur_entry_) return 0.0f;
        uint32_t raw = detail::read_le<uint32_t>(stack_.back().data + cur_entry_->offset);
        float v;
        std::memcpy(&v, &raw, 4);
        return v;
    }

    [[nodiscard]] double read_f64() noexcept override {
        if (!cur_entry_) return 0.0;
        uint64_t raw = detail::read_le<uint64_t>(stack_.back().data + cur_entry_->offset);
        double v;
        std::memcpy(&v, &raw, 8);
        return v;
    }

    [[nodiscard]] std::string_view read_string_view() noexcept override {
        if (!cur_entry_) return {};
        // length-delimited: 先读长度
        const char* p = stack_.back().data + cur_entry_->offset;
        uint32_t len = detail::read_le<uint32_t>(p);
        return std::string_view(p + 4, len);  // 零拷贝
    }

    [[nodiscard]] std::string_view read_bytes_view(size_t /*len*/) noexcept override {
        return read_string_view();  // bytes 与 string 同为 length-delimited
    }

    void skip_value() noexcept override {
        // FlatBuffer 风格: 无需 skip, vtable 已记录所有字段位置
        // 未知字段: 不读即可
    }

    [[nodiscard]] bool has_error() const noexcept override { return err_; }
    [[nodiscard]] operating_message last_error() const noexcept override { return err_msg_; }
    [[nodiscard]] archive_type peek_type() const noexcept override {
        return cur_entry_ ? static_cast<archive_type>(cur_entry_->type) : archive_type::null_t;
    }

private:
    std::string cur_key_;
};

// ============================================================================
// flatbuffer_codec — FlatBuffer 风格编解码器工厂
// ============================================================================
struct flatbuffer_codec final : archive_codec
{
    flatbuffer_codec() noexcept {
        magic[0] = 'L'; magic[1] = 'C'; magic[2] = 'F'; magic[3] = 'B';
    }

    [[nodiscard]] archive_writer* create_writer() const noexcept override {
        return new (std::nothrow) flatbuffer_archive_writer();
    }
    [[nodiscard]] archive_reader* create_reader(std::string_view data) const noexcept override {
        return new (std::nothrow) flatbuffer_archive_reader(data);
    }
    void destroy_writer(archive_writer* w) const noexcept override { delete w; }
    void destroy_reader(archive_reader* r) const noexcept override { delete r; }
};

} // namespace ecs
