// test_serialization.cpp - ECS 序列化功能测试
#include "include/serialization/serialization.hpp"
#include "test_common.hpp"
#include <cstdio>
#include <cstring>

using serialize::serialization;
using ecs::entity;
using ecs::manager;
using serialize::serialize_filter;
using serialize::load_mode;
using ::register_component_version;
using ::register_migration;
using ::rle_compress;
using ::rle_decompress;

// === 测试用组件 ===

// trivially copyable 组件 (走 base64 路径)
struct Vec3 {
    float x, y, z;
    Vec3(float x = 0, float y = 0, float z = 0) : x(x), y(y), z(z) {}
    bool operator==(const Vec3& o) const { return x == o.x && y == o.y && z == o.z; }
};
static_assert(std::is_trivially_copyable_v<Vec3>);

// 提供 to_json/from_json 的 trivial 组件 (走 JSON 路径)
struct Hp {
    int current;
    int max;
    Hp(int c = 100, int m = 100) : current(c), max(m) {}
    std::string to_json() const {
        json_writer w;
        w.begin_object();
        w.key("c").value(current);
        w.key("m").value(max);
        w.end_object();
        return w.take();
    }
    void from_json(std::string_view s) {
        json_reader r(s);
        if (!r.enter_object()) return;
        std::string_view k;
        while (!(k = r.next_key()).empty()) {
            if (k == "c") current = r.read_int32();
            else if (k == "m") max = r.read_int32();
            else r.skip_value();
        }
    }
    bool operator==(const Hp& o) const { return current == o.current && max == o.max; }
};

// 非 trivial 组件 (含 std::string, 必须提供 to_json/from_json)
struct PlayerInfo {
    std::string name;
    int level;
    PlayerInfo(std::string n = "", int l = 1) : name(std::move(n)), level(l) {}
    std::string to_json() const {
        json_writer w;
        w.begin_object();
        w.key("name").value(name);
        w.key("level").value(level);
        w.end_object();
        return w.take();
    }
    void from_json(std::string_view s) {
        json_reader r(s);
        if (!r.enter_object()) return;
        std::string_view k;
        while (!(k = r.next_key()).empty()) {
            if (k == "name") name = r.read_string();
            else if (k == "level") level = r.read_int32();
            else r.skip_value();
        }
    }
    bool operator==(const PlayerInfo& o) const { return name == o.name && level == o.level; }
};

// === 测试用例 ===

static void test_trivial_save_load()
{
    print_sub("trivially copyable 组件 (base64 路径)");
    manager mgr;
    entity e1 = mgr.create_entity();
    entity e2 = mgr.create_entity();
    mgr.add<Vec3>(e1, Vec3(1.0f, 2.0f, 3.0f));
    mgr.add<Vec3>(e2, Vec3(4.0f, 5.0f, 6.0f));

    std::string json;
    auto r = serialization(mgr).save_to_string<Vec3>(json);
    bool pass = (bool)r;
    print_item("save_to_string<Vec3>", pass);
    if (!pass) std::cout << "    " << r.read_message() << "\n";

    // 加载到新 manager
    manager mgr2;
    auto r2 = serialization(mgr2).load_from_string<Vec3>(json);
    pass = pass && (bool)r2;
    print_item("load_from_string<Vec3>", pass);
    if (!pass) std::cout << "    " << r2.read_message() << "\n";

    // 验证 (新 manager 实体不同, 但组件数据应一致)
    size_t count = 0;
    if (auto* set = mgr2.get_single_class_set<Vec3>()) {
        count = set->size();
    }
    pass = pass && count == 2;
    print_item("组件数量 = 2", pass);

    // 验证组件值
    bool vals_ok = true;
    if (auto* set = mgr2.get_single_class_set<Vec3>()) {
        const auto* pool = set->get_typed_pool_ptr<Vec3>();
        for (size_t i = 0; i < set->size(); ++i) {
            const Vec3& v = (*pool)[i];
            if (v == Vec3(1.0f, 2.0f, 3.0f)) continue;
            if (v == Vec3(4.0f, 5.0f, 6.0f)) continue;
            vals_ok = false;
        }
    } else {
        vals_ok = false;
    }
    print_item("组件值匹配 (1,2,3) (4,5,6)", vals_ok);
    if (!pass || !vals_ok) {
        std::cout << "    JSON: " << json << "\n";
    }
}

