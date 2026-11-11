
#pragma once

#include "archive_codec.hpp"
#include "../bit_ops.hpp"
#include "../safety.hpp"
#include "../operating_message.hpp"
#include "../dense.hpp"
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string>
#include <string_view>

namespace fb_detail {

constexpr uint32_t NONE = 0xFFFFFFFFu;

struct node {
    uint8_t  kind;         // 0 标量 1 string 2 bytes 3 table 4 vector
    uint8_t  atype;        // archive_type
    uint32_t field_id;
    uint32_t data_off;
    uint32_t data_len;
    uint32_t first_child;
    uint32_t last_child;
    uint32_t next_sibling;
};

} // namespace fb_detail

class flatbuffer_archive_writer final : public archive_writer
{
    dense<fb_detail::node> nodes_;
    dense<char>            pool_;
    dense<uint32_t>        stack_;
    dense<uint32_t>        dyn_;
    uint32_t               pending_ = 0;
    bool                   root_begin_ = false;
    std::string            out_;



    [[nodiscard]] uint32_t new_node(uint8_t kind, uint8_t atype, uint32_t field_id) noexcept {
        fb_detail::node n{};
        n.kind = kind;
        n.atype = atype;
        n.field_id = field_id;
        n.first_child = fb_detail::NONE;
        n.last_child = fb_detail::NONE;
        n.next_sibling = fb_detail::NONE;
        nodes_.emplace_back(n);
        return static_cast<uint32_t>(nodes_.size() - 1);
    }

    void add_child(uint32_t parent, uint32_t child) noexcept {
        auto& p = nodes_[parent];
        if (p.first_child == fb_detail::NONE)
        {
            p.first_child = child;
        }
        else
        {
            nodes_[p.last_child].next_sibling = child;
        }
        p.last_child = child;
    }

    uint32_t add_scalar(uint8_t atype, const void* data, size_t size) noexcept {
        uint32_t off = static_cast<uint32_t>(pool_.size());
        pool_.resize_fill(pool_.size() + size, 0);
        std::memcpy(&pool_[off], data, size);
        uint32_t n = new_node(0, atype, pending_);
        auto& node = nodes_[n];
        node.data_off = off;
        node.data_len = static_cast<uint32_t>(size);
        add_child(stack_.back(), n);
        return n;
    }

    uint32_t add_blob(uint8_t kind, uint8_t atype, const void* data, size_t size) noexcept {
        uint32_t off = static_cast<uint32_t>(pool_.size());
        pool_.resize_fill(pool_.size() + size, 0);
        if (size > 0) { std::memcpy(&pool_[off], data, size); }
        uint32_t n = new_node(kind, atype, pending_);
        auto& node = nodes_[n];
        node.data_off = off;
        node.data_len = static_cast<uint32_t>(size);
        add_child(stack_.back(), n);
        return n;
    }

    void put_u16(uint32_t pos, uint16_t v) noexcept { detail::write_le(v, &out_[pos]); }
    void put_u32(uint32_t pos, uint32_t v) noexcept { detail::write_le(v, &out_[pos]); }
    void put_i32(uint32_t pos, int32_t v) noexcept { detail::write_le(v, &out_[pos]); }

