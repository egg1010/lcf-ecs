#include "test_common.hpp"
#include "include/serialization/manager.hpp"
#include "include/reflection/manager.hpp"
#include <cstdint>
#include <string>

namespace
{
struct wire_component
{
    int32_t id;
    float scale;
};

void register_reflection()
{
    auto& reflection = reflect::global();
    reflection.register_type_only<wire_component>("protobuf_wire_component");
    reflection.register_member_runtime<wire_component>(
        "protobuf_wire_component", "id", &wire_component::id);
    reflection.register_member_runtime<wire_component>(
        "protobuf_wire_component", "scale", &wire_component::scale);
}
}

int main()
{
    t_begin("test_protobuf_regression — Protobuf wire 回归");
    register_reflection();

    serialize::manager objects;
    objects.set_checksum(false).set_format(serialize::archive_format::protobuf);
    const wire_component source{7, 1.5f};
    std::string data;
    t_check("合法 Protobuf 保存", objects.save_object(data, source));
    wire_component output{};
    t_check("合法 Protobuf 读取", objects.load_object(data, output)
            && output.id == 7 && output.scale == 1.5f);

    protobuf_archive_writer writer;
    writer.begin_object();
    writer.key("v"); writer.write_u32(1);
    writer.key("items"); writer.begin_array(1);
    writer.begin_object();
    writer.key("id"); writer.write_f32(2.0f);
    writer.key("scale"); writer.write_f32(3.0f);
    writer.end_object();
    writer.end_array();
    writer.end_object();
    const std::string wrong_wire = writer.take();
    t_check("字段 wire type 不匹配被拒绝", !objects.load_object(wrong_wire, output));

    const char overflow_data[] = {
        static_cast<char>(0x80), static_cast<char>(0x80), static_cast<char>(0x80),
        static_cast<char>(0x80), static_cast<char>(0x80), static_cast<char>(0x80),
        static_cast<char>(0x80), static_cast<char>(0x80), static_cast<char>(0x80),
        static_cast<char>(0x02),
    };
    const char* cursor = overflow_data;
    bool overflow = false;
    (void)pb_detail::read_varint(cursor, cursor + sizeof(overflow_data), overflow);
    t_check("varint 第十字节溢出被拒绝", overflow);

    const char zero_tag_data[] = {0};
    const char* zero_cursor = zero_tag_data;
    bool zero_error = false;
    (void)pb_detail::read_varint(zero_cursor, zero_cursor + 1, zero_error);
    t_check("零字段号被格式检测拒绝", !zero_error && !protobuf_codec{}.matches(
        std::string_view(zero_tag_data, sizeof(zero_tag_data))));

    return t_end("protobuf regression ");
}