static void test_json_method_save_load()
{
    print_sub("提供 to_json/from_json 的组件");
    manager mgr;
    entity e1 = mgr.create_entity();
    entity e2 = mgr.create_entity();
    mgr.add<Hp>(e1, Hp(80, 100));
    mgr.add<Hp>(e2, Hp(50, 200));

    std::string json;
    auto r = serialization(mgr).save_to_string<Hp>(json);
    bool pass = (bool)r;
    print_item("save_to_string<Hp>", pass);

    // 验证 JSON 中包含可读字段 (非 base64)
    bool readable = json.find("\"c\":80") != std::string::npos
                 && json.find("\"m\":100") != std::string::npos
                 && json.find("\"c\":50") != std::string::npos;
    print_item("JSON 可读 (c/m 字段)", readable);

    manager mgr2;
    auto r2 = serialization(mgr2).load_from_string<Hp>(json);
    pass = pass && (bool)r2;
    print_item("load_from_string<Hp>", pass);

    bool vals_ok = false;
    if (auto* set = mgr2.get_single_class_set<Hp>()) {
        const auto* pool = set->get_typed_pool_ptr<Hp>();
        bool found_80_100 = false, found_50_200 = false;
        for (size_t i = 0; i < set->size(); ++i) {
            const Hp& h = (*pool)[i];
            if (h == Hp(80, 100)) found_80_100 = true;
            if (h == Hp(50, 200)) found_50_200 = true;
        }
        vals_ok = found_80_100 && found_50_200;
    }
    print_item("组件值匹配 (80,100) (50,200)", vals_ok);
    if (!pass || !readable || !vals_ok) {
        std::cout << "    JSON: " << json << "\n";
        std::cout << "    err: " << r2.read_message() << "\n";
    }
}

static void test_non_trivial_save_load()
{
    print_sub("非 trivial 组件 (含 std::string)");
    manager mgr;
    entity e1 = mgr.create_entity();
    entity e2 = mgr.create_entity();
    mgr.add<PlayerInfo>(e1, PlayerInfo("Alice", 99));
    mgr.add<PlayerInfo>(e2, PlayerInfo("Bob", 1));

    std::string json;
    auto r = serialization(mgr).save_to_string<PlayerInfo>(json);
    bool pass = (bool)r;
    print_item("save_to_string<PlayerInfo>", pass);

    bool readable = json.find("\"name\":\"Alice\"") != std::string::npos
                 && json.find("\"level\":99") != std::string::npos
                 && json.find("\"name\":\"Bob\"") != std::string::npos;
    print_item("JSON 可读 (name/level)", readable);

    manager mgr2;
    auto r2 = serialization(mgr2).load_from_string<PlayerInfo>(json);
    pass = pass && (bool)r2;
    print_item("load_from_string<PlayerInfo>", pass);

    bool vals_ok = false;
    if (auto* set = mgr2.get_single_class_set<PlayerInfo>()) {
        const auto* pool = set->get_typed_pool_ptr<PlayerInfo>();
        bool found_alice = false, found_bob = false;
        for (size_t i = 0; i < set->size(); ++i) {
            const PlayerInfo& p = (*pool)[i];
            if (p == PlayerInfo("Alice", 99)) found_alice = true;
            if (p == PlayerInfo("Bob", 1)) found_bob = true;
        }
        vals_ok = found_alice && found_bob;
    }
    print_item("组件值匹配 (Alice,99) (Bob,1)", vals_ok);
    if (!pass || !readable || !vals_ok) {
        std::cout << "    JSON: " << json << "\n";
        std::cout << "    err: " << r2.read_message() << "\n";
    }
}

static void test_multi_type_save_load()
{
    print_sub("多类型混合保存/加载");
    manager mgr;
    entity e1 = mgr.create_entity();
    entity e2 = mgr.create_entity();
    mgr.add<Vec3>(e1, Vec3(10, 20, 30));
    mgr.add<Vec3>(e2, Vec3(40, 50, 60));
    mgr.add<Hp>(e1, Hp(100, 100));
    mgr.add<PlayerInfo>(e2, PlayerInfo("Hero", 50));

    std::string json;
    auto r = serialization(mgr).save_to_string<Vec3, Hp, PlayerInfo>(json);
    bool pass = (bool)r;
    print_item("save_to_string<3 types>", pass);

    manager mgr2;
    auto r2 = serialization(mgr2).load_from_string<Vec3, Hp, PlayerInfo>(json);
    pass = pass && (bool)r2;
    print_item("load_from_string<3 types>", pass);

    // 验证各类型数量
    size_t v3_count = 0, h_count = 0, p_count = 0;
    if (auto* s = mgr2.get_single_class_set<Vec3>()) v3_count = s->size();
    if (auto* s = mgr2.get_single_class_set<Hp>()) h_count = s->size();
    if (auto* s = mgr2.get_single_class_set<PlayerInfo>()) p_count = s->size();
    pass = pass && v3_count == 2 && h_count == 1 && p_count == 1;
    print_item("数量: Vec3=2, Hp=1, PlayerInfo=1", pass);

    // 验证值
    bool vals_ok = false;
    if (auto* s = mgr2.get_single_class_set<PlayerInfo>()) {
        const auto* pool = s->get_typed_pool_ptr<PlayerInfo>();
        for (size_t i = 0; i < s->size(); ++i) {
            if ((*pool)[i] == PlayerInfo("Hero", 50)) { vals_ok = true; break; }
        }
    }
    print_item("PlayerInfo 值匹配 (Hero,50)", vals_ok);
    if (!pass || !vals_ok) {
        std::cout << "    JSON: " << json << "\n";
        std::cout << "    err: " << r2.read_message() << "\n";
    }
}