    uint32_t place(uint32_t idx) noexcept {
        fb_detail::node& n = nodes_[idx];
        if (n.kind == 0)
        {
            out_.resize(align_up(static_cast<uint32_t>(out_.size()), n.data_len), 0);
            uint32_t pos = static_cast<uint32_t>(out_.size());
            if (n.data_len > 0) { out_.append(&pool_[n.data_off], n.data_len); }
            return pos;
        }
        if (n.kind == 1 || n.kind == 2)
        {
            out_.resize(align_up(static_cast<uint32_t>(out_.size()), 4), 0);
            uint32_t pos = static_cast<uint32_t>(out_.size());
            char lenbuf[4];
            detail::write_le<uint32_t>(n.data_len, lenbuf);
            out_.append(lenbuf, 4);
            if (n.data_len > 0) { out_.append(&pool_[n.data_off], n.data_len); }
            if (n.kind == 1) { out_.push_back('\0'); }
            return pos;
        }
        if (n.kind == 4)
        {
            dense<uint32_t> elems;
            uint32_t elem_size = 4;
            for (uint32_t c = n.first_child; c != fb_detail::NONE; c = nodes_[c].next_sibling)
            {
                elems.emplace_back(c);
                if (nodes_[c].kind == 0)
                {
                    elem_size = nodes_[c].data_len;
                }
            }
            uint32_t base_align = (elem_size >= 4) ? elem_size : 4;
            out_.resize(align_up(static_cast<uint32_t>(out_.size()), base_align), 0);
            uint32_t vec_pos = static_cast<uint32_t>(out_.size());
            uint32_t elems_off = static_cast<uint32_t>(align_up(vec_pos + 4, elem_size));
            uint32_t total = (elems_off - vec_pos) + static_cast<uint32_t>(elems.size()) * elem_size;
            out_.resize(vec_pos + total, 0);
            put_u32(vec_pos, static_cast<uint32_t>(elems.size()));
            for (size_t i = 0; i < elems.size(); ++i)
            {
                uint32_t child = elems[i];
                uint32_t at = elems_off + static_cast<uint32_t>(i) * elem_size;
                if (nodes_[child].kind == 0)
                {
                    std::memcpy(&out_[at], &pool_[nodes_[child].data_off], elem_size);
                }
                else
                {
                    uint32_t coff = place(child);
                    put_u32(at, coff - at);
                }
            }
            return vec_pos;
        }
        // table
        dense<uint32_t> fields;
        uint32_t max_field = 0;
        uint32_t max_align = 4;
        for (uint32_t c = n.first_child; c != fb_detail::NONE; c = nodes_[c].next_sibling)
        {
            fields.emplace_back(c);
            if (nodes_[c].field_id > max_field)
            {
                max_field = nodes_[c].field_id;
            }
            uint32_t a = (nodes_[c].kind == 0) ? nodes_[c].data_len : 4;
            if (a > max_align)
            {
                max_align = a;
            }
        }
        uint32_t slot_count = max_field;
        uint32_t vtable_size = 4 + slot_count * 2;

        // 计算各字段内联布局
        dense<uint32_t> field_off;
        for (size_t i = 0; i < fields.size(); ++i) { field_off.emplace_back(uint32_t{0}); }
        uint32_t body = 4;
        for (size_t i = 0; i < fields.size(); ++i)
        {
            uint32_t c = fields[i];
            uint32_t a = (nodes_[c].kind == 0) ? nodes_[c].data_len : 4;
            body = static_cast<uint32_t>(align_up(body, a));
            field_off[i] = body;
            body += a;
        }
        uint32_t table_size = static_cast<uint32_t>(align_up(body, max_align));

        // 先放 vtable, 再放 table
        out_.resize(align_up(static_cast<uint32_t>(out_.size()), 2), 0);
        uint32_t vtable_pos = static_cast<uint32_t>(out_.size());
        out_.resize(vtable_pos + vtable_size, 0);
        put_u16(vtable_pos, static_cast<uint16_t>(vtable_size));
        put_u16(vtable_pos + 2, static_cast<uint16_t>(table_size));

        out_.resize(align_up(static_cast<uint32_t>(out_.size()), max_align), 0);
        uint32_t table_pos = static_cast<uint32_t>(out_.size());
        out_.resize(table_pos + table_size, 0);
        put_i32(table_pos, static_cast<int32_t>(table_pos - vtable_pos));

        for (size_t i = 0; i < fields.size(); ++i)
        {
            uint32_t c = fields[i];
            uint32_t slot = nodes_[c].field_id - 1;
            put_u16(vtable_pos + 4 + slot * 2, static_cast<uint16_t>(field_off[i]));
        }

        for (size_t i = 0; i < fields.size(); ++i)
        {
            uint32_t c = fields[i];
            uint32_t at = table_pos + field_off[i];
            if (nodes_[c].kind == 0)
            {
                std::memcpy(&out_[at], &pool_[nodes_[c].data_off], nodes_[c].data_len);
            }
            else
            {
                uint32_t coff = place(c);
                put_u32(at, coff - at);
            }
        }
        return table_pos;
    }

public:
    flatbuffer_archive_writer() noexcept {
        stack_.increase_capacity(16);
        dyn_.increase_capacity(16);
        uint32_t root = new_node(3, 0, 0);
        stack_.emplace_back(root);
        dyn_.emplace_back(uint32_t{0});
    }

