// test_codec_new.cpp - 新格式编码器 (Protobuf/FlatBuffer) 验证测试
// 验证: 编解码器基本功能 + 格式自动检测 + 零拷贝特性
#include "include/serialization/serialization.hpp"
#include "test_common.hpp"
#include <cstring>

using namespace ecs;
using namespace serialize;

// === 测试用组件 ===
struct PbVec3 {
    float x, y, z;
    PbVec3(float x = 0, float y = 0, float z = 0) : x(x), y(y), z(z) {}
};
static_assert(std::is_trivially_copyable_v<PbVec3>);

// === 1. 编解码器注册表检测 ===
static void test_codec_detection()
{
    print_sub("编码器注册表格式检测");

    // JSON
    json_codec jc;
    print_item("JSON codec matches '{...}'", jc.matches("{\"a\":1}"));

    // Binary
    binary_codec bc;
    std::string bin_data = "LCE1\x01\x01\x00\x00";
    print_item("Binary codec matches 'LCE1'", bc.matches(bin_data));

    // Protobuf
    protobuf_codec pc;
    std::string pb_data = "LCPB\x01\x02\x03";
    print_item("Protobuf codec matches 'LCPB'", pc.matches(pb_data));

    // FlatBuffer
    flatbuffer_codec fc;
    std::string fb_data = "LCFB\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00";
    print_item("FlatBuffer codec matches 'LCFB'", fc.matches(fb_data));

    // 注册表自动检测
    const archive_codec* det1 = detect_codec("{\"version\":1}");
    print_item("detect JSON", det1 && det1->magic[0] == '{');

    const archive_codec* det2 = detect_codec(bin_data);
    print_item("detect Binary", det2 && det2->magic[0] == 'L' && det2->magic[1] == 'C' && det2->magic[2] == 'E');

    const archive_codec* det3 = detect_codec(pb_data);
    print_item("detect Protobuf", det3 && det3->magic[2] == 'P');

    const archive_codec* det4 = detect_codec(fb_data);
    print_item("detect FlatBuffer", det4 && det4->magic[2] == 'F');
}

// === 2. Protobuf 编解码器基本测试 ===
static void test_protobuf_basic()
{
    print_sub("Protobuf 编解码器");

    protobuf_codec pc;
    archive_writer* w = pc.create_writer();

    // 写入对象
    w->begin_object();
    w->key("version"); w->write_u32(42);
    w->key("name");    w->write_string("hello");
    w->key("x");       w->write_f32(3.14f);
    w->key("flag");    w->write_bool(true);
    w->end_object();

    std::string data = w->take();
    pc.destroy_writer(w);

    print_item("Protobuf 写入成功", data.size() > 4);
    print_item("magic LCPB", data.size() >= 4 && data[0]=='L' && data[1]=='C' && data[2]=='P' && data[3]=='B');

    // 读取
    archive_reader* r = pc.create_reader(data);
    print_item("Protobuf reader 创建", r && !r->has_error());

    bool enter_ok = r->enter_object();
    print_item("enter_object", enter_ok);

    // 读取字段 (按 next_key 迭代)
    uint32_t ver = 0;
    std::string_view name_sv;
    float x = 0;
    bool flag = false;
    int field_count = 0;

    if (enter_ok) {
        std::string_view k;
        while (!(k = r->next_key()).empty()) {
            if (k == "f1") ver = r->read_u32();           // version (field 1)
            else if (k == "f2") name_sv = r->read_string_view();  // name (field 2)
            else if (k == "f3") x = r->read_f32();        // x (field 3)
            else if (k == "f4") flag = r->read_bool();    // flag (field 4)
            else r->skip_value();
            ++field_count;
        }
    }

    print_item("字段数 = 4", field_count == 4);
    print_item("version = 42", ver == 42);
    print_item("name = 'hello'", name_sv == "hello");
    print_item("x = 3.14f", x == 3.14f);
    print_item("flag = true", flag == true);

    // 零拷贝验证: name_sv 应指向原 data 缓冲区
    bool zero_copy = name_sv.data() >= data.data() &&
                     name_sv.data() < data.data() + data.size();
    print_item("Protobuf string 零拷贝", zero_copy);

    pc.destroy_reader(r);
}