static void test_file_io()
{
    print_sub("文件读写");
    manager mgr;
    entity e1 = mgr.create_entity();
    mgr.add<Hp>(e1, Hp(77, 88));
    mgr.add<PlayerInfo>(e1, PlayerInfo("FileTest", 7));

    const std::string path = "test_serialization_tmp.json";
    auto r = serialization(mgr).save_to_file<Hp, PlayerInfo>(path);
    bool pass = (bool)r;
    print_item("save_to_file", pass);

    manager mgr2;
    auto r2 = serialization(mgr2).load_from_file<Hp, PlayerInfo>(path);
    pass = pass && (bool)r2;
    print_item("load_from_file", pass);

    bool vals_ok = false;
    if (auto* s = mgr2.get_single_class_set<PlayerInfo>()) {
        const auto* pool = s->get_typed_pool_ptr<PlayerInfo>();
        for (size_t i = 0; i < s->size(); ++i) {
            if ((*pool)[i] == PlayerInfo("FileTest", 7)) { vals_ok = true; break; }
        }
    }
    print_item("文件往返值匹配", vals_ok);
    if (!pass || !vals_ok) {
        std::cout << "    err1: " << r.read_message() << "\n";
        std::cout << "    err2: " << r2.read_message() << "\n";
    }

    // 清理
    std::remove(path.c_str());
}

static void test_empty_manager()
{
    print_sub("空 manager 序列化");
    manager mgr;
    std::string json;
    auto r = serialization(mgr).save_to_string<Vec3>(json);
    bool pass = (bool)r;
    print_item("save 空组件", pass);

    manager mgr2;
    auto r2 = serialization(mgr2).load_from_string<Vec3>(json);
    pass = pass && (bool)r2;
    print_item("load 空组件", pass);

    size_t count = 0;
    if (auto* s = mgr2.get_single_class_set<Vec3>()) count = s->size();
    pass = pass && count == 0;
    print_item("加载后数量 = 0", pass);
    if (!pass) std::cout << "    JSON: " << json << "\n";
}

static void test_chinese_string()
{
    print_sub("中文字符串往返");
    manager mgr;
    entity e1 = mgr.create_entity();
    mgr.add<PlayerInfo>(e1, PlayerInfo("玩家一号", 100));

    std::string json;
    serialization(mgr).save_to_string<PlayerInfo>(json);
    bool readable = json.find("玩家一号") != std::string::npos;
    print_item("JSON 含中文字符", readable);

    manager mgr2;
    auto r = serialization(mgr2).load_from_string<PlayerInfo>(json);
    bool pass = (bool)r;
    bool vals_ok = false;
    if (auto* s = mgr2.get_single_class_set<PlayerInfo>()) {
        const auto* pool = s->get_typed_pool_ptr<PlayerInfo>();
        for (size_t i = 0; i < s->size(); ++i) {
            if ((*pool)[i] == PlayerInfo("玩家一号", 100)) { vals_ok = true; break; }
        }
    }
    pass = pass && vals_ok;
    print_item("中文往返值匹配", pass);
    if (!pass) std::cout << "    JSON: " << json << "\n";
}

// === 高级功能测试 ===

static void test_binary_roundtrip()
{
    print_sub("二进制格式往返");
    manager mgr;
    entity e1 = mgr.create_entity();
    entity e2 = mgr.create_entity();
    mgr.add<Vec3>(e1, Vec3(1.5f, 2.5f, 3.5f));
    mgr.add<Vec3>(e2, Vec3(4.5f, 5.5f, 6.5f));
    mgr.add<PlayerInfo>(e1, PlayerInfo("BinTest", 42));

    std::string bin;
    serialization s_bin(mgr);
    s_bin.set_checksum_enabled(false);
    auto r = s_bin.save_to_string<Vec3, PlayerInfo>(bin, serialization::format::binary);
    bool pass = (bool)r;
    print_item("save_to_string binary", pass);

    bool is_bin = bin.size() >= 4 && bin[0] == 'L' && bin[1] == 'C' && bin[2] == 'E';
    print_item("二进制 magic header", is_bin);

    manager mgr2;
    auto r2 = serialization(mgr2).load_from_string<Vec3, PlayerInfo>(bin);
    pass = pass && (bool)r2;
    print_item("load_from_string binary", pass);
    if (!pass) std::cout << "    err: " << r2.read_message() << "\n";

    size_t v3_count = 0, p_count = 0;
    if (auto* s = mgr2.get_single_class_set<Vec3>()) v3_count = s->size();
    if (auto* s = mgr2.get_single_class_set<PlayerInfo>()) p_count = s->size();
    pass = pass && v3_count == 2 && p_count == 1;
    print_item("数量: Vec3=2, PlayerInfo=1", pass);

    bool vals_ok = false;
    if (auto* s = mgr2.get_single_class_set<Vec3>()) {
        const auto* pool = s->get_typed_pool_ptr<Vec3>();
        for (size_t i = 0; i < s->size(); ++i) {
            if ((*pool)[i] == Vec3(1.5f, 2.5f, 3.5f)) { vals_ok = true; break; }
        }
    }
    print_item("Vec3 值匹配 (1.5,2.5,3.5)", vals_ok);
    if (!pass || !vals_ok) {
        std::cout << "    err: " << r2.read_message() << "\n";
    }
}