    void begin_object() noexcept override {
        if (!root_begin_)
        {
            root_begin_ = true;
            return;
        }
        uint32_t fid = (nodes_[stack_.back()].kind == 4) ? 0 : pending_;
        uint32_t n = new_node(3, 0, fid);
        add_child(stack_.back(), n);
        stack_.emplace_back(n);
        dyn_.emplace_back(uint32_t{0});
    }

    void end_object() noexcept override {
        if (stack_.size() > 1)
        {
            stack_.pop_back();
            dyn_.pop_back();
        }
    }

    void begin_array(size_t /*count*/) noexcept override {
        uint32_t fid = (nodes_[stack_.back()].kind == 4) ? 0 : pending_;
        uint32_t n = new_node(4, 0, fid);
        add_child(stack_.back(), n);
        stack_.emplace_back(n);
        dyn_.emplace_back(uint32_t{0});
    }

    void end_array() noexcept override {
        if (stack_.size() > 1)
        {
            stack_.pop_back();
            dyn_.pop_back();
        }
    }

    void key(std::string_view /*k*/) noexcept override {
        pending_ = ++dyn_.back();
    }

    void write_bool(bool v) noexcept override { add_scalar(static_cast<uint8_t>(archive_type::bool_t), &v, 1); }
    void write_i32(int32_t v) noexcept override { add_scalar(static_cast<uint8_t>(archive_type::i32), &v, 4); }
    void write_u32(uint32_t v) noexcept override { add_scalar(static_cast<uint8_t>(archive_type::u32), &v, 4); }
    void write_i64(int64_t v) noexcept override { add_scalar(static_cast<uint8_t>(archive_type::i64), &v, 8); }
    void write_u64(uint64_t v) noexcept override { add_scalar(static_cast<uint8_t>(archive_type::u64), &v, 8); }
    void write_f32(float v) noexcept override { add_scalar(static_cast<uint8_t>(archive_type::float32_t), &v, 4); }
    void write_f64(double v) noexcept override { add_scalar(static_cast<uint8_t>(archive_type::float64_t), &v, 8); }

    void write_string(std::string_view v) noexcept override {
        add_blob(1, static_cast<uint8_t>(archive_type::string_t), v.data(), v.size());
    }
    void write_bytes(const void* data, size_t len) noexcept override {
        add_blob(2, static_cast<uint8_t>(archive_type::bytes_t), data, len);
    }
    void write_raw(std::string_view fragment) noexcept override {
        add_blob(2, static_cast<uint8_t>(archive_type::bytes_t), fragment.data(), fragment.size());
    }

    [[nodiscard]] std::string take() noexcept override {
        if (stack_.size() == 0)
        {
            return {};
        }
        out_.clear();
        out_.resize(8, 0);
        uint32_t root = stack_[0];
        uint32_t root_pos = place(root);
        put_u32(0, root_pos);
        out_[4] = 'L';
        out_[5] = 'C';
        out_[6] = 'F';
        out_[7] = 'B';
        return std::move(out_);
    }

    [[nodiscard]] size_t size() const noexcept override {
        return out_.size();
    }
};

class flatbuffer_archive_reader final : public archive_reader
{
    struct frame {
        uint32_t table_pos;
        uint32_t table_size;
        uint32_t vtable;
        uint32_t slot_count;
        uint32_t iter;
        bool     is_vec;
        uint32_t vec_pos;
        uint32_t count;
        uint32_t elems_base;
        uint32_t elem_size;
    };
    struct table_info {
        uint32_t pos;
        uint32_t vtable;
        uint32_t table_size;
        uint32_t slot_count;
    };

    const char*                     base_ = nullptr;
    uint32_t                        size_ = 0;
    dense<frame>                    stack_;
    bool                            err_ = false;
    bool                            root_entered_ = false;
    uint32_t                        cur_data_ = 0;
    std::string                     cur_key_;

    friend struct flatbuffer_codec;

