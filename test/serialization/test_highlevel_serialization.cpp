// test_highlevel_serialization.cpp - 高层封装测试: 序列化
#include "test_common.hpp"
#include "include/serialization/manager.hpp"
#include "include/reflection/manager.hpp"
#include <string>
#include <vector>

using serialize::archive_format;

struct position
{
    float x;
    float y;
    float z;
};

enum class test_color : std::uint8_t
{
    red = 1,
    green = 2,
};

struct health
{
    int hp;
};

struct probe_component
{
    float scale;
    int id;
};

struct json_payload_component
{
    int code;
    std::string label;

    std::string to_json() const
    {
        json_writer w;
        w.begin_object();
        w.key("code").value(code);
        w.key("label").value(label);
        w.end_object();
        return w.take();
    }

    void from_json(std::string_view s)
    {
        json_reader r(s);
        if (!r.enter_object())
        {
            return;
        }
        std::string_view k;
        while (!(k = r.next_key()).empty())
        {
            if (k == "code")
            {
                code = r.read_int32();
            }
            else if (k == "label")
            {
                label = r.read_string();
            }
            else
            {
                r.skip_value();
            }
        }
    }
};

struct item_record
{
    int         id;
    float       weight;
    std::string name;
};

struct arr_record
{
    int xs[2];
    int other;
};

struct vec_record
{
    int                tag;
    std::vector<int>   xs;
};

struct opaque_payload
{
    std::string value;
};

static int register_all_types() noexcept
{
    auto& s = reflect::global();

    s.register_type_only<probe_component>("probe_component");
    s.register_member_runtime<probe_component>("probe_component", "scale", &probe_component::scale);
    s.register_member_runtime<probe_component>("probe_component", "id", &probe_component::id);

    s.register_type_only<item_record>("item_record");
    s.register_member_runtime<item_record>("item_record", "id", &item_record::id);
    s.register_member_runtime<item_record>("item_record", "weight", &item_record::weight);
    s.register_member_runtime<item_record>("item_record", "name", &item_record::name);

    s.register_type_only<arr_record>("arr_record");
    s.register_member_runtime<arr_record>("arr_record", "xs", &arr_record::xs);
    s.register_member_runtime<arr_record>("arr_record", "other", &arr_record::other);

    s.register_type_only<vec_record>("vec_record");
    s.register_member_runtime<vec_record>("vec_record", "tag", &vec_record::tag);
    s.register_member_runtime<vec_record>("vec_record", "xs", &vec_record::xs);

    return 0;
}

static const int g_types_registered = register_all_types();