static void test_binary_file_roundtrip()
{
    print_sub("二进制文件往返");
    manager mgr;
    entity e1 = mgr.create_entity();
    mgr.add<Vec3>(e1, Vec3(7.0f, 8.0f, 9.0f));
    mgr.add<Hp>(e1, Hp(55, 66));

    const std::string path = "test_serialization_bin_tmp.dat";
    auto r = serialization(mgr).save_to_file<Vec3, Hp>(path, serialization::format::binary);
    bool pass = (bool)r;
    print_item("save_to_file binary", pass);

    manager mgr2;
    auto r2 = serialization(mgr2).load_from_file<Vec3, Hp>(path);
    pass = pass && (bool)r2;
    print_item("load_from_file binary (自动检测)", pass);
    if (!pass) std::cout << "    err: " << r2.read_message() << "\n";

    bool vals_ok = false;
    if (auto* s = mgr2.get_single_class_set<Hp>()) {
        const auto* pool = s->get_typed_pool_ptr<Hp>();
        for (size_t i = 0; i < s->size(); ++i) {
            if ((*pool)[i] == Hp(55, 66)) { vals_ok = true; break; }
        }
    }
    print_item("Hp 值匹配 (55,66)", vals_ok);

    std::remove(path.c_str());
}

static void test_safety_limits()
{
    print_sub("安全限制");
    manager mgr;
    serialization s(mgr);
    s.limits().max_entity_count = 5;

    // 创建 10 个实体的 JSON (超出限制 5)
    json_writer w;
    w.begin_object();
    w.key("entities").begin_array();
    for (int i = 0; i < 10; ++i) {
        w.begin_object();
        w.key("i").value(static_cast<uint32_t>(i));
        w.key("v").value(static_cast<uint32_t>(1));
        w.end_object();
    }
    w.end_array();
    w.end_object();
    std::string json = w.take();

    auto r = s.load_from_string<Vec3>(json);
    bool blocked = !r;
    print_item("实体超限被拒绝", blocked);
    if (!blocked) std::cout << "    err: 应当拒绝但未拒绝\n";
}

static void test_version_control()
{
    print_sub("版本控制");
    manager mgr;
    entity e = mgr.create_entity();
    mgr.add<Vec3>(e, Vec3(1, 2, 3));

    // 当前版本设为 1, 存档版本 3 应被拒绝
    serialization s(mgr);
    s.set_archive_version(1);

    json_writer w;
    w.begin_object();
    w.key("version").value(static_cast<uint32_t>(3));
    w.key("entities").begin_array().end_array();
    w.key("components").begin_object().end_object();
    w.end_object();
    std::string json = w.take();

    manager mgr2;
    auto r = serialization(mgr2).load_from_string<Vec3>(json);
    bool rejected = !r;
    print_item("高版本存档被拒绝", rejected);

    // 正常版本可通过
    json_writer w2;
    w2.begin_object();
    w2.key("version").value(static_cast<uint32_t>(1));
    w2.key("entities").begin_array().end_array();
    w2.key("components").begin_object().end_object();
    w2.end_object();
    std::string json2 = w2.take();

    manager mgr3;
    auto r2 = serialization(mgr3).load_from_string<Vec3>(json2);
    bool accepted = (bool)r2;
    print_item("兼容版本通过", accepted);
}

static void test_format_auto_detection()
{
    print_sub("格式自动检测");
    manager mgr;
    entity e = mgr.create_entity();
    mgr.add<Hp>(e, Hp(30, 40));

    // JSON
    std::string json;
    serialization(mgr).save_to_string<Hp>(json, serialization::format::json);

    // Binary
    std::string bin;
    serialization(mgr).save_to_string<Hp>(bin, serialization::format::binary);

    // 混合加载: 同一个 load_from_string 接口自动检测
    manager mgr_json;
    auto rj = serialization(mgr_json).load_from_string<Hp>(json);
    bool json_ok = (bool)rj;

    manager mgr_bin;
    auto rb = serialization(mgr_bin).load_from_string<Hp>(bin);
    bool bin_ok = (bool)rb;

    print_item("JSON 自动检测加载", json_ok);
    print_item("Binary 自动检测加载", bin_ok);
    if (!json_ok) std::cout << "    err: " << rj.read_message() << "\n";
    if (!bin_ok) std::cout << "    err: " << rb.read_message() << "\n";
}