    [[nodiscard]] bool has_bytes(uint32_t pos, uint32_t len) const noexcept {
        return pos <= size_ && len <= size_ - pos;
    }

    [[nodiscard]] bool checked_add(uint32_t a, uint32_t b, uint32_t& out) noexcept {
        if (b > std::numeric_limits<uint32_t>::max() - a)
        {
            err_ = true;
            return false;
        }
        out = a + b;
        return true;
    }

    [[nodiscard]] bool checked_mul(uint32_t a, uint32_t b, uint32_t& out) noexcept {
        if (a != 0 && b > std::numeric_limits<uint32_t>::max() / a)
        {
            err_ = true;
            return false;
        }
        out = a * b;
        return true;
    }

    [[nodiscard]] bool require_range(uint32_t pos, uint32_t len) noexcept {
        if (!has_bytes(pos, len))
        {
            err_ = true;
            return false;
        }
        return true;
    }

    [[nodiscard]] uint32_t read_u32_at(uint32_t pos) noexcept {
        if (err_)
        {
            return 0;
        }
        if ((pos & 3u) != 0 || !require_range(pos, 4))
        {
            err_ = true;
            return 0;
        }
        return detail::read_le<uint32_t>(base_ + pos);
    }

    [[nodiscard]] uint16_t read_u16_at(uint32_t pos) noexcept {
        if (err_)
        {
            return 0;
        }
        if ((pos & 1u) != 0 || !require_range(pos, 2))
        {
            err_ = true;
            return 0;
        }
        return detail::read_le<uint16_t>(base_ + pos);
    }

    [[nodiscard]] int32_t read_i32_at(uint32_t pos) noexcept {
        const uint32_t raw = read_u32_at(pos);
        int32_t value = 0;
        if (!err_)
        {
            std::memcpy(&value, &raw, 4);
        }
        return value;
    }

    [[nodiscard]] uint64_t read_u64_at(uint32_t pos) noexcept {
        if (err_)
        {
            return 0;
        }
        if ((pos & 7u) != 0 || !require_range(pos, 8))
        {
            err_ = true;
            return 0;
        }
        return detail::read_le<uint64_t>(base_ + pos);
    }

    [[nodiscard]] bool read_table_info(uint32_t pos, table_info& info) noexcept {
        if (err_)
        {
            return false;
        }
        if ((pos & 3u) != 0 || !require_range(pos, 4))
        {
            err_ = true;
            return false;
        }
        const int32_t soff = read_i32_at(pos);
        if (err_ || soff == 0)
        {
            err_ = true;
            return false;
        }
        const int64_t vpos = static_cast<int64_t>(pos) - static_cast<int64_t>(soff);
        if (vpos < 0 || vpos > std::numeric_limits<uint32_t>::max())
        {
            err_ = true;
            return false;
        }

        info.pos = pos;
        info.vtable = static_cast<uint32_t>(vpos);
        if ((info.vtable & 1u) != 0 || !require_range(info.vtable, 4))
        {
            err_ = true;
            return false;
        }
        uint32_t table_size_pos = 0;
        if (!checked_add(info.vtable, 2, table_size_pos))
        {
            return false;
        }
        const uint32_t vsize = read_u16_at(info.vtable);
        info.table_size = read_u16_at(table_size_pos);
        if (err_ || vsize < 4 || (vsize & 1u) != 0
            || info.table_size < 4 || !require_range(info.vtable, vsize)
            || !require_range(pos, info.table_size))
        {
            err_ = true;
            return false;
        }
        info.slot_count = (vsize - 4) / 2;

        for (uint32_t slot = 0; slot < info.slot_count; ++slot)
        {
            uint32_t slot_pos = 0;
            uint32_t slot_rel = 0;
            if (!checked_mul(slot, 2, slot_rel)
                || !checked_add(info.vtable, 4, slot_pos)
                || !checked_add(slot_pos, slot_rel, slot_pos))
            {
                return false;
            }
            const uint16_t off = read_u16_at(slot_pos);
            if (off == 0)
            {
                continue;
            }
            uint32_t field_pos = 0;
            if (off < 4 || off >= info.table_size
                || !checked_add(pos, off, field_pos)
                || !require_range(field_pos, 1))
            {
                err_ = true;
                return false;
            }
        }
        return !err_;
    }

