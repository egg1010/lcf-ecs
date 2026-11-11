// test_highlevel_flatbuffer_regression.cpp - FlatBuffers 读取边界与结构校验回归测试
#include "test_common.hpp"
#include "include/part/codec/codec_flatbuffer.hpp"
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>

namespace {

void put_u16(std::string& data, uint32_t pos, uint16_t value) noexcept
{
    detail::write_le(value, data.data() + pos);
}

void put_u32(std::string& data, uint32_t pos, uint32_t value) noexcept
{
    detail::write_le(value, data.data() + pos);
}

void put_i32(std::string& data, uint32_t pos, int32_t value) noexcept
{
    detail::write_le(value, data.data() + pos);
}

uint32_t get_u32(const std::string& data, uint32_t pos) noexcept
{
    return detail::read_le<uint32_t>(data.data() + pos);
}

int32_t get_i32(const std::string& data, uint32_t pos) noexcept
{
    return detail::read_le<int32_t>(data.data() + pos);
}

uint32_t field_pos(const std::string& data, uint32_t table, uint32_t slot) noexcept
{
    const uint32_t vtable = static_cast<uint32_t>(static_cast<int64_t>(table)
                                      - static_cast<int64_t>(get_i32(data, table)));
    const uint32_t off = detail::read_le<uint16_t>(data.data() + vtable + 4 + slot * 2);
    return table + off;
}

std::string make_simple_buffer() noexcept
{
    flatbuffer_archive_writer w;
    w.begin_object();
    w.key("flag"); w.write_bool(true);
    w.key("i32"); w.write_i32(-42);
    w.key("i64"); w.write_i64(-5000000000ll);
    w.key("f64"); w.write_f64(3.25);
    w.key("name"); w.write_string("hello");
    w.key("items"); w.begin_array(2);
    w.begin_object();
    w.key("id"); w.write_u32(10);
    w.key("name"); w.write_string("first");
    w.end_object();
    w.begin_object();
    w.key("id"); w.write_u32(20);
    w.key("name"); w.write_string("second");
    w.end_object();
    w.end_array();
    w.key("values"); w.begin_array(3);
    w.write_i32(7); w.write_i32(8); w.write_i32(9);
    w.end_array();
    w.key("nested"); w.begin_array(2);
    w.begin_array(2); w.write_i32(1); w.write_i32(2); w.end_array();
    w.begin_array(2); w.write_i32(3); w.write_i32(4); w.end_array();
    w.end_array();
    w.key("bools"); w.begin_array(3);
    w.write_bool(true); w.write_bool(false); w.write_bool(true);
    w.end_array();
    w.key("wide"); w.begin_array(2);
    w.write_u64(0x1020304050607080ull); w.write_u64(9);
    w.end_array();
    w.end_object();
    return w.take();
}

bool check_simple_buffer(std::string_view data) noexcept
{
    flatbuffer_archive_reader r(data);
    if (!r.enter_object() || !r.has_object_end())
    {
        return false;
    }
    if (r.next_key() != "f1" || !r.read_bool())
    {
        return false;
    }
    if (r.next_key() != "f2" || r.read_i32() != -42)
    {
        return false;
    }
    if (r.next_key() != "f3" || r.read_i64() != -5000000000ll)
    {
        return false;
    }
    if (r.next_key() != "f4" || r.read_f64() != 3.25)
    {
        return false;
    }
    if (r.next_key() != "f5" || r.read_string_view() != "hello")
    {
        return false;
    }
    if (r.next_key() != "f6" || !r.enter_array())
    {
        return false;
    }
    for (uint32_t i = 0; i < 2; ++i)
    {
        if (!r.next_element() || !r.enter_object())
        {
            return false;
        }
        const uint32_t expected_id = (i == 0) ? 10 : 20;
        const std::string_view expected_name = (i == 0)
                                               ? std::string_view("first")
                                               : std::string_view("second");
        if (r.next_key() != "f1" || r.read_u32() != expected_id
            || r.next_key() != "f2" || r.read_string_view() != expected_name)
        {
            return false;
        }
        r.leave_object();
        r.end_element();
    }
    if (r.next_element())
    {
        return false;
    }
    r.leave_array();

    if (r.next_key() != "f7" || !r.enter_array())
    {
        return false;
    }
    r.set_element_size(4);
    for (int32_t expected = 7; expected <= 9; ++expected)
    {
        if (!r.next_element() || r.read_i32() != expected)
        {
            return false;
        }
        r.end_element();
    }
    r.leave_array();

    if (r.next_key() != "f8" || !r.enter_array())
    {
        return false;
    }
    int32_t expected = 1;
    for (uint32_t i = 0; i < 2; ++i)
    {
        if (!r.next_element() || !r.enter_array())
        {
            return false;
        }
        r.set_element_size(4);
        for (uint32_t j = 0; j < 2; ++j)
        {
            if (!r.next_element() || r.read_i32() != expected)
            {
                return false;
            }
            ++expected;
            r.end_element();
        }
        r.leave_array();
        r.end_element();
    }
    r.leave_array();

    if (r.next_key() != "f9" || !r.enter_array())
    {
        return false;
    }
    r.set_element_size(1);
    const bool bools[] = {true, false, true};
    for (uint32_t i = 0; i < 3; ++i)
    {
        if (!r.next_element() || r.read_bool() != bools[i])
        {
            return false;
        }
        r.end_element();
    }
    r.leave_array();

    if (r.next_key() != "f10" || !r.enter_array())
    {
        return false;
    }
    r.set_element_size(8);
    if (!r.next_element() || r.read_u64() != 0x1020304050607080ull)
    {
        return false;
    }
    r.end_element();
    if (!r.next_element() || r.read_u64() != 9)
    {
        return false;
    }
    r.end_element();
    r.leave_array();
    r.leave_object();
    return !r.has_error();
}

std::string make_archive_buffer() noexcept
{
    flatbuffer_archive_writer w;
    w.begin_object();
    w.key("version"); w.write_u32(1);
    w.key("engine"); w.write_u32(0);
    w.key("meta"); w.write_raw("{}");
    w.key("cv"); w.write_raw("{}");
    w.key("entities"); w.begin_array(1);
    w.begin_object();
    w.key("i"); w.write_u32(0);
    w.key("v"); w.write_u32(1);
    w.key("f"); w.write_u32(2);
    w.key("t"); w.write_u32(3);
    w.key("l"); w.write_u32(4);
    w.key("g"); w.write_u32(5);
    w.end_object();
    w.end_array();
    w.key("components"); w.begin_object();
    w.key("raw"); w.begin_array(1);
    w.begin_object();
    w.key("i"); w.write_u32(0);
    w.key("v"); w.write_u32(1);
    w.key("d");
    const char payload[] = {1, 2, 3, 4};
    w.write_bytes(payload, sizeof(payload));
    w.end_object();
    w.end_array();
    w.end_object();
    w.end_object();
    return w.take();
}

bool validates(const flatbuffer_codec& codec, std::string_view data) noexcept
{
    operating_message error;
    return codec.validate(data, error);
}

bool read_entity(std::string_view data) noexcept
{
    flatbuffer_archive_reader r(data);
    if (!r.enter_object())
    {
        return false;
    }
    (void)r.next_key(); (void)r.read_u32();
    (void)r.next_key(); (void)r.read_u32();
    (void)r.next_key(); (void)r.read_string_view();
    (void)r.next_key(); (void)r.read_string_view();
    (void)r.next_key();
    if (!r.enter_array() || !r.next_element() || !r.enter_object())
    {
        return false;
    }
    for (uint32_t i = 0; i < 6; ++i)
    {
        (void)r.next_key();
        (void)r.read_u32();
    }
    r.leave_object();
    r.end_element();
    r.leave_array();
    r.leave_object();
    return !r.has_error();
}

} // namespace