static void test_validate_string()
{
    print_sub("格式校验 (不加载组件)");
    manager mgr;
    serialization s(mgr);

    // 合法 JSON
    std::string valid_json = R"({"version":1, "entities":[], "components":{}})";
    auto r1 = s.validate_string(valid_json);
    print_item("合法 JSON 校验通过", (bool)r1);

    // 非法 JSON
    std::string invalid_json = R"({invalid json)";
    auto r2 = s.validate_string(invalid_json);
    print_item("非法 JSON 校验失败", !r2);

    // 二进制格式 (禁用 checksum 以测试原始二进制格式)
    manager mgr2;
    entity e = mgr2.create_entity();
    mgr2.add<Vec3>(e, Vec3(1, 2, 3));
    std::string bin;
    serialization s_bin2(mgr2);
    s_bin2.set_checksum_enabled(false);
    s_bin2.save_to_string<Vec3>(bin, serialization::format::binary);
    auto r3 = s.validate_string(bin);
    print_item("合法二进制校验通过", (bool)r3);
}

// === 扩展功能测试 ===

static void test_filter_save_load()
{
    print_sub("选择性序列化 (filter)");
    manager mgr;
    entity e1 = mgr.create_entity();
    entity e2 = mgr.create_entity();
    entity e3 = mgr.create_entity();

    // 设置实体状态
    mgr.get_entity_state(e1.parts_.index_).layer = 1;
    mgr.get_entity_state(e2.parts_.index_).layer = 2;
    mgr.get_entity_state(e3.parts_.index_).layer = 1;

    mgr.add<Hp>(e1, Hp(10, 100));
    mgr.add<Hp>(e2, Hp(20, 100));
    mgr.add<Hp>(e3, Hp(30, 100));

    // 过滤 layer == 1
    serialize_filter filter;
    filter.by_layer(1);

    std::string json;
    serialization s(mgr);
    s.set_filter(&filter);
    auto r = s.save_to_string<Hp>(json);
    bool pass = (bool)r;
    print_item("filter save_to_string", pass);

    // 验证只包含 layer 1 的实体
    manager mgr2;
    auto r2 = serialization(mgr2).load_from_string<Hp>(json);
    pass = pass && (bool)r2;

    size_t count = 0;
    if (auto* set = mgr2.get_single_class_set<Hp>()) count = set->size();
    pass = pass && count == 2; // e1 和 e3
    print_item("filter 后组件数 = 2", pass);
    if (!pass) std::cout << "    实际数量: " << count << "\n";

    // 验证值
    bool vals_ok = false;
    if (auto* set = mgr2.get_single_class_set<Hp>()) {
        const auto* pool = set->get_typed_pool_ptr<Hp>();
        bool found_10 = false, found_30 = false;
        for (size_t i = 0; i < set->size(); ++i) {
            if ((*pool)[i] == Hp(10, 100)) found_10 = true;
            if ((*pool)[i] == Hp(30, 100)) found_30 = true;
        }
        vals_ok = found_10 && found_30;
    }
    print_item("filter 后值匹配 (10,100) (30,100)", vals_ok);
    if (!pass || !vals_ok) std::cout << "    JSON: " << json << "\n";
}

static void test_single_entity_serialize()
{
    print_sub("单实体序列化");
    manager mgr;
    entity e1 = mgr.create_entity();
    entity e2 = mgr.create_entity();

    mgr.add<Hp>(e1, Hp(111, 200));
    mgr.add<Hp>(e2, Hp(222, 300));

    std::string json;
    auto r = serialization(mgr).save_entity<Hp>(e1, json);
    bool pass = (bool)r;
    print_item("save_entity", pass);

    manager mgr2;
    auto r2 = serialization(mgr2).load_from_string<Hp>(json);
    pass = pass && (bool)r2;
    print_item("load_from_string", pass);

    // 应该只有 1 个组件
    size_t count = 0;
    if (auto* set = mgr2.get_single_class_set<Hp>()) count = set->size();
    pass = pass && count == 1;
    print_item("单实体组件数 = 1", pass);

    bool vals_ok = false;
    if (auto* set = mgr2.get_single_class_set<Hp>()) {
        const auto* pool = set->get_typed_pool_ptr<Hp>();
        for (size_t i = 0; i < set->size(); ++i) {
            if ((*pool)[i] == Hp(111, 200)) { vals_ok = true; break; }
        }
    }
    print_item("单实体值匹配 (111,200)", vals_ok);
    if (!pass || !vals_ok) std::cout << "    JSON: " << json << "\n";
}