    void push_table(uint32_t pos) noexcept {
        table_info info{};
        if (!read_table_info(pos, info))
        {
            return;
        }
        frame f{};
        f.table_pos = info.pos;
        f.table_size = info.table_size;
        f.vtable = info.vtable;
        f.slot_count = info.slot_count;
        f.iter = 0;
        f.is_vec = false;
        stack_.emplace_back(f);
    }

    void push_vector(uint32_t pos) noexcept {
        if (err_)
        {
            return;
        }
        if ((pos & 3u) != 0 || !require_range(pos, 4))
        {
            err_ = true;
            return;
        }
        frame f{};
        f.is_vec = true;
        f.vec_pos = pos;
        f.count = read_u32_at(pos);
        if (err_)
        {
            return;
        }
        if (!checked_add(pos, 4, f.elems_base))
        {
            return;
        }
        uint32_t min_size = 0;
        if (!checked_mul(f.count, 1, min_size)
            || !require_range(f.elems_base, min_size))
        {
            return;
        }
        f.elem_size = 4;
        f.iter = 0;
        f.table_pos = 0;
        f.table_size = 0;
        f.vtable = 0;
        f.slot_count = 0;
        stack_.emplace_back(f);
    }

    [[nodiscard]] bool validate_vector_region(const frame& f) noexcept {
        if (f.elem_size == 4 && (f.elems_base & 3u) != 0)
        {
            err_ = true;
            return false;
        }
        if (f.elem_size == 8 && (f.elems_base & 7u) != 0)
        {
            err_ = true;
            return false;
        }
        uint32_t bytes = 0;
        return checked_mul(f.count, f.elem_size, bytes)
               && require_range(f.elems_base, bytes);
    }

    [[nodiscard]] uint32_t follow_uoffset(uint32_t pos) noexcept {
        const uint32_t rel = read_u32_at(pos);
        if (err_)
        {
            return 0;
        }
        if (rel == 0)
        {
            err_ = true;
            return 0;
        }
        uint32_t target = 0;
        if (!checked_add(pos, rel, target) || !require_range(target, 1))
        {
            err_ = true;
            return 0;
        }
        return target;
    }

    [[nodiscard]] bool field_position(const table_info& info, uint32_t slot,
                                      uint32_t width, bool required,
                                      uint32_t& field_pos) noexcept {
        field_pos = 0;
        if (slot >= info.slot_count)
        {
            if (required)
            {
                err_ = true;
                return false;
            }
            return true;
        }
        uint32_t slot_pos = 0;
        uint32_t slot_rel = 0;
        if (!checked_mul(slot, 2, slot_rel)
            || !checked_add(info.vtable, 4, slot_pos)
            || !checked_add(slot_pos, slot_rel, slot_pos))
        {
            return false;
        }
        const uint16_t off = read_u16_at(slot_pos);
        if (off == 0)
        {
            if (required)
            {
                err_ = true;
                return false;
            }
            return true;
        }
        return checked_add(info.pos, off, field_pos)
               && require_range(field_pos, width);
    }

    [[nodiscard]] bool validate_u32_field(const table_info& info, uint32_t slot) noexcept {
        uint32_t pos = 0;
        if (!field_position(info, slot, 4, true, pos))
        {
            return false;
        }
        (void)read_u32_at(pos);
        return !err_;
    }

    [[nodiscard]] bool validate_offset_field(const table_info& info, uint32_t slot,
                                             uint32_t& target) noexcept {
        uint32_t pos = 0;
        if (!field_position(info, slot, 4, true, pos))
        {
            return false;
        }
        target = follow_uoffset(pos);
        return !err_;
    }

    [[nodiscard]] bool validate_blob_at(uint32_t pos) noexcept {
        if ((pos & 3u) != 0)
        {
            err_ = true;
            return false;
        }
        const uint32_t len = read_u32_at(pos);
        uint32_t data_pos = 0;
        return !err_ && checked_add(pos, 4, data_pos)
               && require_range(data_pos, len);
    }

    [[nodiscard]] bool validate_blob_field(const table_info& info, uint32_t slot) noexcept {
        uint32_t target = 0;
        return validate_offset_field(info, slot, target) && validate_blob_at(target);
    }