// === 3. FlatBuffer 编解码器基本测试 ===
static void test_flatbuffer_basic()
{
    print_sub("FlatBuffer 编解码器");

    flatbuffer_codec fc;
    archive_writer* w = fc.create_writer();

    // 写入对象
    w->begin_object();
    w->key("version"); w->write_u32(7);
    w->key("name");    w->write_string("world");
    w->key("value");   w->write_i32(-123);
    w->end_object();

    std::string data = w->take();
    fc.destroy_writer(w);

    print_item("FlatBuffer 写入成功", data.size() > 16);
    print_item("magic LCFB", data.size() >= 4 && data[0]=='L' && data[1]=='C' && data[2]=='F' && data[3]=='B');

    // 读取
    archive_reader* r = fc.create_reader(data);
    print_item("FlatBuffer reader 创建", r && !r->has_error());

    // 读取字段 (按 next_key 迭代 vtable)
    uint32_t ver = 0;
    std::string_view name_sv;
    int32_t value = 0;
    int field_count = 0;

    // FlatBuffer 不需要 enter_object (根即为对象)
    std::string_view k;
    while (!(k = r->next_key()).empty()) {
        if (k == "f1") ver = r->read_u32();        // version (field 1)
        else if (k == "f2") name_sv = r->read_string_view();  // name (field 2)
        else if (k == "f3") value = r->read_i32();  // value (field 3)
        ++field_count;
    }

    print_item("字段数 = 3", field_count == 3);
    print_item("version = 7", ver == 7);
    print_item("name = 'world'", name_sv == "world");
    print_item("value = -123", value == -123);

    // 零拷贝验证
    bool zero_copy = name_sv.data() >= data.data() &&
                     name_sv.data() < data.data() + data.size();
    print_item("FlatBuffer string 零拷贝", zero_copy);

    fc.destroy_reader(r);
}

// === 4. 通过公共逻辑层使用各格式 ===
static void test_archive_logic_with_codecs()
{
    print_sub("公共逻辑层 + 多格式");

    manager mgr;
    entity e1 = mgr.create_entity();
    entity e2 = mgr.create_entity();
    mgr.add<PbVec3>(e1, PbVec3(1.0f, 2.0f, 3.0f));
    mgr.add<PbVec3>(e2, PbVec3(4.0f, 5.0f, 6.0f));

    archive_logic logic(mgr);

    // 用 JSON 编码器保存
    json_codec jc;
    archive_writer* jw = jc.create_writer();
    logic.save_header(*jw, 1, 0, {});
    logic.save_component_versions<PbVec3>(*jw);
    logic.save_entities<PbVec3>(*jw);
    logic.save_components<PbVec3>(*jw);
    std::string json_data = jw->take();
    jc.destroy_writer(jw);

    print_item("JSON via archive_logic", json_data.size() > 0);
    bool has_entities = json_data.find("entities") != std::string::npos;
    print_item("JSON 含 'entities'", has_entities);

    // 用 Protobuf 编码器保存相同数据
    protobuf_codec pc;
    archive_writer* pw = pc.create_writer();
    logic.save_header(*pw, 1, 0, {});
    logic.save_component_versions<PbVec3>(*pw);
    logic.save_entities<PbVec3>(*pw);
    logic.save_components<PbVec3>(*pw);
    std::string pb_data = pw->take();
    pc.destroy_writer(pw);

    print_item("Protobuf via archive_logic", pb_data.size() > 4);
    bool pb_magic = pb_data.size() >= 4 && pb_data[0]=='L' && pb_data[1]=='C' && pb_data[2]=='P';
    print_item("Protobuf magic 正确", pb_magic);

    // 用 FlatBuffer 编码器保存相同数据
    flatbuffer_codec fc;
    archive_writer* fw = fc.create_writer();
    logic.save_header(*fw, 1, 0, {});
    logic.save_component_versions<PbVec3>(*fw);
    logic.save_entities<PbVec3>(*fw);
    logic.save_components<PbVec3>(*fw);
    std::string fb_data = fw->take();
    fc.destroy_writer(fw);

    print_item("FlatBuffer via archive_logic", fb_data.size() > 16);
    bool fb_magic = fb_data.size() >= 4 && fb_data[0]=='L' && fb_data[1]=='C' && fb_data[2]=='F';
    print_item("FlatBuffer magic 正确", fb_magic);

    // 三种格式产生不同体积 (FlatBuffer 因 vtable 通常最大)
    print_item("三种格式体积均 > 0",
               json_data.size() > 0 && pb_data.size() > 0 && fb_data.size() > 0);

    // 同一逻辑层, 仅编码器不同 - 验证逻辑复用
    std::cout << "    JSON size = " << json_data.size() << " bytes\n";
    std::cout << "    Protobuf size = " << pb_data.size() << " bytes\n";
    std::cout << "    FlatBuffer size = " << fb_data.size() << " bytes\n";
}

// === 5. 格式切换零拷贝演示 ===
static void test_format_switch_zero_copy()
{
    print_sub("格式切换零拷贝");

    // 构造一个带字符串的数据
    flatbuffer_codec fc;
    archive_writer* w = fc.create_writer();
    w->begin_object();
    w->key("s"); w->write_string("zero_copy_string");
    w->end_object();
    std::string data = w->take();
    fc.destroy_writer(w);

    // 用同一 codec 读取
    archive_reader* r = fc.create_reader(data);
    bool ok = r && !r->has_error();

    std::string_view sv;
    if (ok) {
        std::string_view k;
        while (!(k = r->next_key()).empty()) {
            if (k == "f1") {
                sv = r->read_string_view();
                break;
            }
        }
    }

    print_item("读取字符串值", sv == "zero_copy_string");

    // 零拷贝验证: sv 指向原 data 缓冲区
    bool zero_copy = sv.data() >= data.data() &&
                     sv.data() < data.data() + data.size();
    print_item("零拷贝 (指针指向原缓冲区)", zero_copy);

    if (r) fc.destroy_reader(r);
}