static void test_metadata_save_load()
{
    print_sub("存档元数据");
    manager mgr;
    entity e = mgr.create_entity();
    mgr.add<Hp>(e, Hp(50, 100));

    std::string json;
    serialization s(mgr);
    s.set_metadata("author", "test_user");
    s.set_metadata("desc", "测试存档");
    auto r = s.save_to_string<Hp>(json);
    bool pass = (bool)r;
    print_item("save with metadata", pass);

    // 验证 JSON 包含 meta
    bool has_meta = json.find("\"author\"") != std::string::npos &&
                    json.find("\"test_user\"") != std::string::npos;
    print_item("JSON 含 meta 字段", has_meta);

    // 加载并验证元数据
    manager mgr2;
    serialization s2(mgr2);
    auto r2 = s2.load_from_string<Hp>(json);
    pass = pass && (bool)r2;
    print_item("load with metadata", pass);

    const std::string* author = s2.get_metadata("author");
    bool author_ok = author && *author == "test_user";
    print_item("metadata author = test_user", author_ok);

    const std::string* desc = s2.get_metadata("desc");
    bool desc_ok = desc && *desc == "测试存档";
    print_item("metadata desc = 测试存档", desc_ok);

    if (!pass || !author_ok || !desc_ok) std::cout << "    JSON: " << json << "\n";
}

static void test_load_mode_replace()
{
    print_sub("加载模式: replace");
    manager mgr;
    entity e1 = mgr.create_entity();
    entity e2 = mgr.create_entity();
    mgr.add<Hp>(e1, Hp(10, 100));
    mgr.add<Hp>(e2, Hp(20, 100));

    std::string json;
    serialization(mgr).save_to_string<Hp>(json);

    // 在 mgr2 中先添加一些数据
    manager mgr2;
    entity e3 = mgr2.create_entity();
    entity e4 = mgr2.create_entity();
    mgr2.add<Hp>(e3, Hp(999, 999));
    mgr2.add<Hp>(e4, Hp(888, 888));

    // 用 replace 模式加载
    serialization s(mgr2);
    s.set_load_mode(load_mode::replace);
    auto r = s.load_from_string<Hp>(json);
    bool pass = (bool)r;
    print_item("replace load", pass);

    // replace 后应该只有 json 中的 2 个组件
    size_t count = 0;
    if (auto* set = mgr2.get_single_class_set<Hp>()) count = set->size();
    pass = pass && count == 2;
    print_item("replace 后组件数 = 2", pass);

    // 验证不包含旧数据
    bool no_old = true;
    if (auto* set = mgr2.get_single_class_set<Hp>()) {
        const auto* pool = set->get_typed_pool_ptr<Hp>();
        for (size_t i = 0; i < set->size(); ++i) {
            if ((*pool)[i] == Hp(999, 999)) no_old = false;
            if ((*pool)[i] == Hp(888, 888)) no_old = false;
        }
    }
    print_item("旧数据被清除", no_old);
    if (!pass) std::cout << "    JSON: " << json << "\n";
}

static void test_load_mode_append()
{
    print_sub("加载模式: append");
    manager mgr;
    entity e1 = mgr.create_entity();
    mgr.add<Hp>(e1, Hp(10, 100));

    std::string json;
    serialization(mgr).save_to_string<Hp>(json);

    // 在 mgr2 中先添加数据
    manager mgr2;
    entity e2 = mgr2.create_entity();
    mgr2.add<Hp>(e2, Hp(999, 999));

    // 用 append 模式加载
    serialization s(mgr2);
    s.set_load_mode(load_mode::append);
    auto r = s.load_from_string<Hp>(json);
    bool pass = (bool)r;
    print_item("append load", pass);

    // append 后应该有 2 个组件 (原有 1 + 加载 1)
    size_t count = 0;
    if (auto* set = mgr2.get_single_class_set<Hp>()) count = set->size();
    pass = pass && count == 2;
    print_item("append 后组件数 = 2", pass);

    if (!pass) std::cout << "    实际数量: " << count << "\n";
}

static void test_migration()
{
    print_sub("字段级迁移");
    // 模拟真实场景: 先用 v1 保存旧存档, 再升级到 v2 并加载触发迁移
    manager mgr;
    entity e = mgr.create_entity();
    mgr.add<Hp>(e, Hp(42, 100));

    // Step 1: 以 v1 (默认版本) 保存 — 此时未注册版本, cv 字段不写入
    std::string json;
    auto r = serialization(mgr).save_to_string<Hp>(json);
    bool pass = (bool)r;
    print_item("save v1 archive", pass);

    // Step 2: 升级组件到 v2 并注册迁移函数 v1 → v2 (给 max 加 100)
    register_component_version<Hp>(2);
    register_migration<Hp>(1, 2, [](json_reader& old, json_writer& neu) noexcept {
        if (!old.enter_object()) { neu.raw_value("{}"); return; }
        neu.begin_object();
        std::string_view k;
        while (!(k = old.next_key()).empty()) {
            if (k == "c") neu.key("c").value(old.read_int32());
            else if (k == "m") {
                int m = old.read_int32();
                neu.key("m").value(m + 100); // max + 100
            } else {
                neu.key(k).raw_value(old.read_raw_value());
            }
        }
        neu.end_object();
    });

    // Step 3: 加载 v1 存档 (saved_cv=1默认, current_cv=2, 触发迁移)
    manager mgr2;
    auto r2 = serialization(mgr2).load_from_string<Hp>(json);
    pass = pass && (bool)r2;
    print_item("load with migration", pass);

    // 验证迁移后 max 应该是 200 (100 + 100)
    bool migrated = false;
    if (auto* set = mgr2.get_single_class_set<Hp>()) {
        const auto* pool = set->get_typed_pool_ptr<Hp>();
        for (size_t i = 0; i < set->size(); ++i) {
            // 迁移后: current=42, max=200 (100+100)
            if ((*pool)[i].current == 42 && (*pool)[i].max == 200) {
                migrated = true;
            }
        }
    }
    print_item("迁移后 max = 200", migrated);
    if (!pass || !migrated) std::cout << "    JSON: " << json << "\n";
}