    [[nodiscard]] bool validate_entity_table_at(uint32_t pos) noexcept {
        table_info info{};
        if (!read_table_info(pos, info))
        {
            return false;
        }
        for (uint32_t slot = 0; slot < 6; ++slot)
        {
            if (!validate_u32_field(info, slot))
            {
                return false;
            }
        }
        return true;
    }

    [[nodiscard]] bool validate_payload_at(uint32_t pos) noexcept {
        const bool old_err = err_;
        err_ = false;
        table_info info{};
        if (read_table_info(pos, info))
        {
            return true;
        }
        err_ = old_err;
        return validate_blob_at(pos);
    }

    [[nodiscard]] bool validate_entry_table_at(uint32_t pos) noexcept {
        table_info info{};
        if (!read_table_info(pos, info)
            || !validate_u32_field(info, 0)
            || !validate_u32_field(info, 1))
        {
            return false;
        }
        uint32_t payload = 0;
        return validate_offset_field(info, 2, payload)
               && validate_payload_at(payload);
    }

    [[nodiscard]] bool validate_entity_vector_at(uint32_t pos) noexcept {
        if ((pos & 3u) != 0)
        {
            err_ = true;
            return false;
        }
        const uint32_t count = read_u32_at(pos);
        uint32_t elems_base = 0;
        uint32_t bytes = 0;
        if (err_ || !checked_add(pos, 4, elems_base)
            || !checked_mul(count, 4, bytes)
            || !require_range(elems_base, bytes))
        {
            err_ = true;
            return false;
        }
        for (uint32_t i = 0; i < count; ++i)
        {
            uint32_t rel = 0;
            uint32_t elem = 0;
            if (!checked_mul(i, 4, rel) || !checked_add(elems_base, rel, elem))
            {
                return false;
            }
            const uint32_t table = follow_uoffset(elem);
            if (err_ || !validate_entity_table_at(table))
            {
                return false;
            }
        }
        return true;
    }

    [[nodiscard]] bool validate_entry_vector_at(uint32_t pos) noexcept {
        if ((pos & 3u) != 0)
        {
            err_ = true;
            return false;
        }
        const uint32_t count = read_u32_at(pos);
        uint32_t elems_base = 0;
        uint32_t bytes = 0;
        if (err_ || !checked_add(pos, 4, elems_base)
            || !checked_mul(count, 4, bytes)
            || !require_range(elems_base, bytes))
        {
            err_ = true;
            return false;
        }
        for (uint32_t i = 0; i < count; ++i)
        {
            uint32_t rel = 0;
            uint32_t elem = 0;
            if (!checked_mul(i, 4, rel) || !checked_add(elems_base, rel, elem))
            {
                return false;
            }
            const uint32_t table = follow_uoffset(elem);
            if (err_ || !validate_entry_table_at(table))
            {
                return false;
            }
        }
        return true;
    }

    [[nodiscard]] bool validate_components_at(uint32_t pos) noexcept {
        table_info info{};
        if (!read_table_info(pos, info))
        {
            return false;
        }
        for (uint32_t slot = 0; slot < info.slot_count; ++slot)
        {
            uint32_t field = 0;
            if (!field_position(info, slot, 4, false, field))
            {
                return false;
            }
            if (field == 0)
            {
                continue;
            }
            const uint32_t vec = follow_uoffset(field);
            if (err_ || !validate_entry_vector_at(vec))
            {
                return false;
            }
        }
        return true;
    }

    [[nodiscard]] bool validate_archive_at(uint32_t pos) noexcept {
        table_info info{};
        if (!read_table_info(pos, info)
            || !validate_u32_field(info, 0)
            || !validate_u32_field(info, 1)
            || !validate_blob_field(info, 2)
            || !validate_blob_field(info, 3))
        {
            return false;
        }
        uint32_t entities = 0;
        uint32_t components = 0;
        return validate_offset_field(info, 4, entities)
               && validate_entity_vector_at(entities)
               && validate_offset_field(info, 5, components)
               && validate_components_at(components);
    }