int main()
{
    t_begin("test_highlevel_flatbuffer_regression — FlatBuffers 读取安全回归");
    const flatbuffer_codec codec;
    const std::string simple = make_simple_buffer();
    const std::string archive = make_archive_buffer();
    const uint32_t root = get_u32(archive, 0);
    const uint32_t vtable = static_cast<uint32_t>(static_cast<int64_t>(root)
                                      - static_cast<int64_t>(get_i32(archive, root)));
    const uint32_t entities_field = field_pos(archive, root, 4);
    const uint32_t entities = entities_field + get_u32(archive, entities_field);
    const uint32_t meta_field = field_pos(archive, root, 2);

    t_section(1, "合法 table / vector / string 与嵌套数组");
    t_check("完整简单缓冲区保持往返行为", check_simple_buffer(simple));
    t_check("实际 Archive 深度校验通过", validates(codec, archive));
    t_check("实际 Archive 读取通过", read_entity(archive));

    t_section(2, "所有截断均被拒绝");
    bool all_truncated_rejected = true;
    for (size_t size = 0; size < archive.size(); ++size)
    {
        if (validates(codec, std::string_view(archive.data(), size)))
        {
            all_truncated_rejected = false;
            break;
        }
    }
    t_check("Archive 每个真前缀均校验失败", all_truncated_rejected);
    bool simple_truncation_errors = true;
    const size_t simple_cuts[] = {0, 4, 8, simple.size() / 2, simple.size() - 1};
    for (size_t i = 0; i < sizeof(simple_cuts) / sizeof(simple_cuts[0]); ++i)
    {
        if (check_simple_buffer(std::string_view(simple.data(), simple_cuts[i])))
        {
            simple_truncation_errors = false;
            break;
        }
    }
    t_check("简单缓冲区截断读取被拒绝", simple_truncation_errors);

    t_section(3, "非法 root 与 vtable");
    {
        std::string bad = archive;
        put_u32(bad, 0, 4);
        t_check("root 指向文件头被拒绝", !validates(codec, bad));
        flatbuffer_archive_reader r(bad);
        t_check("非法 root 在构造时置错", r.has_error());
    }
    {
        std::string bad = archive;
        put_u32(bad, 0, std::numeric_limits<uint32_t>::max() - 3);
        t_check("root 溢出偏移被拒绝", !validates(codec, bad));
    }
    {
        std::string bad = archive;
        put_i32(bad, root, 0);
        t_check("零 vtable 距离被拒绝", !validates(codec, bad));
    }
    {
        std::string bad = archive;
        put_i32(bad, root, std::numeric_limits<int32_t>::max());
        t_check("越界 vtable 距离被拒绝", !validates(codec, bad));
    }
    {
        std::string bad = archive;
        put_u16(bad, vtable, 3);
        t_check("非法 vtable 长度被拒绝", !validates(codec, bad));
    }
    {
        std::string bad = archive;
        put_u16(bad, vtable + 2, 4);
        t_check("table 元素区域截断被拒绝", !validates(codec, bad));
    }

    t_section(4, "非法 vector / string offset 与整数溢出");
    {
        std::string bad = archive;
        put_u32(bad, entities_field, 0);
        t_check("零 vector offset 被拒绝", !validates(codec, bad) && !read_entity(bad));
    }
    {
        std::string bad = archive;
        put_u32(bad, entities_field, std::numeric_limits<uint32_t>::max());
        t_check("vector offset 加法溢出被拒绝", !validates(codec, bad) && !read_entity(bad));
    }
    {
        std::string bad = archive;
        put_u32(bad, entities, std::numeric_limits<uint32_t>::max());
        t_check("vector 元素区域乘法溢出被拒绝", !validates(codec, bad) && !read_entity(bad));
    }
    {
        std::string bad = archive;
        put_u32(bad, entities + 4, 0);
        t_check("vector 内非法 table offset 被拒绝", !validates(codec, bad) && !read_entity(bad));
    }
    {
        std::string bad = archive;
        put_u32(bad, meta_field, std::numeric_limits<uint32_t>::max());
        t_check("string offset 加法溢出被拒绝", !validates(codec, bad));
    }
    {
        std::string bad = archive;
        const uint32_t meta = meta_field + get_u32(archive, meta_field);
        put_u32(bad, meta, std::numeric_limits<uint32_t>::max());
        t_check("string 长度区域溢出被拒绝", !validates(codec, bad));
    }

    return t_end("flatbuffer regression ");
}