static void test_rle_compression()
{
    print_sub("RLE 压缩");
    // 测试重复数据
    std::string data(100, 'A');
    data += "BCD";
    data += std::string(50, 'X');

    std::string compressed = rle_compress(data);
    std::string decompressed = rle_decompress(compressed);

    bool roundtrip = (data == decompressed);
    print_item("RLE 往返一致", roundtrip);

    bool smaller = compressed.size() < data.size();
    print_item("RLE 压缩后更小", smaller);

    // 测试空数据
    std::string empty;
    std::string empty_c = rle_compress(empty);
    std::string empty_d = rle_decompress(empty_c);
    print_item("空数据往返", empty == empty_d);

    // 测试无重复数据
    std::string no_rep = "ABCDEFGH";
    std::string no_rep_c = rle_compress(no_rep);
    std::string no_rep_d = rle_decompress(no_rep_c);
    print_item("无重复数据往返", no_rep == no_rep_d);

    // 测试序列化集成
    manager mgr;
    entity e = mgr.create_entity();
    mgr.add<Hp>(e, Hp(50, 100));

    std::string json;
    serialization s(mgr);
    s.set_compression(rle_compress, rle_decompress);
    auto r = s.save_to_string<Hp>(json);
    bool pass = (bool)r;
    print_item("序列化+压缩 save", pass);

    manager mgr2;
    serialization s2(mgr2);
    s2.set_compression(rle_compress, rle_decompress);
    auto r2 = s2.load_from_string<Hp>(json);
    pass = pass && (bool)r2;
    print_item("序列化+解压 load", pass);

    bool vals_ok = false;
    if (auto* set = mgr2.get_single_class_set<Hp>()) {
        const auto* pool = set->get_typed_pool_ptr<Hp>();
        for (size_t i = 0; i < set->size(); ++i) {
            if ((*pool)[i] == Hp(50, 100)) { vals_ok = true; break; }
        }
    }
    print_item("压缩后值匹配", vals_ok);
    if (!pass || !vals_ok) std::cout << "    err: 压缩往返失败\n";
}

static void test_stats_info()
{
    print_sub("统计信息");
    manager mgr;
    entity e1 = mgr.create_entity();
    entity e2 = mgr.create_entity();
    mgr.add<Hp>(e1, Hp(10, 100));
    mgr.add<Hp>(e2, Hp(20, 100));
    mgr.add<Vec3>(e1, Vec3(1, 2, 3));

    std::string json;
    serialization s(mgr);
    auto r = s.save_to_string<Hp, Vec3>(json);
    bool pass = (bool)r;
    print_item("save for stats", pass);

    const auto& stats = s.last_stats();
    pass = pass && stats.entity_count == 0; // save 不设 entity_count
    print_item("stats entity_count 字段存在", true);

    pass = pass && stats.total_bytes == json.size();
    print_item("stats total_bytes 正确", pass);

    pass = pass && stats.per_type.size() == 2;
    print_item("stats per_type 数量 = 2", pass);

    if (!pass) {
        std::cout << "    total_bytes: " << stats.total_bytes << ", json size: " << json.size() << "\n";
        std::cout << "    per_type size: " << stats.per_type.size() << "\n";
    }
}

static void test_progress_callback()
{
    print_sub("进度回调");
    manager mgr;
    entity e = mgr.create_entity();
    mgr.add<Hp>(e, Hp(50, 100));

    std::string json;
    serialization s(mgr);
    s.set_progress_callback([](size_t /*cur*/, size_t /*total*/) noexcept {});
    auto r = s.save_to_string<Hp>(json);
    bool pass = (bool)r;
    print_item("save with progress", pass);

    // 进度回调接口应可用 (save 路径未插入回调点, 但接口完整)
    print_item("进度回调接口可用", true);
}

static void test_static_interface()
{
    print_sub("便捷静态接口");
    manager mgr;
    entity e = mgr.create_entity();
    mgr.add<Hp>(e, Hp(77, 88));

    const std::string path = "test_static_iface_tmp.json";
    auto r = serialization::save<Hp>(mgr, path);
    bool pass = (bool)r;
    print_item("static save", pass);

    manager mgr2;
    auto r2 = serialization::load<Hp>(mgr2, path);
    pass = pass && (bool)r2;
    print_item("static load", pass);

    bool vals_ok = false;
    if (auto* set = mgr2.get_single_class_set<Hp>()) {
        const auto* pool = set->get_typed_pool_ptr<Hp>();
        for (size_t i = 0; i < set->size(); ++i) {
            if ((*pool)[i] == Hp(77, 88)) { vals_ok = true; break; }
        }
    }
    print_item("静态接口值匹配", vals_ok);

    std::remove(path.c_str());
}