// === 6. serialization 主类四格式往返测试 ===
static void test_serializer_four_formats()
{
    print_sub("serialization 主类四格式往返");

    struct FmtCase {
        const char* name;
        serialization::format fmt;
        bool expect_magic_lcpb;
        bool expect_magic_lcfb;
        bool expect_magic_lce1;
    };

    FmtCase cases[] = {
        {"JSON",       serialization::format::json,       false, false, false},
        {"Binary",     serialization::format::binary,     false, false, true},
        {"Protobuf",   serialization::format::protobuf,  true,  false, false},
        {"FlatBuffer", serialization::format::flatbuffer, false, true,  false},
    };

    for (const auto& c : cases) {
        // 创建 manager 并填充数据
        manager mgr;
        entity e1 = mgr.create_entity();
        entity e2 = mgr.create_entity();
        mgr.add<PbVec3>(e1, PbVec3(1.0f, 2.0f, 3.0f));
        mgr.add<PbVec3>(e2, PbVec3(4.0f, 5.0f, 6.0f));

        // 保存 (禁用校验和前缀, 验证裸格式 magic; 完整管线由 save/load 往返覆盖)
        serialization saver(mgr);
        saver.set_archive_version(2);
        saver.set_metadata("author", "test");
        saver.set_checksum_enabled(false);
        std::string data;
        operating_message r = saver.save_to_string<PbVec3>(data, c.fmt);
        bool save_ok = r && !data.empty();

        std::string label_save = std::string(c.name) + " save";
        print_item(label_save.c_str(), save_ok);

        // magic 头校验
        if (c.expect_magic_lcpb) {
            print_item((std::string(c.name) + " magic LCPB").c_str(),
                       data.size() >= 4 && data[0]=='L' && data[1]=='C' && data[2]=='P' && data[3]=='B');
        }
        if (c.expect_magic_lcfb) {
            print_item((std::string(c.name) + " magic LCFB").c_str(),
                       data.size() >= 4 && data[0]=='L' && data[1]=='C' && data[2]=='F' && data[3]=='B');
        }
        if (c.expect_magic_lce1) {
            print_item((std::string(c.name) + " magic LCE1").c_str(),
                       data.size() >= 4 && data[0]=='L' && data[1]=='C' && data[2]=='E' && data[3]=='1');
        }

        // 加载到新 manager
        manager mgr2;
        serialization loader(mgr2);
        loader.set_archive_version(2);
        operating_message r2 = loader.load_from_string<PbVec3>(data);
        bool load_ok = (bool)r2;
        std::string label_load = std::string(c.name) + " load";
        print_item(label_load.c_str(), load_ok);

        // 数据校验
        PbVec3* v1 = mgr2.get_ptr<PbVec3>(e1);
        PbVec3* v2 = mgr2.get_ptr<PbVec3>(e2);
        bool data_ok = (v1 && v2 &&
                        v1->x == 1.0f && v1->y == 2.0f && v1->z == 3.0f &&
                        v2->x == 4.0f && v2->y == 5.0f && v2->z == 6.0f);
        std::string label_data = std::string(c.name) + " 数据一致";
        print_item(label_data.c_str(), data_ok);

        // 元数据校验
        const std::string* meta = loader.get_metadata("author");
        std::string label_meta = std::string(c.name) + " 元数据保留";
        print_item(label_meta.c_str(), meta && *meta == "test");
    }

    // 格式自动检测: 用 protobuf save (禁校验和前缀, detect_codec 消费裸格式),
    // 用 detect 确认; 带前缀场景由 load_from_string 的剥离管线覆盖
    {
        manager mgr;
        entity e = mgr.create_entity();
        mgr.add<PbVec3>(e, PbVec3(7.0f, 8.0f, 9.0f));

        serialization saver(mgr);
        saver.set_checksum_enabled(false);
        std::string data;
        saver.save_to_string<PbVec3>(data, serialization::format::protobuf);

        const archive_codec* detected = detect_codec(data);
        print_item("detect_codec 识别 protobuf", detected && detected->magic[2] == 'P');
    }
}

int main()
{
    SetConsoleOutputCP(CP_UTF8);

    print_section(1, "新格式编解码器测试");

    test_codec_detection();
    test_protobuf_basic();
    test_flatbuffer_basic();
    test_archive_logic_with_codecs();
    test_format_switch_zero_copy();
    test_serializer_four_formats();

    print_summary("新格式测试");
    return 0;
}