    [[nodiscard]] bool validate_structure() noexcept {
        if (err_)
        {
            return false;
        }
        const uint32_t root = read_u32_at(0);
        if (err_ || root < 8)
        {
            err_ = true;
            return false;
        }
        table_info info{};
        if (!read_table_info(root, info))
        {
            return false;
        }
        if (info.slot_count < 6)
        {
            return true;
        }
        return validate_archive_at(root);
    }

    [[nodiscard]] std::string_view read_blob_view(size_t expected_size) noexcept {
        const uint32_t pos = follow_uoffset(cur_data_);
        if (err_ || (pos & 3u) != 0)
        {
            err_ = true;
            return {};
        }
        const uint32_t len = read_u32_at(pos);
        uint32_t data_pos = 0;
        if (err_ || !checked_add(pos, 4, data_pos)
            || !require_range(data_pos, len))
        {
            err_ = true;
            return {};
        }
        if (expected_size != 0 && len != expected_size)
        {
            err_ = true;
            return {};
        }
        return std::string_view(base_ + data_pos, len);
    }

public:
    explicit flatbuffer_archive_reader(std::string_view data) noexcept {
        base_ = data.data();
        if (data.size() > std::numeric_limits<uint32_t>::max())
        {
            err_ = true;
            return;
        }
        size_ = static_cast<uint32_t>(data.size());
        if (size_ < 8)
        {
            err_ = true;
            return;
        }
        if (!(data[4] == 'L' && data[5] == 'C' && data[6] == 'F' && data[7] == 'B'))
        {
            err_ = true;
            return;
        }
        const uint32_t root = read_u32_at(0);
        if (err_ || root < 8 || root >= size_)
        {
            err_ = true;
            return;
        }
        stack_.increase_capacity(16);
    }

    bool enter_object() noexcept override {
        if (err_)
        {
            return false;
        }
        if (!root_entered_)
        {
            root_entered_ = true;
            uint32_t root = read_u32_at(0);
            if (root >= size_)
            {
                err_ = true;
                return false;
            }
            push_table(root);
            return !err_;
        }
        if (stack_.size() == 0)
        {
            return false;
        }
        push_table(follow_uoffset(cur_data_));
        return !err_;
    }

    void leave_object() noexcept override {
        if (stack_.size() > 0)
        {
            stack_.pop_back();
        }
    }

    bool enter_array() noexcept override {
        if (err_ || stack_.size() == 0)
        {
            return false;
        }
        push_vector(follow_uoffset(cur_data_));
        return !err_;
    }

    void leave_array() noexcept override {
        if (stack_.size() > 0)
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
        if (!cur.is_vec || cur.iter >= cur.count)
        {
            return false;
        }
        if (!validate_vector_region(cur))
        {
            return false;
        }
        uint32_t rel = 0;
        if (!checked_mul(cur.iter, cur.elem_size, rel)
            || !checked_add(cur.elems_base, rel, cur_data_))
        {
            return false;
        }
        ++cur.iter;
        return true;
    }

    void end_element() noexcept override {}

    void set_element_size(size_t size) noexcept override {
        if (err_)
        {
            return;
        }
        if (stack_.size() == 0 || !stack_.back().is_vec
            || (size != 1 && size != 4 && size != 8))
        {
            err_ = true;
            return;
        }
        frame& cur = stack_.back();
        cur.elem_size = static_cast<uint32_t>(size);
        uint32_t elems_base = 0;
        if (!checked_add(cur.vec_pos, 4, elems_base))
        {
            return;
        }
        if (cur.elem_size == 8)
        {
            uint32_t rounded = 0;
            if (!checked_add(elems_base, 7, rounded))
            {
                return;
            }
            elems_base = rounded & ~7u;
        }
        cur.elems_base = elems_base;
        (void)validate_vector_region(cur);
    }