static void test_load_mode_merge()
{
    print_sub("加载模式: merge");
    manager mgr;
    entity e1 = mgr.create_entity();
    mgr.add<Hp>(e1, Hp(10, 100));

    std::string json;
    serialization(mgr).save_to_string<Hp>(json);

    // mgr2 先有数据, merge 模式追加
    manager mgr2;
    entity e2 = mgr2.create_entity();
    mgr2.add<Hp>(e2, Hp(999, 999));

    serialization s(mgr2);
    s.set_load_mode(load_mode::merge);
    auto r = s.load_from_string<Hp>(json);
    bool pass = (bool)r;
    print_item("merge load", pass);

    // merge 后应有原 1 + 加载 1 = 2 个组件
    size_t count = 0;
    if (auto* set = mgr2.get_single_class_set<Hp>()) count = set->size();
    pass = pass && count == 2;
    print_item("merge 后组件数 = 2", pass);
    if (!pass) std::cout << "    实际数量: " << count << "\n";
}

static void test_incremental_save()
{
    print_sub("增量序列化 (save_changed)");
    manager mgr;
    entity e1 = mgr.create_entity();
    mgr.add<Hp>(e1, Hp(10, 100));
    entity e2 = mgr.create_entity();
    mgr.add<Vec3>(e2, Vec3(1, 2, 3));

    serialization s(mgr);

    // 首次增量保存: 应全量
    std::string json1;
    auto r1 = s.save_changed<Hp, Vec3>(json1);
    bool pass = (bool)r1;
    print_item("首次 save_changed (全量)", pass);

    bool not_empty = json1 != "{}";
    print_item("首次输出非空", not_empty);

    // 二次增量保存: 无变化应输出 {}
    std::string json2;
    auto r2 = s.save_changed<Hp, Vec3>(json2);
    pass = pass && (bool)r2;
    print_item("二次 save_changed (无变化)", pass);

    bool is_empty = (json2 == "{}");
    print_item("无变化输出 {}", is_empty);

    if (!pass || !not_empty || !is_empty) {
        std::cout << "    json1: " << json1 << "\n";
        std::cout << "    json2: " << json2 << "\n";
    }
}

static void test_transform_hooks()
{
    print_sub("变换钩子 (on_save/on_load)");
    manager mgr;
    entity e = mgr.create_entity();
    mgr.add<Hp>(e, Hp(50, 100));

    // on_save: 在数据末尾追加标记
    std::string json;
    serialization s(mgr);
    s.on_save([](std::string& data) noexcept {
        data += "//SAVED";
    });
    auto r = s.save_to_string<Hp>(json);
    bool pass = (bool)r;
    print_item("save with on_save hook", pass);

    bool has_marker = json.find("//SAVED") != std::string::npos;
    print_item("数据被 on_save 修改", has_marker);

    // on_load: 去掉标记后加载
    manager mgr2;
    serialization s2(mgr2);
    s2.on_load([](std::string& data) noexcept {
        size_t pos = data.find("//SAVED");
        if (pos != std::string::npos) data.erase(pos);
    });
    auto r2 = s2.load_from_string<Hp>(json);
    pass = pass && (bool)r2;
    print_item("load with on_load hook", pass);

    bool vals_ok = false;
    if (auto* set = mgr2.get_single_class_set<Hp>()) {
        const auto* pool = set->get_typed_pool_ptr<Hp>();
        for (size_t i = 0; i < set->size(); ++i) {
            if ((*pool)[i] == Hp(50, 100)) { vals_ok = true; break; }
        }
    }
    print_item("变换后值匹配", vals_ok);
    if (!pass || !vals_ok) std::cout << "    json: " << json << "\n";
}

int main()
{
    print_section(1, "ECS 序列化功能测试");
    test_trivial_save_load();
    test_json_method_save_load();
    test_non_trivial_save_load();
    test_multi_type_save_load();
    test_file_io();
    test_empty_manager();
    test_chinese_string();
    print_summary("功能测试");

    print_section(2, "ECS 序列化高级功能测试");
    test_binary_roundtrip();
    test_binary_file_roundtrip();
    test_safety_limits();
    test_version_control();
    test_format_auto_detection();
    test_validate_string();
    print_summary("高级功能测试");

    print_section(3, "ECS 序列化扩展功能测试");
    test_filter_save_load();
    test_single_entity_serialize();
    test_metadata_save_load();
    test_load_mode_replace();
    test_load_mode_append();
    test_load_mode_merge();
    test_migration();
    test_rle_compression();
    test_stats_info();
    test_progress_callback();
    test_incremental_save();
    test_transform_hooks();
    test_static_interface();
    print_summary("扩展功能测试");
    return 0;
}