int main()
{
    t_begin("test_highlevel_serialization — 高层封装测试: 序列化");

    t_section(1, "JSON 往返");
    {
        ecs::manager mgr;
        auto e1 = mgr.create_entity();
        auto e2 = mgr.create_entity();
        (void)mgr.add(e1, position{1.0f, 2.0f, 3.0f});
        (void)mgr.add(e2, position{4.0f, 5.0f, 6.0f});
        (void)mgr.add(e1, health{100});

        serialize::manager m(mgr);
        m.world()->register_components<position, health>();
        m.set_checksum(false);
        std::string json;
        t_check("save_world 成功", m.save_world(json));
        t_check("json 非空", !json.empty());

        ecs::manager mgr2;
        serialize::manager m2(mgr2);
        m2.world()->register_components<position, health>();
        t_check("load_world 成功", m2.load_world(json));

        auto* pset = mgr2.get_single_class_set<position>();
        auto* hset = mgr2.get_single_class_set<health>();
        t_check("position 数量 == 2", pset != nullptr && pset->size() == 2);
        t_check("health 数量 == 1", hset != nullptr && hset->size() == 1);
    }

    t_section(2, "二进制往返");
    {
        ecs::manager mgr;
        auto e1 = mgr.create_entity();
        (void)mgr.add(e1, position{7.0f, 8.0f, 9.0f});

        serialize::manager m(mgr);
        m.world()->register_components<position>();
        m.set_checksum(false).set_format(archive_format::binary);
        std::string bin;
        t_check("二进制 save 成功", m.save_world(bin));
        bool magic_ok = bin.size() >= 3 && bin[0] == 'L' && bin[1] == 'C' && bin[2] == 'E';
        t_check("二进制 magic", magic_ok);

        ecs::manager mgr2;
        serialize::manager m2(mgr2);
        m2.world()->register_components<position>();
        t_check("二进制 load 成功", m2.load_world(bin));
        auto* pset = mgr2.get_single_class_set<position>();
        t_check("二进制 position 数量 == 1", pset != nullptr && pset->size() == 1);
    }

    t_section(3, "校验和");
    {
        ecs::manager mgr;
        auto e1 = mgr.create_entity();
        (void)mgr.add(e1, position{1.0f, 1.0f, 1.0f});

        serialize::manager m(mgr);
        m.world()->register_components<position>();
        m.set_checksum(true);
        std::string json;
        t_check("带校验和 save", m.save_world(json));
        bool has_prefix = json.size() >= 4 && json.compare(0, 4, "LCCS") == 0;
        t_check("带校验和前缀", has_prefix);

        ecs::manager mgr2;
        serialize::manager m2(mgr2);
        m2.world()->register_components<position>();
        m2.set_checksum(true);
        t_check("带校验和 load", m2.load_world(json));
    }

    t_section(4, "空管理器");
    {
        ecs::manager mgr;
        serialize::manager m(mgr);
        m.world()->register_components<position>();
        m.set_checksum(false);
        std::string json;
        t_check("空 manager save", m.save_world(json));

        ecs::manager mgr2;
        serialize::manager m2(mgr2);
        m2.world()->register_components<position>();
        t_check("空 manager load", m2.load_world(json));
    }

    t_section(5, "格式自动检测");
    {
        ecs::manager mgr;
        auto e = mgr.create_entity();
        (void)mgr.add(e, position{1.0f, 2.0f, 3.0f});
        serialize::manager m(mgr);
        m.world()->register_components<position>();
        m.set_checksum(false);

        std::string json;
        m.set_format(archive_format::json).save_world(json);
        std::string bin;
        m.set_format(archive_format::binary).save_world(bin);

        ecs::manager m1;
        ecs::manager m2;
        serialize::manager a1(m1);
        a1.world()->register_components<position>();
        serialize::manager a2(m2);
        a2.world()->register_components<position>();
        t_check("JSON 自动检测", a1.load_world(json));
        t_check("二进制自动检测", a2.load_world(bin));
    }

    t_section(6, "Protobuf / FlatBuffer 类型化往返");
    {
        serialize::register_type_name<probe_component>("probe_component");
        ecs::manager mgr;
        auto e = mgr.create_entity();
        (void)mgr.add(e, probe_component{1.5f, 7});

        serialize::manager m(mgr);
        m.world()->register_components<probe_component>();
        m.set_checksum(false).set_format(archive_format::protobuf);

        std::string pb;
        t_check("protobuf save 成功", m.save_world(pb));

        ecs::manager m1;
        serialize::manager a1(m1);
        a1.world()->register_components<probe_component>();
        bool pb_ok = a1.load_world(pb);
        if (pb_ok)
        {
            auto* set = m1.get_single_class_set<probe_component>();
            const auto* pool = set ? set->get_typed_pool_ptr<probe_component>() : nullptr;
            pb_ok = pool != nullptr && set->size() == 1
                    && (*pool)[0].scale == 1.5f && (*pool)[0].id == 7;
        }
        t_check("protobuf 类型化字段还原", pb_ok);

        std::string fb;
        m.set_format(archive_format::flatbuffer);
        t_check("flatbuffer save 成功", m.save_world(fb));

        ecs::manager m2;
        serialize::manager a2(m2);
        a2.world()->register_components<probe_component>();
        bool fb_ok = a2.load_world(fb);
        if (fb_ok)
        {
            auto* set = m2.get_single_class_set<probe_component>();
            const auto* pool = set ? set->get_typed_pool_ptr<probe_component>() : nullptr;
            fb_ok = pool != nullptr && set->size() == 1
                    && (*pool)[0].scale == 1.5f && (*pool)[0].id == 7;
        }
        t_check("flatbuffer 类型化字段还原", fb_ok);
    }

    t_section(7, "跨语言 schema 导出");
    {
        std::string proto = serialize::schema_export::to_proto<probe_component>();
        std::string fbs = serialize::schema_export::to_fbs<probe_component>();
        t_check("proto 含 syntax 声明", proto.find("syntax = \"proto3\";") != std::string::npos);
        t_check("proto 含组件条目消息", proto.find("message probe_componentEntry") != std::string::npos);
        t_check("proto 含根字段编号", proto.find("string cv = 4;") != std::string::npos);
        t_check("fbs 含 file_identifier", fbs.find("file_identifier \"LCFB\";") != std::string::npos);
        t_check("fbs 含 root_type", fbs.find("root_type Archive;") != std::string::npos);
    }

    t_section(8, "完整 ECS 存档门面 (免 Ts...)");
    {
        auto seed = [](ecs::manager& m) {
            auto e1 = m.create_entity();
            (void)m.add(e1, probe_component{2.5f, 21});
            (void)m.add(e1, position{1.0f, 2.0f, 3.0f});
            m.get_entity_state(e1.parts_.index_).tag = 5;
            auto e2 = m.create_entity();
            (void)m.add(e2, position{4.0f, 5.0f, 6.0f});
            (void)m.create_entity();
            auto e4 = m.create_entity();
            (void)m.add(e4, probe_component{8.0f, 88});
            m.delete_entity(e4);
        };

        auto verify = [](ecs::manager& m) {
            const ecs::entity_manager& em = m.get_entity_manager();
            bool index_ok = em.is_alive(1) && em.is_alive(2) && em.is_alive(3)
                            && !em.is_alive(4);
            auto* pset = m.get_single_class_set<position>();
            auto* qset = m.get_single_class_set<probe_component>();
            bool comp_ok = false;
            if (qset != nullptr && qset->size() == 1) {
                const auto* pool = qset->get_typed_pool_ptr<probe_component>();
                comp_ok = pool != nullptr && (*pool)[0].scale == 2.5f && (*pool)[0].id == 21;
            }
            return index_ok && pset != nullptr && pset->size() == 2 && comp_ok
                   && m.get_entity_state(1).tag == 5;
        };

        const char* names[] = {"json", "binary", "protobuf", "flatbuffer"};
        archive_format fmts[] = {
            archive_format::json,
            archive_format::binary,
            archive_format::protobuf,
            archive_format::flatbuffer,
        };
        for (int i = 0; i < 4; ++i) {
            ecs::manager mgr;
            seed(mgr);
            serialize::manager m(mgr);
            m.world()->register_components<probe_component, position, health>();
            m.set_checksum(false).set_format(fmts[i]);
            std::string data;
            bool rs = m.save_world(data);
            t_check((std::string(names[i]) + " manager save").c_str(), rs);

            ecs::manager m2;
            serialize::manager a2(m2);
            a2.world()->register_components<probe_component, position, health>();
            bool rl = a2.load_world(data);
            t_check((std::string(names[i]) + " manager load").c_str(), rl);
            if (rl) {
                t_check((std::string(names[i]) + " 全量状态还原").c_str(), verify(m2));
            }
        }

        ecs::manager mgr;
        serialize::manager m(mgr);
        m.world()->register_components<probe_component, position, health>();
        std::string sp = m.world_schema_proto();
        std::string sf = m.world_schema_fbs();
        t_check("运行期 proto 含组件条目消息",
                sp.find("message probe_componentEntry") != std::string::npos);
        t_check("运行期 fbs 含组件向量",
                sf.find("probe_component:[probe_componentEntry];") != std::string::npos);
    }

    t_section(9, "JSON 官方格式");
    {
        ecs::manager mgr;
        auto e = mgr.create_entity();
        (void)mgr.add(e, probe_component{1.5f, 11});
        (void)mgr.add(e, json_payload_component{3, "abc"});

        serialize::manager m(mgr);
        m.world()->register_components<probe_component, json_payload_component>();
        m.set_checksum(false);
        (void)m.set_world_metadata("k", "v");
        std::string data;
        t_check("json save 成功", m.save_world(data));
        t_check("meta 为嵌套对象", data.find("\"meta\":{") != std::string::npos);
        t_check("反射载荷为嵌套对象", data.find("\"d\":{\"scale\":") != std::string::npos);
        t_check("to_json 载荷为嵌套对象", data.find("\"d\":{\"code\":") != std::string::npos);

        ecs::manager m2;
        serialize::manager a2(m2);
        a2.world()->register_components<probe_component, json_payload_component>();
        bool r2 = a2.load_world(data);
        t_check("json load 成功", r2);
        bool ok = false;
        if (r2)
        {
            auto* set = m2.get_single_class_set<json_payload_component>();
            if (set != nullptr && set->size() == 1)
            {
                const auto* pool = set->get_typed_pool_ptr<json_payload_component>();
                ok = pool != nullptr && (*pool)[0].code == 3 && (*pool)[0].label == "abc";
            }
        }
        t_check("to_json 组件往返", ok);
    }

    t_section(10, "通用对象序列化 (与 ECS 解耦)");
    {
        serialize::manager m;
        m.set_checksum(false);

        item_record one{7, 1.5f, "alpha"};
        std::string js;
        t_check("对象 save 成功", m.save_object(js, one));
        t_check("对象 JSON 为原生对象", js.find("\"items\":[{") != std::string::npos);

        item_record back{};
        t_check("对象 load 成功", m.load_object(js, back));
        t_check("对象字段还原", back.id == 7 && back.weight == 1.5f && back.name == "alpha");

        dense<item_record> src;
        for (int i = 0; i < 5; ++i) {
            src.emplace_back(item_record{i, static_cast<float>(i) * 0.5f, std::string("n") + std::to_string(i)});
        }

        const char* names[] = {"json", "binary", "protobuf", "flatbuffer"};
        archive_format fmts[] = {
            archive_format::json,
            archive_format::binary,
            archive_format::protobuf,
            archive_format::flatbuffer,
        };
        for (int k = 0; k < 4; ++k) {
            std::string data;
            m.set_format(fmts[k]);
            bool r1 = m.save_array(data, src.data(), src.size());
            dense<item_record> dst;
            bool r2 = m.load_array(data, dst);
            bool ok = r1 && r2 && dst.size() == src.size();
            for (size_t i = 0; ok && i < src.size(); ++i) {
                ok = dst[i].id == src[i].id && dst[i].weight == src[i].weight
                     && dst[i].name == src[i].name;
            }
            t_check((std::string(names[k]) + " 批量对象往返").c_str(), ok);
        }

        position p{1.0f, 2.0f, 3.0f};
        std::string ps;
        bool rps = m.save_object(ps, p);
        position p2{};
        bool rpl = m.load_object(ps, p2);
        t_check("平凡对象往返", rps && rpl
                               && p2.x == 1.0f && p2.y == 2.0f && p2.z == 3.0f);
    }

    t_section(11, "数组字段与不支持类型");
    {
        serialize::manager m;
        m.set_checksum(false).set_format(archive_format::json);

        arr_record a{};
        a.xs[0] = 1;
        a.xs[1] = 2;
        a.other = 7;
        std::string s;
        bool rs = m.save_object(s, a);
        arr_record b{};
        bool rl = m.load_object(s, b);
        t_check("数组字段往返", rs && rl
                && b.xs[0] == 1 && b.xs[1] == 2 && b.other == 7);

        std::string over = R"({"v":1,"items":[{"xs":[1,2,3],"other":9}]})";
        arr_record c{};
        bool rc = m.load_object(over, c);
        t_check("数组超容量截断且后续字段同步", rc
                && c.xs[0] == 1 && c.xs[1] == 2 && c.other == 9);

        vec_record v{};
        v.tag = 5;
        v.xs = {1, 2, 3};
        std::string vs;
        const operating_message rv = m.save_object(vs, v);
        t_check("不支持字段类型显式报错", !rv && vs.empty()
                && rv.is_code(om_err_unsupported_operation));

        opaque_payload opaque{"must-not-disappear"};
        std::string os;
        const operating_message ro = m.save_object(os, opaque);
        t_check("非平凡未反射对象显式报错", !ro && os.empty()
                && ro.is_code(om_err_unsupported_operation));
    }


    t_section(20, "枚举底层类型复用反射");
    {
        serialize::register_enum<test_color>();
        const int tid = type_id::get_type_id<test_color>();
        t_check("底层类型查询", serialize::lookup_enum_underlying(tid) ==
                                    type_id::get_type_id<std::uint8_t>());
        const reflect::enum_meta* em = reflect::global().find_enum(tid);
        t_check("反射枚举元数据存在",
                em != nullptr && em->underlying_type_id == type_id::get_type_id<std::uint8_t>());
    }

    t_section(21, "实体重映射表 (稀疏旧 id)");
    {
        serialize::detail::entity_remap remap;
        t_check("初始为空", remap.empty() && remap.size() == 0);

        const ecs::entity e7 = ecs::entity(7, 1);
        const ecs::entity e9 = ecs::entity(9, 2);
        remap.set(3, e7, 11);
        remap.set(100000, e9, 22);
        t_check("条目数", remap.size() == 2 && !remap.empty());

        ecs::entity out{};
        t_check("低 id 命中", remap.find(3, out) && out.parts_.index_ == 7);
        t_check("高 id 命中", remap.find(100000, out) && out.parts_.index_ == 9);
        t_check("中间空洞未命中", !remap.find(4, out));
        t_check("版本一致", remap.version(3) == 11 && remap.version(100000) == 22);
        t_check("未命中版本为 0", remap.version(4) == 0);
        t_check("新实体标记", remap.is_new_entity(7) && remap.is_new_entity(9)
                                  && !remap.is_new_entity(8));

        remap.set(3, ecs::entity(8, 3), 33);
        t_check("同 id 覆盖", remap.size() == 2 && remap.version(3) == 33
                                  && remap.find(3, out) && out.parts_.index_ == 8);

        remap.set(0xFFFFFFFFu, e7, 1);
        t_check("超范围旧 id 丢弃", remap.size() == 2 && !remap.find(0xFFFFFFFFu, out));
    }

    t_section(22, "replace 在非空 world 上保持索引");
    {
        ecs::manager source;
        auto e1 = source.create_entity();
        auto e2 = source.create_entity();
        auto e3 = source.create_entity();
        (void)source.add(e1, position{1.0f, 1.0f, 1.0f});
        (void)source.add(e3, position{3.0f, 3.0f, 3.0f});
        source.get_entity_state(e3.parts_.index_).tag = 9;
        source.delete_entity(e2);

        serialize::manager writer(source);
        writer.world()->register_components<position>();
        writer.set_checksum(false);
        std::string data;
        t_check("稀疏索引存档成功", writer.save_world(data));

        ecs::manager target;
        target.preallocate_entities(8);
        for (int i = 0; i < 5; ++i)
        {
            (void)target.create_entity();
        }
        serialize::manager reader(target);
        reader.world()->register_components<position>();
        t_check("非空 world replace 成功", reader.load_world(data));

        const ecs::entity_manager& em = target.get_entity_manager();
        auto* set = target.get_single_class_set<position>();
        t_check("replace 保留存档索引和空洞", em.is_alive(1) && !em.is_alive(2)
            && em.is_alive(3));
        t_check("replace 恢复实体状态", target.get_entity_state(3).tag == 9);
        t_check("replace 恢复组件数量", set != nullptr && set->size() == 2);
    }

    t_section(23, "统一错误对象");
    {
        serialize::manager m;
        m.set_format(archive_format::json);
        position p{1.0f, 2.0f, 3.0f};
        std::string data;

        t_check("保存成功返回空对象", m.save_object(data, p));

        position back{};
        const operating_message bad = m.load_object(std::string_view("{bad}"), back);
        t_check("损坏数据加载失败", !bad);
        t_check("失败码非零", bad.code() != 0);
        t_check("失败级别有效", bad.level() == msg_level::warn);

        const operating_message write_error = m.save_object_to_file(std::string{}, p);
        t_check("空路径写入失败", !write_error);
        t_check("写入失败码为 io_failed", write_error.is_code(om_err_io_failed));
    }

    return t_end("serialization ");
}