    [[nodiscard]] std::string_view next_key() noexcept override {
        if (err_ || stack_.size() == 0)
        {
            return {};
        }
        frame& cur = stack_.back();
        if (cur.is_vec)
        {
            return {};
        }
        while (cur.iter < cur.slot_count)
        {
            const uint32_t slot = cur.iter;
            ++cur.iter;
            uint32_t slot_rel = 0;
            uint32_t slot_pos = 0;
            if (!checked_mul(slot, 2, slot_rel)
                || !checked_add(cur.vtable, 4, slot_pos)
                || !checked_add(slot_pos, slot_rel, slot_pos))
            {
                return {};
            }
            const uint16_t off = read_u16_at(slot_pos);
            if (err_)
            {
                return {};
            }
            if (off == 0)
            {
                continue;
            }
            if (off < 4 || off >= cur.table_size
                || !checked_add(cur.table_pos, off, cur_data_)
                || !require_range(cur_data_, 1))
            {
                err_ = true;
                return {};
            }
            char buf[16];
            const int nn = std::snprintf(buf, sizeof(buf), "f%u", slot + 1);
            cur_key_ = std::string(buf, static_cast<size_t>(nn));
            return cur_key_;
        }
        return {};
    }

    [[nodiscard]] bool read_bool() noexcept override {
        if (err_ || !require_range(cur_data_, 1))
        {
            err_ = true;
            return false;
        }
        return base_[cur_data_] != 0;
    }
    [[nodiscard]] int32_t read_i32() noexcept override { return read_i32_at(cur_data_); }
    [[nodiscard]] uint32_t read_u32() noexcept override { return read_u32_at(cur_data_); }
    [[nodiscard]] int64_t read_i64() noexcept override {
        const uint64_t raw = read_u64_at(cur_data_);
        int64_t value = 0;
        if (!err_)
        {
            std::memcpy(&value, &raw, 8);
        }
        return value;
    }
    [[nodiscard]] uint64_t read_u64() noexcept override { return read_u64_at(cur_data_); }
    [[nodiscard]] float read_f32() noexcept override {
        const uint32_t raw = read_u32_at(cur_data_);
        float value = 0.0f;
        if (!err_)
        {
            std::memcpy(&value, &raw, 4);
        }
        return value;
    }
    [[nodiscard]] double read_f64() noexcept override {
        const uint64_t raw = read_u64_at(cur_data_);
        double value = 0.0;
        if (!err_)
        {
            std::memcpy(&value, &raw, 8);
        }
        return value;
    }

    [[nodiscard]] std::string_view read_string_view() noexcept override {
        return read_blob_view(0);
    }

    [[nodiscard]] std::string_view read_bytes_view(size_t len) noexcept override {
        return read_blob_view(len);
    }

    void skip_value() noexcept override {}
    [[nodiscard]] bool has_object_end() const noexcept override { return true; }

    [[nodiscard]] bool has_error() const noexcept override { return err_; }
    [[nodiscard]] operating_message last_error() const noexcept override {
        operating_message m;
        if (err_)
        {
            m.write(false, om_err_decode_failed, "flatbuffer: 二进制布局解析错误", msg::warn, msg::here());
        }
        return m;
    }
    [[nodiscard]] archive_type peek_type() const noexcept override {
        return archive_type::bytes_t;
    }
};

struct flatbuffer_codec final : archive_codec
{
    flatbuffer_codec() noexcept {
        magic[0] = 0; magic[1] = 0; magic[2] = 0; magic[3] = 0;
    }

    [[nodiscard]] archive_writer* create_writer() const noexcept override {
        return new (std::nothrow) flatbuffer_archive_writer();
    }
    [[nodiscard]] archive_reader* create_reader(std::string_view data) const noexcept override {
        return new (std::nothrow) flatbuffer_archive_reader(data);
    }
    void destroy_writer(archive_writer* w) const noexcept override { delete w; }
    void destroy_reader(archive_reader* r) const noexcept override { delete r; }

    [[nodiscard]] bool matches(std::string_view data) const noexcept override {
        if (data.size() < 8)
        {
            return false;
        }
        if (!(data[4] == 'L' && data[5] == 'C' && data[6] == 'F' && data[7] == 'B'))
        {
            return false;
        }
        uint32_t root = detail::read_le<uint32_t>(data.data());
        return root >= 8 && root < data.size();
    }

    [[nodiscard]] bool validate(std::string_view data, operating_message& error) const noexcept override {
        flatbuffer_archive_reader reader(data);
        if (!reader.validate_structure())
        {
            error.write_failure(om_err_decode_failed.code, msg_level::warn, msg::here());
            return false;
        }
        return true;
    }
};
